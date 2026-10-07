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

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "perception/http/hpack.h"
#include "testing.h"

namespace perception {
namespace http {
namespace {

// Client stream identifier for the first HTTP/2 request.
constexpr uint32_t kFirstClientStreamId = 1;

// Client stream identifier for the second HTTP/2 request.
constexpr uint32_t kSecondClientStreamId = 3;

// Large payload size used to test multi-frame DATA delivery and flow control.
constexpr size_t kLargeDataPayloadSize = 600000;

// Maximum frame size used in test chunks.
constexpr size_t kTestChunkSize = 16000;

// Small initial window size used to test outbound flow control blocking.
constexpr uint32_t kSmallInitialWindowSize = 10;

void AppendHeadersFrame(uint32_t stream_id,
                        std::span<const uint8_t> header_block, bool end_stream,
                        bool end_headers, std::vector<uint8_t>& out) {
  Http2Frame frame;
  frame.header.length = static_cast<uint32_t>(header_block.size());
  frame.header.type = static_cast<uint8_t>(Http2FrameType::HEADERS);
  frame.header.flags = (end_stream ? kHttp2FlagEndStream : 0) |
                       (end_headers ? kHttp2FlagEndHeaders : 0);
  frame.header.stream_id = stream_id;
  frame.payload.assign(header_block.begin(), header_block.end());
  SerializeHttp2Frame(frame, out);
}

void AppendContinuationFrame(uint32_t stream_id,
                             std::span<const uint8_t> header_block,
                             bool end_headers, std::vector<uint8_t>& out) {
  Http2Frame frame;
  frame.header.length = static_cast<uint32_t>(header_block.size());
  frame.header.type = static_cast<uint8_t>(Http2FrameType::CONTINUATION);
  frame.header.flags = end_headers ? kHttp2FlagEndHeaders : 0;
  frame.header.stream_id = stream_id;
  frame.payload.assign(header_block.begin(), header_block.end());
  SerializeHttp2Frame(frame, out);
}

void AppendDataFrame(uint32_t stream_id, std::span<const uint8_t> data,
                     bool end_stream, std::vector<uint8_t>& out) {
  Http2Frame frame;
  frame.header.length = static_cast<uint32_t>(data.size());
  frame.header.type = static_cast<uint8_t>(Http2FrameType::DATA);
  frame.header.flags = end_stream ? kHttp2FlagEndStream : 0;
  frame.header.stream_id = stream_id;
  frame.payload.assign(data.begin(), data.end());
  SerializeHttp2Frame(frame, out);
}

TEST(HpackIntegerEncodeDecode) {
  std::vector<uint8_t> buf;
  EncodeHpackInteger(10, 5, 0x00, buf);
  EXPECT(1u, buf.size());
  EXPECT(10u, static_cast<unsigned int>(buf[0]));

  size_t offset = 0;
  uint64_t val = 0;
  EXPECT(true, DecodeHpackInteger(buf, offset, 5, val));
  EXPECT(10u, val);
  EXPECT(1u, offset);

  buf.clear();
  EncodeHpackInteger(1337, 5, 0x00, buf);
  EXPECT(3u, buf.size());
  EXPECT(31u, static_cast<unsigned int>(buf[0]));
  EXPECT(154u, static_cast<unsigned int>(buf[1]));
  EXPECT(10u, static_cast<unsigned int>(buf[2]));

  offset = 0;
  EXPECT(true, DecodeHpackInteger(buf, offset, 5, val));
  EXPECT(1337u, val);
  EXPECT(3u, offset);

  buf.clear();
  EncodeHpackInteger(42, 8, 0x00, buf);
  EXPECT(1u, buf.size());
  EXPECT(42u, static_cast<unsigned int>(buf[0]));

  offset = 0;
  EXPECT(true, DecodeHpackInteger(buf, offset, 8, val));
  EXPECT(42u, val);
}

TEST(HpackHuffmanEncodeDecodeRFC7541Examples) {
  // RFC 7541 C.4.1: "www.example.com" -> f1e3 c2e5 f23a 6ba0 ab90 f4ff
  const std::vector<uint8_t> kExpectedExampleCom = {
      0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
  std::vector<uint8_t> encoded;
  EncodeHpackHuffman("www.example.com", encoded);
  ASSERT(kExpectedExampleCom.size(), encoded.size());
  for (size_t i = 0; i < encoded.size(); ++i)
    EXPECT(kExpectedExampleCom[i], encoded[i]);

  std::string decoded;
  EXPECT(true, DecodeHpackHuffman(encoded, decoded));
  EXPECT(std::string("www.example.com"), decoded);

  // Round-trip all 256 possible byte values.
  std::string all_bytes(256, '\0');
  for (size_t i = 0; i < 256; ++i)
    all_bytes[i] = static_cast<char>(i);
  std::vector<uint8_t> all_encoded;
  EncodeHpackHuffman(all_bytes, all_encoded);
  std::string all_decoded;
  EXPECT(true, DecodeHpackHuffman(all_encoded, all_decoded));
  EXPECT(all_bytes, all_decoded);

  // Invalid padding (non-1s padding bits) must fail decoding.
  const std::vector<uint8_t> kInvalidPadding = {0x00};  // '0' (5 bits) + 000
  EXPECT(false, DecodeHpackHuffman(kInvalidPadding, decoded));
}

TEST(HpackRfc7541AppendixC4RequestExamplesWithHuffman) {
  HpackDecoder decoder;

  // C.4.1 First Request
  const std::vector<uint8_t> kBlock1 = {
      0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5,
      0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
  std::vector<HeaderField> headers1;
  EXPECT(true, decoder.Decode(kBlock1, headers1));
  ASSERT(4u, headers1.size());
  EXPECT(std::string(":method"), headers1[0].name);
  EXPECT(std::string("GET"), headers1[0].value);
  EXPECT(std::string(":scheme"), headers1[1].name);
  EXPECT(std::string("http"), headers1[1].value);
  EXPECT(std::string(":path"), headers1[2].name);
  EXPECT(std::string("/"), headers1[2].value);
  EXPECT(std::string(":authority"), headers1[3].name);
  EXPECT(std::string("www.example.com"), headers1[3].value);
  EXPECT(57u, decoder.DynamicTable().CurrentSize());

  // C.4.2 Second Request
  const std::vector<uint8_t> kBlock2 = {
      0x82, 0x86, 0x84, 0xbe, 0x58, 0x86, 0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf};
  std::vector<HeaderField> headers2;
  EXPECT(true, decoder.Decode(kBlock2, headers2));
  ASSERT(5u, headers2.size());
  EXPECT(std::string(":authority"), headers2[3].name);
  EXPECT(std::string("www.example.com"), headers2[3].value);
  EXPECT(std::string("cache-control"), headers2[4].name);
  EXPECT(std::string("no-cache"), headers2[4].value);
  EXPECT(110u, decoder.DynamicTable().CurrentSize());

  // C.4.3 Third Request
  const std::vector<uint8_t> kBlock3 = {
      0x82, 0x87, 0x85, 0xbf, 0x40, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b,
      0xa9, 0x7d, 0x7f, 0x89, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8,
      0xb4, 0xbf};
  std::vector<HeaderField> headers3;
  EXPECT(true, decoder.Decode(kBlock3, headers3));
  ASSERT(5u, headers3.size());
  EXPECT(std::string(":scheme"), headers3[1].name);
  EXPECT(std::string("https"), headers3[1].value);
  EXPECT(std::string(":path"), headers3[2].name);
  EXPECT(std::string("/index.html"), headers3[2].value);
  EXPECT(std::string("custom-key"), headers3[4].name);
  EXPECT(std::string("custom-value"), headers3[4].value);
  EXPECT(164u, decoder.DynamicTable().CurrentSize());
}

TEST(HpackEncodeAndDecodeRoundTripWithDynamicTableEviction) {
  HpackEncoder encoder(128, true);
  HpackDecoder decoder(128);

  std::vector<HeaderField> req1 = {
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "perception-os.dev"},
      {":path", "/api/v1/resource"},
      {"user-agent", "NetSurf/3.11 (Perception)"},
      {"accept-encoding", "gzip, deflate"},
      {"x-custom-header-1", "value-1-that-takes-space"},
      {"x-custom-header-2", "value-2-that-causes-eviction"},
  };

  std::vector<uint8_t> block1;
  encoder.Encode(req1, block1);
  std::vector<HeaderField> decoded1;
  EXPECT(true, decoder.Decode(block1, decoded1));
  ASSERT(req1.size(), decoded1.size());
  for (size_t i = 0; i < req1.size(); ++i) {
    EXPECT(req1[i].name, decoded1[i].name);
    EXPECT(req1[i].value, decoded1[i].value);
  }
  EXPECT(true, encoder.DynamicTable().CurrentSize() <= 128u);
  EXPECT(encoder.DynamicTable().CurrentSize(),
         decoder.DynamicTable().CurrentSize());

  // Second request reuses dynamic table entries that survived eviction.
  std::vector<uint8_t> block2;
  encoder.Encode(req1, block2);
  std::vector<HeaderField> decoded2;
  EXPECT(true, decoder.Decode(block2, decoded2));
  ASSERT(req1.size(), decoded2.size());
  for (size_t i = 0; i < req1.size(); ++i) {
    EXPECT(req1[i].name, decoded2[i].name);
    EXPECT(req1[i].value, decoded2[i].value);
  }
}

TEST(Http2SerializeAndParseFrames) {
  std::vector<uint8_t> wire;
  std::vector<std::pair<uint16_t, uint32_t>> settings = {
      {static_cast<uint16_t>(Http2SettingId::MAX_CONCURRENT_STREAMS), 100},
      {static_cast<uint16_t>(Http2SettingId::INITIAL_WINDOW_SIZE), 65535},
  };
  SerializeHttp2Frame(BuildHttp2SettingsFrame(settings, false), wire);
  uint8_t ping_opaque[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  SerializeHttp2Frame(BuildHttp2PingFrame(ping_opaque, true), wire);
  SerializeHttp2Frame(
      BuildHttp2WindowUpdateFrame(kFirstClientStreamId, 32768), wire);

  Http2FrameHeader h1;
  EXPECT(true, ParseHttp2FrameHeader(wire, h1));
  EXPECT(static_cast<uint8_t>(Http2FrameType::SETTINGS), h1.type);
  EXPECT(12u, h1.length);
  EXPECT(0u, h1.stream_id);

  size_t offset = kHttp2FrameHeaderSize + h1.length;
  Http2FrameHeader h2;
  EXPECT(true,
         ParseHttp2FrameHeader(
             std::span<const uint8_t>(wire.data() + offset, wire.size() - offset),
             h2));
  EXPECT(static_cast<uint8_t>(Http2FrameType::PING), h2.type);
  EXPECT(kHttp2FlagAck, h2.flags);
  EXPECT(8u, h2.length);
  EXPECT(8u, static_cast<unsigned int>(wire[offset + kHttp2FrameHeaderSize + 7]));

  offset += kHttp2FrameHeaderSize + h2.length;
  Http2FrameHeader h3;
  EXPECT(true,
         ParseHttp2FrameHeader(
             std::span<const uint8_t>(wire.data() + offset, wire.size() - offset),
             h3));
  EXPECT(static_cast<uint8_t>(Http2FrameType::WINDOW_UPDATE), h3.type);
  EXPECT(kFirstClientStreamId, h3.stream_id);
}

TEST(Http2FullClientRequestResponseAndContinuationAndFlowControl) {
  Http2ClientSession client;
  client.Initialize();

  // Client must queue the 24-byte preface + initial SETTINGS + WINDOW_UPDATE.
  std::span<const uint8_t> initial_send = client.PendingSendBytes();
  EXPECT(true, initial_send.size() > kHttp2ConnectionPreface.size());
  std::string preface_prefix(
      reinterpret_cast<const char*>(initial_send.data()),
      kHttp2ConnectionPreface.size());
  EXPECT(std::string(kHttp2ConnectionPreface), preface_prefix);
  client.ConsumePendingSendBytes(initial_send.size());

  // Open a GET request stream.
  std::vector<HeaderField> extra_headers = {
      {"User-Agent", "NetSurf/3.11"},
      {"Accept", "text/html"},
  };
  uint32_t stream_id = client.OpenStream(
      "GET", "https", "example.com", "/index.html", extra_headers, "");
  EXPECT(kFirstClientStreamId, stream_id);
  EXPECT(true, client.PendingSendBytes().size() > 0u);
  client.ConsumePendingSendBytes(client.PendingSendBytes().size());

  // Server sends its initial SETTINGS frame, SETTINGS ACK, and a PING frame.
  std::vector<uint8_t> server_to_client;
  std::vector<std::pair<uint16_t, uint32_t>> server_settings = {
      {static_cast<uint16_t>(Http2SettingId::MAX_CONCURRENT_STREAMS), 128},
  };
  SerializeHttp2Frame(BuildHttp2SettingsFrame(server_settings, false),
                      server_to_client);
  SerializeHttp2Frame(BuildHttp2SettingsFrame({}, true), server_to_client);
  uint8_t ping_data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  SerializeHttp2Frame(BuildHttp2PingFrame(ping_data, false), server_to_client);

  EXPECT(true, client.ReceiveBytes(server_to_client));
  EXPECT(true, client.PeerAckedSettings());
  EXPECT(128u, client.PeerMaxConcurrentStreams());

  // Client must have queued a SETTINGS ACK (9 bytes) and a PING ACK (17 bytes).
  std::span<const uint8_t> ack_bytes = client.PendingSendBytes();
  EXPECT(26u, ack_bytes.size());
  client.ConsumePendingSendBytes(ack_bytes.size());

  // Server sends response headers split across HEADERS + CONTINUATION,
  // preceded by a 100 Continue informational HEADERS block.
  HpackEncoder server_encoder(kDefaultHpackDynamicTableSize, true);
  std::vector<HeaderField> info_headers = {{":status", "100"}};
  std::vector<uint8_t> info_block;
  server_encoder.Encode(info_headers, info_block);

  std::vector<HeaderField> resp_headers = {
      {":status", "200"},
      {"content-type", "text/html; charset=utf-8"},
      {"x-split-header", "split-across-continuation"},
  };
  std::vector<uint8_t> resp_block;
  server_encoder.Encode(resp_headers, resp_block);
  size_t split_point = resp_block.size() / 2;

  server_to_client.clear();
  AppendHeadersFrame(stream_id, info_block, false, true, server_to_client);
  AppendHeadersFrame(
      stream_id,
      std::span<const uint8_t>(resp_block.data(), split_point),
      false, false, server_to_client);
  AppendContinuationFrame(
      stream_id,
      std::span<const uint8_t>(resp_block.data() + split_point,
                               resp_block.size() - split_point),
      true, server_to_client);

  EXPECT(true, client.ReceiveBytes(server_to_client));
  EXPECT(false, client.IsStreamComplete(stream_id));

  // Server sends a large 600 KB response body in 16 KB DATA chunks.
  std::string full_body(kLargeDataPayloadSize, 'A');
  server_to_client.clear();
  size_t sent = 0;
  while (sent < kLargeDataPayloadSize) {
    size_t chunk = std::min(kTestChunkSize, kLargeDataPayloadSize - sent);
    bool end_stream = (sent + chunk == kLargeDataPayloadSize);
    AppendDataFrame(
        stream_id,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(full_body.data() + sent), chunk),
        end_stream, server_to_client);
    sent += chunk;
  }

  EXPECT(true, client.ReceiveBytes(server_to_client));
  EXPECT(true, client.IsStreamComplete(stream_id));

  const Http2StreamState* stream = client.GetStream(stream_id);
  ASSERT(true, stream != nullptr);
  EXPECT(200, stream->status_code);
  EXPECT(kLargeDataPayloadSize, stream->response_body.size());

  // Verify HTTP/1.1 compatibility synthesis for NetSurf's header parser.
  std::string synth = client.FormatStreamAsHttp1Response(stream_id);
  EXPECT(true, synth.find("200") != std::string::npos);
  EXPECT(true,
         synth.find("content-type: text/html; charset=utf-8\r\n") !=
             std::string::npos);
  EXPECT(synth.find("\r\n\r\n") + 4 + kLargeDataPayloadSize, synth.size());
}

TEST(Http2OutboundPostFlowControlAndWindowUpdate) {
  Http2ClientSession client;
  client.Initialize();
  client.ConsumePendingSendBytes(client.PendingSendBytes().size());

  // Server sets initial window size to 10 bytes.
  std::vector<uint8_t> server_frames;
  std::vector<std::pair<uint16_t, uint32_t>> server_settings = {
      {static_cast<uint16_t>(Http2SettingId::INITIAL_WINDOW_SIZE),
       kSmallInitialWindowSize},
  };
  SerializeHttp2Frame(BuildHttp2SettingsFrame(server_settings, false),
                      server_frames);
  EXPECT(true, client.ReceiveBytes(server_frames));
  client.ConsumePendingSendBytes(client.PendingSendBytes().size());

  // Client opens a POST request with a 25-byte body.
  std::string post_body = "0123456789abcdefghijklmno";
  std::vector<HeaderField> post_headers = {{"content-type", "text/plain"}};
  uint32_t stream_id = client.OpenStream(
      "POST", "https", "example.com", "/submit", post_headers, post_body);
  EXPECT(kFirstClientStreamId, stream_id);

  // Only the HEADERS frame and the first 10 bytes of DATA can be sent now.
  const Http2StreamState* stream = client.GetStream(stream_id);
  ASSERT(true, stream != nullptr);
  EXPECT(10u, stream->pending_request_offset);
  EXPECT(false, stream->local_end_stream);
  client.ConsumePendingSendBytes(client.PendingSendBytes().size());

  // Server grants 20 more bytes on the stream via WINDOW_UPDATE.
  server_frames.clear();
  SerializeHttp2Frame(BuildHttp2WindowUpdateFrame(stream_id, 20),
                      server_frames);
  EXPECT(true, client.ReceiveBytes(server_frames));

  // Remaining 15 bytes should now be flushed with END_STREAM.
  EXPECT(true, stream->local_end_stream);
  EXPECT(true, client.PendingSendBytes().size() > 0u);
}

TEST(Http2GoawayMarksHigherUnprocessedStreamsFailed) {
  Http2ClientSession client;
  client.Initialize();
  client.ConsumePendingSendBytes(client.PendingSendBytes().size());

  uint32_t s1 = client.OpenStream("GET", "https", "example.com", "/1", {}, "");
  uint32_t s2 = client.OpenStream("GET", "https", "example.com", "/2", {}, "");
  EXPECT(kFirstClientStreamId, s1);
  EXPECT(kSecondClientStreamId, s2);

  // Server sends GOAWAY with last_stream_id = 1.
  std::vector<uint8_t> goaway;
  SerializeHttp2Frame(
      BuildHttp2GoAwayFrame(kFirstClientStreamId,
                            static_cast<uint32_t>(Http2ErrorCode::NO_ERROR)),
      goaway);
  EXPECT(true, client.ReceiveBytes(goaway));

  EXPECT(false, client.IsStreamFailed(s1));
  EXPECT(true, client.IsStreamFailed(s2));
}

}  // namespace
}  // namespace http
}  // namespace perception
