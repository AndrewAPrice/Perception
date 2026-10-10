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

#include "xhci.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "driver_loader.h"
#include "perception/cache.h"
#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/pci.h"
#include "perception/processes.h"
#include "perception/services.h"
#include "perception/time.h"
#include "usb_drivers.h"
#include "xhci_types.h"

using ::perception::AllocateMemoryPages;
using ::perception::DoesProcessExist;
using ::perception::Fiber;
using ::perception::FlushRange;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kPageSize;
using ::perception::kPciHdrBar0;
using ::perception::kPciHdrBar1;
using ::perception::kPciHdrCommand;
using ::perception::kPciHdrCommandBitBusMaster;
using ::perception::kPciHdrCommandBitMemorySpace;
using ::perception::MapPhysicalMemory;
using ::perception::NotifyUponProcessTermination;
using ::perception::NotifyWhenServiceDisappears;
using ::perception::ProcessId;
using ::perception::Read32BitsFromPciConfig;
using ::perception::Read8BitsFromPciConfig;
using ::perception::ReleaseMemoryPages;
using ::perception::SleepForDuration;
using ::perception::Write8BitsToPciConfig;
using ::perception::devices::kUsbMaxCompletions;
using ::perception::devices::kUsbRingEventOffset;
using ::perception::devices::kUsbRingHeaderOffset;
using ::perception::devices::kUsbTransferRingEntries;
using ::perception::devices::kUsbTransferRingLinkIndex;
using ::perception::devices::OpenUsbEndpointRequest;
using ::perception::devices::RegisterUsbDeviceListenerRequest;
using ::perception::devices::UsbControlTransferRequest;
using ::perception::devices::UsbControlTransferResponse;
using ::perception::devices::UsbDeviceId;
using ::perception::devices::UsbDeviceListener;
using ::perception::devices::UsbEndpointRingInfo;
using ::perception::devices::UsbEndpointRingSharedHeader;
using ::perception::devices::UsbInterfaceFilter;
using ::perception::devices::UsbInterfaceInfo;
using ::perception::devices::UsbInterfaces;
using ::perception::devices::UsbTrb;

namespace {

// PCI config offset for capabilities pointer.
constexpr uint8 kPciCapPtrOffset = 0x34;

// PCI Capability ID for MSI.
constexpr uint8 kPciCapIdMsi = 0x05;

// PCI Capability ID for MSI-X.
constexpr uint8 kPciCapIdMsiX = 0x11;

// Bit 10 in PCI Command register that disables legacy INTx interrupts.
constexpr uint16 kPciCmdInterruptDisableBit = 1 << 10;

// Bit in MSI control register low byte that enables MSI.
constexpr uint8 kMsiControlEnableBit = 1 << 0;

// Bit in MSI-X control register high byte that enables MSI-X.
constexpr uint8 kMsixControlHighEnableBit = 1 << 7;

// Number of TRBs in the controller Command Ring.
constexpr size_t kCommandRingSize = 64;

// Index of the Link TRB at the end of the Command Ring.
constexpr size_t kCommandRingLinkIndex = kCommandRingSize - 1;

// Number of TRBs in the Primary Event Ring.
constexpr size_t kEventRingSize = 256;

// Maximum number of ports supported per controller or external hub.
constexpr size_t kMaxPorts = 64;

// Maximum number of device slots supported per xHCI controller.
constexpr size_t kMaxSlots = 64;

// Maximum number of endpoints per USB device slot (DCI 1..31).
constexpr size_t kMaxEndpointsPerSlot = 32;

// Default number of 4KB MMIO pages mapped for an xHCI controller.
constexpr size_t kDefaultMmioPages = 16;

// Page offset mask for 4KB pages.
constexpr size_t kPageOffsetMask = kPageSize - 1;

// xHCI Operational Register offset for USBCMD.
constexpr size_t kOpRegUsbCmd = 0x00;

// xHCI Operational Register offset for USBSTS.
constexpr size_t kOpRegUsbSts = 0x04;

// xHCI Operational Register offset for CRCR.
constexpr size_t kOpRegCrcr = 0x18;

// xHCI Operational Register offset for DCBAAP.
constexpr size_t kOpRegDcbaap = 0x30;

// xHCI Operational Register offset for CONFIG.
constexpr size_t kOpRegConfig = 0x38;

// xHCI Operational Register offset for Port Register Set base.
constexpr size_t kOpRegPortBase = 0x400;

// Stride in bytes between consecutive xHCI Port Register Sets.
constexpr size_t kPortRegStride = 0x10;

// USBCMD bit: Run/Stop.
constexpr uint32 kUsbCmdRunStop = 1U << 0;

// USBCMD bit: Host Controller Reset.
constexpr uint32 kUsbCmdHcReset = 1U << 1;

// USBCMD bit: Interrupter Enable.
constexpr uint32 kUsbCmdIntEnable = 1U << 2;

// USBSTS bit: Host Controller Halted.
constexpr uint32 kUsbStsHalted = 1U << 0;

// USBSTS bit: Event Interrupt (RW1C).
constexpr uint32 kUsbStsEventInterrupt = 1U << 3;

// USBSTS bit: Port Change Detect (RW1C).
constexpr uint32 kUsbStsPortChangeDetect = 1U << 4;

// USBSTS bit: Controller Not Ready.
constexpr uint32 kUsbStsControllerNotReady = 1U << 11;

// PORTSC bit: Current Connect Status.
constexpr uint32 kPortScCcs = 1U << 0;

// PORTSC bit: Port Enabled/Disabled (RW1C).
constexpr uint32 kPortScPed = 1U << 1;

// PORTSC bit: Port Reset.
constexpr uint32 kPortScPr = 1U << 4;

// PORTSC bit: Port Power.
constexpr uint32 kPortScPp = 1U << 9;

// PORTSC bit: Connect Status Change (RW1C).
constexpr uint32 kPortScCsc = 1U << 17;

// PORTSC bit: Port Reset Change (RW1C).
constexpr uint32 kPortScPrc = 1U << 21;

// Mask of all RW1C change bits in PORTSC (bits 17..23).
constexpr uint32 kPortScChangeBitsMask =
    (1U << 17) | (1U << 18) | (1U << 19) | (1U << 20) | (1U << 21) |
    (1U << 22) | (1U << 23);

// Mask of all RW1C bits in PORTSC (including PED bit 1 and change bits 17..23).
constexpr uint32 kPortScRw1cMask = kPortScPed | kPortScChangeBitsMask;

// TRB Control bit: Cycle bit.
constexpr uint32 kTrbCycleBit = 1U << 0;

// TRB Control bit: Toggle Cycle (for Link TRBs).
constexpr uint32 kTrbLinkToggleCycle = 1U << 1;

// TRB Control bit: Interrupt On Completion.
constexpr uint32 kTrbIocBit = 1U << 5;

// TRB Control bit: Immediate Data (for Setup Stage TRBs).
constexpr uint32 kTrbIdtBit = 1U << 6;

// TRB Control bit: Direction IN (for Data/Status Stage TRBs).
constexpr uint32 kTrbDirInBit = 1U << 16;

// Shift for TRB Type field in TRB Control word.
constexpr uint32 kTrbTypeShift = 10;

// Mask for 6-bit TRB Type field after shifting.
constexpr uint32 kTrbTypeMask = 0x3F;

// xHCI Extended Capability ID for USB Legacy Support.
constexpr uint8 kExtCapIdLegacySupport = 1;

// xHCI Extended Capability ID for Supported Protocol.
constexpr uint8 kExtCapIdSupportedProtocol = 2;

// USB Speed ID for Full Speed (12 Mbps).
constexpr uint8 kUsbSpeedFull = 1;

// USB Speed ID for Low Speed (1.5 Mbps).
constexpr uint8 kUsbSpeedLow = 2;

// USB Speed ID for High Speed (480 Mbps).
constexpr uint8 kUsbSpeedHigh = 3;

// USB Speed ID for SuperSpeed (5 Gbps).
constexpr uint8 kUsbSpeedSuper = 4;

// USB Descriptor Type: Device.
constexpr uint8 kUsbDescTypeDevice = 0x01;

// USB Descriptor Type: Configuration.
constexpr uint8 kUsbDescTypeConfiguration = 0x02;

// USB Descriptor Type: Interface.
constexpr uint8 kUsbDescTypeInterface = 0x04;

// USB Descriptor Type: Endpoint.
constexpr uint8 kUsbDescTypeEndpoint = 0x05;

// USB Descriptor Type: HID.
constexpr uint8 kUsbDescTypeHid = 0x21;

// USB Descriptor Type: HID Report.
constexpr uint8 kUsbDescTypeHidReport = 0x22;

// USB Descriptor Type: USB 2.0 Hub.
constexpr uint8 kUsbDescTypeHub = 0x29;

// USB Descriptor Type: SuperSpeed USB 3.0 Hub.
constexpr uint8 kUsbDescTypeSsHub = 0x2A;

// USB Class Code: Human Interface Device (HID).
constexpr uint8 kUsbClassHid = 0x03;

// USB Class Code: Hub.
constexpr uint8 kUsbClassHub = 0x09;

// Standard USB Request: GET_STATUS.
constexpr uint8 kUsbReqGetStatus = 0x00;

// Standard USB Request: CLEAR_FEATURE.
constexpr uint8 kUsbReqClearFeature = 0x01;

// Standard USB Request: SET_FEATURE.
constexpr uint8 kUsbReqSetFeature = 0x03;

// Standard USB Request: GET_DESCRIPTOR.
constexpr uint8 kUsbReqGetDescriptor = 0x06;

// Standard USB Request: SET_CONFIGURATION.
constexpr uint8 kUsbReqSetConfiguration = 0x09;

// Hub Port Feature: PORT_RESET.
constexpr uint16 kHubFeaturePortReset = 4;

// Hub Port Feature: PORT_POWER.
constexpr uint16 kHubFeaturePortPower = 8;

// Hub Port Feature: C_PORT_CONNECTION.
constexpr uint16 kHubFeatureCPortConnection = 16;

// Hub Port Feature: C_PORT_RESET.
constexpr uint16 kHubFeatureCPortReset = 20;

struct PciAddress {
  uint8 bus;
  uint8 slot;
  uint8 function;
};

struct RegisteredUsbListener {
  UsbDeviceListener::Client client;
  UsbInterfaceFilter filter;
};

std::vector<PciAddress> pending_xhci_pci_devices;
std::vector<RegisteredUsbListener> usb_device_listeners;

void PruneTerminatedUsbListeners() {
  std::erase_if(usb_device_listeners, [](const RegisteredUsbListener& entry) {
    return !DoesProcessExist(entry.client.ServerProcessId());
  });
}

class XhciController {
 public:
  XhciController(uint32 controller_index, uint8 bus, uint8 slot, uint8 function)
      : controller_index_(controller_index),
        bus_(bus),
        slot_(slot),
        function_(function) {}

  bool Initialize() {
    EnablePciDevice();

    uint32 bar0 = Read32BitsFromPciConfig(bus_, slot_, function_, kPciHdrBar0);
    if (bar0 == 0 || bar0 == 0xFFFFFFFF || (bar0 & 1) != 0) return false;

    uint64 phys_base = bar0 & 0xFFFFFFF0ULL;
    if ((bar0 & 0x06) == 0x04) {
      uint32 bar1 =
          Read32BitsFromPciConfig(bus_, slot_, function_, kPciHdrBar1);
      phys_base |= (static_cast<uint64>(bar1) << 32);
    }
    if (phys_base == 0) return false;

    mmio_phys_base_ = phys_base & ~kPageOffsetMask;
    size_t page_offset = phys_base & kPageOffsetMask;
    mapped_pages_ = kDefaultMmioPages;
    void* mapped = MapPhysicalMemory(mmio_phys_base_, mapped_pages_);
    if (!mapped) return false;

    mmio_base_ = reinterpret_cast<volatile uint8*>(mapped) + page_offset;
    uint8 cap_length = *mmio_base_;
    uint32 hcsparams1 = ReadCap32(0x04);
    uint32 hcsparams2 = ReadCap32(0x08);
    uint32 hccparams1 = ReadCap32(0x10);
    uint32 dboff = ReadCap32(0x14) & ~0x3U;
    uint32 rtsoff = ReadCap32(0x18) & ~0x1FU;

    max_slots_ = std::min<uint8>(hcsparams1 & 0xFF, kMaxSlots - 1);
    max_ports_ = std::min<uint8>((hcsparams1 >> 24) & 0xFF, kMaxPorts);
    context_size_ = ((hccparams1 & (1U << 2)) != 0) ? 64 : 32;
    uint32 max_scratchpad_bufs =
        (((hcsparams2 >> 21) & 0x1F) << 5) | ((hcsparams2 >> 27) & 0x1F);

    size_t required_span =
        std::max({static_cast<size_t>(cap_length + kOpRegPortBase +
                                      max_ports_ * kPortRegStride),
                  static_cast<size_t>(dboff + (max_slots_ + 1) * 4),
                  static_cast<size_t>(rtsoff + 0x40)});
    size_t needed_pages =
        (page_offset + required_span + kPageSize - 1) / kPageSize;
    if (needed_pages > mapped_pages_) {
      mapped = MapPhysicalMemory(mmio_phys_base_, needed_pages);
      if (!mapped) return false;
      mapped_pages_ = needed_pages;
      mmio_base_ = reinterpret_cast<volatile uint8*>(mapped) + page_offset;
    }

    op_base_ = mmio_base_ + cap_length;
    db_base_ = reinterpret_cast<volatile uint32*>(mmio_base_ + dboff);
    db_phys_base_ = phys_base + dboff;
    rt_base_ = mmio_base_ + rtsoff;

    PerformBiosHandoffAndParseProtocols(hccparams1);

    if (!ResetController()) {
      std::cout << "xHCI controller failed to reset." << std::endl;
      return false;
    }

    WriteOp32(kOpRegConfig, max_slots_);

    // Allocate Device Context Base Address Array (DCBAA).
    dcbaa_ = static_cast<uint64*>(AllocateZeroedPage(dcbaa_phys_));
    if (!dcbaa_) return false;

    if (max_scratchpad_bufs > 0) {
      uint64 scratchpad_array_phys = 0;
      uint64* scratchpad_array =
          static_cast<uint64*>(AllocateZeroedPage(scratchpad_array_phys));
      if (!scratchpad_array) return false;
      for (uint32 i = 0; i < max_scratchpad_bufs && i < 512; ++i) {
        uint64 sp_phys = 0;
        if (!AllocateZeroedPage(sp_phys)) return false;
        scratchpad_array[i] = sp_phys;
      }
      FlushRange(scratchpad_array, kPageSize);
      dcbaa_[0] = scratchpad_array_phys;
    }
    FlushRange(dcbaa_, kPageSize);
    WriteOp64(kOpRegDcbaap, dcbaa_phys_);

    // Allocate and initialize the Command Ring.
    cmd_ring_ = static_cast<UsbTrb*>(AllocateZeroedPage(cmd_ring_phys_));
    if (!cmd_ring_) return false;
    cmd_ring_[kCommandRingLinkIndex].parameter = cmd_ring_phys_;
    cmd_ring_[kCommandRingLinkIndex].status = 0;
    cmd_ring_[kCommandRingLinkIndex].control =
        (static_cast<uint32>(XhciTrbType::kLink) << kTrbTypeShift) |
        kTrbLinkToggleCycle | kTrbCycleBit;
    FlushRange(cmd_ring_, kPageSize);
    cmd_enqueue_idx_ = 0;
    cmd_pcs_ = 1;
    WriteOp64(kOpRegCrcr, cmd_ring_phys_ | kTrbCycleBit);

    // Allocate and initialize Primary Event Ring and ERST.
    event_ring_ = static_cast<UsbTrb*>(AllocateZeroedPage(event_ring_phys_));
    erst_ = static_cast<XhciErstEntry*>(AllocateZeroedPage(erst_phys_));
    if (!event_ring_ || !erst_) return false;
    erst_[0].ring_segment_base_address = event_ring_phys_;
    erst_[0].ring_segment_size = kEventRingSize;
    FlushRange(erst_, kPageSize);
    event_dequeue_idx_ = 0;
    event_ccs_ = 1;

    WriteIntr32(0x00, 0x1);  // IMAN: Clear IP (bit 0), leave IE disabled
    WriteIntr32(0x04, 0);    // IMOD: Disable interrupt throttling
    WriteIntr32(0x08, 1);    // ERSTSZ = 1 segment
    WriteIntr64(0x18, event_ring_phys_ | (1ULL << 3));  // ERDP (clear EHB)
    WriteIntr64(0x10, erst_phys_);                      // ERSTBA

    // Start controller (polled Event Ring mode to avoid PCI INTx storm).
    WriteOp32(kOpRegUsbCmd, kUsbCmdRunStop);
    for (int i = 0; i < 100; ++i) {
      if ((ReadOp32(kOpRegUsbSts) & kUsbStsHalted) == 0) break;
      SleepForDuration(std::chrono::milliseconds(1));
    }

    // Power on root ports if not already powered.
    for (uint8 p = 0; p < max_ports_; ++p) {
      uint32 portsc = ReadPortSc(p);
      if ((portsc & kPortScPp) == 0)
        WritePortSc(p, (portsc & ~kPortScRw1cMask) | kPortScPp);
    }
    SleepForDuration(std::chrono::milliseconds(20));

    // Perform initial root-hub enumeration synchronously so boot pointing
    // devices are known before fallback driver decisions.
    ScanPortsAndHubs();

    // Start background fiber to poll event ring and handle hotplug / hubs.
    Fiber::Create([this]() {
      uint32 tick = 0;
      while (true) {
        PollEventRing();
        if (++tick >= 50 || port_status_changed_) {
          port_status_changed_ = false;
          tick = 0;
          ScanPortsAndHubs();
        }
        SleepForDuration(std::chrono::milliseconds(2));
      }
    })->WakeUp();

    return true;
  }

  void AppendMatchingInterfaces(const UsbInterfaceFilter& filter,
                                std::vector<UsbInterfaceInfo>& out) const {
    for (const auto& [slot_id, slot] : slots_) {
      if (!slot || !slot->active) continue;
      for (const auto& iface : slot->interfaces) {
        if (MatchesFilter(iface, filter)) out.push_back(iface);
      }
    }
  }

  StatusOr<UsbEndpointRingInfo> OpenInterruptEndpoint(
      const OpenUsbEndpointRequest& request) {
    uint8 slot_id = static_cast<uint8>(request.device_handle & 0xFF);
    auto it = slots_.find(slot_id);
    if (it == slots_.end() || !it->second || !it->second->active)
      return Status::INVALID_ARGUMENT;

    SlotState& slot = *it->second;
    uint8 dci = request.endpoint_dci;
    if (dci < 2 || dci >= kMaxEndpointsPerSlot) return Status::INVALID_ARGUMENT;

    const UsbInterfaceInfo* matched_iface = nullptr;
    for (const auto& iface : slot.interfaces) {
      if (iface.interface_number == request.interface_number &&
          iface.interrupt_in_dci == dci) {
        matched_iface = &iface;
        break;
      }
    }
    if (!matched_iface) return Status::INVALID_ARGUMENT;

    auto& ep = slot.endpoints[dci];
    if (!ep.ring_memory) {
      ep.ring_memory = perception::SharedMemory::FromSize(
          kPageSize, perception::SharedMemory::kJoinersCanWrite);
      if (!ep.ring_memory || !ep.ring_memory->Join())
        return Status::OUT_OF_MEMORY;

      void* raw_page = **ep.ring_memory;
      std::memset(raw_page, 0, kPageSize);

      auto phys_opt = ep.ring_memory->GetPhysicalAddress(0);
      if (!phys_opt) return Status::INTERNAL_ERROR;
      ep.ring_phys = *phys_opt;

      UsbTrb* ring = static_cast<UsbTrb*>(raw_page);
      ring[kUsbTransferRingLinkIndex].parameter = ep.ring_phys;
      ring[kUsbTransferRingLinkIndex].status = 0;
      ring[kUsbTransferRingLinkIndex].control =
          (static_cast<uint32>(XhciTrbType::kLink) << kTrbTypeShift) |
          kTrbLinkToggleCycle | kTrbCycleBit;
      FlushRange(raw_page, kPageSize);

      if (!ConfigureInterruptInEndpoint(slot, dci, matched_iface->max_packet_size,
                                        matched_iface->interval, ep.ring_phys)) {
        ep.ring_memory.reset();
        return Status::INTERNAL_ERROR;
      }
    }

    UsbEndpointRingInfo info;
    info.ring_memory =
        std::make_shared<perception::SharedMemory>(ep.ring_memory->Clone());
    info.ring_phys_address = ep.ring_phys;
    info.doorbell_phys_address = db_phys_base_ + static_cast<uint64>(slot_id) * 4;
    info.doorbell_value = dci;
    return info;
  }

  StatusOr<UsbControlTransferResponse> ControlTransferRpc(
      const UsbControlTransferRequest& request) {
    uint8 slot_id = static_cast<uint8>(request.device_handle & 0xFF);
    auto it = slots_.find(slot_id);
    if (it == slots_.end() || !it->second || !it->second->active)
      return Status::INVALID_ARGUMENT;

    SlotState& slot = *it->second;
    uint16 len = std::min<uint16>(request.length, kPageSize);
    std::string rx_buf(len, '\0');
    uint16 actual_len = 0;
    if (!ControlTransfer(slot, request.request_type, request.request,
                         request.value, request.index, len,
                         request.data.data(), rx_buf.data(), actual_len)) {
      return Status::INTERNAL_ERROR;
    }
    rx_buf.resize(actual_len);
    UsbControlTransferResponse response;
    response.data = std::move(rx_buf);
    return response;
  }

  static bool MatchesFilter(const UsbInterfaceInfo& iface,
                            const UsbInterfaceFilter& filter) {
    if (filter.interface_class != -1 &&
        iface.interface_class != static_cast<uint8>(filter.interface_class))
      return false;
    if (filter.interface_subclass != -1 &&
        iface.interface_subclass !=
            static_cast<uint8>(filter.interface_subclass))
      return false;
    if (filter.interface_protocol != -1 &&
        iface.interface_protocol !=
            static_cast<uint8>(filter.interface_protocol))
      return false;
    return true;
  }

 private:
  struct EndpointState {
    std::shared_ptr<perception::SharedMemory> ring_memory;
    uint64 ring_phys = 0;
  };

  struct SlotState {
    uint8 slot_id = 0;
    uint32 device_handle = 0;
    bool active = false;
    uint8 root_port = 0;
    uint32 route_string = 0;
    uint8 parent_hub_slot_id = 0;
    uint8 parent_port = 0;
    uint8 speed = 0;
    uint16 max_packet_size_0 = 8;

    void* output_context = nullptr;
    uint64 output_context_phys = 0;
    void* input_context = nullptr;
    uint64 input_context_phys = 0;

    UsbTrb* ep0_ring = nullptr;
    uint64 ep0_ring_phys = 0;
    size_t ep0_enqueue_idx = 0;
    uint32 ep0_pcs = 1;
    void* ep0_dma_buffer = nullptr;
    uint64 ep0_dma_phys = 0;

    bool ep0_transfer_done = false;
    uint32 ep0_completion_code = 0;
    uint32 ep0_residual_length = 0;

    bool is_hub = false;
    uint8 num_hub_ports = 0;
    uint8 hub_port_slots[kMaxPorts] = {};

    std::vector<UsbInterfaceInfo> interfaces;
    EndpointState endpoints[kMaxEndpointsPerSlot];

    ~SlotState() {
      if (output_context) ReleaseMemoryPages(output_context, 1);
      if (input_context) ReleaseMemoryPages(input_context, 1);
      if (ep0_ring) ReleaseMemoryPages(ep0_ring, 1);
      if (ep0_dma_buffer) ReleaseMemoryPages(ep0_dma_buffer, 1);
    }
  };

  uint32 controller_index_;
  uint8 bus_;
  uint8 slot_;
  uint8 function_;

  uint64 mmio_phys_base_ = 0;
  size_t mapped_pages_ = 0;
  volatile uint8* mmio_base_ = nullptr;
  volatile uint8* op_base_ = nullptr;
  volatile uint32* db_base_ = nullptr;
  uint64 db_phys_base_ = 0;
  volatile uint8* rt_base_ = nullptr;

  uint8 max_slots_ = 0;
  uint8 max_ports_ = 0;
  size_t context_size_ = 32;
  bool is_usb3_port_[kMaxPorts] = {};
  uint8 root_port_slot_[kMaxPorts] = {};

  uint64* dcbaa_ = nullptr;
  uint64 dcbaa_phys_ = 0;

  UsbTrb* cmd_ring_ = nullptr;
  uint64 cmd_ring_phys_ = 0;
  size_t cmd_enqueue_idx_ = 0;
  uint32 cmd_pcs_ = 1;

  bool cmd_done_ = false;
  uint64 pending_cmd_phys_ = 0;
  uint32 cmd_completion_code_ = 0;
  uint8 cmd_completion_slot_ = 0;

  UsbTrb* event_ring_ = nullptr;
  uint64 event_ring_phys_ = 0;
  XhciErstEntry* erst_ = nullptr;
  uint64 erst_phys_ = 0;
  size_t event_dequeue_idx_ = 0;
  uint32 event_ccs_ = 1;

  bool port_status_changed_ = false;
  bool is_scanning_ = false;
  std::map<uint8, std::unique_ptr<SlotState>> slots_;

  void* AllocateZeroedPage(uint64& out_phys) {
    void* page = AllocateMemoryPages(1);
    if (!page) return nullptr;
    std::memset(page, 0, kPageSize);
    out_phys = GetPhysicalAddressOfVirtualAddress(reinterpret_cast<size_t>(page));
    return page;
  }

  uint32 ReadCap32(size_t offset) const {
    return *reinterpret_cast<volatile const uint32*>(mmio_base_ + offset);
  }

  uint32 ReadOp32(size_t offset) const {
    return *reinterpret_cast<volatile const uint32*>(op_base_ + offset);
  }

  void WriteOp32(size_t offset, uint32 value) {
    *reinterpret_cast<volatile uint32*>(op_base_ + offset) = value;
  }

  void WriteOp64(size_t offset, uint64 value) {
    *reinterpret_cast<volatile uint32*>(op_base_ + offset) =
        static_cast<uint32>(value & 0xFFFFFFFFULL);
    *reinterpret_cast<volatile uint32*>(op_base_ + offset + 4) =
        static_cast<uint32>(value >> 32);
  }

  uint32 ReadPortSc(uint8 port_idx) const {
    return ReadOp32(kOpRegPortBase + port_idx * kPortRegStride);
  }

  void WritePortSc(uint8 port_idx, uint32 value) {
    WriteOp32(kOpRegPortBase + port_idx * kPortRegStride, value);
  }

  uint32 ReadIntr32(size_t offset) const {
    return *reinterpret_cast<volatile const uint32*>(rt_base_ + 0x20 + offset);
  }

  void WriteIntr32(size_t offset, uint32 value) {
    *reinterpret_cast<volatile uint32*>(rt_base_ + 0x20 + offset) = value;
  }

  void WriteIntr64(size_t offset, uint64 value) {
    *reinterpret_cast<volatile uint32*>(rt_base_ + 0x20 + offset) =
        static_cast<uint32>(value & 0xFFFFFFFFULL);
    *reinterpret_cast<volatile uint32*>(rt_base_ + 0x20 + offset + 4) =
        static_cast<uint32>(value >> 32);
  }

  void EnablePciDevice() {
    uint8 cmd_low =
        Read8BitsFromPciConfig(bus_, slot_, function_, kPciHdrCommand);
    uint8 cmd_high =
        Read8BitsFromPciConfig(bus_, slot_, function_, kPciHdrCommand + 1);
    uint16 cmd = cmd_low | (static_cast<uint16>(cmd_high) << 8);
    cmd |= kPciHdrCommandBitMemorySpace | kPciHdrCommandBitBusMaster |
           kPciCmdInterruptDisableBit;
    Write8BitsToPciConfig(bus_, slot_, function_, kPciHdrCommand, cmd & 0xFF);
    Write8BitsToPciConfig(bus_, slot_, function_, kPciHdrCommand + 1,
                          (cmd >> 8) & 0xFF);

    uint8 cap_ptr =
        Read8BitsFromPciConfig(bus_, slot_, function_, kPciCapPtrOffset);
    int safety = 48;
    while (cap_ptr != 0 && cap_ptr != 0xFF && safety-- > 0) {
      uint8 cap_id = Read8BitsFromPciConfig(bus_, slot_, function_, cap_ptr);
      uint8 next_ptr =
          Read8BitsFromPciConfig(bus_, slot_, function_, cap_ptr + 1);
      if (cap_id == kPciCapIdMsi) {
        uint8 msi_ctrl =
            Read8BitsFromPciConfig(bus_, slot_, function_, cap_ptr + 2);
        if (msi_ctrl & kMsiControlEnableBit) {
          msi_ctrl &= ~kMsiControlEnableBit;
          Write8BitsToPciConfig(bus_, slot_, function_, cap_ptr + 2, msi_ctrl);
        }
      } else if (cap_id == kPciCapIdMsiX) {
        uint8 msix_ctrl_high =
            Read8BitsFromPciConfig(bus_, slot_, function_, cap_ptr + 3);
        if (msix_ctrl_high & kMsixControlHighEnableBit) {
          msix_ctrl_high &= ~kMsixControlHighEnableBit;
          Write8BitsToPciConfig(bus_, slot_, function_, cap_ptr + 3,
                                msix_ctrl_high);
        }
      }
      cap_ptr = next_ptr;
    }
  }

  void PerformBiosHandoffAndParseProtocols(uint32 hccparams1) {
    uint32 xecp_dwords = (hccparams1 >> 16) & 0xFFFF;
    if (xecp_dwords == 0) return;

    size_t offset = static_cast<size_t>(xecp_dwords) * 4;
    int safety = 64;
    while (offset != 0 && offset + 8 <= mapped_pages_ * kPageSize &&
           safety-- > 0) {
      uint32 cap_hdr = ReadCap32(offset);
      uint8 cap_id = cap_hdr & 0xFF;
      uint8 next_dwords = (cap_hdr >> 8) & 0xFF;

      if (cap_id == kExtCapIdLegacySupport) {
        volatile uint8* leg_sup = mmio_base_ + offset;
        if (leg_sup[2] & 1) {
          leg_sup[3] |= 1;  // Set OS Owned Semaphore
          for (int i = 0; i < 200; ++i) {
            if ((leg_sup[2] & 1) == 0) break;
            SleepForDuration(std::chrono::milliseconds(5));
          }
        }
        // Disable SMI generation in USBLEGCTLSTS.
        *reinterpret_cast<volatile uint32*>(mmio_base_ + offset + 4) &=
            ~0xE005FFFU;
      } else if (cap_id == kExtCapIdSupportedProtocol) {
        uint8 major_rev = (cap_hdr >> 24) & 0xFF;
        uint32 port_info = ReadCap32(offset + 8);
        uint8 first_port = port_info & 0xFF;
        uint8 port_count = (port_info >> 8) & 0xFF;
        if (major_rev >= 3 && first_port >= 1) {
          for (uint8 i = 0; i < port_count; ++i) {
            uint8 port_idx = (first_port - 1) + i;
            if (port_idx < kMaxPorts) is_usb3_port_[port_idx] = true;
          }
        }
      }

      if (next_dwords == 0) break;
      offset += static_cast<size_t>(next_dwords) * 4;
    }
  }

  bool ResetController() {
    uint32 cmd = ReadOp32(kOpRegUsbCmd);
    cmd &= ~kUsbCmdRunStop;
    WriteOp32(kOpRegUsbCmd, cmd);

    for (int i = 0; i < 100; ++i) {
      if (ReadOp32(kOpRegUsbSts) & kUsbStsHalted) break;
      SleepForDuration(std::chrono::milliseconds(1));
    }

    WriteOp32(kOpRegUsbCmd, kUsbCmdHcReset);
    for (int i = 0; i < 500; ++i) {
      uint32 usbcmd = ReadOp32(kOpRegUsbCmd);
      uint32 usbsts = ReadOp32(kOpRegUsbSts);
      if ((usbcmd & kUsbCmdHcReset) == 0 &&
          (usbsts & kUsbStsControllerNotReady) == 0) {
        return true;
      }
      SleepForDuration(std::chrono::milliseconds(1));
    }
    return false;
  }

  void PollEventRing() {
    uint32 usbsts = ReadOp32(kOpRegUsbSts);
    if (usbsts & (kUsbStsEventInterrupt | kUsbStsPortChangeDetect))
      WriteOp32(kOpRegUsbSts, usbsts & (kUsbStsEventInterrupt |
                                        kUsbStsPortChangeDetect));

    uint32 iman = ReadIntr32(0x00);
    if (iman & 1U) WriteIntr32(0x00, iman | 1U);

    bool processed_any = false;
    int safety = kEventRingSize * 2;
    while (safety-- > 0) {
      FlushRange(&event_ring_[event_dequeue_idx_], sizeof(UsbTrb));
      UsbTrb ev = event_ring_[event_dequeue_idx_];
      if ((ev.control & kTrbCycleBit) != event_ccs_) break;

      processed_any = true;
      XhciTrbType trb_type = static_cast<XhciTrbType>(
          (ev.control >> kTrbTypeShift) & kTrbTypeMask);
      uint32 comp_code = (ev.status >> 24) & 0xFF;

      switch (trb_type) {
        case XhciTrbType::kCommandCompletionEvent: {
          uint64 cmd_phys = ev.parameter;
          uint8 slot_id = (ev.control >> 24) & 0xFF;
          if (cmd_phys == pending_cmd_phys_) {
            cmd_completion_code_ = comp_code;
            cmd_completion_slot_ = slot_id;
            cmd_done_ = true;
          }
          break;
        }
        case XhciTrbType::kPortStatusChangeEvent:
          port_status_changed_ = true;
          break;
        case XhciTrbType::kTransferEvent: {
          uint8 slot_id = (ev.control >> 24) & 0xFF;
          uint8 dci = (ev.control >> 16) & 0x1F;
          uint32 residual = ev.status & 0xFFFFFF;
          auto it = slots_.find(slot_id);
          if (it != slots_.end() && it->second) {
            SlotState& slot = *it->second;
            if (dci == 1) {
              slot.ep0_completion_code = comp_code;
              slot.ep0_residual_length = residual;
              slot.ep0_transfer_done = true;
            } else if (dci < kMaxEndpointsPerSlot &&
                       slot.endpoints[dci].ring_memory) {
              auto& shm = slot.endpoints[dci].ring_memory;
              auto* hdr = reinterpret_cast<UsbEndpointRingSharedHeader*>(
                  static_cast<uint8*>(**shm) + kUsbRingHeaderOffset);
              uint32 w_idx =
                  hdr->completion_write_index.load(std::memory_order_relaxed);
              hdr->completions[w_idx % kUsbMaxCompletions] = {
                  ev.parameter, comp_code, residual};
              hdr->completion_write_index.store(w_idx + 1,
                                                std::memory_order_release);
              shm->TriggerEvent(kUsbRingEventOffset);
            }
          }
          break;
        }
        default:
          break;
      }

      event_dequeue_idx_++;
      if (event_dequeue_idx_ >= kEventRingSize) {
        event_dequeue_idx_ = 0;
        event_ccs_ ^= 1;
      }
    }

    if (processed_any) {
      uint64 erdp = event_ring_phys_ + event_dequeue_idx_ * sizeof(UsbTrb);
      WriteIntr64(0x18, erdp | (1ULL << 3));
    }
  }

  bool SubmitCommand(XhciTrbType type, uint64 param, uint32 control_extra,
                     uint8* out_slot_id = nullptr) {
    uint64 trb_phys = cmd_ring_phys_ + cmd_enqueue_idx_ * sizeof(UsbTrb);
    pending_cmd_phys_ = trb_phys;
    cmd_done_ = false;
    cmd_completion_code_ = 0;
    cmd_completion_slot_ = 0;

    UsbTrb& trb = cmd_ring_[cmd_enqueue_idx_];
    trb.parameter = param;
    trb.status = 0;
    trb.control = (static_cast<uint32>(type) << kTrbTypeShift) | control_extra |
                  cmd_pcs_;
    FlushRange(&trb, sizeof(UsbTrb));

    cmd_enqueue_idx_++;
    if (cmd_enqueue_idx_ >= kCommandRingLinkIndex) {
      UsbTrb& link = cmd_ring_[kCommandRingLinkIndex];
      link.control = (static_cast<uint32>(XhciTrbType::kLink) << kTrbTypeShift) |
                     kTrbLinkToggleCycle | cmd_pcs_;
      FlushRange(&link, sizeof(UsbTrb));
      cmd_enqueue_idx_ = 0;
      cmd_pcs_ ^= 1;
    }

    db_base_[0] = 0;

    for (int i = 0; i < 1000; ++i) {
      PollEventRing();
      if (cmd_done_) break;
      SleepForDuration(std::chrono::milliseconds(1));
    }

    if (!cmd_done_ ||
        cmd_completion_code_ !=
            static_cast<uint32>(XhciCompletionCode::kSuccess)) {
      return false;
    }
    if (out_slot_id) *out_slot_id = cmd_completion_slot_;
    return true;
  }

  void EnqueueEp0Trb(SlotState& slot, uint64 param, uint32 status,
                     uint32 control_without_cycle) {
    UsbTrb& trb = slot.ep0_ring[slot.ep0_enqueue_idx];
    trb.parameter = param;
    trb.status = status;
    trb.control = control_without_cycle | slot.ep0_pcs;
    FlushRange(&trb, sizeof(UsbTrb));

    slot.ep0_enqueue_idx++;
    if (slot.ep0_enqueue_idx >= kUsbTransferRingLinkIndex) {
      UsbTrb& link = slot.ep0_ring[kUsbTransferRingLinkIndex];
      link.control = (static_cast<uint32>(XhciTrbType::kLink) << kTrbTypeShift) |
                     kTrbLinkToggleCycle | slot.ep0_pcs;
      FlushRange(&link, sizeof(UsbTrb));
      slot.ep0_enqueue_idx = 0;
      slot.ep0_pcs ^= 1;
    }
  }

  bool ControlTransfer(SlotState& slot, uint8 request_type, uint8 request,
                       uint16 value, uint16 index, uint16 length,
                       const void* out_data, void* in_data,
                       uint16& actual_length) {
    actual_length = 0;
    slot.ep0_transfer_done = false;
    slot.ep0_completion_code = 0;
    slot.ep0_residual_length = 0;

    bool is_in = (request_type & 0x80) != 0;
    uint64 setup_packet =
        static_cast<uint64>(request_type) |
        (static_cast<uint64>(request) << 8) |
        (static_cast<uint64>(value) << 16) |
        (static_cast<uint64>(index) << 32) |
        (static_cast<uint64>(length) << 48);

    uint32 trt = 0;
    if (length > 0) trt = is_in ? 3U : 2U;

    EnqueueEp0Trb(slot, setup_packet, 8,
                  (static_cast<uint32>(XhciTrbType::kSetupStage)
                   << kTrbTypeShift) |
                      kTrbIdtBit | (trt << 16));

    if (length > 0) {
      if (!is_in && out_data)
        std::memcpy(slot.ep0_dma_buffer, out_data, length);
      else
        std::memset(slot.ep0_dma_buffer, 0, length);
      FlushRange(slot.ep0_dma_buffer, length);

      EnqueueEp0Trb(
          slot, slot.ep0_dma_phys, length,
          (static_cast<uint32>(XhciTrbType::kDataStage) << kTrbTypeShift) |
              (is_in ? kTrbDirInBit : 0U));
    }

    bool status_in = (length == 0 || !is_in);
    EnqueueEp0Trb(
        slot, 0, 0,
        (static_cast<uint32>(XhciTrbType::kStatusStage) << kTrbTypeShift) |
            kTrbIocBit | (status_in ? kTrbDirInBit : 0U));

    db_base_[slot.slot_id] = 1;

    for (int i = 0; i < 1000; ++i) {
      PollEventRing();
      if (slot.ep0_transfer_done) break;
      SleepForDuration(std::chrono::milliseconds(1));
    }

    if (!slot.ep0_transfer_done) return false;
    if (slot.ep0_completion_code !=
            static_cast<uint32>(XhciCompletionCode::kSuccess) &&
        slot.ep0_completion_code !=
            static_cast<uint32>(XhciCompletionCode::kShortPacket)) {
      return false;
    }

    if (length > 0 && is_in && in_data) {
      FlushRange(slot.ep0_dma_buffer, length);
      actual_length = length;
      std::memcpy(in_data, slot.ep0_dma_buffer, actual_length);
    }
    return true;
  }

  XhciInputControlContext* GetInputControlContext(SlotState& slot) const {
    return reinterpret_cast<XhciInputControlContext*>(slot.input_context);
  }

  XhciSlotContext* GetInputSlotContext(SlotState& slot) const {
    return reinterpret_cast<XhciSlotContext*>(
        static_cast<uint8*>(slot.input_context) + context_size_);
  }

  XhciEndpointContext* GetInputEndpointContext(SlotState& slot,
                                               uint8 dci) const {
    return reinterpret_cast<XhciEndpointContext*>(
        static_cast<uint8*>(slot.input_context) + (dci + 1) * context_size_);
  }

  XhciSlotContext* GetOutputSlotContext(SlotState& slot) const {
    return reinterpret_cast<XhciSlotContext*>(slot.output_context);
  }

  XhciEndpointContext* GetOutputEndpointContext(SlotState& slot,
                                                uint8 dci) const {
    return reinterpret_cast<XhciEndpointContext*>(
        static_cast<uint8*>(slot.output_context) + dci * context_size_);
  }

  static uint16 InitialMaxPacketSizeForSpeed(uint8 speed) {
    switch (speed) {
      case kUsbSpeedLow:
      case kUsbSpeedFull:
        return 8;
      case kUsbSpeedHigh:
        return 64;
      case kUsbSpeedSuper:
      default:
        return 512;
    }
  }

  static uint32 ComputeXhciInterval(uint8 speed, uint8 desc_interval) {
    if (speed == kUsbSpeedLow || speed == kUsbSpeedFull) {
      uint32 frames = std::max<uint8>(1, desc_interval);
      uint32 microframes = frames * 8;
      uint32 exponent = 3;
      while ((1U << (exponent + 1)) <= microframes && exponent < 10)
        exponent++;
      return exponent;
    }
    uint8 clamped = std::clamp<uint8>(desc_interval, 1, 16);
    return clamped - 1;
  }

  bool ConfigureInterruptInEndpoint(SlotState& slot, uint8 dci,
                                    uint16 max_packet_size, uint8 interval,
                                    uint64 ring_phys) {
    std::memset(slot.input_context, 0, kPageSize);
    auto* icc = GetInputControlContext(slot);
    icc->add_context_flags = (1U << 0) | (1U << dci);

    auto* out_slot = GetOutputSlotContext(slot);
    auto* in_slot = GetInputSlotContext(slot);
    *in_slot = *out_slot;

    uint8 current_entries = (in_slot->route_speed_entries >> 27) & 0x1F;
    uint8 new_entries = std::max(current_entries, dci);
    in_slot->route_speed_entries =
        (in_slot->route_speed_entries & ~(0x1FU << 27)) |
        (static_cast<uint32>(new_entries) << 27);

    auto* in_ep = GetInputEndpointContext(slot, dci);
    uint32 xhci_interval = ComputeXhciInterval(slot.speed, interval);
    in_ep->ep_state_mult_interval = (xhci_interval << 16);
    in_ep->ep_type_cerr_max_packet_size =
        (3U << 1) | (7U << 3) | (static_cast<uint32>(max_packet_size) << 16);
    in_ep->tr_dequeue_pointer = ring_phys | kTrbCycleBit;
    in_ep->average_trb_length_max_esit =
        static_cast<uint32>(max_packet_size) |
        (static_cast<uint32>(max_packet_size) << 16);

    FlushRange(slot.input_context, kPageSize);
    return SubmitCommand(XhciTrbType::kConfigureEndpointCommand,
                         slot.input_context_phys,
                         static_cast<uint32>(slot.slot_id) << 24);
  }

  void ScanPortsAndHubs() {
    if (is_scanning_) return;
    is_scanning_ = true;

    for (uint8 p = 0; p < max_ports_; ++p) {
      uint32 portsc = ReadPortSc(p);
      uint32 change_bits = portsc & kPortScChangeBitsMask;
      if (change_bits != 0)
        WritePortSc(p, (portsc & ~kPortScRw1cMask) | kPortScPp | change_bits);

      bool connected = (portsc & kPortScCcs) != 0;
      if (connected && root_port_slot_[p] == 0) {
        if (ResetRootPort(p)) {
          uint32 new_portsc = ReadPortSc(p);
          uint8 speed = (new_portsc >> 10) & 0x0F;
          uint8 slot_id =
              EnumerateDevice(p + 1, /*route_string=*/0,
                              /*parent_hub_slot_id=*/0, /*parent_port=*/0,
                              speed);
          if (slot_id != 0) root_port_slot_[p] = slot_id;
        }
      } else if (!connected && root_port_slot_[p] != 0) {
        DisconnectSlot(root_port_slot_[p]);
        root_port_slot_[p] = 0;
      }
    }

    // Snapshot active hub slot IDs so new slots added during hub scans don't
    // invalidate iteration.
    std::vector<uint8> hub_slots;
    for (const auto& [slot_id, slot] : slots_) {
      if (slot && slot->active && slot->is_hub) hub_slots.push_back(slot_id);
    }
    for (uint8 hub_slot_id : hub_slots) {
      auto it = slots_.find(hub_slot_id);
      if (it != slots_.end() && it->second && it->second->active)
        ScanExternalHub(*it->second);
    }

    is_scanning_ = false;
  }

  bool ResetRootPort(uint8 port_idx) {
    uint32 portsc = ReadPortSc(port_idx);
    if (is_usb3_port_[port_idx] && (portsc & kPortScPed) != 0) return true;

    WritePortSc(port_idx,
                (portsc & ~kPortScRw1cMask) | kPortScPp | kPortScPr);

    for (int i = 0; i < 100; ++i) {
      SleepForDuration(std::chrono::milliseconds(2));
      portsc = ReadPortSc(port_idx);
      if ((portsc & kPortScPr) == 0 && (portsc & kPortScPed) != 0) {
        WritePortSc(port_idx,
                    (portsc & ~kPortScRw1cMask) | kPortScPp | kPortScPrc);
        SleepForDuration(std::chrono::milliseconds(10));
        return true;
      }
    }
    return false;
  }

  uint8 EnumerateDevice(uint8 root_port, uint32 route_string,
                        uint8 parent_hub_slot_id, uint8 parent_port,
                        uint8 speed) {
    if (speed == 0) speed = kUsbSpeedFull;

    uint8 slot_id = 0;
    if (!SubmitCommand(XhciTrbType::kEnableSlotCommand, 0, 0, &slot_id) ||
        slot_id == 0) {
      return 0;
    }

    auto slot_ptr = std::make_unique<SlotState>();
    SlotState& slot = *slot_ptr;
    slot.slot_id = slot_id;
    slot.device_handle = (controller_index_ << 16) | slot_id;
    slot.root_port = root_port;
    slot.route_string = route_string;
    slot.parent_hub_slot_id = parent_hub_slot_id;
    slot.parent_port = parent_port;
    slot.speed = speed;
    slot.max_packet_size_0 = InitialMaxPacketSizeForSpeed(speed);

    slot.output_context = AllocateZeroedPage(slot.output_context_phys);
    slot.input_context = AllocateZeroedPage(slot.input_context_phys);
    slot.ep0_ring = static_cast<UsbTrb*>(AllocateZeroedPage(slot.ep0_ring_phys));
    slot.ep0_dma_buffer = AllocateZeroedPage(slot.ep0_dma_phys);
    if (!slot.output_context || !slot.input_context || !slot.ep0_ring ||
        !slot.ep0_dma_buffer) {
      return 0;
    }

    slot.ep0_ring[kUsbTransferRingLinkIndex].parameter = slot.ep0_ring_phys;
    slot.ep0_ring[kUsbTransferRingLinkIndex].status = 0;
    slot.ep0_ring[kUsbTransferRingLinkIndex].control =
        (static_cast<uint32>(XhciTrbType::kLink) << kTrbTypeShift) |
        kTrbLinkToggleCycle | kTrbCycleBit;
    FlushRange(slot.ep0_ring, kPageSize);

    dcbaa_[slot_id] = slot.output_context_phys;
    FlushRange(&dcbaa_[slot_id], sizeof(uint64));

    auto* icc = GetInputControlContext(slot);
    icc->add_context_flags = (1U << 0) | (1U << 1);

    auto* in_slot = GetInputSlotContext(slot);
    in_slot->route_speed_entries = (route_string & 0xFFFFFU) |
                                   (static_cast<uint32>(speed) << 20) |
                                   (1U << 27);
    in_slot->latency_root_port_num_ports =
        static_cast<uint32>(root_port) << 16;

    if (parent_hub_slot_id != 0 &&
        (speed == kUsbSpeedLow || speed == kUsbSpeedFull)) {
      auto parent_it = slots_.find(parent_hub_slot_id);
      if (parent_it != slots_.end() && parent_it->second &&
          parent_it->second->speed == kUsbSpeedHigh) {
        in_slot->tt_info_interrupter =
            static_cast<uint32>(parent_hub_slot_id) |
            (static_cast<uint32>(parent_port) << 8);
      }
    }

    auto* in_ep0 = GetInputEndpointContext(slot, 1);
    in_ep0->ep_type_cerr_max_packet_size =
        (3U << 1) | (4U << 3) |
        (static_cast<uint32>(slot.max_packet_size_0) << 16);
    in_ep0->tr_dequeue_pointer = slot.ep0_ring_phys | kTrbCycleBit;
    in_ep0->average_trb_length_max_esit = 8;

    FlushRange(slot.input_context, kPageSize);

    slots_[slot_id] = std::move(slot_ptr);
    SlotState& active_slot = *slots_[slot_id];

    if (!SubmitCommand(XhciTrbType::kAddressDeviceCommand,
                       active_slot.input_context_phys,
                       static_cast<uint32>(slot_id) << 24)) {
      SubmitCommand(XhciTrbType::kDisableSlotCommand, 0,
                    static_cast<uint32>(slot_id) << 24);
      slots_.erase(slot_id);
      return 0;
    }

    SleepForDuration(std::chrono::milliseconds(10));

    // Read first 8 bytes of the Device Descriptor to discover bMaxPacketSize0.
    UsbDeviceDescriptor dev_desc = {};
    uint16 actual_len = 0;
    if (!ControlTransfer(active_slot, 0x80, kUsbReqGetDescriptor,
                         (kUsbDescTypeDevice << 8) | 0, 0, 8, nullptr,
                         &dev_desc, actual_len) ||
        actual_len < 8) {
      DisconnectSlot(slot_id);
      return 0;
    }

    uint16 new_mps = (speed >= kUsbSpeedSuper)
                         ? (1U << dev_desc.max_packet_size_0)
                         : dev_desc.max_packet_size_0;
    if (new_mps >= 8 && new_mps != active_slot.max_packet_size_0) {
      active_slot.max_packet_size_0 = new_mps;
      std::memset(active_slot.input_context, 0, kPageSize);
      GetInputControlContext(active_slot)->add_context_flags = (1U << 1);
      auto* ep0_ctx = GetInputEndpointContext(active_slot, 1);
      *ep0_ctx = *GetOutputEndpointContext(active_slot, 1);
      ep0_ctx->ep_type_cerr_max_packet_size =
          (3U << 1) | (4U << 3) | (static_cast<uint32>(new_mps) << 16);
      FlushRange(active_slot.input_context, kPageSize);
      (void)SubmitCommand(XhciTrbType::kEvaluateContextCommand,
                          active_slot.input_context_phys,
                          static_cast<uint32>(slot_id) << 24);
    }

    // Read the complete 18-byte Device Descriptor.
    if (!ControlTransfer(active_slot, 0x80, kUsbReqGetDescriptor,
                         (kUsbDescTypeDevice << 8) | 0, 0,
                         sizeof(UsbDeviceDescriptor), nullptr, &dev_desc,
                         actual_len)) {
      DisconnectSlot(slot_id);
      return 0;
    }

    // Read Configuration Descriptor header (9 bytes), then full configuration.
    UsbConfigDescriptor cfg_hdr = {};
    if (!ControlTransfer(active_slot, 0x80, kUsbReqGetDescriptor,
                         (kUsbDescTypeConfiguration << 8) | 0, 0,
                         sizeof(UsbConfigDescriptor), nullptr, &cfg_hdr,
                         actual_len) ||
        cfg_hdr.total_length < sizeof(UsbConfigDescriptor)) {
      DisconnectSlot(slot_id);
      return 0;
    }

    uint16 total_cfg_len = std::min<uint16>(cfg_hdr.total_length, 1024);
    std::vector<uint8> cfg_data(total_cfg_len, 0);
    if (!ControlTransfer(active_slot, 0x80, kUsbReqGetDescriptor,
                         (kUsbDescTypeConfiguration << 8) | 0, 0, total_cfg_len,
                         nullptr, cfg_data.data(), actual_len)) {
      DisconnectSlot(slot_id);
      return 0;
    }
    uint16 cfg_actual_len = actual_len;

    // Set Configuration.
    (void)ControlTransfer(active_slot, 0x00, kUsbReqSetConfiguration,
                          cfg_hdr.configuration_value, 0, 0, nullptr, nullptr,
                          actual_len);

    active_slot.active = true;

    if (dev_desc.device_class == kUsbClassHub)
      InitializeExternalHub(active_slot);

    ParseConfigurationInterfaces(active_slot, dev_desc, cfg_data.data(),
                                 cfg_actual_len);
    return slot_id;
  }

  void ParseConfigurationInterfaces(SlotState& slot,
                                    const UsbDeviceDescriptor& dev_desc,
                                    const uint8* data, size_t length) {
    size_t offset = 0;
    UsbInterfaceInfo* current_iface = nullptr;
    std::vector<uint16> hid_report_lengths;

    while (offset + 2 <= length) {
      uint8 desc_len = data[offset];
      uint8 desc_type = data[offset + 1];
      if (desc_len < 2 || offset + desc_len > length) break;

      if (desc_type == kUsbDescTypeInterface &&
          desc_len >= sizeof(UsbInterfaceDescriptor)) {
        const auto* idesc =
            reinterpret_cast<const UsbInterfaceDescriptor*>(data + offset);
        if (idesc->interface_class == kUsbClassHub && !slot.is_hub)
          InitializeExternalHub(slot);

        if (idesc->alternate_setting == 0) {
          slot.interfaces.push_back({});
          current_iface = &slot.interfaces.back();
          current_iface->device_handle = slot.device_handle;
          current_iface->vendor_id = dev_desc.vendor_id;
          current_iface->product_id = dev_desc.product_id;
          current_iface->device_speed = slot.speed;
          current_iface->interface_number = idesc->interface_number;
          current_iface->alternate_setting = idesc->alternate_setting;
          current_iface->interface_class = idesc->interface_class;
          current_iface->interface_subclass = idesc->interface_subclass;
          current_iface->interface_protocol = idesc->interface_protocol;
          hid_report_lengths.push_back(0);
        } else {
          current_iface = nullptr;
        }
      } else if (desc_type == kUsbDescTypeHid && current_iface &&
                 desc_len >= sizeof(UsbHidDescriptor)) {
        const auto* hdesc =
            reinterpret_cast<const UsbHidDescriptor*>(data + offset);
        if (hdesc->class_descriptor_type == kUsbDescTypeHidReport)
          hid_report_lengths.back() = hdesc->class_descriptor_length;
      } else if (desc_type == kUsbDescTypeEndpoint && current_iface &&
                 desc_len >= sizeof(UsbEndpointDescriptor)) {
        const auto* edesc =
            reinterpret_cast<const UsbEndpointDescriptor*>(data + offset);
        bool is_in = (edesc->endpoint_address & 0x80) != 0;
        bool is_interrupt = (edesc->attributes & 0x03) == 0x03;
        if (is_in && is_interrupt &&
            current_iface->interrupt_in_dci == 0) {
          uint8 ep_num = edesc->endpoint_address & 0x0F;
          current_iface->interrupt_in_endpoint_address =
              edesc->endpoint_address;
          current_iface->interrupt_in_dci = ep_num * 2 + 1;
          current_iface->max_packet_size = edesc->max_packet_size & 0x7FF;
          current_iface->interval = edesc->interval;
        }
      }

      offset += desc_len;
    }

    PruneTerminatedUsbListeners();

    for (size_t i = 0; i < slot.interfaces.size(); ++i) {
      auto& iface = slot.interfaces[i];
      if (iface.interface_class == kUsbClassHid) {
        uint16 report_len = hid_report_lengths[i];
        if (report_len == 0) report_len = 256;
        report_len = std::min<uint16>(report_len, 1024);

        std::string report_buf(report_len, '\0');
        uint16 actual_len = 0;
        if (ControlTransfer(slot, 0x81, kUsbReqGetDescriptor,
                            (kUsbDescTypeHidReport << 8) | 0,
                            iface.interface_number, report_len, nullptr,
                            report_buf.data(), actual_len) &&
            actual_len > 0) {
          report_buf.resize(actual_len);
          iface.hid_report_descriptor = std::move(report_buf);
        }
      }

      bool handled_by_running_driver = false;
      for (auto& entry : usb_device_listeners) {
        if (MatchesFilter(iface, entry.filter)) {
          entry.client.UsbInterfaceAttached(iface, nullptr);
          handled_by_running_driver = true;
        }
      }

      if (!LoadUsbDriver(iface, handled_by_running_driver) &&
          !handled_by_running_driver) {
        std::cout << "Encountered unknown USB interface "
                  << static_cast<int>(iface.interface_class) << ":"
                  << static_cast<int>(iface.interface_subclass) << ":"
                  << static_cast<int>(iface.interface_protocol) << std::endl;
      }
    }
  }

  void InitializeExternalHub(SlotState& slot) {
    uint16 desc_type =
        (slot.speed >= kUsbSpeedSuper) ? kUsbDescTypeSsHub : kUsbDescTypeHub;
    UsbHubDescriptor hub_desc = {};
    uint16 actual_len = 0;
    if (!ControlTransfer(slot, 0xA0, kUsbReqGetDescriptor, (desc_type << 8) | 0,
                         0, sizeof(UsbHubDescriptor), nullptr, &hub_desc,
                         actual_len) ||
        hub_desc.num_ports == 0) {
      return;
    }

    slot.is_hub = true;
    slot.num_hub_ports = std::min<uint8>(hub_desc.num_ports, kMaxPorts - 1);

    std::memset(slot.input_context, 0, kPageSize);
    GetInputControlContext(slot)->add_context_flags = (1U << 0);
    auto* in_slot = GetInputSlotContext(slot);
    *in_slot = *GetOutputSlotContext(slot);
    in_slot->route_speed_entries |= (1U << 26);  // Hub bit
    in_slot->latency_root_port_num_ports =
        (in_slot->latency_root_port_num_ports & 0x00FFFFFFU) |
        (static_cast<uint32>(slot.num_hub_ports) << 24);
    if (slot.speed == kUsbSpeedHigh) {
      uint32 tt_think_time = (hub_desc.hub_characteristics >> 5) & 0x3;
      in_slot->tt_info_interrupter =
          (in_slot->tt_info_interrupter & ~(0x3U << 16)) |
          (tt_think_time << 16);
    }
    FlushRange(slot.input_context, kPageSize);
    (void)SubmitCommand(XhciTrbType::kEvaluateContextCommand,
                        slot.input_context_phys,
                        static_cast<uint32>(slot.slot_id) << 24);

    for (uint8 port = 1; port <= slot.num_hub_ports; ++port) {
      (void)ControlTransfer(slot, 0x23, kUsbReqSetFeature,
                            kHubFeaturePortPower, port, 0, nullptr, nullptr,
                            actual_len);
    }
    SleepForDuration(
        std::chrono::milliseconds(std::max<uint8>(20, hub_desc.power_on_to_power_good * 2)));
  }

  void ScanExternalHub(SlotState& hub_slot) {
    for (uint8 port = 1; port <= hub_slot.num_hub_ports; ++port) {
      uint32 port_status_and_change = 0;
      uint16 actual_len = 0;
      if (!ControlTransfer(hub_slot, 0xA3, kUsbReqGetStatus, 0, port, 4,
                           nullptr, &port_status_and_change, actual_len) ||
          actual_len < 4) {
        continue;
      }

      uint16 status = port_status_and_change & 0xFFFF;
      uint16 change = (port_status_and_change >> 16) & 0xFFFF;

      if (change & 1U) {
        (void)ControlTransfer(hub_slot, 0x23, kUsbReqClearFeature,
                              kHubFeatureCPortConnection, port, 0, nullptr,
                              nullptr, actual_len);
      }

      bool connected = (status & 1U) != 0;
      uint8 child_slot = hub_slot.hub_port_slots[port];

      if (connected && child_slot == 0) {
        // Reset downstream hub port.
        (void)ControlTransfer(hub_slot, 0x23, kUsbReqSetFeature,
                              kHubFeaturePortReset, port, 0, nullptr, nullptr,
                              actual_len);
        SleepForDuration(std::chrono::milliseconds(50));
        (void)ControlTransfer(hub_slot, 0x23, kUsbReqClearFeature,
                              kHubFeatureCPortReset, port, 0, nullptr, nullptr,
                              actual_len);

        if (!ControlTransfer(hub_slot, 0xA3, kUsbReqGetStatus, 0, port, 4,
                             nullptr, &port_status_and_change, actual_len)) {
          continue;
        }
        status = port_status_and_change & 0xFFFF;
        uint8 child_speed = kUsbSpeedFull;
        if (hub_slot.speed >= kUsbSpeedSuper) {
          child_speed = kUsbSpeedSuper;
        } else if (status & (1U << 9)) {
          child_speed = kUsbSpeedLow;
        } else if (status & (1U << 10)) {
          child_speed = kUsbSpeedHigh;
        }

        // Compute route string for downstream port.
        uint32 route = hub_slot.route_string;
        int shift = 0;
        while (shift < 20 && ((route >> shift) & 0xF) != 0) shift += 4;
        if (shift < 20)
          route |= (static_cast<uint32>( std::min<uint8>(port, 15)) << shift);

        uint8 new_slot = EnumerateDevice(hub_slot.root_port, route,
                                         hub_slot.slot_id, port, child_speed);
        if (new_slot != 0) hub_slot.hub_port_slots[port] = new_slot;
      } else if (!connected && child_slot != 0) {
        DisconnectSlot(child_slot);
        hub_slot.hub_port_slots[port] = 0;
      }
    }
  }

  void DisconnectSlot(uint8 slot_id) {
    auto it = slots_.find(slot_id);
    if (it == slots_.end() || !it->second) return;

    std::unique_ptr<SlotState> slot = std::move(it->second);
    slots_.erase(it);

    if (slot->is_hub) {
      for (uint8 p = 1; p <= slot->num_hub_ports; ++p) {
        if (slot->hub_port_slots[p] != 0)
          DisconnectSlot(slot->hub_port_slots[p]);
      }
    }

    for (size_t dci = 2; dci < kMaxEndpointsPerSlot; ++dci) {
      auto& shm = slot->endpoints[dci].ring_memory;
      if (shm) {
        auto* hdr = reinterpret_cast<UsbEndpointRingSharedHeader*>(
            static_cast<uint8*>(**shm) + kUsbRingHeaderOffset);
        hdr->device_disconnected.store(1, std::memory_order_release);
        shm->TriggerEvent(kUsbRingEventOffset);
      }
    }

    PruneTerminatedUsbListeners();
    UsbDeviceId dev_id;
    dev_id.device_handle = slot->device_handle;
    for (auto& entry : usb_device_listeners)
      entry.client.UsbInterfaceDetached(dev_id, nullptr);

    (void)SubmitCommand(XhciTrbType::kDisableSlotCommand, 0,
                        static_cast<uint32>(slot_id) << 24);
    dcbaa_[slot_id] = 0;
    FlushRange(&dcbaa_[slot_id], sizeof(uint64));
  }
};

std::vector<std::unique_ptr<XhciController>> xhci_controllers;

}  // namespace

void RegisterXhciController(uint8 bus, uint8 slot, uint8 function) {
  pending_xhci_pci_devices.push_back({bus, slot, function});
}

void InitializeXhciControllers() {
  for (size_t i = 0; i < pending_xhci_pci_devices.size(); ++i) {
    const auto& pci = pending_xhci_pci_devices[i];
    auto controller = std::make_unique<XhciController>(
        static_cast<uint32>(i), pci.bus, pci.slot, pci.function);
    if (controller->Initialize())
      xhci_controllers.push_back(std::move(controller));
  }
}

StatusOr<UsbInterfaces> QueryXhciUsbInterfaces(
    const UsbInterfaceFilter& filter) {
  UsbInterfaces result;
  for (const auto& controller : xhci_controllers)
    controller->AppendMatchingInterfaces(filter, result.interfaces);
  return result;
}

Status RegisterXhciUsbDeviceListener(
    const RegisterUsbDeviceListenerRequest& request) {
  if (!request.listener.IsValid() ||
      !DoesProcessExist(request.listener.ServerProcessId())) {
    return Status::INVALID_ARGUMENT;
  }
  PruneTerminatedUsbListeners();
  usb_device_listeners.push_back({request.listener, request.filter});

  ProcessId pid = request.listener.ServerProcessId();
  NotifyUponProcessTermination(pid, [pid]() {
    std::erase_if(usb_device_listeners, [pid](const RegisteredUsbListener& entry) {
      return entry.client.ServerProcessId() == pid;
    });
  });

  NotifyWhenServiceDisappears(
      request.listener, [listener = request.listener]() {
        std::erase_if(usb_device_listeners,
                      [&listener](const RegisteredUsbListener& entry) {
                        return entry.client.ServerProcessId() ==
                                   listener.ServerProcessId() &&
                               entry.client.ServiceId() == listener.ServiceId();
                      });
      });
  return Status::OK;
}

StatusOr<UsbEndpointRingInfo> OpenXhciInterruptEndpoint(
    const OpenUsbEndpointRequest& request) {
  uint32 controller_idx = (request.device_handle >> 16) & 0xFFFF;
  if (controller_idx >= xhci_controllers.size())
    return Status::INVALID_ARGUMENT;
  return xhci_controllers[controller_idx]->OpenInterruptEndpoint(request);
}

StatusOr<UsbControlTransferResponse> ExecuteXhciControlTransfer(
    const UsbControlTransferRequest& request) {
  uint32 controller_idx = (request.device_handle >> 16) & 0xFFFF;
  if (controller_idx >= xhci_controllers.size())
    return Status::INVALID_ARGUMENT;
  return xhci_controllers[controller_idx]->ControlTransferRpc(request);
}
