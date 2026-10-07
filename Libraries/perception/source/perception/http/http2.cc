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

#include "perception/http/http2.h"

#include <algorithm>
#include <cstdlib>

namespace perception {
namespace http {

namespace {

// Maximum valid 24-bit frame size allowed by the HTTP/2 specification.
constexpr uint32_t kMaxAllowedFrameSize = 16777215;

// Maximum legal flow-control window size (2^31 - 1).
constexpr int64_t kMaxFlowControlWindow = 0x7FFFFFFF;

// Mask for 31-bit stream identifiers and window increments.
constexpr uint32_t kStreamIdMask = 0x7FFFFFFFu;

// Threshold in bytes at which a WINDOW_UPDATE frame is sent.
constexpr uint32_t kWindowUpdateThreshold = 32768;

// Size in bytes of a PING frame payload.
constexpr size_t kPingPayloadSize = 8;

// Size in bytes of a RST_STREAM frame payload.
constexpr size_t kRstStreamPayloadSize = 4;

// Size in bytes of a WINDOW_UPDATE frame payload.
constexpr size_t kWindowUpdatePayloadSize = 4;

// Size in bytes of a PRIORITY frame payload or HEADERS priority section.
constexpr size_t kPrioritySectionSize = 5;

// Size in bytes of a single SETTINGS parameter entry.
constexpr size_t kSettingEntrySize = 6;

// Minimum size in bytes of a GOAWAY frame payload.
constexpr size_t kGoAwayMinPayloadSize = 8;

// Threshold of consumed bytes in send/receive buffers before compaction.
constexpr size_t kBufferCompactionThreshold = 16384;

// Default HTTP status code when none is parsed.
constexpr int kDefaultStatusCode = 200;

// Lowest informational HTTP status code.
constexpr int kMinInformationalStatus = 100;

// Lowest non-informational HTTP status code.
constexpr int kMinFinalStatus = 200;

uint32_t ReadUint32Be(std::span<const uint8_t> bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

uint16_t ReadUint16Be(std::span<const uint8_t> bytes) {
  return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                               static_cast<uint16_t>(bytes[1]));
}

void AppendUint32Be(uint32_t value, std::vector<uint8_t>& output) {
  output.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
  output.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
  output.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
  output.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void AppendUint16Be(uint16_t value, std::vector<uint8_t>& output) {
  output.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
  output.push_back(static_cast<uint8_t>(value & 0xFFu));
}

std::string ToLowerAscii(std::string_view input) {
  std::string result(input);
  for (char& ch : result) {
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch - 'A' + 'a');
  }
  return result;
}

std::string_view TrimAsciiWhitespace(std::string_view input) {
  while (!input.empty() && (input.front() == ' ' || input.front() == '\t' ||
                            input.front() == '\r' || input.front() == '\n'))
    input.remove_prefix(1);
  while (!input.empty() && (input.back() == ' ' || input.back() == '\t' ||
                            input.back() == '\r' || input.back() == '\n'))
    input.remove_suffix(1);
  return input;
}

bool IsForbiddenHttp2RequestHeader(std::string_view lower_name,
                                   std::string_view value) {
  if (lower_name == "connection" || lower_name == "keep-alive" ||
      lower_name == "proxy-connection" || lower_name == "transfer-encoding" ||
      lower_name == "upgrade" || lower_name == "host")
    return true;
  if (lower_name == "te" &&
      ToLowerAscii(TrimAsciiWhitespace(value)) != "trailers")
    return true;
  return false;
}

}  // namespace

void SerializeHttp2FrameHeader(const Http2FrameHeader& header,
                               std::vector<uint8_t>& output) {
  output.push_back(static_cast<uint8_t>((header.length >> 16) & 0xFFu));
  output.push_back(static_cast<uint8_t>((header.length >> 8) & 0xFFu));
  output.push_back(static_cast<uint8_t>(header.length & 0xFFu));
  output.push_back(header.type);
  output.push_back(header.flags);
  AppendUint32Be(header.stream_id & kStreamIdMask, output);
}

bool ParseHttp2FrameHeader(std::span<const uint8_t> input,
                           Http2FrameHeader& header) {
  if (input.size() < kHttp2FrameHeaderSize)
    return false;

  header.length = (static_cast<uint32_t>(input[0]) << 16) |
                  (static_cast<uint32_t>(input[1]) << 8) |
                  static_cast<uint32_t>(input[2]);
  header.type = input[3];
  header.flags = input[4];
  header.stream_id = ReadUint32Be(input.subspan(5, 4)) & kStreamIdMask;
  return true;
}

void SerializeHttp2Frame(const Http2Frame& frame,
                         std::vector<uint8_t>& output) {
  Http2FrameHeader header = frame.header;
  header.length = static_cast<uint32_t>(frame.payload.size());
  SerializeHttp2FrameHeader(header, output);
  output.insert(output.end(), frame.payload.begin(), frame.payload.end());
}

Http2Frame BuildHttp2SettingsFrame(
    std::span<const std::pair<uint16_t, uint32_t>> settings, bool ack) {
  Http2Frame frame;
  frame.header.type = static_cast<uint8_t>(Http2FrameType::SETTINGS);
  frame.header.flags = ack ? kHttp2FlagAck : 0;
  frame.header.stream_id = 0;

  if (!ack) {
    frame.payload.reserve(settings.size() * kSettingEntrySize);
    for (const auto& [id, val] : settings) {
      AppendUint16Be(id, frame.payload);
      AppendUint32Be(val, frame.payload);
    }
  }
  frame.header.length = static_cast<uint32_t>(frame.payload.size());
  return frame;
}

Http2Frame BuildHttp2WindowUpdateFrame(uint32_t stream_id, uint32_t increment) {
  Http2Frame frame;
  frame.header.type = static_cast<uint8_t>(Http2FrameType::WINDOW_UPDATE);
  frame.header.flags = 0;
  frame.header.stream_id = stream_id & kStreamIdMask;
  AppendUint32Be(increment & kStreamIdMask, frame.payload);
  frame.header.length = static_cast<uint32_t>(frame.payload.size());
  return frame;
}

Http2Frame BuildHttp2PingFrame(std::span<const uint8_t> opaque_data, bool ack) {
  Http2Frame frame;
  frame.header.type = static_cast<uint8_t>(Http2FrameType::PING);
  frame.header.flags = ack ? kHttp2FlagAck : 0;
  frame.header.stream_id = 0;
  frame.payload.resize(kPingPayloadSize, 0);
  size_t copy_len = std::min(opaque_data.size(), kPingPayloadSize);
  std::copy_n(opaque_data.begin(), copy_len, frame.payload.begin());
  frame.header.length = kPingPayloadSize;
  return frame;
}

Http2Frame BuildHttp2RstStreamFrame(uint32_t stream_id, uint32_t error_code) {
  Http2Frame frame;
  frame.header.type = static_cast<uint8_t>(Http2FrameType::RST_STREAM);
  frame.header.flags = 0;
  frame.header.stream_id = stream_id & kStreamIdMask;
  AppendUint32Be(error_code, frame.payload);
  frame.header.length = kRstStreamPayloadSize;
  return frame;
}

Http2Frame BuildHttp2GoAwayFrame(uint32_t last_stream_id, uint32_t error_code) {
  Http2Frame frame;
  frame.header.type = static_cast<uint8_t>(Http2FrameType::GOAWAY);
  frame.header.flags = 0;
  frame.header.stream_id = 0;
  AppendUint32Be(last_stream_id & kStreamIdMask, frame.payload);
  AppendUint32Be(error_code, frame.payload);
  frame.header.length = kGoAwayMinPayloadSize;
  return frame;
}

std::vector<HeaderField> SanitizeRequestHeadersForHttp2(
    std::span<const HeaderField> raw_headers) {
  std::vector<HeaderField> sanitized;
  sanitized.reserve(raw_headers.size());
  for (const auto& field : raw_headers) {
    std::string lower_name = ToLowerAscii(TrimAsciiWhitespace(field.name));
    if (lower_name.empty() || lower_name.front() == ':')
      continue;
    std::string_view trimmed_val = TrimAsciiWhitespace(field.value);
    if (IsForbiddenHttp2RequestHeader(lower_name, trimmed_val))
      continue;
    sanitized.push_back(
        HeaderField{std::move(lower_name), std::string(trimmed_val)});
  }
  return sanitized;
}

Http2ClientSession::Http2ClientSession() = default;

void Http2ClientSession::Initialize() {
  if (initialized_)
    return;
  initialized_ = true;

  send_buffer_.insert(send_buffer_.end(), kHttp2ConnectionPreface.begin(),
                      kHttp2ConnectionPreface.end());

  const std::pair<uint16_t, uint32_t> initial_settings[] = {
      {static_cast<uint16_t>(Http2SettingId::ENABLE_PUSH), 0},
      {static_cast<uint16_t>(Http2SettingId::MAX_CONCURRENT_STREAMS),
       kHttp2DefaultMaxConcurrentStreams},
      {static_cast<uint16_t>(Http2SettingId::INITIAL_WINDOW_SIZE),
       kHttp2LocalInitialStreamWindowSize},
  };
  QueueFrame(BuildHttp2SettingsFrame(initial_settings, false));

  uint32_t conn_window_increment =
      kHttp2LocalConnectionWindowSize -
      static_cast<uint32_t>(kHttp2DefaultInitialWindowSize);
  QueueFrame(BuildHttp2WindowUpdateFrame(0, conn_window_increment));
  connection_recv_window_ = kHttp2LocalConnectionWindowSize;
}

bool Http2ClientSession::CanOpenNewStream() const {
  if (connection_error_ || goaway_received_)
    return false;
  if (next_stream_id_ > kStreamIdMask)
    return false;
  if (ActiveStreamCount() >= peer_max_concurrent_streams_)
    return false;
  return true;
}

uint32_t Http2ClientSession::OpenStream(std::string_view method,
                                        std::string_view scheme,
                                        std::string_view authority,
                                        std::string_view path,
                                        std::span<const HeaderField> headers,
                                        std::string_view body) {
  if (!initialized_)
    Initialize();
  if (!CanOpenNewStream())
    return 0;

  uint32_t stream_id = next_stream_id_;
  next_stream_id_ += 2;

  Http2StreamState& stream = streams_[stream_id];
  stream.stream_id = stream_id;
  stream.send_window = static_cast<int64_t>(peer_initial_window_size_);
  stream.recv_window = kHttp2LocalInitialStreamWindowSize;

  std::vector<HeaderField> full_headers;
  full_headers.reserve(4 + headers.size());
  full_headers.push_back(HeaderField{":method", std::string(method)});
  full_headers.push_back(HeaderField{":scheme", std::string(scheme)});
  full_headers.push_back(HeaderField{":authority", std::string(authority)});
  full_headers.push_back(
      HeaderField{":path", std::string(path.empty() ? "/" : path)});

  std::vector<HeaderField> clean_headers =
      SanitizeRequestHeadersForHttp2(headers);
  for (auto& field : clean_headers)
    full_headers.push_back(std::move(field));

  std::vector<uint8_t> encoded_block;
  hpack_encoder_.Encode(full_headers, encoded_block);

  size_t max_chunk = std::max<size_t>(peer_max_frame_size_, 1);
  size_t offset = 0;
  bool first_frame = true;

  do {
    size_t chunk_len = std::min(max_chunk, encoded_block.size() - offset);
    bool is_last_chunk = (offset + chunk_len >= encoded_block.size());

    Http2Frame frame;
    frame.header.type = static_cast<uint8_t>(
        first_frame ? Http2FrameType::HEADERS : Http2FrameType::CONTINUATION);
    frame.header.stream_id = stream_id;
    frame.header.flags = 0;
    if (first_frame && body.empty())
      frame.header.flags |= kHttp2FlagEndStream;
    if (is_last_chunk)
      frame.header.flags |= kHttp2FlagEndHeaders;

    frame.payload.assign(encoded_block.begin() + offset,
                         encoded_block.begin() + offset + chunk_len);
    QueueFrame(frame);

    offset += chunk_len;
    first_frame = false;
  } while (offset < encoded_block.size());

  if (body.empty()) {
    stream.local_end_stream = true;
  } else {
    stream.pending_request_body.assign(body);
    stream.pending_request_offset = 0;
    FlushStreamRequestData(stream);
  }

  return stream_id;
}

void Http2ClientSession::CancelStream(uint32_t stream_id) {
  auto it = streams_.find(stream_id);
  if (it == streams_.end())
    return;
  if (!it->second.remote_end_stream && !it->second.reset_by_peer &&
      !connection_error_)
    QueueFrame(BuildHttp2RstStreamFrame(
        stream_id, static_cast<uint32_t>(Http2ErrorCode::CANCEL)));
  streams_.erase(it);
}

void Http2ClientSession::RemoveStream(uint32_t stream_id) {
  streams_.erase(stream_id);
}

bool Http2ClientSession::ReceiveBytes(std::span<const uint8_t> data) {
  if (connection_error_)
    return false;
  if (!data.empty())
    recv_buffer_.insert(recv_buffer_.end(), data.begin(), data.end());

  while (recv_buffer_.size() - recv_offset_ >= kHttp2FrameHeaderSize) {
    std::span<const uint8_t> remaining(recv_buffer_.data() + recv_offset_,
                                       recv_buffer_.size() - recv_offset_);
    Http2FrameHeader header;
    ParseHttp2FrameHeader(remaining, header);

    if (header.length > kMaxAllowedFrameSize)
      return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);

    if (remaining.size() < kHttp2FrameHeaderSize + header.length)
      break;

    std::span<const uint8_t> payload =
        remaining.subspan(kHttp2FrameHeaderSize, header.length);
    recv_offset_ += kHttp2FrameHeaderSize + header.length;

    if (!ProcessFrame(header, payload))
      return false;
  }

  if (recv_offset_ > 0) {
    if (recv_offset_ == recv_buffer_.size()) {
      recv_buffer_.clear();
      recv_offset_ = 0;
    } else if (recv_offset_ >= kBufferCompactionThreshold) {
      recv_buffer_.erase(recv_buffer_.begin(),
                         recv_buffer_.begin() + recv_offset_);
      recv_offset_ = 0;
    }
  }

  return !connection_error_;
}

std::span<const uint8_t> Http2ClientSession::PendingSendBytes() const {
  if (send_offset_ >= send_buffer_.size())
    return {};
  return std::span<const uint8_t>(send_buffer_.data() + send_offset_,
                                  send_buffer_.size() - send_offset_);
}

void Http2ClientSession::ConsumePendingSendBytes(size_t count) {
  send_offset_ = std::min(send_buffer_.size(), send_offset_ + count);
  if (send_offset_ == send_buffer_.size()) {
    send_buffer_.clear();
    send_offset_ = 0;
  } else if (send_offset_ >= kBufferCompactionThreshold) {
    send_buffer_.erase(send_buffer_.begin(),
                       send_buffer_.begin() + send_offset_);
    send_offset_ = 0;
  }
}

const Http2StreamState* Http2ClientSession::GetStream(
    uint32_t stream_id) const {
  auto it = streams_.find(stream_id);
  if (it == streams_.end())
    return nullptr;
  return &it->second;
}

bool Http2ClientSession::IsStreamComplete(uint32_t stream_id) const {
  auto it = streams_.find(stream_id);
  if (it == streams_.end())
    return false;
  return it->second.remote_end_stream && !it->second.reset_by_peer &&
         !connection_error_;
}

bool Http2ClientSession::IsStreamFailed(uint32_t stream_id) const {
  if (connection_error_)
    return true;
  auto it = streams_.find(stream_id);
  if (it == streams_.end())
    return true;
  if (it->second.reset_by_peer)
    return true;
  if (goaway_received_ && stream_id > goaway_last_stream_id_ &&
      !it->second.remote_end_stream)
    return true;
  return false;
}

std::string Http2ClientSession::FormatStreamAsHttp1Response(
    uint32_t stream_id) const {
  auto it = streams_.find(stream_id);
  if (it == streams_.end())
    return "";

  const Http2StreamState& stream = it->second;
  int status =
      (stream.status_code > 0) ? stream.status_code : kDefaultStatusCode;

  std::string out = "HTTP/2 " + std::to_string(status) + " OK\r\n";
  for (const auto& field : stream.response_headers) {
    if (field.name.empty() || field.name.front() == ':')
      continue;
    out += field.name + ": " + field.value + "\r\n";
  }
  out += "\r\n";
  out.append(stream.response_body);
  return out;
}

size_t Http2ClientSession::ActiveStreamCount() const {
  size_t count = 0;
  for (const auto& [id, stream] : streams_) {
    if (!stream.remote_end_stream && !stream.reset_by_peer)
      ++count;
  }
  return count;
}

bool Http2ClientSession::ProcessFrame(const Http2FrameHeader& header,
                                      std::span<const uint8_t> payload) {
  if (continuation_stream_id_ != 0) {
    if (header.type != static_cast<uint8_t>(Http2FrameType::CONTINUATION) ||
        header.stream_id != continuation_stream_id_)
      return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
    return HandleContinuationFrame(header, payload);
  }

  switch (static_cast<Http2FrameType>(header.type)) {
    case Http2FrameType::DATA:
      return HandleDataFrame(header, payload);
    case Http2FrameType::HEADERS:
      return HandleHeadersFrame(header, payload);
    case Http2FrameType::PRIORITY:
      if (header.stream_id == 0)
        return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
      if (header.length != kPrioritySectionSize)
        return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);
      return true;
    case Http2FrameType::RST_STREAM:
      return HandleRstStreamFrame(header, payload);
    case Http2FrameType::SETTINGS:
      return HandleSettingsFrame(header, payload);
    case Http2FrameType::PUSH_PROMISE:
      return HandlePushPromiseFrame(header, payload);
    case Http2FrameType::PING:
      return HandlePingFrame(header, payload);
    case Http2FrameType::GOAWAY:
      return HandleGoAwayFrame(header, payload);
    case Http2FrameType::WINDOW_UPDATE:
      return HandleWindowUpdateFrame(header, payload);
    case Http2FrameType::CONTINUATION:
      return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
    default:
      // Unknown frame types MUST be ignored per RFC 9113 Section 4.1.
      return true;
  }
}

bool Http2ClientSession::HandleDataFrame(const Http2FrameHeader& header,
                                         std::span<const uint8_t> payload) {
  if (header.stream_id == 0)
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);

  if (header.length > 0) {
    connection_recv_window_ -= static_cast<int64_t>(header.length);
    unacked_connection_recv_bytes_ += header.length;
    if (unacked_connection_recv_bytes_ >= kWindowUpdateThreshold) {
      QueueFrame(BuildHttp2WindowUpdateFrame(0, unacked_connection_recv_bytes_));
      connection_recv_window_ +=
          static_cast<int64_t>(unacked_connection_recv_bytes_);
      unacked_connection_recv_bytes_ = 0;
    }
  }

  std::span<const uint8_t> data_span = payload;
  if ((header.flags & kHttp2FlagPadded) != 0) {
    if (payload.empty())
      return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
    uint8_t pad_length = payload[0];
    if (static_cast<size_t>(pad_length) + 1 > payload.size())
      return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
    data_span = payload.subspan(1, payload.size() - 1 - pad_length);
  }

  auto it = streams_.find(header.stream_id);
  if (it == streams_.end() || it->second.reset_by_peer)
    return true;

  Http2StreamState& stream = it->second;
  if (!data_span.empty())
    stream.response_body.append(
        reinterpret_cast<const char*>(data_span.data()), data_span.size());

  if (header.length > 0) {
    stream.recv_window -= static_cast<int64_t>(header.length);
    stream.unacked_recv_bytes += header.length;
  }

  if ((header.flags & kHttp2FlagEndStream) != 0) {
    stream.remote_end_stream = true;
  } else if (stream.unacked_recv_bytes >= kWindowUpdateThreshold) {
    QueueFrame(BuildHttp2WindowUpdateFrame(header.stream_id,
                                           stream.unacked_recv_bytes));
    stream.recv_window += static_cast<int64_t>(stream.unacked_recv_bytes);
    stream.unacked_recv_bytes = 0;
  }

  return true;
}

bool Http2ClientSession::HandleHeadersFrame(const Http2FrameHeader& header,
                                            std::span<const uint8_t> payload) {
  if (header.stream_id == 0)
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);

  size_t offset = 0;
  uint8_t pad_length = 0;
  if ((header.flags & kHttp2FlagPadded) != 0) {
    if (payload.empty())
      return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
    pad_length = payload[0];
    offset += 1;
  }

  if ((header.flags & kHttp2FlagPriority) != 0)
    offset += kPrioritySectionSize;

  if (offset + pad_length > payload.size())
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);

  std::span<const uint8_t> fragment =
      payload.subspan(offset, payload.size() - offset - pad_length);

  pending_header_block_.assign(fragment.begin(), fragment.end());
  continuation_stream_id_ = header.stream_id;
  continuation_end_stream_ = (header.flags & kHttp2FlagEndStream) != 0;
  continuation_is_push_promise_ = false;

  if ((header.flags & kHttp2FlagEndHeaders) != 0)
    return CompleteHeaderBlockDecode();

  return true;
}

bool Http2ClientSession::HandleContinuationFrame(
    const Http2FrameHeader& header, std::span<const uint8_t> payload) {
  pending_header_block_.insert(pending_header_block_.end(), payload.begin(),
                               payload.end());
  if ((header.flags & kHttp2FlagEndHeaders) != 0)
    return CompleteHeaderBlockDecode();
  return true;
}

bool Http2ClientSession::CompleteHeaderBlockDecode() {
  uint32_t stream_id = continuation_stream_id_;
  bool end_stream = continuation_end_stream_;
  bool is_push = continuation_is_push_promise_;
  uint32_t promised_id = promised_stream_id_;

  continuation_stream_id_ = 0;
  continuation_end_stream_ = false;
  continuation_is_push_promise_ = false;
  promised_stream_id_ = 0;

  std::vector<HeaderField> decoded_headers;
  if (!hpack_decoder_.Decode(pending_header_block_, decoded_headers)) {
    pending_header_block_.clear();
    return FailConnection(Http2ErrorCode::COMPRESSION_ERROR);
  }
  pending_header_block_.clear();

  if (is_push) {
    if (promised_id != 0)
      QueueFrame(BuildHttp2RstStreamFrame(
          promised_id, static_cast<uint32_t>(Http2ErrorCode::CANCEL)));
    return true;
  }

  auto it = streams_.find(stream_id);
  if (it == streams_.end() || it->second.reset_by_peer)
    return true;

  Http2StreamState& stream = it->second;
  if (!stream.headers_received) {
    int parsed_status = 0;
    for (const auto& field : decoded_headers) {
      if (field.name == ":status") {
        parsed_status = std::atoi(field.value.c_str());
        break;
      }
    }

    if (parsed_status >= kMinInformationalStatus &&
        parsed_status < kMinFinalStatus && !end_stream)
      return true;

    stream.status_code =
        (parsed_status > 0) ? parsed_status : kDefaultStatusCode;
    for (auto& field : decoded_headers) {
      if (!field.name.empty() && field.name.front() != ':')
        stream.response_headers.push_back(std::move(field));
    }
    stream.headers_received = true;
  }

  if (end_stream)
    stream.remote_end_stream = true;

  return true;
}

bool Http2ClientSession::HandleSettingsFrame(const Http2FrameHeader& header,
                                             std::span<const uint8_t> payload) {
  if (header.stream_id != 0)
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);

  if ((header.flags & kHttp2FlagAck) != 0) {
    if (header.length != 0)
      return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);
    peer_acked_settings_ = true;
    return true;
  }

  if (payload.size() % kSettingEntrySize != 0)
    return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);

  for (size_t offset = 0; offset < payload.size();
       offset += kSettingEntrySize) {
    uint16_t id = ReadUint16Be(payload.subspan(offset, 2));
    uint32_t value = ReadUint32Be(payload.subspan(offset + 2, 4));

    switch (static_cast<Http2SettingId>(id)) {
      case Http2SettingId::HEADER_TABLE_SIZE:
        hpack_encoder_.SetMaxTableSize(value);
        break;
      case Http2SettingId::ENABLE_PUSH:
        if (value > 1)
          return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
        break;
      case Http2SettingId::MAX_CONCURRENT_STREAMS:
        peer_max_concurrent_streams_ = value;
        break;
      case Http2SettingId::INITIAL_WINDOW_SIZE: {
        if (value > static_cast<uint32_t>(kMaxFlowControlWindow))
          return FailConnection(Http2ErrorCode::FLOW_CONTROL_ERROR);
        int64_t delta = static_cast<int64_t>(value) -
                        static_cast<int64_t>(peer_initial_window_size_);
        for (auto& [sid, stream] : streams_) {
          stream.send_window += delta;
          if (stream.send_window > kMaxFlowControlWindow)
            return FailConnection(Http2ErrorCode::FLOW_CONTROL_ERROR);
        }
        peer_initial_window_size_ = value;
        break;
      }
      case Http2SettingId::MAX_FRAME_SIZE:
        if (value < kHttp2DefaultMaxFrameSize || value > kMaxAllowedFrameSize)
          return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
        peer_max_frame_size_ = value;
        break;
      case Http2SettingId::MAX_HEADER_LIST_SIZE:
        peer_max_header_list_size_ = value;
        break;
      default:
        break;
    }
  }

  QueueFrame(BuildHttp2SettingsFrame({}, true));
  FlushPendingRequestData();
  return true;
}

bool Http2ClientSession::HandlePingFrame(const Http2FrameHeader& header,
                                         std::span<const uint8_t> payload) {
  if (header.stream_id != 0)
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
  if (payload.size() != kPingPayloadSize)
    return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);

  if ((header.flags & kHttp2FlagAck) == 0)
    QueueFrame(BuildHttp2PingFrame(payload, true));

  return true;
}

bool Http2ClientSession::HandleWindowUpdateFrame(
    const Http2FrameHeader& header, std::span<const uint8_t> payload) {
  if (payload.size() != kWindowUpdatePayloadSize)
    return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);

  uint32_t increment = ReadUint32Be(payload) & kStreamIdMask;
  if (increment == 0) {
    if (header.stream_id == 0)
      return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
    QueueFrame(BuildHttp2RstStreamFrame(
        header.stream_id,
        static_cast<uint32_t>(Http2ErrorCode::PROTOCOL_ERROR)));
    return true;
  }

  if (header.stream_id == 0) {
    connection_send_window_ += static_cast<int64_t>(increment);
    if (connection_send_window_ > kMaxFlowControlWindow)
      return FailConnection(Http2ErrorCode::FLOW_CONTROL_ERROR);
    FlushPendingRequestData();
    return true;
  }

  auto it = streams_.find(header.stream_id);
  if (it != streams_.end()) {
    it->second.send_window += static_cast<int64_t>(increment);
    if (it->second.send_window > kMaxFlowControlWindow) {
      it->second.reset_by_peer = true;
      it->second.rst_error_code =
          static_cast<uint32_t>(Http2ErrorCode::FLOW_CONTROL_ERROR);
      QueueFrame(BuildHttp2RstStreamFrame(
          header.stream_id,
          static_cast<uint32_t>(Http2ErrorCode::FLOW_CONTROL_ERROR)));
      return true;
    }
    FlushStreamRequestData(it->second);
  }

  return true;
}

bool Http2ClientSession::HandleRstStreamFrame(
    const Http2FrameHeader& header, std::span<const uint8_t> payload) {
  if (header.stream_id == 0)
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
  if (payload.size() != kRstStreamPayloadSize)
    return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);

  uint32_t error_code = ReadUint32Be(payload);
  auto it = streams_.find(header.stream_id);
  if (it != streams_.end() && !it->second.remote_end_stream) {
    it->second.reset_by_peer = true;
    it->second.rst_error_code = error_code;
  }
  return true;
}

bool Http2ClientSession::HandleGoAwayFrame(const Http2FrameHeader& header,
                                           std::span<const uint8_t> payload) {
  if (header.stream_id != 0)
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
  if (payload.size() < kGoAwayMinPayloadSize)
    return FailConnection(Http2ErrorCode::FRAME_SIZE_ERROR);

  goaway_received_ = true;
  goaway_last_stream_id_ = ReadUint32Be(payload.subspan(0, 4)) & kStreamIdMask;
  goaway_error_code_ = ReadUint32Be(payload.subspan(4, 4));
  if (goaway_error_code_ != static_cast<uint32_t>(Http2ErrorCode::NO_ERROR))
    connection_error_code_ = goaway_error_code_;

  return true;
}

bool Http2ClientSession::HandlePushPromiseFrame(
    const Http2FrameHeader& header, std::span<const uint8_t> payload) {
  if (header.stream_id == 0)
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);

  size_t offset = 0;
  uint8_t pad_length = 0;
  if ((header.flags & kHttp2FlagPadded) != 0) {
    if (payload.empty())
      return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);
    pad_length = payload[0];
    offset += 1;
  }

  if (offset + 4 + pad_length > payload.size())
    return FailConnection(Http2ErrorCode::PROTOCOL_ERROR);

  promised_stream_id_ =
      ReadUint32Be(payload.subspan(offset, 4)) & kStreamIdMask;
  offset += 4;

  std::span<const uint8_t> fragment =
      payload.subspan(offset, payload.size() - offset - pad_length);

  pending_header_block_.assign(fragment.begin(), fragment.end());
  continuation_stream_id_ = header.stream_id;
  continuation_end_stream_ = false;
  continuation_is_push_promise_ = true;

  if ((header.flags & kHttp2FlagEndHeaders) != 0)
    return CompleteHeaderBlockDecode();

  return true;
}

void Http2ClientSession::FlushPendingRequestData() {
  for (auto& [sid, stream] : streams_) {
    if (!stream.local_end_stream && !stream.reset_by_peer)
      FlushStreamRequestData(stream);
  }
}

void Http2ClientSession::FlushStreamRequestData(Http2StreamState& stream) {
  while (!stream.local_end_stream &&
        stream.pending_request_offset < stream.pending_request_body.size()) {
    int64_t available_window =
        std::min(connection_send_window_, stream.send_window);
    if (available_window <= 0)
      break;

    size_t remaining =
        stream.pending_request_body.size() - stream.pending_request_offset;
    size_t chunk_len = std::min(
        {remaining, static_cast<size_t>(available_window),
         static_cast<size_t>(peer_max_frame_size_)});
    if (chunk_len == 0)
      break;

    bool is_final_chunk =
        (stream.pending_request_offset + chunk_len >=
         stream.pending_request_body.size());

    Http2Frame data_frame;
    data_frame.header.type = static_cast<uint8_t>(Http2FrameType::DATA);
    data_frame.header.stream_id = stream.stream_id;
    data_frame.header.flags = is_final_chunk ? kHttp2FlagEndStream : 0;
    const auto* start_ptr = reinterpret_cast<const uint8_t*>(
        stream.pending_request_body.data() + stream.pending_request_offset);
    data_frame.payload.assign(start_ptr, start_ptr + chunk_len);
    QueueFrame(data_frame);

    stream.pending_request_offset += chunk_len;
    connection_send_window_ -= static_cast<int64_t>(chunk_len);
    stream.send_window -= static_cast<int64_t>(chunk_len);

    if (is_final_chunk) {
      stream.local_end_stream = true;
      stream.pending_request_body.clear();
      stream.pending_request_offset = 0;
    }
  }
}

void Http2ClientSession::QueueFrame(const Http2Frame& frame) {
  SerializeHttp2Frame(frame, send_buffer_);
}

bool Http2ClientSession::FailConnection(Http2ErrorCode error_code) {
  connection_error_ = true;
  connection_error_code_ = static_cast<uint32_t>(error_code);
  QueueFrame(BuildHttp2GoAwayFrame(0, static_cast<uint32_t>(error_code)));
  return false;
}

}  // namespace http
}  // namespace perception
