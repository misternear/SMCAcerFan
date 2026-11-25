# SMCAcerFan

macOS VirtualSMC sensor and fan-control kext for the **Acer Aspire E5-575** (direct Embedded Controller port I/O over `0x62`/`0x66`, exposing standard Apple SMC fan keys for CLI control).

> [!WARNING]
> **AI-Generated ("Vibecoded") Kernel Extension:** This driver was completely vibecoded with an AI assistant rather than written by an experienced macOS kernel developer. Because it runs in kernel space and performs raw port I/O directly against the Embedded Controller (`0x62`/`0x66`), bugs or running it on an incompatible laptop may cause EC lockups, kernel panics, or improper thermal/fan behavior—use at your own risk and keep a working backup EFI/bootable recovery USB handy.

**Confirmed Working Environment:** macOS **15.7.5** (Build `24G624`, `x86_64`) on an Acer Aspire E5-575 (Intel Skylake, IT8987 EC). Other macOS versions and Acer models have not been tested.

---

## Overview & How It Works

Acer Aspire E5-575 laptops manage fan speed through an Embedded Controller (EC) register map rather than standard ACPI fan methods or Super I/O fan PWM registers.

`SMCAcerFan.kext` bridges this EC interface to macOS's VirtualSMC ecosystem:

1. **EC Sanity Probe (`AcerFanController::probe`):** Reads EC registers `0x93` (fan mode) and `0x55` (fan level) over ports `0x62`/`0x66`. If `0x93` is not a recognized state (`0x00`, `0x04`, or `0x14`) or `0x55 > 8`, the kext aborts cleanly without writing to the EC.
2. **VirtualSMC Key Registration:** Registers 8 standard fan keys (`FNum`, `F0ID`, `F0Ac`, `F0Mn`, `F0Mx`, `F0Tg`, `F0Md`, and `FS! `) after a ~5-second deferred delay so its keys take precedence over any duplicate definitions.
3. **500 ms Watchdog Keep-Alive (`AcerFanController::tick`):** When manual mode is active (`F0Md = 1` or `FS! = 0x0001`), the workloop timer verifies `0x93 == 0x14` and writes the quantized target level (`0–8`) to `0x55` every `500 ms` so the Acer EC watchdog does not revert to BIOS automatic control.
4. **90 °C CPU Thermal Failsafe:** Reads Intel CPU package/core thermal MSRs (`0x1A2` for TjMax, `0x1B1` / `0x19C` for digital readout) on every tick. If CPU temperature reaches **90 °C**, `SMCAcerFan` overrides manual mode and returns fan control to the BIOS (`0x93 = 0x04`, `0x55 = 0`) until the CPU cools to **80 °C**.
5. **Lifecycle Restoration:** Automatically hands fan control back to the BIOS on system sleep, shutdown, kext unload (`restoreAuto()`), or after 5 consecutive EC I/O timeouts.

---

## Hardware Specificity & EC Register Map

EC register layouts are **strictly board- and firmware-specific**. Running `SMCAcerFan` on a different Acer model with a different EC firmware map may write to unrelated EC registers.

Verified register map on the **Acer Aspire E5-575**:

| EC Register | Name | Values | Description |
| :--- | :--- | :--- | :--- |
| `0x93` | Fan Mode | `0x00` / `0x04` = Auto<br>`0x14` = Manual | `0x00` is cold-boot BIOS default; `0x04` restores BIOS automatic control; `0x14` enables manual override. |
| `0x55` | Fan Level | `0` – `8` | Discrete speed steps (`0` = fan off / `0 RPM`, `8` = max speed / `4500 RPM`, `562.5 RPM` per step). |

---

## Exposed VirtualSMC Keys

`SMCAcerFan` publishes a single fan (`FNum = 1`):

| Key | Type | Access | Description |
| :--- | :--- | :--- | :--- |
| `FNum` | `ui8` | Read | Number of fans (`1`). |
| `F0ID` | `{fds` | Read | Fan identifier string (`"CPU Fan"`). |
| `F0Ac` | `fpe2` | Read | Current actual RPM reported from EC level `0x55` (`level × 4500 / 8`). |
| `F0Mn` | `fpe2` | Read | Minimum RPM (`0`). |
| `F0Mx` | `fpe2` | Read | Maximum RPM (`4500`). |
| `F0Tg` | `fpe2` | Read / Write | Target RPM (`0–4500`), quantized to the nearest discrete EC level (`0–8`). |
| `F0Md` | `ui8` | Read / Write | Fan control mode (`0` = BIOS Auto, `1` = Manual). |
| `FS! ` | `ui16` | Read / Write | Force-manual bitmask (`0x0001` = Manual, `0x0000` = Auto). |

---

## Requirements

- **Tested OS:** macOS 15.7.5 (Build `24G624`, `x86_64`)
- **Build Dependencies:**
  - Xcode Command Line Tools (`clang++`, `xcrun`, `make`)
  - [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)
  - [Lilu](https://github.com/acidanthera/Lilu)
  - [VirtualSMC](https://github.com/acidanthera/VirtualSMC)
- **Runtime Dependencies:**
  - [Lilu.kext](https://github.com/acidanthera/Lilu)
  - [VirtualSMC.kext](https://github.com/acidanthera/VirtualSMC)

---

## Building from Source

1. Clone the SDK dependencies into `deps/`:
   ```sh
   mkdir -p deps
   git clone [https://github.com/acidanthera/MacKernelSDK.git](https://github.com/acidanthera/MacKernelSDK.git) deps/MacKernelSDK
   git clone [https://github.com/acidanthera/Lilu.git](https://github.com/acidanthera/Lilu.git) deps/Lilu
   git clone [https://github.com/acidanthera/VirtualSMC.git](https://github.com/acidanthera/VirtualSMC.git) deps/VirtualSMC
   ```
2. Compile `SMCAcerFan.kext` and the `smctest` CLI utility:
   ```sh
   make
   clang -O2 -Wall -framework IOKit -framework CoreFoundation tools/smctest.c -o tools/smctest
   ```
   The compiled kext is written to `build/SMCAcerFan.kext`.

---

## Installation (OpenCore)

1. **Copy Kext:**
   Place `build/SMCAcerFan.kext` into `EFI/OC/Kexts/` on your EFI partition.
2. **Register in `EFI/OC/config.plist` (`Kernel -> Add`):**
   Ensure `Lilu.kext` and `VirtualSMC.kext` load **before** `SMCAcerFan.kext`.
3. **Disable `SMCSuperIO.kext`:**
   Set `Enabled = false` for `SMCSuperIO.kext` in `config.plist`. On the E5-575's IT8987 Super I/O, `SMCSuperIO` publishes dummy fan keys (`F0Ac = 0`, `F0Mx = 3315`) that collide with `SMCAcerFan`.
4. **Install `smctest` CLI Helper:**
   ```sh
   sudo install -m 755 tools/smctest /usr/local/bin/smctest
   ```
5. **Reboot macOS.**

---

## Verification & Diagnostics

- **Check IOReg Diagnostic Log:**
  ```sh
  ioreg -l -w0 | grep "SMCAcerFan-Log"
  ```
  A healthy load reports `probe: EC ok`, `plugin submitted OK`, and periodic `alive tick=... usable=1 manual=0 failsafe=0` heartbeats.
- **Read Live SMC Fan Keys:**
  ```sh
  smctest read
  ```
  Confirm `FNum -> 1`, `F0Mx -> 4500 rpm`, and `F0Ac` tracks live fan RPM.

---

## CLI Usage

`fpe2` values are encoded as big-endian unsigned 16-bit integers equal to `RPM × 4`:

| Target Speed | EC Level | `fpe2` Hex Bytes | Command |
| :--- | :--- | :--- | :--- |
| **0 RPM (Off)** | `0` | `00 00` | `sudo smctest write F0Tg 00 00 && sudo smctest write F0Md 01` |
| **1125 RPM (25%)** | `2` | `11 94` | `sudo smctest write F0Md 01 && sudo smctest write F0Tg 11 94` |
| **2250 RPM (50%)** | `4` | `23 28` | `sudo smctest write F0Md 01 && sudo smctest write F0Tg 23 28` |
| **3375 RPM (75%)** | `6` | `34 BC` | `sudo smctest write F0Md 01 && sudo smctest write F0Tg 34 BC` |
| **4500 RPM (100%)** | `8` | `46 50` | `sudo smctest write F0Md 01 && sudo smctest write F0Tg 46 50` |
| **BIOS Auto** | Auto | `00 00` | `sudo smctest write F0Tg 00 00 && sudo smctest write F0Md 00` |

---

## Known Bugs, Limitations & Quirks

### 1. Coarse 9-Step RPM Granularity
- **Cause:** The Acer E5-575 EC register `0x55` only accepts integer levels `0` through `8` (`562.5 RPM` per step). Any `F0Tg` value written in RPM is rounded to the nearest discrete EC step (`0`, `562`, `1125`, `1687`, `2250`, `2812`, `3375`, `3937`, or `4500 RPM`).

### 2. No Tachometer Feedback in BIOS Auto Mode
- **Cause:** EC register `0x55` reflects the EC's commanded level rather than a raw hall-effect pulse counter, so `F0Ac` reports the quantized step RPM (`level × 4500 / 8`).
