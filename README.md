# ESPHome Integration: Stiebel Eltron WPF 7 (WPM i CAN) + Wilo Stratos PICO (BT Smart Connect)

Complete open-source **ESPHome + Home Assistant** integration running on a single **ESP32-S3** board that bridges both:

1. **Stiebel Eltron WPF 7** ground-source heat pump (**WPM i** controller) via the **Elster CAN bus** (`ESP32-S3 TWAI`, 20 kbps)
2. **Wilo Stratos PICO** heating circulation pump equipped with the **Wilo-Smart Connect module BT** (Art.-Nr. `4239241`) via **BLE CoAP/CBOR** (custom ESPHome component `wilo_bt`)

Because both buses run on the same ESP32-S3, the firmware couples live **Wilo volume flow (`m³/h`)** directly into the **WPF 7 thermal power, live COP, and low-flow protection** calculations — no external flow meter required.

---

## Key Features

### 1. Wilo-Smart Connect Module BT (`components/wilo_bt`)
- Custom ESPHome `BLEClientNode` component (`esp-idf`, PSRAM offload, gentle Wi-Fi/BLE coexistence that stops BLE scanning while connected).
- Implements the full **Wilo BLE credit-based transport**, session bootstrap, 4-digit display PIN authentication, and **RFC 7252 CoAP + CBOR** protocol documented in [docs/wilo/PROTOCOL_SPEC.md](docs/wilo/PROTOCOL_SPEC.md).
- **Live Telemetry:** Flow rate (`m³/h`), delivery head (`m`), medium temperature (`°C`, `0x03A2`), electrical power (`W`), resettable energy counter (`kWh`, `0x0A23`), motor speed (`RPM`), operating hours (`h`), setpoint (`m` & `%`), and control mode (`Δp-c`, `Δp-v`, `n-const`, `Dynamic Adapt`).
- **Real Fault & Warning Diagnostics:** Reads `0x0C8B` (`Failure.List`) + indexed `0x0C8C` detail lookups (`< 500` = Fault, `>= 500` = Warning) with German trouble-code descriptions ([docs/wilo/trouble_codes.json](docs/wilo/trouble_codes.json)).
- **Verified Pump Controls (`CoAP 0.03 PUT`):** Pump Run ON/OFF (`0x0669`), delivery head & percent setpoint (`0x0079`, clamped to pump limits `0x06F5`..`0x06F6`), control mode (`0x0001`), night setback (`0x025A`), pressure-independent valve (`0x0191`), air venting (`0x0605`), manual restart (`0x05C9`), and energy counter reset (`0x0A22`).
- Can also be used **standalone** without a heat pump — see [esphome/examples/wilo_stratos_pico_standalone.yaml](esphome/examples/wilo_stratos_pico_standalone.yaml).

### 2. Stiebel Eltron WPF 7 / WPM i CAN Bridge (`esphome/`)
- Based on `ha-stiebel-control` (LGPL-3.0) and upgraded to **ESP-IDF** with ComfortSoft-verified WPM i register decoding ([docs/wpf7/WPM_I_REGISTERS.md](docs/wpf7/WPM_I_REGISTERS.md)):
  - **`0x0a20` (`SAMMEL_RELAISSTATUS`) Relay Bitmask:** Native binary sensors for Compressor (`0x000C`, Bits 2 & 3), Brine Pump (`0x0100`, Bit 8 — starts ~30 s *before* the compressor), Buffer Pump (`0x0040`), Heating Circuit Pump (`0x0020`), Backup Heater DHC1 (`0x0001`), and DHC2 (`0x0002`), plus human-readable status text.
  - **`0x0268` (`MASCHINENDRUCK`) R410A Refrigeration Diagnostics:** High-pressure gauge sensor (`bar`), NIST-fitted R410A condensing temperature (`°C`, max error `0.015 °C`), condenser approach (`T_kond − T_VL`), and hot-gas superheat (`T_HG − T_kond`).
  - **Runtime & Heating Parameters:** Compressor runtime (`0x01c4`), DHC1/DHC2 runtimes (`0x0259`/`0x025a`), remaining anti-short-cycle standstill timer (`0x01cc`), heating curve slope (`0x010e`), flow setpoint (`0x0004`), summer mode flag (`0xfdb4`, `et_little_bool`), and WPM i error decoder (`0x0001`).
  - **Control & Diagnostics:** Native API operating mode select, room setpoints, SG Ready setpoint boost, daily WPM i clock sync, read-only CAN scanner, and passive CAN bus monitor.

### 3. Live WPF 7 ↔ Wilo Coupling & Home Assistant Automation (`homeassistant/`)
- **Live Thermal Power & COP:** Uses live Wilo flow (`Q = Q_wilo × 1.163 × ΔT`) whenever BLE is connected, falling back gracefully to a configurable nominal flow (`0.90 m³/h`) if BLE is offline. Publishes a continuous `0 kW` idle sensor (`thermal_power_live`) for exact Riemann thermal energy integration.
- **Home Assistant Packages ([homeassistant/packages/](homeassistant/packages/)):**
  - `heizung.yaml`: Utility meters for electricity, thermal energy (Riemann left integration), Wilo auxiliary energy, Daily/Monthly/Yearly Seasonal Performance Factor (**AZ / JAZ**), compressor runtime & start counters.
  - `wpf7_wilo_steuerung.yaml`:
    - **Minimum-Flow Protection** (default ON): raises Wilo delivery head by `+0.5 m` (up to `3.5 m`) and notifies if flow drops below `0.75 m³/h` for `>= 15 s` during heat pump demand (WPF 7 datasheet minimum: `0.70 m³/h`).
    - **Standby Head Setback** (opt-in): boosts Wilo to operating head **30 s before compressor start** using WPM i's Brine Pump relay (`Bit 8`) and lowers head only after configurable overrun time in standby.
    - **Fault & Backup Heater Alerts:** Persistent notifications on active WPM i faults (`0x0001`) or DHC1/DHC2 backup heater activation.
  - `wpf7_raumfuehrung.yaml`: Slow PI room-temperature controller that shifts `RAUMSOLLTEMP_I` / `_NACHT` without needing physical FEK/FE7 hardware.
- **3-Tab Home Assistant Sections Dashboard ([homeassistant/dashboard/](homeassistant/dashboard/)):**
  - *Übersicht* (live status, room PI control, temperatures, COP/AZ/JAZ, Wilo summary, 24h & 7d ApexCharts)
  - *Wilo Pumpe* (live telemetry, direct BT controls, protection & standby controls, 24h pump chart, BLE diagnostics)
  - *Diagnose & Wartung* (R410A refrigeration circuit, WPM i relays & runtimes, CAN/ESP32 diagnostics)

---

## Hardware

| Component | Tested Model | Notes |
|---|---|---|
| MCU / CAN Board | Waveshare ESP32-S3-RS485-CAN (`esp32-s3-devkitc-1`, 16 MB Flash, Octal PSRAM) | CAN TX `GPIO15`, RX `GPIO16`, 20 kbps |
| Heat Pump | Stiebel Eltron WPF 7 (WPM i, Device ID `190-08`) | Connected via Elster CAN bus (`CAN H` / `CAN L` / `GND`) |
| Circulation Pump | Wilo Stratos PICO + Wilo-Smart Connect module BT (`4239241`) | BLE GATT (`0000xxxx-0000-1000-8000-0023f5000001`) |
| Power Meter (optional) | Shelly Pro 3EM | Imported from Home Assistant for COP / JAZ calculation |

---

## Repository Structure

```text
.
├── components/wilo_bt/                  # Standalone ESPHome external component (C++ / Python)
├── esphome/
│   ├── heatingpump.yaml                 # Combined WPF 7 CAN + Wilo BT ESPHome config
│   ├── secrets.yaml.example             # Template for local credentials, MAC & optional PIN
│   ├── heatpump-include/                # Modular YAML packages & C++ Elster/CAN headers
│   └── examples/
│       └── wilo_stratos_pico_standalone.yaml
├── homeassistant/
│   ├── packages/                        # Energy/JAZ, PI room control, Wilo protection & alarms
│   └── dashboard/                       # 3-tab Sections dashboard JSON + generator script
├── docs/
│   ├── wilo/                            # Full Wilo BT protocol spec, 206 registers & trouble codes
│   └── wpf7/                            # Verified WPM i CAN register & relay documentation
├── tests/                               # Host-side C++17 & Python unit tests
└── tools/
    └── analyze_wilo_pklg.py             # Offline Apple PacketLogger (.pklg) Wilo BLE decoder
```

---

## Quick Start

### 1. Configure Secrets & Build ESPHome Firmware

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml
# Edit esphome/secrets.yaml with your Wi-Fi, API key, MQTT (optional), and Wilo BLE MAC address
```

Review board pins (`tx_pin`, `rx_pin`), timezone, and your Shelly power meter entity ID in [esphome/heatingpump.yaml](esphome/heatingpump.yaml). Then compile and flash:

```bash
cd esphome
esphome run heatingpump.yaml
```

> **Wilo Session PIN Authentication:** When the ESP32 connects to the Wilo-Smart Connect module BT, the pump displays a 4-digit PIN. Enter it in the **Wilo Session PIN** entity in Home Assistant and press **Wilo Authentifizieren** (or configure a fixed `pin: !secret wilo_pin` if your pump retains a pairing PIN).

### 2. Install Home Assistant Packages & Dashboard

1. Copy the files from [homeassistant/packages/](homeassistant/packages/) into your Home Assistant `/config/packages/` directory and enable packages in `configuration.yaml`:
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
2. Adjust entity IDs in the packages to match your Home Assistant device prefix (e.g. Shelly power meter and living-room temperature sensor).
3. Import [homeassistant/dashboard/dashboard_heatingpump.json](homeassistant/dashboard/dashboard_heatingpump.json) (requires HACS cards `mushroom` and `apexcharts-card`), or regenerate/validate it via:
   ```bash
   python3 homeassistant/dashboard/build_dashboard.py
   # Optional live validation against your Home Assistant instance:
   HASS_URL="https://ha.example.local" HASS_TOKEN="..." python3 homeassistant/dashboard/build_dashboard.py --validate
   ```

---

## Running Unit Tests

All protocol encoders/decoders and WPF 7 thermodynamic/relay helpers can be tested locally on macOS/Linux without ESP32 hardware:

```bash
# Wilo BT CoAP/CBOR/framing unit tests
c++ -std=c++17 -Wall -Wextra -Werror -Icomponents/wilo_bt \
  components/wilo_bt/wilo_protocol.cpp tests/test_wilo_protocol.cpp \
  -o /tmp/test_wilo_protocol && /tmp/test_wilo_protocol

# WPF 7 R410A saturation, 0x0a20 relay bitmask & COP unit tests
c++ -std=c++17 -Wall -Wextra -Werror tests/test_wpf7_logic.cpp \
  -o /tmp/test_wpf7_logic && /tmp/test_wpf7_logic

# Offline .pklg analyzer tests
python3 -m unittest discover -s tests -v
```

---

## Credits & License

- Elster CAN protocol table (`ElsterTable.h`, `KElsterTable.*`) originally by **Jürg Müller** (LGPL-3.0).
- Base ESPHome CAN integration (`ha-stiebel-control`) originally by **Bastian Stahmer** (LGPL-3.0).
- Wilo-Smart Connect BT ESPHome component (`wilo_bt`), protocol documentation, WPF 7 / WPM i extensions, and Home Assistant packages/dashboard by **Robert Ressl**.

Licensed under the **GNU Lesser General Public License v3.0 (LGPL-3.0)** — see [LICENSE](LICENSE).
