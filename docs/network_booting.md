# Network Booting Perception

This guide explains how to host Perception on your Mac and network boot a physical x86-64 PC (or test locally in QEMU) via UEFI Network Boot over Ethernet.

---

## Requirements

1. **Host (Mac)**:
   - Python 3
   - `dnsmasq` (for DHCP and TFTP):
     ```bash
     brew install dnsmasq
     ```
   - An Ethernet port (`en0`, Thunderbolt Ethernet, or USB-C Ethernet adapter).
2. **Client (x86-64 PC)**:
   - A 64-bit PC supporting UEFI network boot (~2018 or newer).
   - An onboard Ethernet port.
3. **Cable**:
   - Any standard Ethernet patch cable (Cat5e or Cat6). A crossover cable is **not** required because all Gigabit Ethernet ports feature Auto-MDI/MDI-X.

---

## 1. Building Perception

Build Perception using REBS:

```bash
/usr/local/bin/rebs --all --optimized
```

*(Or `/usr/local/bin/rebs --all` for the fast configuration)*.

This compiles the kernel, essential services, libraries, and generates `.build/<configuration>/image.iso`.

---

## 2. Testing Locally in QEMU (Optional)

Before connecting physical hardware, you can test the entire UEFI network boot sequence locally:

1. **Start the local netboot server**:
   ```bash
   python3 tools/netboot/serve.py --configuration=optimized --local-test --ip=10.0.2.2
   ```

2. **In a second terminal, start QEMU in UEFI network boot mode**:
   ```bash
   ./tools/run_qemu_netboot.sh optimized
   ```

QEMU will download `bootx64.efi` via TFTP, fetch the kernel and `image.iso` over HTTP, and boot to the Perception desktop.

---

## 3. Network Booting a Real PC

### Step 1: Connect the Cable
Plug the Ethernet cable directly between your Mac's Ethernet port and the PC's Ethernet port.
- Your Mac can stay connected to Wi-Fi for normal internet access.
- The Ethernet port is dedicated to serving Perception.

### Step 2: Start the Netboot Server
On your Mac, run:

```bash
sudo python3 tools/netboot/serve.py --configuration=optimized
```

- If you have multiple Ethernet interfaces, you can specify `--interface=<name>` (e.g. `--interface=en0` or `--interface=en5`).
- The script will:
  1. Validate that all files referenced in `grub.cfg` and `image.iso` exist in `.build/optimized/`.
  2. Prepare `.build/netboot-optimized/` with TFTP and HTTP roots.
  3. Configure your Ethernet interface with static IP `192.168.42.1`.
  4. Start `dnsmasq` to provide DHCP leases (`192.168.42.50`–`100`) and TFTP delivery of `bootx64.efi`.
  5. Start the HTTP server on port 8080 to serve the kernel, modules, and `image.iso`.

### Step 3: Boot the PC
1. Power on the PC.
2. Immediately press the motherboard's **Boot Menu key**:
   - **Dell / Lenovo**: `F12`
   - **ASUS**: `F8`
   - **MSI / ASRock**: `F11`
   - **HP**: `F9` or `Esc` -> `F9`
3. In the boot menu, select **UEFI Network Boot** (or **UEFI IPv4 PXE**).
4. Watch the Mac's terminal:
   - You will see the DHCP request and lease granted.
   - You will see `bootx64.efi` transferred via TFTP.
   - You will see HTTP `GET` requests for `Kernel.app`, essential modules, and `image.iso`.
5. The PC will display the GRUB menu and boot into Perception!

---

## Motherboard BIOS Settings

If the PC does not show the network boot option or cannot boot:
- **Enable Network Boot**: Look for "Network Stack", "PXE Boot", or "UEFI Network Boot" in BIOS settings and set to **Enabled**.
- **Boot Mode**: Ensure boot mode is set to **UEFI** (not Legacy / CSM).
- **USB Legacy Emulation**: Ensure "USB Legacy Support" or "Port 60h/64h Emulation" is **Enabled** so your USB keyboard and mouse work with Perception's PS/2 driver.
- **Secure Boot**: `bootx64.efi` is signed with the Microsoft UEFI CA key, but if your motherboard firmware rejects it, temporarily disable Secure Boot.
