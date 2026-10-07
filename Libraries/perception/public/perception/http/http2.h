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

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perception/http/hpack.h"

namespace perception {
namespace http {

namespace {

// Size in bytes of an HTTP/2 frame header (RFC 9113 Section 4.1).
constexpr size_t kHttp2FrameHeaderSize = 9;

// Default initial flow-control window size in bytes (RFC 9113 Section 6.9.2).
constexpr int64_t kHttp2DefaultInitialWindowSize = 65535;

// Local initial stream receive window size advertised in client SETTINGS (1 MB).
constexpr uint32_t kHttp2LocalInitialStreamWindowSize = 1048576;

// Local target connection receive window size maintained via WINDOW_UPDATE (16 MB).
constexpr uint32_t kHttp2LocalConnectionWindowSize = 16777216;

// Default maximum frame payload size in bytes (RFC 9113 Section 4.2).
constexpr uint32_t kHttp2DefaultMaxFrameSize = 16384;

// Default maximum concurrent streams before receiving server SETTINGS.
constexpr uint32_t kHttp2DefaultMaxConcurrentStreams = 100;

// HTTP/2 frame flag indicating end of stream (DATA, HEADERS).
constexpr uint8_t kHttp2FlagEndStream = 0x01;

// HTTP/2 frame flag indicating acknowledgement (SETTINGS, PING).
constexpr uint8_t kHttp2FlagAck = 0x01;

// HTTP/2 frame flag indicating end of header block (HEADERS, PUSH_PROMISE, CONTINUATION).
constexpr uint8_t kHttp2FlagEndHeaders = 0x04;

// HTTP/2 frame flag indicating frame payload is padded (DATA, HEADERS, PUSH_PROMISE).
constexpr uint8_t kHttp2FlagPadded = 0x08;

// HTTP/2 frame flag indicating priority fields are present (HEADERS).
constexpr uint8_t kHttp2FlagPriority = 0x20;

// HTTP/2 client connection preface string (RFC 9113 Section 3.4).
constexpr std::string_view kHttp2ConnectionPreface =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

}  // namespace

// HTTP/2 frame types (RFC 9113 Section 6).
enum class Http2FrameType : uint8_t {
  DATA = 0x0,
  HEADERS = 0x1,
  PRIORITY = 0x2,
  RST_STREAM = 0x3,
  SETTINGS = 0x4,
  PUSH_PROMISE = 0x5,
  PING = 0x6,
  GOAWAY = 0x7,
  WINDOW_UPDATE = 0x8,
  CONTINUATION = 0x9,
};

// HTTP/2 error codes (RFC 9113 Section 7).
enum class Http2ErrorCode : uint32_t {
  NO_ERROR = 0x0,
  PROTOCOL_ERROR = 0x1,
  INTERNAL_ERROR = 0x2,
  FLOW_CONTROL_ERROR = 0x3,
  SETTINGS_TIMEOUT = 0x4,
  STREAM_CLOSED = 0x5,
  FRAME_SIZE_ERROR = 0x6,
  REFUSED_STREAM = 0x7,
  CANCEL = 0x8,
  COMPRESSION_ERROR = 0x9,
  CONNECT_ERROR = 0xa,
  ENHANCE_YOUR_CALM = 0xb,
  INADEQUATE_SECURITY = 0xc,
  HTTP_1_1_REQUIRED = 0xd,
};

// HTTP/2 SETTINGS identifiers (RFC 9113 Section 6.5.2).
enum class Http2SettingId : uint16_t {
  HEADER_TABLE_SIZE = 0x1,
  ENABLE_PUSH = 0x2,
  MAX_CONCURRENT_STREAMS = 0x3,
  INITIAL_WINDOW_SIZE = 0x4,
  MAX_FRAME_SIZE = 0x5,
  MAX_HEADER_LIST_SIZE = 0x6,
};

// Represents the 9-byte header of an HTTP/2 frame.
struct Http2FrameHeader {
  uint32_t length = 0;
  uint8_t type = 0;
  uint8_t flags = 0;
  uint32_t stream_id = 0;
};

// Represents a complete HTTP/2 frame with header and payload.
struct Http2Frame {
  Http2FrameHeader header;
  std::vector<uint8_t> payload;
};

// Tracks the state of an individual HTTP/2 request/response stream.
struct Http2StreamState {
  uint32_t stream_id = 0;
  int status_code = 0;
  bool headers_received = false;
  bool remote_end_stream = false;
  bool local_end_stream = false;
  bool reset_by_peer = false;
  uint32_t rst_error_code = 0;
  std::vector<HeaderField> response_headers;
  std::string response_body;
  std::string pending_request_body;
  size_t pending_request_offset = 0;
  int64_t send_window = kHttp2DefaultInitialWindowSize;
  int64_t recv_window = kHttp2LocalInitialStreamWindowSize;
  uint32_t unacked_recv_bytes = 0;
};

// Serializes a 9-byte HTTP/2 frame header and appends it to output.
void SerializeHttp2FrameHeader(const Http2FrameHeader& header,
                               std::vector<uint8_t>& output);

// Parses a 9-byte HTTP/2 frame header from the start of input.
bool ParseHttp2FrameHeader(std::span<const uint8_t> input,
                           Http2FrameHeader& header);

// Serializes a complete HTTP/2 frame (header + payload) and appends to output.
void SerializeHttp2Frame(const Http2Frame& frame,
                         std::vector<uint8_t>& output);

// Builds an HTTP/2 SETTINGS frame (or SETTINGS ACK if ack is true).
Http2Frame BuildHttp2SettingsFrame(
    std::span<const std::pair<uint16_t, uint32_t>> settings, bool ack = false);

// Builds an HTTP/2 WINDOW_UPDATE frame for the given stream (or 0 for connection).
Http2Frame BuildHttp2WindowUpdateFrame(uint32_t stream_id, uint32_t increment);

// Builds an HTTP/2 PING frame (or PING ACK if ack is true).
Http2Frame BuildHttp2PingFrame(std::span<const uint8_t> opaque_data,
                               bool ack = false);

// Builds an HTTP/2 RST_STREAM frame.
Http2Frame BuildHttp2RstStreamFrame(uint32_t stream_id, uint32_t error_code);

// Builds an HTTP/2 GOAWAY frame.
Http2Frame BuildHttp2GoAwayFrame(uint32_t last_stream_id, uint32_t error_code);

// Normalizes and filters HTTP/1.1 request headers for HTTP/2 compliance
// (lowercasing header names and stripping connection-specific headers).
std::vector<HeaderField> SanitizeRequestHeadersForHttp2(
    std::span<const HeaderField> raw_headers);

// Manages an HTTP/2 client connection state machine, streams, and flow control.
class Http2ClientSession {
 public:
  Http2ClientSession();

  // Queues the HTTP/2 connection preface, initial SETTINGS, and connection
  // WINDOW_UPDATE frames.
  void Initialize();

  // Returns true if the session has been initialized.
  bool IsInitialized() const { return initialized_; }

  // Returns true if a new request stream can be opened on this connection.
  bool CanOpenNewStream() const;

  // Opens a new client request stream, queues its HEADERS (and DATA) frames,
  // and returns the assigned odd stream ID (or 0 if a stream cannot be opened).
  uint32_t OpenStream(std::string_view method, std::string_view scheme,
                      std::string_view authority, std::string_view path,
                      std::span<const HeaderField> headers,
                      std::string_view body = "");

  // Cancels an active stream by sending RST_STREAM(CANCEL) and removing it.
  void CancelStream(uint32_t stream_id);

  // Removes a completed or failed stream from the session's state map.
  void RemoveStream(uint32_t stream_id);

  // Feeds incoming bytes from the transport into the HTTP/2 frame parser.
  // Returns false if a fatal connection-level protocol error occurred.
  bool ReceiveBytes(std::span<const uint8_t> data);

  // Returns true if there are serialized frame bytes waiting to be sent.
  bool HasPendingSendBytes() const {
    return send_offset_ < send_buffer_.size();
  }

  // Returns a view of the bytes waiting to be written to the transport.
  std::span<const uint8_t> PendingSendBytes() const;

  // Advances the send buffer after count bytes have been written.
  void ConsumePendingSendBytes(size_t count);

  // Returns the state of stream_id, or nullptr if not found.
  const Http2StreamState* GetStream(uint32_t stream_id) const;

  // Returns true if stream_id has received its complete response.
  bool IsStreamComplete(uint32_t stream_id) const;

  // Returns true if stream_id failed due to RST_STREAM, GOAWAY, or session error.
  bool IsStreamFailed(uint32_t stream_id) const;

  // Formats the response headers and body of stream_id as an HTTP/1.1-compatible
  // raw response buffer ("HTTP/2 <status> OK\r\n<headers>\r\n\r\n<body>").
  std::string FormatStreamAsHttp1Response(uint32_t stream_id) const;

  // Returns the number of currently active (unfinished) streams.
  size_t ActiveStreamCount() const;

  // Returns true if the session encountered a connection error.
  bool HasConnectionError() const { return connection_error_; }

  // Returns the HTTP/2 error code if a connection error or GOAWAY occurred.
  uint32_t ConnectionErrorCode() const { return connection_error_code_; }

  // Returns true if a GOAWAY frame has been received from the server.
  bool IsGoAwayReceived() const { return goaway_received_; }

  // Returns true if the server acknowledged the client's initial SETTINGS.
  bool PeerAckedSettings() const { return peer_acked_settings_; }

  // Returns the peer's advertised maximum concurrent streams.
  uint32_t PeerMaxConcurrentStreams() const {
    return peer_max_concurrent_streams_;
  }

  // Returns the peer's advertised maximum frame size.
  uint32_t PeerMaxFrameSize() const { return peer_max_frame_size_; }

  // Returns the current connection-level send window.
  int64_t ConnectionSendWindow() const { return connection_send_window_; }

  // Returns the current connection-level receive window.
  int64_t ConnectionRecvWindow() const { return connection_recv_window_; }

 private:
  bool ProcessFrame(const Http2FrameHeader& header,
                    std::span<const uint8_t> payload);
  bool HandleDataFrame(const Http2FrameHeader& header,
                       std::span<const uint8_t> payload);
  bool HandleHeadersFrame(const Http2FrameHeader& header,
                          std::span<const uint8_t> payload);
  bool HandleContinuationFrame(const Http2FrameHeader& header,
                               std::span<const uint8_t> payload);
  bool HandleSettingsFrame(const Http2FrameHeader& header,
                           std::span<const uint8_t> payload);
  bool HandlePingFrame(const Http2FrameHeader& header,
                       std::span<const uint8_t> payload);
  bool HandleWindowUpdateFrame(const Http2FrameHeader& header,
                               std::span<const uint8_t> payload);
  bool HandleRstStreamFrame(const Http2FrameHeader& header,
                            std::span<const uint8_t> payload);
  bool HandleGoAwayFrame(const Http2FrameHeader& header,
                         std::span<const uint8_t> payload);
  bool HandlePushPromiseFrame(const Http2FrameHeader& header,
                              std::span<const uint8_t> payload);
  bool CompleteHeaderBlockDecode();
  void FlushPendingRequestData();
  void FlushStreamRequestData(Http2StreamState& stream);
  void QueueFrame(const Http2Frame& frame);
  bool FailConnection(Http2ErrorCode error_code);

  bool initialized_ = false;
  bool connection_error_ = false;
  uint32_t connection_error_code_ = 0;
  bool goaway_received_ = false;
  uint32_t goaway_last_stream_id_ = 0x7FFFFFFFu;
  uint32_t goaway_error_code_ = 0;
  bool peer_acked_settings_ = false;

  uint32_t next_stream_id_ = 1;
  uint32_t peer_max_concurrent_streams_ = kHttp2DefaultMaxConcurrentStreams;
  uint32_t peer_initial_window_size_ = kHttp2DefaultInitialWindowSize;
  uint32_t peer_max_frame_size_ = kHttp2DefaultMaxFrameSize;
  uint32_t peer_max_header_list_size_ = 0;

  int64_t connection_send_window_ = kHttp2DefaultInitialWindowSize;
  int64_t connection_recv_window_ = kHttp2DefaultInitialWindowSize;
  uint32_t unacked_connection_recv_bytes_ = 0;

  uint32_t continuation_stream_id_ = 0;
  bool continuation_end_stream_ = false;
  bool continuation_is_push_promise_ = false;
  uint32_t promised_stream_id_ = 0;
  std::vector<uint8_t> pending_header_block_;

  HpackEncoder hpack_encoder_;
  HpackDecoder hpack_decoder_;

  std::unordered_map<uint32_t, Http2StreamState> streams_;
  std::vector<uint8_t> send_buffer_;
  size_t send_offset_ = 0;
  std::vector<uint8_t> recv_buffer_;
  size_t recv_offset_ = 0;
};

}  // namespace http
}  // namespace perception
