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

#include "interface.h"

#include <iostream>
#include <string>

#include "protocols.h"

namespace {

std::vector<NetworkInterface> interfaces;
std::vector<std::pair<size_t, ::perception::Fiber*>> fibers_waiting_for_arp;

}  // namespace

const std::vector<NetworkInterface>& GetNetworkInterfaces() {
  return interfaces;
}

NetworkInterface& GetNetworkInterface(size_t index) {
  return interfaces[index];
}

size_t GetNetworkInterfaceCount() {
  return interfaces.size();
}

size_t AddNetworkInterface(NetworkInterface interface) {
  size_t idx = interfaces.size();
  interfaces.push_back(std::move(interface));
  return idx;
}

void WakeFibersWaitingForArp(size_t iface_idx) {
  auto it = fibers_waiting_for_arp.begin();
  while (it != fibers_waiting_for_arp.end()) {
    if (it->first == iface_idx) {
      it->second->WakeUp();
      it = fibers_waiting_for_arp.erase(it);
    } else {
      ++it;
    }
  }
}

void WaitForArp(size_t iface_idx) {
  fibers_waiting_for_arp.push_back(
      {iface_idx, ::perception::GetCurrentlyExecutingFiber()});
  ::perception::Sleep();
}

void SendArpRequest(size_t iface_idx, uint32 target_ip) {
  std::string packet_data(sizeof(EthernetHeader) + sizeof(ArpHeader), '\0');

  EthernetHeader* eth = (EthernetHeader*)packet_data.data();
  for (int i = 0; i < 6; i++) {
    eth->dest_mac[i] = 0xFF;  // Broadcast
    eth->src_mac[i] = interfaces[iface_idx].mac[i];
  }
  eth->type = Swap16BitEndian(0x0806);

  ArpHeader* arp = (ArpHeader*)(packet_data.data() + sizeof(EthernetHeader));
  arp->htype = Swap16BitEndian(1);
  arp->ptype = Swap16BitEndian(0x0800);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = Swap16BitEndian(1);  // Request
  for (int i = 0; i < 6; i++) {
    arp->sha[i] = interfaces[iface_idx].mac[i];
    arp->tha[i] = 0;
  }
  arp->spa = interfaces[iface_idx].ip;
  arp->tpa = target_ip;

  ::perception::devices::Packet pkt;
  pkt.data = packet_data;
  interfaces[iface_idx].device.SendPacket(pkt);
}

void SendArpReply(size_t iface_idx, const uint8* target_mac, uint32 target_ip) {
  std::string packet_data(sizeof(EthernetHeader) + sizeof(ArpHeader), '\0');

  EthernetHeader* eth = (EthernetHeader*)packet_data.data();
  for (int i = 0; i < 6; i++) {
    eth->dest_mac[i] = target_mac[i];
    eth->src_mac[i] = interfaces[iface_idx].mac[i];
  }
  eth->type = Swap16BitEndian(0x0806);

  ArpHeader* arp = (ArpHeader*)(packet_data.data() + sizeof(EthernetHeader));
  arp->htype = Swap16BitEndian(1);
  arp->ptype = Swap16BitEndian(0x0800);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = Swap16BitEndian(2);  // Reply
  for (int i = 0; i < 6; i++) {
    arp->sha[i] = interfaces[iface_idx].mac[i];
    arp->tha[i] = target_mac[i];
  }
  arp->spa = interfaces[iface_idx].ip;
  arp->tpa = target_ip;

  ::perception::devices::Packet pkt;
  pkt.data = packet_data;
  interfaces[iface_idx].device.SendPacket(pkt);
}
