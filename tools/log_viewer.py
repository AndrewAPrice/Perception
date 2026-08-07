#!/usr/bin/env python3
# Copyright 2026 Perception OS Authors
# Demultiplexes and prints Perception OS serial COM1 logs to stdout.

import datetime
import json
import os
import re
import struct
import sys

# Control code escape sequence regex: \033]P;<pid>;<channel_id>;<name>\007
CONTROL_CODE_REGEX = re.compile(bytes([0x1b]) + rb']P;(-?\d+);(\d+);([^\x07]*)\x07')

# ANSI color codes for process distinction in terminal output
ANSI_COLORS = [
    "\033[36m",  # Cyan (Kernel)
    "\033[32m",  # Green
    "\033[33m",  # Yellow
    "\033[35m",  # Magenta
    "\033[31m",  # Red
    "\033[34m",  # Blue
    "\033[96m",  # Bright Cyan
    "\033[92m",  # Bright Green
    "\033[93m",  # Bright Yellow
]
ANSI_RESET = "\033[0m"


class DemuxParser:
    def __init__(self):
        self.current_pid = 0
        self.current_name = "Kernel"
        self.current_channel = 0
        self.line_buffer = bytearray()
        self.raw_buffer = bytearray()
        self.pid_colors = {0: ANSI_COLORS[0]}
        
        # Tracing attributes (Channel 2)
        self.string_tables = {}  # pid -> {string_id -> string}
        self.trace_events = []
        self.open_spans = {}  # span_id -> dict
        self.trace_buffer = bytearray()
        self.known_processes = {0: "Kernel"}  # pid -> name
        self.last_switch_tsc = None
        self.last_cpu_pid = 0
        self.last_cpu_tid = 0
        self.process_creation_ts = {}
        self.process_termination_ts = {}

    def get_color(self, pid: int) -> str:
        if pid not in self.pid_colors:
            color_idx = (len(self.pid_colors) % (len(ANSI_COLORS) - 1)) + 1
            self.pid_colors[pid] = ANSI_COLORS[color_idx]
        return self.pid_colors[pid]

    def feed(self, data: bytes):
        self.raw_buffer.extend(data)
        self._process_buffer()

    def _process_buffer(self):
        while True:
            match = CONTROL_CODE_REGEX.search(self.raw_buffer)
            if not match:
                self._flush_pending_text()
                break

            start, end = match.span()
            try:
                pid = int(match.group(1))
                channel = int(match.group(2))
                name = match.group(3).decode('utf-8', errors='replace')
            except Exception:
                pid = None

            # Flush any text prior to the match
            self._flush_lines_up_to(start)

            if pid is not None:
                if self.line_buffer:
                    self._commit_line()

                self.current_pid = pid
                self.current_name = name
                self.current_channel = channel
                self.known_processes[pid] = name

            # Consume control code from raw buffer
            code_length = end - start
            del self.raw_buffer[:code_length]

    def _flush_pending_text(self):
        if self.current_channel == 2:
            esc_idx = self.raw_buffer.find(b'\x1b]P;')
            if esc_idx != -1:
                self._flush_lines_up_to(esc_idx)
            else:
                self._flush_lines_up_to(len(self.raw_buffer))
            return

        esc_idx = self.raw_buffer.find(b'\x1b]P;')
        if esc_idx != -1:
            self._flush_lines_up_to(esc_idx)
            if len(self.raw_buffer) > 256 or b'\n' in self.raw_buffer:
                self._flush_lines_up_to(esc_idx + 1)
            return

        esc_idx = self.raw_buffer.find(b'\x1b')
        if esc_idx != -1:
            if esc_idx == len(self.raw_buffer) - 1 or self.raw_buffer[esc_idx:].startswith(b'\x1b]'):
                self._flush_lines_up_to(esc_idx)
            else:
                self._flush_lines_up_to(esc_idx + 1)
            return

        last_nl = self.raw_buffer.rfind(b'\n')
        if last_nl != -1:
            self._flush_lines_up_to(last_nl + 1)

    def _flush_lines_up_to(self, index: int):
        if index <= 0:
            return

        chunk = self.raw_buffer[:index]
        del self.raw_buffer[:index]

        if self.current_channel == 2:
            self.trace_buffer.extend(chunk)
            self._process_trace_buffer()
        else:
            for byte in chunk:
                if byte == ord('\n'):
                    self._commit_line()
                elif byte != ord('\r'):
                    self.line_buffer.append(byte)

    def _process_trace_buffer(self):
        while len(self.trace_buffer) > 0:
            opcode = self.trace_buffer[0]
            if opcode == 0x01:  # REGISTER_STRING: [0x01][string_id: u16][len: u8][str: N]
                if len(self.trace_buffer) < 4:
                    break
                str_id, str_len = struct.unpack('<HB', self.trace_buffer[1:4])
                if len(self.trace_buffer) < 4 + str_len:
                    break
                s_bytes = self.trace_buffer[4:4 + str_len]
                pid = self.current_pid
                if pid not in self.string_tables:
                    self.string_tables[pid] = {}
                self.string_tables[pid][str_id] = s_bytes.decode('utf-8', errors='replace')
                del self.trace_buffer[:4 + str_len]

            elif opcode == 0x02:  # SPAN_BEGIN: [0x02][trace_id: u64][span_id: u64][parent_id: u64][tsc: u64][tid: u32][name_id: u16][cat_id: u16]
                if len(self.trace_buffer) < 41:
                    break
                trace_id, span_id, parent_id, tsc, tid, name_id, cat_id = struct.unpack(
                    '<QQQQIHH', self.trace_buffer[1:41]
                )
                pid = self.current_pid
                if tsc > 100_000_000_000_000 or pid == 0 or pid > 1000 or (tid > 1000 and tid != 99991 and tid != 99992) or pid not in self.string_tables or name_id not in self.string_tables[pid]:
                    del self.trace_buffer[:1]
                    continue
                self.open_spans[span_id] = {
                    'trace_id': trace_id,
                    'span_id': span_id,
                    'parent_span_id': parent_id,
                    'ts': tsc,
                    'pid': pid,
                    'tid': tid,
                    'name_id': name_id,
                    'cat_id': cat_id,
                }
                del self.trace_buffer[:41]

            elif opcode == 0x03:  # SPAN_END: [0x03][span_id: u64][tsc: u64][tid: u32]
                if len(self.trace_buffer) < 21:
                    break
                span_id, tsc, tid = struct.unpack('<QQI', self.trace_buffer[1:21])
                pid = self.current_pid
                if tsc > 100_000_000_000_000 or pid == 0 or pid > 1000 or (tid > 1000 and tid != 99991 and tid != 99992):
                    del self.trace_buffer[:1]
                    continue
                begin_span = self.open_spans.pop(span_id, None)
                if begin_span:
                    dur = tsc - begin_span['ts'] if tsc >= begin_span['ts'] else 0
                    self.trace_events.append({
                        'ph': 'X',
                        'trace_id': begin_span['trace_id'],
                        'span_id': span_id,
                        'parent_span_id': begin_span['parent_span_id'],
                        'ts': begin_span['ts'],
                        'dur': dur,
                        'pid': begin_span['pid'],
                        'tid': begin_span['tid'],
                        'name_id': begin_span['name_id'],
                        'cat_id': begin_span['cat_id'],
                    })
                del self.trace_buffer[:21]

            elif opcode == 0x04:  # INSTANT_EVENT: [0x04][tsc: u64][tid: u32][name_id: u16][cat_id: u16]
                if len(self.trace_buffer) < 17:
                    break
                tsc, tid, name_id, cat_id = struct.unpack('<QIHH', self.trace_buffer[1:17])
                pid = self.current_pid
                if tsc > 100_000_000_000_000 or pid == 0 or pid > 1000 or (tid > 1000 and tid != 99991 and tid != 99992) or pid not in self.string_tables or name_id not in self.string_tables[pid]:
                    del self.trace_buffer[:1]
                    continue
                self.trace_events.append({
                    'ph': 'i',
                    's': 'g',
                    'ts': tsc,
                    'pid': pid,
                    'process_name': self.known_processes.get(pid, f"PID {pid}"),
                    'tid': tid,
                    'name_id': name_id,
                    'cat_id': cat_id,
                })
                del self.trace_buffer[:17]

            elif opcode == 0x05:  # CONTEXT_SWITCH: [0x05][tsc: u64][prev_pid: u32][prev_tid: u32][next_pid: u32][next_tid: u32][reason: u8]
                if len(self.trace_buffer) < 26:
                    break
                tsc, prev_pid, prev_tid, next_pid, next_tid, reason = struct.unpack(
                    '<QIIIIB', self.trace_buffer[1:26]
                )
                if tsc > 100_000_000_000_000 or prev_pid > 1000 or next_pid > 1000 or prev_tid > 1000 or next_tid > 1000:
                    del self.trace_buffer[:1]
                    continue
                if self.last_switch_tsc is not None and tsc < self.last_switch_tsc:
                    del self.trace_buffer[:1]
                    continue
                
                if self.last_switch_tsc is not None and tsc > self.last_switch_tsc:
                    dur = tsc - self.last_switch_tsc
                    if self.last_cpu_pid != 0 and dur < 100_000_000_000_000:
                        proc_name = self.known_processes.get(
                            self.last_cpu_pid, f"PID {self.last_cpu_pid}"
                        )
                        self.trace_events.append({
                            'ph': 'X',
                            'cat': 'cpu',
                            'name': proc_name,
                            'ts': self.last_switch_tsc,
                            'dur': dur,
                            'pid': 99990,
                            'tid': 1,
                        })

                self.last_switch_tsc = tsc
                self.last_cpu_pid = next_pid
                self.last_cpu_tid = next_tid
                del self.trace_buffer[:26]

            elif opcode == 0x06:  # IPC_TRANSFER: [0x06][tsc: u64][sender_pid: u32][target_pid: u32][msg_id: u64][msg_type: u8]
                if len(self.trace_buffer) < 26:
                    break
                tsc, sender_pid, target_pid, msg_id, msg_type = struct.unpack(
                    '<QIIQB', self.trace_buffer[1:26]
                )
                if tsc > 100_000_000_000_000 or sender_pid > 100000 or target_pid > 100000:
                    del self.trace_buffer[:1]
                    continue
                self.trace_events.append({
                    'ph': 'i',
                    'cat': 'ipc',
                    'name': 'IPCTransfer',
                    'ts': tsc,
                    'pid': sender_pid,
                    'process_name': self.known_processes.get(sender_pid, f"PID {sender_pid}"),
                    'tid': 0,
                    'args': {
                        'target_pid': target_pid,
                        'msg_id': msg_id,
                        'msg_type': msg_type,
                    },
                })
                del self.trace_buffer[:26]

            elif opcode == 0x07:  # PROCESS_CREATED: [0x07][tsc: u64][pid: u32][len: u8][name: N]
                if len(self.trace_buffer) < 14:
                    break
                tsc, pid, name_len = struct.unpack('<QIB', self.trace_buffer[1:14])
                if tsc > 100_000_000_000_000 or pid > 100000 or name_len > 128:
                    del self.trace_buffer[:1]
                    continue
                if len(self.trace_buffer) < 14 + name_len:
                    break
                p_name = self.trace_buffer[14:14 + name_len].decode('utf-8', errors='replace')
                if p_name:
                    self.known_processes[pid] = p_name
                    self.process_creation_ts[pid] = tsc
                del self.trace_buffer[:14 + name_len]

            elif opcode == 0x08:  # PROCESS_TERMINATED: [0x08][tsc: u64][pid: u32]
                if len(self.trace_buffer) < 13:
                    break
                tsc, pid = struct.unpack('<QI', self.trace_buffer[1:13])
                if tsc > 100_000_000_000_000 or pid > 100000:
                    del self.trace_buffer[:1]
                    continue
                self.process_termination_ts[pid] = tsc
                del self.trace_buffer[:13]

            else:
                # Resynchronize by discarding 1 byte
                del self.trace_buffer[:1]

    def _commit_line(self):
        text = self.line_buffer.decode('utf-8', errors='replace')
        self.line_buffer.clear()
        timestamp = datetime.datetime.now().strftime('%H:%M:%S.%f')[:-3]

        color = self.get_color(self.current_pid)
        channel_str = f" [ch:{self.current_channel}]" if self.current_channel != 0 else ""

        if sys.stdout.isatty():
            print(f"{color}[{timestamp}] [{self.current_name}]{channel_str} {text}{ANSI_RESET}", flush=True)
        else:
            print(f"[{timestamp}] [{self.current_name}]{channel_str} {text}", flush=True)

    def export_trace_json(self, output_path: str = "trace.json"):
        if not self.trace_events and not self.process_creation_ts:
            return

        # Calculate timestamp bounds for RDTSC-to-microsecond scaling and normalization
        valid_ts = [e['ts'] for e in self.trace_events if 'ts' in e and 0 <= e['ts'] < 100_000_000_000_000]
        valid_ts += [t for t in self.process_creation_ts.values() if 0 <= t < 100_000_000_000_000]
        valid_ts += [t for t in self.process_termination_ts.values() if 0 <= t < 100_000_000_000_000]
        if not valid_ts:
            return

        min_ts = min(valid_ts)
        max_ts = max(valid_ts)

        def to_us(raw_tsc):
            if raw_tsc < min_ts:
                return 0
            return (raw_tsc - min_ts) // 1000

        def scale_dur(raw_dur):
            if raw_dur <= 0:
                return 0
            return max(1, raw_dur // 1000)

        print("Exporting trace to trace.json", flush=True)

        chrome_events = []
        
        # Add process metadata events for all known processes
        seen_pids = set(self.known_processes.keys())
        for pid in self.process_creation_ts.keys():
            seen_pids.add(pid)
        for event in self.trace_events:
            if 'pid' in event:
                seen_pids.add(event['pid'])

        for pid in sorted(seen_pids):
            pname = self.known_processes.get(pid, f"PID {pid}")
            chrome_events.append({
                'name': 'process_name',
                'ph': 'M',
                'pid': pid,
                'args': {'name': pname},
            })
            if pid != 0 and pid != 99990:
                chrome_events.append({
                    'name': 'thread_name',
                    'ph': 'M',
                    'pid': pid,
                    'tid': 99991,
                    'args': {'name': 'RPC Sent'},
                })
                chrome_events.append({
                    'name': 'thread_name',
                    'ph': 'M',
                    'pid': pid,
                    'tid': 99992,
                    'args': {'name': 'RPC Received'},
                })
        
        # Add CPU 1 metadata
        chrome_events.append({
            'name': 'process_name',
            'ph': 'M',
            'pid': 99990,
            'args': {'name': 'CPU'},
        })
        chrome_events.append({
            'name': 'thread_name',
            'ph': 'M',
            'pid': 99990,
            'tid': 1,
            'args': {'name': 'CPU 1'},
        })

        # Calculate process lifespans scaled to microseconds
        for pid, create_ts in self.process_creation_ts.items():
            if pid == 0 or pid == 99990 or not (0 <= create_ts < 100_000_000_000_000):
                continue
            term_ts = self.process_termination_ts.get(pid, max_ts)
            if not (0 <= term_ts < 100_000_000_000_000):
                term_ts = max_ts
            raw_dur = term_ts - create_ts if term_ts >= create_ts else 0
            if raw_dur > 0 and raw_dur < 100_000_000_000_000:
                chrome_events.append({
                    'ph': 'X',
                    'cat': 'process',
                    'name': 'Lifespan',
                    'ts': to_us(create_ts),
                    'dur': scale_dur(raw_dur),
                    'pid': pid,
                    'tid': 9999,
                })
                chrome_events.append({
                    'name': 'thread_name',
                    'ph': 'M',
                    'pid': pid,
                    'tid': 9999,
                    'args': {'name': 'Lifespan'},
                })

        # Close any unclosed spans on exit as complete duration spans (ph: X)
        for span_id, begin_span in self.open_spans.items():
            pid = begin_span.get('pid', 0)
            name_id = begin_span.get('name_id')
            if pid == 0 or pid > 1000 or pid not in self.string_tables or name_id not in self.string_tables[pid]:
                continue
            cat_id = begin_span.get('cat_id')
            cat_str = self.string_tables.get(pid, {}).get(cat_id, 'app')
            if cat_str in ('rpc_out', 'rpc_in'):
                dur = 0
            else:
                dur = max_ts - begin_span['ts'] if max_ts >= begin_span['ts'] else 0

            self.trace_events.append({
                'ph': 'X',
                'trace_id': begin_span['trace_id'],
                'span_id': span_id,
                'parent_span_id': begin_span['parent_span_id'],
                'ts': begin_span['ts'],
                'dur': dur,
                'pid': begin_span['pid'],
                'tid': begin_span['tid'],
                'name_id': begin_span['name_id'],
                'cat_id': begin_span['cat_id'],
            })
        self.open_spans.clear()

        # Convert trace events into chrome_events
        for event in self.trace_events:
            ts_val = event.get('ts', 0)
            dur_val = event.get('dur', 0)
            if ts_val > 100_000_000_000_000 or dur_val > 100_000_000_000_000:
                continue

            e = dict(event)
            name_id = e.pop('name_id', None)
            cat_id = e.pop('cat_id', None)

            pid_str_table = self.string_tables.get(e.get('pid', 0), {})

            if name_id is not None:
                name = pid_str_table.get(name_id, f"string_{name_id}")
            else:
                name = e.get('name', 'Event')
            
            if cat_id is not None:
                cat = pid_str_table.get(cat_id, 'app')
            else:
                cat = e.get('cat', 'app')

            if 'ts' in e:
                e['ts'] = to_us(e['ts'])
            if 'dur' in e:
                e['dur'] = scale_dur(e['dur'])

            if cat == 'rpc_out':
                e['tid'] = 99991
            elif cat == 'rpc_in':
                e['tid'] = 99992

            e['name'] = name
            e['cat'] = cat
            e.pop('process_name', None)
            chrome_events.append(e)

        try:
            with open(output_path, 'w', encoding='utf-8') as f:
                json.dump(chrome_events, f, indent=2)
        except Exception as err:
            print(f"Failed to write trace to {output_path}: {err}", file=sys.stderr)


def main():
    parser = DemuxParser()
    stdin_fd = sys.stdin.fileno()
    while True:
        try:
            chunk = os.read(stdin_fd, 1024)
            if not chunk:
                break
            parser.feed(chunk)
        except (OSError, ValueError, KeyboardInterrupt):
            break

    # Export trace if any binary trace events were recorded
    parser.export_trace_json("trace.json")


if __name__ == '__main__':
    main()

