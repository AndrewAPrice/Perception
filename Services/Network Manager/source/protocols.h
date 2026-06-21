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

#include <stddef.h>
#include <types.h>

#include "endian.h"

// Ethernet frame header representation.
struct EthernetHeader {
  // Destination MAC hardware address (6 bytes).
  uint8 dest_mac[6];
  // Source MAC hardware address (6 bytes).
  uint8 src_mac[6];
  // EtherType field (e.g., IPv4 = 0x0800, ARP = 0x0806) in big-endian.
  uint16 type;
} __attribute__((packed));

// Address Resolution Protocol (ARP) header representation.
struct ArpHeader {
  // Hardware Type (e.g., Ethernet = 1).
  uint16 htype;
  // Protocol Type (e.g., IPv4 = 0x0800).
  uint16 ptype;
  // Hardware Address Length (e.g., MAC size = 6 bytes).
  uint8 hlen;
  // Protocol Address Length (e.g., IP size = 4 bytes).
  uint8 plen;
  // Opcode field (e.g., Request = 1, Reply = 2).
  uint16 oper;
  // Sender Hardware Address (MAC).
  uint8 sha[6];
  // Sender Protocol Address (IPv4).
  uint32 spa;
  // Target Hardware Address (MAC).
  uint8 tha[6];
  // Target Protocol Address (IPv4).
  uint32 tpa;
} __attribute__((packed));

// IPv4 header representation.
struct IpHeader {
  // Version (4 bits) and Internet Header Length (4 bits).
  uint8 version_ihl;
  // Type of Service / DSCP / ECN.
  uint8 tos;
  // Total packet length (header + payload size) in big-endian.
  uint16 len;
  // Identification tag for reassembly.
  uint16 id;
  // Flags (3 bits) and Fragment Offset (13 bits).
  uint16 flags_offset;
  // Time to Live router count boundary.
  uint8 ttl;
  // Transport protocol identifier (TCP = 6, UDP = 17, ICMP = 1).
  uint8 protocol;
  // IP Header checksum verification.
  uint16 checksum;
  // Source IP address in host-endianness.
  uint32 src_ip;
  // Destination IP address in host-endianness.
  uint32 dest_ip;
} __attribute__((packed));

// Internet Control Message Protocol (ICMP) header representation.
struct IcmpHeader {
  // Type of ICMP packet (e.g., Echo Reply = 0, Echo Request = 8).
  uint8 type;
  // Subcode identifier (usually 0).
  uint8 code;
  // ICMP Header/Payload checksum verify.
  uint16 checksum;
  // Query packet identifier tag.
  uint16 id;
  // Query packet sequence number.
  uint16 seq;
} __attribute__((packed));

// User Datagram Protocol (UDP) header representation.
struct UdpHeader {
  // Source transport port.
  uint16 src_port;
  // Destination transport port.
  uint16 dest_port;
  // Total length (UDP header + payload size) in big-endian.
  uint16 len;
  // Optional UDP checksum verify (can be set to 0).
  uint16 checksum;
} __attribute__((packed));

// Transmission Control Protocol (TCP) header representation.
struct TcpHeader {
  // Source transport port.
  uint16 src_port;
  // Destination transport port.
  uint16 dest_port;
  // Sequence number of the segment in big-endian.
  uint32 seq;
  // Acknowledgment number of the expected next byte in big-endian.
  uint32 ack;
  // Data Offset (4 bits), Reserved (3 bits), and flags (9 bits).
  uint16 flags;
  // Advertised window size.
  uint16 window;
  // Header & payload checksum verify.
  uint16 checksum;
  // Urgent data offset pointer.
  uint16 urgent;
} __attribute__((packed));

// Pseudo IP Header representation used to calculate TCP and UDP checksums.
struct PseudoHeader {
  // Source IP address.
  uint32 src_ip;
  // Destination IP address.
  uint32 dest_ip;
  // Reserved byte (must be 0).
  uint8 reserved;
  // Protocol identifier (e.g., TCP = 6, UDP = 17).
  uint8 protocol;
  // Total TCP/UDP header + payload size.
  uint16 length;
} __attribute__((packed));

// Domain Name System (DNS) header representation.
struct DnsHeader {
  // Transaction identification token.
  uint16 id;
  // Query flags (e.g., Standard Query = 0x0100).
  uint16 flags;
  // Number of questions in the DNS query.
  uint16 questions;
  // Number of answers returned.
  uint16 answers;
  // Number of authority resource records.
  uint16 authority;
  // Number of additional resource records.
  uint16 additional;
} __attribute__((packed));

// Standard Internet Checksum algorithm (1's complement sum of 16-bit words).
inline uint16 CalculateChecksum(const uint16* data, size_t length) {
  uint32 sum = 0;
  while (length > 1) {
    sum += *data++;
    length -= 2;
  }
  if (length > 0) {
    sum += *(const uint8*)data;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (uint16)~sum;
}

// Computes the TCP packet checksum incorporating the IP pseudo-header contents.
inline uint16 CalculateTcpChecksum(uint32 src_ip, uint32 dest_ip,
                                   const uint8* tcp_packet, uint16 tcp_length) {
  PseudoHeader pseudo;
  pseudo.src_ip = src_ip;
  pseudo.dest_ip = dest_ip;
  pseudo.reserved = 0;
  pseudo.protocol = 6;  // TCP
  pseudo.length = Swap16BitEndian(tcp_length);

  uint32 sum = 0;
  const uint16* pseudo_words = (const uint16*)&pseudo;
  for (size_t i = 0; i < sizeof(pseudo) / 2; i++) {
    sum += pseudo_words[i];
  }

  const uint16* tcp_words = (const uint16*)tcp_packet;
  size_t length = tcp_length;
  while (length > 1) {
    sum += *tcp_words++;
    length -= 2;
  }
  if (length > 0) {
    sum += *(const uint8*)tcp_words;
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (uint16)~sum;
}

// Computes the UDP packet checksum incorporating the IP pseudo-header contents.
inline uint16 CalculateUdpChecksum(uint32 src_ip, uint32 dest_ip,
                                   const uint8* udp_packet, uint16 udp_length) {
  PseudoHeader pseudo;
  pseudo.src_ip = src_ip;
  pseudo.dest_ip = dest_ip;
  pseudo.reserved = 0;
  pseudo.protocol = 17;  // UDP
  pseudo.length = Swap16BitEndian(udp_length);

  uint32 sum = 0;
  const uint16* pseudo_words = (const uint16*)&pseudo;
  for (size_t i = 0; i < sizeof(pseudo) / 2; i++) {
    sum += pseudo_words[i];
  }

  const uint16* udp_words = (const uint16*)udp_packet;
  size_t length = udp_length;
  while (length > 1) {
    sum += *udp_words++;
    length -= 2;
  }
  if (length > 0) {
    sum += *(const uint8*)udp_words;
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (uint16)~sum;
}
