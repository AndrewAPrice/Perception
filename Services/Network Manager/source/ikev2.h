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

#include <types.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ipsec.h"
#include "perception/network/ip_address.h"

// IKEv2 exchange types (RFC 7296 §3.1).
enum class IkeExchangeType : uint8 {
  IkeSaInit = 34,
  IkeAuth = 35,
  CreateChildSa = 36,
  Informational = 37,
};

// IKEv2 payload types (RFC 7296 §3.2).
enum class IkePayloadType : uint8 {
  NoNextPayload = 0,
  SecurityAssociation = 33,
  KeyExchange = 34,
  IdInitiator = 35,
  IdResponder = 36,
  Certificate = 37,
  Authentication = 39,
  Nonce = 40,
  Notify = 41,
  Delete = 42,
  TrafficSelectorInitiator = 44,
  TrafficSelectorResponder = 45,
  EncryptedAndAuthenticated = 46,
  Eap = 48,
};

// IKEv2 Authentication methods (RFC 7296 §3.8).
enum class IkeAuthMethod : uint8 {
  RsaDigitalSignature = 1,
  SharedKeyMessageIntegrityCode = 2,
};

// IKEv2 Notify message types (RFC 7296 §3.10.1 & RFC 4555 MOBIKE).
enum class IkeNotifyType : uint16 {
  MobikeSupported = 16396,
  UpdateSaAddresses = 16400,
  Cookie2 = 16401,
};

// Generic IKEv2 payload entry in a decoded/encoded IKEv2 message.
struct IkePayload {
  IkePayloadType type = IkePayloadType::NoNextPayload;
  bool critical = false;
  std::string body;
};

// Decoded or encodable IKEv2 message (28-byte header + payload chain).
struct IkeMessage {
  uint64 initiator_spi = 0;
  uint64 responder_spi = 0;
  IkeExchangeType exchange_type = IkeExchangeType::IkeSaInit;
  bool is_initiator = true;
  bool is_response = false;
  uint32 message_id = 0;
  std::vector<IkePayload> payloads;
};

// Encodes an IKEv2 message into wire format (optionally encrypting the payload
// chain inside an SK payload when `sk_key` is provided).
std::string SerializeIkeMessage(
    const IkeMessage& message,
    const std::optional<std::array<uint8, 32>>& sk_key = std::nullopt,
    const std::array<uint8, 4>& sk_salt = {});

// Decodes an IKEv2 wire message (and decrypts its SK payload if `sk_key` is
// provided).
std::optional<IkeMessage> ParseIkeMessage(
    std::string_view packet,
    const std::optional<std::array<uint8, 32>>& sk_key = std::nullopt,
    const std::array<uint8, 4>& sk_salt = {});

// Builds an IKEv2 Notify payload body.
IkePayload BuildIkeNotifyPayload(uint8 protocol_id, std::string_view spi,
                                 IkeNotifyType notify_type,
                                 std::string_view notify_data = {});

// Parses an IKEv2 Notify payload body, returning `(notify_type, notify_data)`.
std::optional<std::pair<IkeNotifyType, std::string>> ParseIkeNotifyPayload(
    std::string_view body);

// State of an IKEv2 Security Association session.
enum class IkeSessionState : uint8 {
  Idle = 0,
  SaInitSent = 1,
  SaInitestablished = 2,
  EapInProgress = 3,
  Established = 4,
  RekeyingChildSa = 5,
  MobikeCookie2Pending = 6,
  Deleted = 7,
};

// RFC 7296 IKEv2 + EAP + RFC 4555 MOBIKE state machine.
class Ikev2Session {
 public:
  Ikev2Session(bool is_initiator, uint64 local_ike_spi,
               const ::perception::network::IpAddress& local_address,
               const ::perception::network::IpAddress& remote_address);

  IkeSessionState state() const { return state_; }
  bool mobike_enabled() const { return mobike_enabled_; }
  const ::perception::network::IpAddress& local_address() const {
    return local_address_;
  }
  const ::perception::network::IpAddress& remote_address() const {
    return remote_address_;
  }
  uint32 child_inbound_spi() const { return child_inbound_spi_; }
  uint32 child_outbound_spi() const { return child_outbound_spi_; }

  // Initiates IKE_SA_INIT.
  IkeMessage BuildSaInitRequest(std::string_view dh_public,
                                std::string_view nonce);

  // Responds to IKE_SA_INIT and derives SKEYSEED / SK_e / SK_p.
  std::optional<IkeMessage> HandleSaInitRequest(const IkeMessage& request,
                                                std::string_view dh_public,
                                                std::string_view nonce,
                                                std::string_view shared_secret);

  // Processes IKE_SA_INIT response on the initiator and derives keys.
  bool HandleSaInitResponse(const IkeMessage& response,
                            std::string_view shared_secret);

  // Builds an IKE_AUTH request using PSK, Certificate, or EAP-initiation
  // (when `use_eap` is true, omits the AUTH payload per RFC 7296 §2.16).
  IkeMessage BuildAuthRequest(std::string_view id, IkeAuthMethod method,
                              std::string_view secret_or_cert,
                              uint32 child_spi, bool use_eap = false);

  // Processes IKE_AUTH on the responder; initiates EAP if `require_eap` is true
  // or completes PSK/Certificate authentication and installs Child SAs.
  std::optional<IkeMessage> HandleAuthRequest(
      const IkeMessage& request, std::string_view responder_id,
      IkeAuthMethod method, std::string_view expected_secret_or_cert,
      uint32 responder_child_spi, IpsecEngine& ipsec, bool require_eap = false,
      std::string_view eap_challenge = {});

  // Processes an EAP request on the initiator and returns the EAP response
  // IKE_AUTH message, or sends the MSK-derived AUTH payload upon EAP-Success.
  std::optional<IkeMessage> HandleEapStepOnInitiator(
      const IkeMessage& response, std::string_view eap_shared_secret,
      std::string_view initiator_id);

  // Completes multi-round EAP on the responder: returns EAP-Success after the
  // challenge response, and then verifies the initiator's MSK-derived AUTH to
  // establish the IKE + Child SA.
  std::optional<IkeMessage> HandleEapStepOnResponder(
      const IkeMessage& request, std::string_view eap_shared_secret,
      std::string_view responder_id, uint32 responder_child_spi,
      IpsecEngine& ipsec);

  // Processes the final IKE_AUTH response on the initiator and installs Child
  // SAs in `ipsec`.
  bool HandleFinalAuthResponse(const IkeMessage& response,
                               std::string_view auth_secret,
                               IpsecEngine& ipsec);

  // Builds and processes CREATE_CHILD_SA for Child SA rekeying.
  IkeMessage BuildCreateChildSaRequest(uint32 new_inbound_spi,
                                       std::string_view nonce);
  std::optional<IkeMessage> HandleCreateChildSaRequest(
      const IkeMessage& request, uint32 new_responder_spi,
      std::string_view responder_nonce, IpsecEngine& ipsec);
  bool HandleCreateChildSaResponse(const IkeMessage& response,
                                   IpsecEngine& ipsec);

  // RFC 4555 MOBIKE: updates local/remote addresses, sends INFORMATIONAL with
  // UPDATE_SA_ADDRESSES + COOKIE2 return-routability challenge, and updates
  // outer tunnel endpoints in `ipsec`.
  IkeMessage InitiateMobikeAddressUpdate(
      const ::perception::network::IpAddress& new_local_address,
      const std::array<uint8, 8>& cookie2, IpsecEngine& ipsec);
  std::optional<IkeMessage> HandleInformationalRequest(
      const IkeMessage& request,
      const ::perception::network::IpAddress& outer_source,
      const ::perception::network::IpAddress& outer_destination,
      IpsecEngine& ipsec);
  bool HandleInformationalResponse(const IkeMessage& response);

 private:
  void DeriveIkeKeys(std::string_view shared_secret);
  std::string ComputePrfAuth(std::string_view secret,
                             std::string_view id_payload) const;
  void InstallChildSas(uint32 inbound_spi, uint32 outbound_spi,
                       IpsecEngine& ipsec);

  bool is_initiator_ = true;
  uint64 initiator_spi_ = 0;
  uint64 responder_spi_ = 0;
  ::perception::network::IpAddress local_address_;
  ::perception::network::IpAddress remote_address_;
  IkeSessionState state_ = IkeSessionState::Idle;
  bool mobike_enabled_ = false;
  uint32 next_message_id_ = 0;
  std::string initiator_nonce_;
  std::string responder_nonce_;
  std::string initiator_id_;
  std::string eap_challenge_;
  std::array<uint8, 32> sk_key_{};
  std::array<uint8, 4> sk_salt_{};
  std::array<uint8, 8> expected_cookie2_{};
  uint32 child_inbound_spi_ = 0;
  uint32 child_outbound_spi_ = 0;
};
