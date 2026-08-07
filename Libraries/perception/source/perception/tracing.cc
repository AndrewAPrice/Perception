// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "perception/tracing.h"

#ifdef ENABLE_TRACING

#include <atomic>
#include <cstring>
#include <map>
#include <string>

#include "perception/debug.h"
#ifndef KERNEL
#include "perception/processes.h"
#endif
#include "perception/threads.h"

namespace {

// Next available string ID counter.
uint16 next_string_id = 1;

// Global span ID counter.
std::atomic<uint64> next_global_span_id{1};

// Global trace ID counter.
std::atomic<uint64> next_global_trace_id{1};

// Registered string table cache to avoid re-emitting strings.
std::map<std::string, uint16> string_id_map;

// Thread-local trace context.
thread_local perception::TraceContext current_thread_trace_context{0, 0, 0};

// Reads the x86_64 timestamp counter (TSC).
inline uint64 ReadTimestampCounter() {
  uint32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64)hi << 32) | lo;
}

}  // namespace

namespace perception {

TraceContext GetCurrentTraceContext() {
  return TraceContext{
      .trace_id = current_thread_trace_context.trace_id,
      .span_id = current_thread_trace_context.span_id,
      .parent_span_id = current_thread_trace_context.parent_span_id,
  };
}

void SetCurrentTraceContext(const TraceContext& context) {
  current_thread_trace_context.trace_id = context.trace_id;
  current_thread_trace_context.span_id = context.span_id;
  current_thread_trace_context.parent_span_id = context.parent_span_id;
}

void EmitTraceBytes(const char* data, size_t size) {
  DebugPrinter printer(2);
  for (size_t i = 0; i < size; i++) printer << data[i];
}

uint16 RegisterTraceString(const char* str) {
  if (str == nullptr) return 0;

  std::string key(str);
  auto it = string_id_map.find(key);
  if (it != string_id_map.end()) return it->second;

  uint16 str_id = next_string_id++;
  string_id_map[key] = str_id;

  size_t str_len = key.length();
  if (str_len > 255) str_len = 255;

  // REGISTER_STRING (opcode 0x01) layout:
  // [0x01: u8][string_id: u16][len: u8][str: N]
  char packet[4];
  packet[0] = 0x01;
  packet[1] = static_cast<char>(str_id & 0xFF);
  packet[2] = static_cast<char>((str_id >> 8) & 0xFF);
  packet[3] = static_cast<char>(str_len);

  EmitTraceBytes(packet, 4);
  EmitTraceBytes(key.data(), str_len);
  return str_id;
}

ScopedTraceSpan::ScopedTraceSpan(std::string_view name,
                                 std::string_view category) {
  span_id_ = next_global_span_id.fetch_add(1, std::memory_order_relaxed);

  uint64 parent_span_id = current_thread_trace_context.span_id;
  uint64 trace_id = current_thread_trace_context.trace_id;
  if (trace_id == 0) {
    trace_id = next_global_trace_id.fetch_add(1, std::memory_order_relaxed);
  }

  current_thread_trace_context.trace_id = trace_id;
  current_thread_trace_context.parent_span_id = parent_span_id;
  current_thread_trace_context.span_id = span_id_;

  uint64 tsc = ReadTimestampCounter();
  uint32 tid = static_cast<uint32>(GetThreadId());
  std::string name_str(name);
  std::string cat_str(category);
  uint16 name_id = RegisterTraceString(name_str.c_str());
  uint16 cat_id = RegisterTraceString(cat_str.c_str());

  // SPAN_BEGIN (opcode 0x02) layout:
  // [0x02: u8][trace_id: u64][span_id: u64][parent_id: u64][tsc: u64][tid:
  // u32][name_id: u16][cat_id: u16]
  char packet[41];
  packet[0] = 0x02;
  std::memcpy(&packet[1], &trace_id, 8);
  std::memcpy(&packet[9], &span_id_, 8);
  std::memcpy(&packet[17], &parent_span_id, 8);
  std::memcpy(&packet[25], &tsc, 8);
  std::memcpy(&packet[33], &tid, 4);
  std::memcpy(&packet[37], &name_id, 2);
  std::memcpy(&packet[39], &cat_id, 2);

  EmitTraceBytes(packet, 41);
}

ScopedTraceSpan::~ScopedTraceSpan() {
  uint64 tsc = ReadTimestampCounter();
  uint32 tid = static_cast<uint32>(GetThreadId());

  // Restore parent span ID in thread context
  current_thread_trace_context.span_id =
      current_thread_trace_context.parent_span_id;

  // SPAN_END (opcode 0x03) layout:
  // [0x03: u8][span_id: u64][tsc: u64][tid: u32]
  char packet[21];
  packet[0] = 0x03;
  std::memcpy(&packet[1], &span_id_, 8);
  std::memcpy(&packet[9], &tsc, 8);
  std::memcpy(&packet[17], &tid, 4);

  EmitTraceBytes(packet, 21);
}

AsyncTraceSpan::AsyncTraceSpan(std::string_view name,
                               std::string_view category) {
  span_id_ = next_global_span_id.fetch_add(1, std::memory_order_relaxed);

  uint64 parent_span_id = current_thread_trace_context.span_id;
  uint64 trace_id = current_thread_trace_context.trace_id;
  if (trace_id == 0) {
    trace_id = next_global_trace_id.fetch_add(1, std::memory_order_relaxed);
  }

  uint64 tsc = ReadTimestampCounter();
  uint32 tid = static_cast<uint32>(GetThreadId());
  std::string name_str(name);
  std::string cat_str(category);
  uint16 name_id = RegisterTraceString(name_str.c_str());
  uint16 cat_id = RegisterTraceString(cat_str.c_str());

  char packet[41];
  packet[0] = 0x02;
  std::memcpy(&packet[1], &trace_id, 8);
  std::memcpy(&packet[9], &span_id_, 8);
  std::memcpy(&packet[17], &parent_span_id, 8);
  std::memcpy(&packet[25], &tsc, 8);
  std::memcpy(&packet[33], &tid, 4);
  std::memcpy(&packet[37], &name_id, 2);
  std::memcpy(&packet[39], &cat_id, 2);

  EmitTraceBytes(packet, 41);
}

void AsyncTraceSpan::End() {
  if (ended_) return;
  ended_ = true;

  uint64 tsc = ReadTimestampCounter();
  uint32 tid = static_cast<uint32>(GetThreadId());

  char packet[21];
  packet[0] = 0x03;
  std::memcpy(&packet[1], &span_id_, 8);
  std::memcpy(&packet[9], &tsc, 8);
  std::memcpy(&packet[17], &tid, 4);

  EmitTraceBytes(packet, 21);
}

AsyncTraceSpan::~AsyncTraceSpan() { End(); }

void EmitInstantTraceEvent(std::string_view name, std::string_view category) {
  uint64 tsc = ReadTimestampCounter();
  uint32 tid = static_cast<uint32>(GetThreadId());
  std::string name_str(name);
  std::string cat_str(category);
  uint16 name_id = RegisterTraceString(name_str.c_str());
  uint16 cat_id = RegisterTraceString(cat_str.c_str());

  // INSTANT_EVENT (opcode 0x04) layout:
  // [0x04: u8][tsc: u64][tid: u32][name_id: u16][cat_id: u16]
  char packet[17];
  packet[0] = 0x04;
  std::memcpy(&packet[1], &tsc, 8);
  std::memcpy(&packet[9], &tid, 4);
  std::memcpy(&packet[13], &name_id, 2);
  std::memcpy(&packet[15], &cat_id, 2);

  EmitTraceBytes(packet, 17);
}

}  // namespace perception

#endif  // ENABLE_TRACING
