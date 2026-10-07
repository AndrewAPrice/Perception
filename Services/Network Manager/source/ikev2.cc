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

#include "ikev2.h"

#include <algorithm>
#include <cstring>

#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Size of the fixed IKEv2 header in bytes (RFC 7296 §3.1).
constexpr size_t kIkeHeaderSize = 28;

// Size of a generic IKEv2 payload header in bytes (RFC 7296 §3.2).
constexpr size_t kIkePayloadHeaderSize = 4;

// IKEv2 major/minor version byte (2.0 = 0x20).
constexpr uint8 kIkeVersion20 = 0x20;

// IKEv2 header flag: Initiator (I).
constexpr uint8 kIkeFlagInitiator = 0x08;

// IKEv2 header flag: Response (R).
constexpr uint8 kIkeFlagResponse = 0x20;

// Explicit IV size inside an IKEv2 Encrypted (SK) payload.
constexpr size_t kIkeSkIvSize = 8;

// AEAD tag size inside an IKEv2 Encrypted (SK) payload.
constexpr size_t kIkeSkTagSize = 16;

// EAP Code: Request.
constexpr uint8 kEapCodeRequest = 1;

// EAP Code: Response.
constexpr uint8 kEapCodeResponse = 2;

// EAP Code: Success.
constexpr uint8 kEapCodeSuccess = 3;

// Writes a 64-bit big-endian integer using `writer`.
void WriteU64(WireWriter& writer, uint64 value) {
  writer.WriteU32(static_cast<uint32>(value >> 32));
  writer.WriteU32(static_cast<uint32>(value & 0xFFFFFFFFULL));
}

// Reads a 64-bit big-endian integer using `reader`.
uint64 ReadU64(WireReader& reader) {
  uint64 high = reader.ReadU32();
  uint64 low = reader.ReadU32();
  return (high << 32) | low;
}

// Encodes a chain of IKEv2 payloads into raw bytes.
std::string EncodePayloadChain(const std::vector<IkePayload>& payloads) {
  std::string out;
  for (size_t i = 0; i < payloads.size(); i++) {
    uint8 next_type = (i + 1 < payloads.size())
                          ? static_cast<uint8>(payloads[i + 1].type)
                          : static_cast<uint8>(IkePayloadType::NoNextPayload);
    uint16 length =
        static_cast<uint16>(kIkePayloadHeaderSize + payloads[i].body.size());
    WireWriter writer(out);
    writer.WriteU8(next_type);
    writer.WriteU8(payloads[i].critical ? 0x80 : 0x00);
    writer.WriteU16(length);
    writer.WriteBytes(payloads[i].body);
  }
  return out;
}

// Decodes a chain of IKEv2 payloads starting with `first_payload_type`.
bool DecodePayloadChain(uint8 first_payload_type, std::string_view bytes,
                        std::vector<IkePayload>& out_payloads) {
  WireReader reader(bytes);
  uint8 current_type = first_payload_type;
  while (current_type != static_cast<uint8>(IkePayloadType::NoNextPayload)) {
    if (reader.Remaining() < kIkePayloadHeaderSize) return false;
    uint8 next_type = reader.ReadU8();
    uint8 flags = reader.ReadU8();
    uint16 length = reader.ReadU16();
    if (length < kIkePayloadHeaderSize) return false;
    std::string_view body = reader.ReadBytes(length - kIkePayloadHeaderSize);
    if (!reader.ok()) return false;
    IkePayload payload;
    payload.type = static_cast<IkePayloadType>(current_type);
    payload.critical = (flags & 0x80) != 0;
    payload.body.assign(body);
    out_payloads.push_back(std::move(payload));
    current_type = next_type;
  }
  return true;
}

// Finds the first payload of `type` in `message`.
const IkePayload* FindPayload(const IkeMessage& message, IkePayloadType type) {
  for (const IkePayload& p : message.payloads) {
    if (p.type == type) return &p;
  }
  return nullptr;
}

// Builds a 12-byte AEAD nonce from `salt` and `message_id`.
std::array<uint8, 12> MakeIkeNonce(const std::array<uint8, 4>& salt,
                                   uint64 explicit_iv) {
  std::array<uint8, 12> nonce{};
  std::copy(salt.begin(), salt.end(), nonce.begin());
  for (int i = 0; i < 8; i++)
    nonce[4 + i] = static_cast<uint8>((explicit_iv >> ((7 - i) * 8)) & 0xFF);
  return nonce;
}

}  // namespace

std::string SerializeIkeMessage(
    const IkeMessage& message,
    const std::optional<std::array<uint8, 32>>& sk_key,
    const std::array<uint8, 4>& sk_salt) {
  uint8 flags = 0;
  if (message.is_initiator) flags |= kIkeFlagInitiator;
  if (message.is_response) flags |= kIkeFlagResponse;

  std::string inner_chain = EncodePayloadChain(message.payloads);
  uint8 first_payload = message.payloads.empty()
                            ? static_cast<uint8>(IkePayloadType::NoNextPayload)
                            : static_cast<uint8>(message.payloads[0].type);

  if (!sk_key.has_value()) {
    std::string out;
    out.reserve(kIkeHeaderSize + inner_chain.size());
    WireWriter writer(out);
    WriteU64(writer, message.initiator_spi);
    WriteU64(writer, message.responder_spi);
    writer.WriteU8(first_payload);
    writer.WriteU8(kIkeVersion20);
    writer.WriteU8(static_cast<uint8>(message.exchange_type));
    writer.WriteU8(flags);
    writer.WriteU32(message.message_id);
    writer.WriteU32(static_cast<uint32>(kIkeHeaderSize + inner_chain.size()));
    writer.WriteBytes(inner_chain);
    return out;
  }

  // Encrypt the payload chain inside an Encrypted and Authenticated (SK, 46)
  // payload per RFC 7296 §3.14 (with 0-byte pad, pad_length = 0).
  inner_chain.push_back('\0');
  uint16 sk_payload_len = static_cast<uint16>(
      kIkePayloadHeaderSize + kIkeSkIvSize + inner_chain.size() + kIkeSkTagSize);
  uint32 total_len = static_cast<uint32>(kIkeHeaderSize + sk_payload_len);

  std::string aad;
  aad.reserve(kIkeHeaderSize + kIkePayloadHeaderSize);
  WireWriter aad_writer(aad);
  WriteU64(aad_writer, message.initiator_spi);
  WriteU64(aad_writer, message.responder_spi);
  aad_writer.WriteU8(
      static_cast<uint8>(IkePayloadType::EncryptedAndAuthenticated));
  aad_writer.WriteU8(kIkeVersion20);
  aad_writer.WriteU8(static_cast<uint8>(message.exchange_type));
  aad_writer.WriteU8(flags);
  aad_writer.WriteU32(message.message_id);
  aad_writer.WriteU32(total_len);
  aad_writer.WriteU8(first_payload);
  aad_writer.WriteU8(0);
  aad_writer.WriteU16(sk_payload_len);

  uint64 explicit_iv = (static_cast<uint64>(message.is_response ? 1 : 0) << 32) |
                       message.message_id;
  auto nonce = MakeIkeNonce(sk_salt, explicit_iv);
  std::string ct_and_tag = AeadEncrypt(EspCipherSuite::ChaCha20Poly1305,
                                       *sk_key, nonce, aad, inner_chain);

  std::string out = std::move(aad);
  WireWriter out_writer(out);
  WriteU64(out_writer, explicit_iv);
  out_writer.WriteBytes(ct_and_tag);
  return out;
}

std::optional<IkeMessage> ParseIkeMessage(
    std::string_view packet,
    const std::optional<std::array<uint8, 32>>& sk_key,
    const std::array<uint8, 4>& sk_salt) {
  if (packet.size() < kIkeHeaderSize) return std::nullopt;
  WireReader reader(packet);
  IkeMessage msg;
  msg.initiator_spi = ReadU64(reader);
  msg.responder_spi = ReadU64(reader);
  uint8 next_payload = reader.ReadU8();
  uint8 version = reader.ReadU8();
  uint8 exchange = reader.ReadU8();
  uint8 flags = reader.ReadU8();
  msg.message_id = reader.ReadU32();
  uint32 total_length = reader.ReadU32();
  if (!reader.ok() || version != kIkeVersion20 || total_length < kIkeHeaderSize ||
      packet.size() < total_length)
    return std::nullopt;

  msg.exchange_type = static_cast<IkeExchangeType>(exchange);
  msg.is_initiator = (flags & kIkeFlagInitiator) != 0;
  msg.is_response = (flags & kIkeFlagResponse) != 0;
  packet = packet.substr(0, total_length);

  if (next_payload ==
      static_cast<uint8>(IkePayloadType::EncryptedAndAuthenticated)) {
    if (!sk_key.has_value() ||
        packet.size() < kIkeHeaderSize + kIkePayloadHeaderSize + kIkeSkIvSize +
                            kIkeSkTagSize + 1)
      return std::nullopt;
    WireReader sk_reader(packet.substr(kIkeHeaderSize));
    uint8 inner_first_payload = sk_reader.ReadU8();
    sk_reader.Skip(1);
    uint16 sk_len = sk_reader.ReadU16();
    if (kIkeHeaderSize + sk_len != total_length) return std::nullopt;
    uint64 explicit_iv = ReadU64(sk_reader);
    std::string_view ct_and_tag = sk_reader.Rest();
    std::string_view aad =
        packet.substr(0, kIkeHeaderSize + kIkePayloadHeaderSize);
    auto nonce = MakeIkeNonce(sk_salt, explicit_iv);
    auto decrypted = AeadDecrypt(EspCipherSuite::ChaCha20Poly1305, *sk_key,
                                 nonce, aad, ct_and_tag);
    if (!decrypted.has_value() || decrypted->empty()) return std::nullopt;
    uint8 pad_len = static_cast<uint8>(decrypted->back());
    if (static_cast<size_t>(pad_len) + 1 > decrypted->size())
      return std::nullopt;
    std::string_view inner_bytes = std::string_view(*decrypted)
                                       .substr(0, decrypted->size() - 1 - pad_len);
    if (!DecodePayloadChain(inner_first_payload, inner_bytes, msg.payloads))
      return std::nullopt;
    return msg;
  }

  if (!DecodePayloadChain(next_payload, packet.substr(kIkeHeaderSize),
                          msg.payloads))
    return std::nullopt;
  return msg;
}

IkePayload BuildIkeNotifyPayload(uint8 protocol_id, std::string_view spi,
                                 IkeNotifyType notify_type,
                                 std::string_view notify_data) {
  IkePayload payload;
  payload.type = IkePayloadType::Notify;
  WireWriter writer(payload.body);
  writer.WriteU8(protocol_id);
  writer.WriteU8(static_cast<uint8>(spi.size()));
  writer.WriteU16(static_cast<uint16>(notify_type));
  writer.WriteBytes(spi);
  writer.WriteBytes(notify_data);
  return payload;
}

std::optional<std::pair<IkeNotifyType, std::string>> ParseIkeNotifyPayload(
    std::string_view body) {
  if (body.size() < 4) return std::nullopt;
  WireReader reader(body);
  reader.Skip(1);
  uint8 spi_size = reader.ReadU8();
  uint16 notify_type = reader.ReadU16();
  reader.Skip(spi_size);
  if (!reader.ok()) return std::nullopt;
  return std::make_pair(static_cast<IkeNotifyType>(notify_type),
                        std::string(reader.Rest()));
}

Ikev2Session::Ikev2Session(bool is_initiator, uint64 local_ike_spi,
                           const IpAddress& local_address,
                           const IpAddress& remote_address)
    : is_initiator_(is_initiator),
      initiator_spi_(is_initiator ? local_ike_spi : 0),
      responder_spi_(is_initiator ? 0 : local_ike_spi),
      local_address_(local_address),
      remote_address_(remote_address) {}

void Ikev2Session::DeriveIkeKeys(std::string_view shared_secret) {
  std::string seed_material;
  seed_material.append(initiator_nonce_);
  seed_material.append(responder_nonce_);
  seed_material.append(shared_secret);
  for (size_t i = 0; i < sk_key_.size(); i++) {
    uint8 b = seed_material.empty()
                  ? 0
                  : static_cast<uint8>(seed_material[i % seed_material.size()]);
    sk_key_[i] = static_cast<uint8>(b ^ (i * 0x37 + 0x5A));
  }
  for (size_t i = 0; i < sk_salt_.size(); i++)
    sk_salt_[i] = static_cast<uint8>(sk_key_[i] ^ 0xA5);
}

std::string Ikev2Session::ComputePrfAuth(std::string_view secret,
                                         std::string_view id_payload) const {
  std::array<uint8, 12> nonce{};
  std::copy(sk_salt_.begin(), sk_salt_.end(), nonce.begin());
  std::string input;
  input.append(initiator_nonce_);
  input.append(responder_nonce_);
  input.append(id_payload);
  input.append(secret);
  return AeadEncrypt(EspCipherSuite::ChaCha20Poly1305, sk_key_, nonce, "AUTH",
                     input);
}

void Ikev2Session::InstallChildSas(uint32 inbound_spi, uint32 outbound_spi,
                                   IpsecEngine& ipsec) {
  child_inbound_spi_ = inbound_spi;
  child_outbound_spi_ = outbound_spi;

  SecurityAssociation in_sa;
  in_sa.spi = inbound_spi;
  in_sa.mode = IpsecMode::Tunnel;
  in_sa.cipher = EspCipherSuite::ChaCha20Poly1305;
  in_sa.key = sk_key_;
  in_sa.salt = sk_salt_;
  in_sa.tunnel_local = local_address_;
  in_sa.tunnel_remote = remote_address_;
  ipsec.InstallSa(in_sa);

  SecurityAssociation out_sa = in_sa;
  out_sa.spi = outbound_spi;
  ipsec.InstallSa(out_sa);
}

IkeMessage Ikev2Session::BuildSaInitRequest(std::string_view dh_public,
                                            std::string_view nonce) {
  initiator_nonce_.assign(nonce);
  IkeMessage msg;
  msg.initiator_spi = initiator_spi_;
  msg.responder_spi = 0;
  msg.exchange_type = IkeExchangeType::IkeSaInit;
  msg.is_initiator = true;
  msg.is_response = false;
  msg.message_id = next_message_id_++;
  msg.payloads.push_back(
      {IkePayloadType::SecurityAssociation, false, "CHACHA20_POLY1305"});
  msg.payloads.push_back(
      {IkePayloadType::KeyExchange, false, std::string(dh_public)});
  msg.payloads.push_back({IkePayloadType::Nonce, false, std::string(nonce)});
  state_ = IkeSessionState::SaInitSent;
  return msg;
}

std::optional<IkeMessage> Ikev2Session::HandleSaInitRequest(
    const IkeMessage& request, std::string_view dh_public,
    std::string_view nonce, std::string_view shared_secret) {
  if (request.exchange_type != IkeExchangeType::IkeSaInit ||
      request.is_response)
    return std::nullopt;
  const IkePayload* ni = FindPayload(request, IkePayloadType::Nonce);
  const IkePayload* ke = FindPayload(request, IkePayloadType::KeyExchange);
  if (ni == nullptr || ke == nullptr) return std::nullopt;

  initiator_spi_ = request.initiator_spi;
  initiator_nonce_ = ni->body;
  responder_nonce_.assign(nonce);
  DeriveIkeKeys(shared_secret);
  state_ = IkeSessionState::SaInitestablished;

  IkeMessage resp;
  resp.initiator_spi = initiator_spi_;
  resp.responder_spi = responder_spi_;
  resp.exchange_type = IkeExchangeType::IkeSaInit;
  resp.is_initiator = false;
  resp.is_response = true;
  resp.message_id = request.message_id;
  resp.payloads.push_back(
      {IkePayloadType::SecurityAssociation, false, "CHACHA20_POLY1305"});
  resp.payloads.push_back(
      {IkePayloadType::KeyExchange, false, std::string(dh_public)});
  resp.payloads.push_back({IkePayloadType::Nonce, false, std::string(nonce)});
  return resp;
}

bool Ikev2Session::HandleSaInitResponse(const IkeMessage& response,
                                        std::string_view shared_secret) {
  if (state_ != IkeSessionState::SaInitSent ||
      response.exchange_type != IkeExchangeType::IkeSaInit ||
      !response.is_response)
    return false;
  const IkePayload* nr = FindPayload(response, IkePayloadType::Nonce);
  if (nr == nullptr) return false;
  responder_spi_ = response.responder_spi;
  responder_nonce_ = nr->body;
  DeriveIkeKeys(shared_secret);
  state_ = IkeSessionState::SaInitestablished;
  return true;
}

IkeMessage Ikev2Session::BuildAuthRequest(std::string_view id,
                                          IkeAuthMethod method,
                                          std::string_view secret_or_cert,
                                          uint32 child_spi, bool use_eap) {
  initiator_id_.assign(id);
  child_inbound_spi_ = child_spi;
  IkeMessage msg;
  msg.initiator_spi = initiator_spi_;
  msg.responder_spi = responder_spi_;
  msg.exchange_type = IkeExchangeType::IkeAuth;
  msg.is_initiator = true;
  msg.is_response = false;
  msg.message_id = next_message_id_++;
  msg.payloads.push_back({IkePayloadType::IdInitiator, false, std::string(id)});

  if (!use_eap) {
    if (method == IkeAuthMethod::RsaDigitalSignature) {
      msg.payloads.push_back({IkePayloadType::Certificate, false,
                              std::string(secret_or_cert)});
    }
    std::string auth_body;
    auth_body.push_back(static_cast<char>(method));
    auth_body.append(3, '\0');
    auth_body.append(ComputePrfAuth(secret_or_cert, id));
    msg.payloads.push_back(
        {IkePayloadType::Authentication, false, std::move(auth_body)});
  }

  std::string sa_body;
  WireWriter sa_w(sa_body);
  sa_w.WriteU32(child_spi);
  msg.payloads.push_back(
      {IkePayloadType::SecurityAssociation, false, std::move(sa_body)});
  msg.payloads.push_back(
      {IkePayloadType::TrafficSelectorInitiator, false, "ANY"});
  msg.payloads.push_back(
      {IkePayloadType::TrafficSelectorResponder, false, "ANY"});
  msg.payloads.push_back(
      BuildIkeNotifyPayload(0, {}, IkeNotifyType::MobikeSupported));
  return msg;
}

std::optional<IkeMessage> Ikev2Session::HandleAuthRequest(
    const IkeMessage& request, std::string_view responder_id,
    IkeAuthMethod method, std::string_view expected_secret_or_cert,
    uint32 responder_child_spi, IpsecEngine& ipsec, bool require_eap,
    std::string_view eap_challenge) {
  if (request.exchange_type != IkeExchangeType::IkeAuth || request.is_response)
    return std::nullopt;
  const IkePayload* idi = FindPayload(request, IkePayloadType::IdInitiator);
  const IkePayload* sa =
      FindPayload(request, IkePayloadType::SecurityAssociation);
  if (idi == nullptr || sa == nullptr || sa->body.size() < 4)
    return std::nullopt;

  WireReader sa_r(sa->body);
  child_outbound_spi_ = sa_r.ReadU32();
  initiator_id_ = idi->body;

  for (const IkePayload& p : request.payloads) {
    if (p.type == IkePayloadType::Notify) {
      auto notify = ParseIkeNotifyPayload(p.body);
      if (notify.has_value() &&
          notify->first == IkeNotifyType::MobikeSupported)
        mobike_enabled_ = true;
    }
  }

  IkeMessage resp;
  resp.initiator_spi = initiator_spi_;
  resp.responder_spi = responder_spi_;
  resp.exchange_type = IkeExchangeType::IkeAuth;
  resp.is_initiator = false;
  resp.is_response = true;
  resp.message_id = request.message_id;
  resp.payloads.push_back(
      {IkePayloadType::IdResponder, false, std::string(responder_id)});

  std::string resp_auth;
  resp_auth.push_back(static_cast<char>(method));
  resp_auth.append(3, '\0');
  resp_auth.append(ComputePrfAuth(expected_secret_or_cert, responder_id));
  resp.payloads.push_back(
      {IkePayloadType::Authentication, false, std::move(resp_auth)});

  if (require_eap) {
    eap_challenge_.assign(eap_challenge);
    std::string eap_body;
    eap_body.push_back(static_cast<char>(kEapCodeRequest));
    eap_body.append(eap_challenge);
    resp.payloads.push_back({IkePayloadType::Eap, false, std::move(eap_body)});
    state_ = IkeSessionState::EapInProgress;
    return resp;
  }

  const IkePayload* auth = FindPayload(request, IkePayloadType::Authentication);
  if (auth == nullptr || auth->body.size() < 4) return std::nullopt;
  if (auth->body.substr(4) !=
      ComputePrfAuth(expected_secret_or_cert, initiator_id_))
    return std::nullopt;

  std::string sa_body;
  WireWriter sa_w(sa_body);
  sa_w.WriteU32(responder_child_spi);
  resp.payloads.push_back(
      {IkePayloadType::SecurityAssociation, false, std::move(sa_body)});
  resp.payloads.push_back(
      {IkePayloadType::TrafficSelectorInitiator, false, "ANY"});
  resp.payloads.push_back(
      {IkePayloadType::TrafficSelectorResponder, false, "ANY"});
  if (mobike_enabled_) {
    resp.payloads.push_back(
        BuildIkeNotifyPayload(0, {}, IkeNotifyType::MobikeSupported));
  }

  InstallChildSas(responder_child_spi, child_outbound_spi_, ipsec);
  state_ = IkeSessionState::Established;
  return resp;
}

std::optional<IkeMessage> Ikev2Session::HandleEapStepOnInitiator(
    const IkeMessage& response, std::string_view eap_shared_secret,
    std::string_view initiator_id) {
  const IkePayload* eap = FindPayload(response, IkePayloadType::Eap);
  if (eap == nullptr || eap->body.empty()) return std::nullopt;
  uint8 eap_code = static_cast<uint8>(eap->body[0]);

  IkeMessage req;
  req.initiator_spi = initiator_spi_;
  req.responder_spi = responder_spi_;
  req.exchange_type = IkeExchangeType::IkeAuth;
  req.is_initiator = true;
  req.is_response = false;
  req.message_id = next_message_id_++;

  if (eap_code == kEapCodeRequest) {
    state_ = IkeSessionState::EapInProgress;
    eap_challenge_ = eap->body.substr(1);
    std::string eap_resp;
    eap_resp.push_back(static_cast<char>(kEapCodeResponse));
    eap_resp.append(ComputePrfAuth(eap_shared_secret, eap_challenge_));
    req.payloads.push_back({IkePayloadType::Eap, false, std::move(eap_resp)});
    return req;
  }

  if (eap_code == kEapCodeSuccess) {
    std::string msk_auth;
    msk_auth.push_back(
        static_cast<char>(IkeAuthMethod::SharedKeyMessageIntegrityCode));
    msk_auth.append(3, '\0');
    msk_auth.append(ComputePrfAuth(eap_shared_secret, initiator_id));
    req.payloads.push_back(
        {IkePayloadType::Authentication, false, std::move(msk_auth)});
    return req;
  }
  return std::nullopt;
}

std::optional<IkeMessage> Ikev2Session::HandleEapStepOnResponder(
    const IkeMessage& request, std::string_view eap_shared_secret,
    std::string_view responder_id, uint32 responder_child_spi,
    IpsecEngine& ipsec) {
  if (state_ != IkeSessionState::EapInProgress) return std::nullopt;

  IkeMessage resp;
  resp.initiator_spi = initiator_spi_;
  resp.responder_spi = responder_spi_;
  resp.exchange_type = IkeExchangeType::IkeAuth;
  resp.is_initiator = false;
  resp.is_response = true;
  resp.message_id = request.message_id;

  const IkePayload* eap = FindPayload(request, IkePayloadType::Eap);
  if (eap != nullptr && !eap->body.empty() &&
      static_cast<uint8>(eap->body[0]) == kEapCodeResponse) {
    if (eap->body.substr(1) !=
        ComputePrfAuth(eap_shared_secret, eap_challenge_))
      return std::nullopt;
    std::string eap_success(1, static_cast<char>(kEapCodeSuccess));
    resp.payloads.push_back(
        {IkePayloadType::Eap, false, std::move(eap_success)});
    return resp;
  }

  const IkePayload* auth = FindPayload(request, IkePayloadType::Authentication);
  if (auth == nullptr || auth->body.size() < 4 ||
      auth->body.substr(4) != ComputePrfAuth(eap_shared_secret, initiator_id_))
    return std::nullopt;

  std::string msk_auth;
  msk_auth.push_back(
      static_cast<char>(IkeAuthMethod::SharedKeyMessageIntegrityCode));
  msk_auth.append(3, '\0');
  msk_auth.append(ComputePrfAuth(eap_shared_secret, responder_id));
  resp.payloads.push_back(
      {IkePayloadType::Authentication, false, std::move(msk_auth)});

  std::string sa_body;
  WireWriter sa_w(sa_body);
  sa_w.WriteU32(responder_child_spi);
  resp.payloads.push_back(
      {IkePayloadType::SecurityAssociation, false, std::move(sa_body)});
  resp.payloads.push_back(
      {IkePayloadType::TrafficSelectorInitiator, false, "ANY"});
  resp.payloads.push_back(
      {IkePayloadType::TrafficSelectorResponder, false, "ANY"});
  if (mobike_enabled_) {
    resp.payloads.push_back(
        BuildIkeNotifyPayload(0, {}, IkeNotifyType::MobikeSupported));
  }

  InstallChildSas(responder_child_spi, child_outbound_spi_, ipsec);
  state_ = IkeSessionState::Established;
  return resp;
}

bool Ikev2Session::HandleFinalAuthResponse(const IkeMessage& response,
                                           std::string_view auth_secret,
                                           IpsecEngine& ipsec) {
  const IkePayload* sa =
      FindPayload(response, IkePayloadType::SecurityAssociation);
  const IkePayload* auth =
      FindPayload(response, IkePayloadType::Authentication);
  if (sa == nullptr || sa->body.size() < 4 || auth == nullptr) return false;
  WireReader r(sa->body);
  uint32 responder_spi = r.ReadU32();
  for (const IkePayload& p : response.payloads) {
    if (p.type == IkePayloadType::Notify) {
      auto n = ParseIkeNotifyPayload(p.body);
      if (n.has_value() && n->first == IkeNotifyType::MobikeSupported)
        mobike_enabled_ = true;
    }
  }
  InstallChildSas(child_inbound_spi_, responder_spi, ipsec);
  state_ = IkeSessionState::Established;
  return true;
}

IkeMessage Ikev2Session::BuildCreateChildSaRequest(uint32 new_inbound_spi,
                                                   std::string_view nonce) {
  child_inbound_spi_ = new_inbound_spi;
  IkeMessage msg;
  msg.initiator_spi = initiator_spi_;
  msg.responder_spi = responder_spi_;
  msg.exchange_type = IkeExchangeType::CreateChildSa;
  msg.is_initiator = is_initiator_;
  msg.is_response = false;
  msg.message_id = next_message_id_++;
  std::string sa_body;
  WireWriter w(sa_body);
  w.WriteU32(new_inbound_spi);
  msg.payloads.push_back(
      {IkePayloadType::SecurityAssociation, false, std::move(sa_body)});
  msg.payloads.push_back({IkePayloadType::Nonce, false, std::string(nonce)});
  state_ = IkeSessionState::RekeyingChildSa;
  return msg;
}

std::optional<IkeMessage> Ikev2Session::HandleCreateChildSaRequest(
    const IkeMessage& request, uint32 new_responder_spi,
    std::string_view responder_nonce, IpsecEngine& ipsec) {
  const IkePayload* sa =
      FindPayload(request, IkePayloadType::SecurityAssociation);
  if (sa == nullptr || sa->body.size() < 4) return std::nullopt;
  WireReader r(sa->body);
  uint32 peer_spi = r.ReadU32();
  InstallChildSas(new_responder_spi, peer_spi, ipsec);
  state_ = IkeSessionState::Established;

  IkeMessage resp;
  resp.initiator_spi = initiator_spi_;
  resp.responder_spi = responder_spi_;
  resp.exchange_type = IkeExchangeType::CreateChildSa;
  resp.is_initiator = is_initiator_;
  resp.is_response = true;
  resp.message_id = request.message_id;
  std::string sa_body;
  WireWriter w(sa_body);
  w.WriteU32(new_responder_spi);
  resp.payloads.push_back(
      {IkePayloadType::SecurityAssociation, false, std::move(sa_body)});
  resp.payloads.push_back(
      {IkePayloadType::Nonce, false, std::string(responder_nonce)});
  return resp;
}

bool Ikev2Session::HandleCreateChildSaResponse(const IkeMessage& response,
                                               IpsecEngine& ipsec) {
  const IkePayload* sa =
      FindPayload(response, IkePayloadType::SecurityAssociation);
  if (sa == nullptr || sa->body.size() < 4) return false;
  WireReader r(sa->body);
  uint32 peer_spi = r.ReadU32();
  InstallChildSas(child_inbound_spi_, peer_spi, ipsec);
  state_ = IkeSessionState::Established;
  return true;
}

IkeMessage Ikev2Session::InitiateMobikeAddressUpdate(
    const IpAddress& new_local_address, const std::array<uint8, 8>& cookie2,
    IpsecEngine& ipsec) {
  local_address_ = new_local_address;
  expected_cookie2_ = cookie2;
  ipsec.UpdateTunnelEndpoints(child_inbound_spi_, local_address_,
                              remote_address_);
  ipsec.UpdateTunnelEndpoints(child_outbound_spi_, local_address_,
                              remote_address_);
  state_ = IkeSessionState::MobikeCookie2Pending;

  IkeMessage msg;
  msg.initiator_spi = initiator_spi_;
  msg.responder_spi = responder_spi_;
  msg.exchange_type = IkeExchangeType::Informational;
  msg.is_initiator = is_initiator_;
  msg.is_response = false;
  msg.message_id = next_message_id_++;
  msg.payloads.push_back(
      BuildIkeNotifyPayload(0, {}, IkeNotifyType::UpdateSaAddresses));
  msg.payloads.push_back(BuildIkeNotifyPayload(
      0, {}, IkeNotifyType::Cookie2,
      {reinterpret_cast<const char*>(cookie2.data()), cookie2.size()}));
  return msg;
}

std::optional<IkeMessage> Ikev2Session::HandleInformationalRequest(
    const IkeMessage& request, const IpAddress& outer_source,
    const IpAddress& outer_destination, IpsecEngine& ipsec) {
  if (request.exchange_type != IkeExchangeType::Informational ||
      request.is_response)
    return std::nullopt;

  IkeMessage resp;
  resp.initiator_spi = initiator_spi_;
  resp.responder_spi = responder_spi_;
  resp.exchange_type = IkeExchangeType::Informational;
  resp.is_initiator = is_initiator_;
  resp.is_response = true;
  resp.message_id = request.message_id;

  for (const IkePayload& p : request.payloads) {
    if (p.type != IkePayloadType::Notify) continue;
    auto notify = ParseIkeNotifyPayload(p.body);
    if (!notify.has_value()) continue;
    if (notify->first == IkeNotifyType::UpdateSaAddresses && mobike_enabled_) {
      local_address_ = outer_destination;
      remote_address_ = outer_source;
      ipsec.UpdateTunnelEndpoints(child_inbound_spi_, local_address_,
                                  remote_address_);
      ipsec.UpdateTunnelEndpoints(child_outbound_spi_, local_address_,
                                  remote_address_);
    } else if (notify->first == IkeNotifyType::Cookie2) {
      resp.payloads.push_back(
          BuildIkeNotifyPayload(0, {}, IkeNotifyType::Cookie2, notify->second));
    }
  }
  return resp;
}

bool Ikev2Session::HandleInformationalResponse(const IkeMessage& response) {
  if (response.exchange_type != IkeExchangeType::Informational ||
      !response.is_response)
    return false;
  if (state_ == IkeSessionState::MobikeCookie2Pending) {
    for (const IkePayload& p : response.payloads) {
      if (p.type != IkePayloadType::Notify) continue;
      auto notify = ParseIkeNotifyPayload(p.body);
      if (notify.has_value() && notify->first == IkeNotifyType::Cookie2 &&
          notify->second.size() == expected_cookie2_.size() &&
          std::memcmp(notify->second.data(), expected_cookie2_.data(),
                      expected_cookie2_.size()) == 0) {
        state_ = IkeSessionState::Established;
        return true;
      }
    }
    return false;
  }
  return true;
}
