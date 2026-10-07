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

#include "endian.h"

// Represents an Ethernet II frame header.
struct EthernetHeader {
  // Destination hardware MAC address.
  uint8 dest_mac[6];
  // Source hardware MAC address.
  uint8 src_mac[6];
  // Protocol type (e.g. 0x0800 for IPv4, 0x0806 for ARP, 0x86DD for IPv6).
  uint16 ether_type;
} __attribute__((packed));

// Represents an Address Resolution Protocol (ARP) packet for IPv4 over
// Ethernet.
struct ArpHeader {
  // Hardware type (1 for Ethernet).
  uint16 htype;
  // Protocol type (0x0800 for IPv4).
  uint16 ptype;
  // Hardware address length (6 for MAC).
  uint8 hlen;
  // Protocol address length (4 for IPv4).
  uint8 plen;
  // Operation (1 for Request, 2 for Reply).
  uint16 oper;
  // Sender hardware address.
  uint8 sha[6];
  // Sender protocol address in network byte order.
  uint8 spa[4];
  // Target hardware address.
  uint8 tha[6];
  // Target protocol address in network byte order.
  uint8 tpa[4];
} __attribute__((packed));

// Represents an IPv4 packet header.
struct IpHeader {
  // Version (4 bits) and Internet Header Length (4 bits).
  uint8 version_ihl;
  // Differentiated Services Code Point (DSCP) and Explicit Congestion
  // Notification (ECN).
  uint8 dscp_ecn;
  // Total length of the IPv4 packet (header + payload).
  uint16 total_length;
  // Identification field for fragmentation.
  uint16 identification;
  // Flags (3 bits) and Fragment Offset (13 bits).
  uint16 flags_fragment;
  // Time To Live (hop count limit).
  uint8 ttl;
  // Transport layer protocol (1 for ICMP, 6 for TCP, 17 for UDP).
  uint8 protocol;
  // Header checksum.
  uint16 checksum;
  // Source IPv4 address in network byte order.
  uint8 src_ip[4];
  // Destination IPv4 address in network byte order.
  uint8 dest_ip[4];
} __attribute__((packed));

// Represents an ICMPv4 header (specifically structured for Echo Request/Reply).
struct IcmpHeader {
  // Message type (8 for Echo Request, 0 for Echo Reply).
  uint8 type;
  // Message subtype code.
  uint8 code;
  // Internet Checksum of the ICMP header and payload.
  uint16 checksum;
  // Identifier to aid in matching Echo Replies to Requests.
  uint16 id;
  // Sequence number to aid in matching Echo Replies to Requests.
  uint16 sequence;
} __attribute__((packed));

// Represents a User Datagram Protocol (UDP) packet header.
struct UdpHeader {
  // Source port number.
  uint16 src_port;
  // Destination port number.
  uint16 dest_port;
  // Length of the UDP header and payload in bytes.
  uint16 length;
  // Checksum of the pseudo-header, UDP header, and payload.
  uint16 checksum;
} __attribute__((packed));

// Represents a Transmission Control Protocol (TCP) packet header.
struct TcpHeader {
  // Source port number.
  uint16 src_port;
  // Destination port number.
  uint16 dest_port;
  // Sequence number of the first data octet in this segment (unless SYN is
  // present).
  uint32 seq_num;
  // Acknowledgment number: next sequence number the sender of the ACK is
  // expecting.
  uint32 ack_num;
  // Data offset (4 bits, header length in 32-bit words) + Reserved (3 bits) +
  // NS flag (1 bit).
  uint8 data_offset;
  // Control flags (FIN=0x01, SYN=0x02, RST=0x04, PSH=0x08, ACK=0x10, URG=0x20).
  uint8 flags;
  // Receive window size (number of data octets the sender is willing to
  // accept).
  uint16 window_size;
  // Checksum of the pseudo-header, TCP header, and payload.
  uint16 checksum;
  // Urgent pointer.
  uint16 urgent_ptr;
} __attribute__((packed));
