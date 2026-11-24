# Wilo-Smart Connect BT / CoAP-over-BLE Protocol Specification

Extracted and verified from **Wilo-Assistant iOS v6.13.0** (decompiled Dart AOT snapshot + `Allv3.json` / `CFP_iOSApi` / localization tables) and cross-verified live against a **Wilo Stratos PICO** (`Stratos PICO`, SW `01.02.22`) with **Wilo-Smart Connect module BT** (`Art.-Nr. 4239241`).

Machine-readable catalogs generated alongside this document:
- [coap_registers.json](coap_registers.json) — all **206 unique CoAP register IDs** (219 `PumpMessageTypes` enum mappings) with CFP names, RW flags, and app modules.
- [trouble_codes.json](trouble_codes.json) — official German/English Wilo error and warning codes (`< 500` = Error, `>= 500` = Warning).

---

## 1. BLE GATT Profile & Transport Framing

Base UUID (Wilo SE OUI `00:23:F5`): `0000xxxx-0000-1000-8000-0023f5000001`

| Role | UUID | Direction / Properties |
|---|---|---|
| Primary Service | `00000001-0000-1000-8000-0023f5000001` | Wilo BT Data Service |
| TX Data | `00000002-0000-1000-8000-0023f5000001` | Client → Pump (Write Without Response) |
| RX Data | `00000003-0000-1000-8000-0023f5000001` | Pump → Client (Notify) |
| RX Credits | `00000004-0000-1000-8000-0023f5000001` | Pump → Client (Indicate) — pump grants TX credits to client (`0x14` initial, `0x0E` refill) |
| TX Credits | `00000005-0000-1000-8000-0023f5000001` | Client → Pump (Write With Response) — client grants RX credits (`[0x00]`, `[0x18]`; periodic `updateTXCredit()` every **500 ms**) |

### Wilo Transport Envelope
Every message on TX Data (`00000002`) / RX Data (`00000003`) uses a 4-byte header:

```
[channel : u8] [frag : u8] [len_lo : u8] [len_hi : u8] [payload : len bytes]
```

- **`frag`** (verified in `CCommunicationHandler::_txDataCallbackHandler`):
  - `0x03` = complete / unfragmented message
  - `0x02` = first segment (`"Data control first segment"`)
  - `0x01` = middle segment (`"Data control second segment"`)
  - `0x00` = last segment (`"Data control last segment"`)
- **`channel`**:
  - `0x01` = Transport / addressing bootstrap
  - `0x05` = Session PIN authentication
  - `0x1F` = RFC 7252 CoAP over BLE (CBOR payload, Content-Format `60`)

### Session Bootstrap & PIN Authentication (`BluetoothController`)
1. Client sends TX credits (`[0x00]`, `[0x18]`) on `00000005`.
2. Client writes bootstrap frames on `00000002` (`sendPinRequestMessage`):
   - `01 03 01 00 01`
   - `01 03 01 00 03`
   - `01 03 01 00 05` → Pump responds `01 03 02 00 06 <client_addr>` (e.g. `0x41` or `0x42`).
   - `05 03 01 00 01` → Pump responds `05 03 02 00 02 05` (PIN requested; pump displays a 4-digit decimal PIN).
3. Client submits 4-digit decimal PIN as uint16 little-endian (`sendPinResponseMessage` / `Constants.wiloTXDataPinResponse`):
   - `05 03 03 00 03 <pin & 0xFF> <(pin >> 8) & 0xFF>` (e.g. `1234 = 0x04D2` → `05 03 03 00 03 D2 04`)
   - Accepted response: `05 03 01 00 04`.
   - Wrong PIN: pump rejects and generates a new PIN (`pinDialog.wrongPinWarning`); repeated wrong attempts temporarily block the pump (`pinDialog.blockedPinWarningMessage`).

---

## 2. CoAP + CBOR Framing (`CoAPController`)

All pump telemetry and control uses RFC 7252 CoAP inside channel `0x1F`:
- **Option 11 (Uri-Path)**: 4-character uppercase hex register ID (e.g. `"08FD"`, `"F000"`).
- **Option 12 (Content-Format)**: `60` (`application/cbor`).
- **Option 2048**: Source address (`<client_addr>` from `01 03 02 00 06`).
- **Option 2050**: Destination address (`0x00` = pump).
- **Operations**:
  - **Single Read**: `0.01 GET` to `/<HEX4>` (optional CBOR argument in payload, e.g. failure ID for `0x0C8C`).
  - **Multi-Read**: `0.03 PUT` to `/F000` with CBOR array of uint16 register IDs (e.g. `[0x08FD, 0x08FE, ...]`).
  - **Write (`CoAPWriteRequest`)**: `0.03 PUT` to `/<HEX4>` with CBOR-encoded value (`CborEncoder`).

---

## 3. Verified Live Process Values & Exact Scaling Formulas

All numeric CoAP registers use `Helper::convertDecimalToDouble(raw, divisor, offset)`:

```
value = (double)(raw - offset) / (double)divisor
```

**Invalid / Unavailable Sentinel**: `raw == 0xFFFFFFFF` (`4294967295`) → `null` / unavailable.

| CoAP ID | CFP Name | Access | Formula (`(raw - offset) / divisor`) | Unit | Live Example (`Stratos PICO`) |
|---|---|---|---|---|---|
| `0x08FD` | `PV.DeliveryHead` | RO | `raw / 1024.0` | m | `2545` → **2.49 m** |
| `0x08FE` | `PV.VolumeFlow` | RO | `(raw - 2147483648) / 1024.0` | m³/h | `2147484582` → **0.91 m³/h** |
| `0x09D9` | `PV.MotorSpeed` | RO | `(raw - 32768) / 2.0` | RPM | `38084` → **2658 RPM** |
| `0x03A2` | `PV.MediumTemp` | RO | `(raw - 14335) / 256.0` | °C | `20132` → **22.6 °C** |
| `0x09EE` | `PV.ElectricalInputPower` | RO | `raw / 2.0` | W | `37` → **18.5 W** |
| `0x0A23` | *(Resettable Energy Counter)* | RO | `raw / 128.0` | kWh | `7440` → **58.13 kWh** |
| `0x0A0C` | `PV.TotalEnergyConsumption` | RO | `raw / 128.0` | kWh | Total lifetime kWh |
| `0x09DC` | `PV.TotalMotorOperatingTime` | RO | `raw / 3600.0` (raw is seconds) | h | `17106361` → **4751.8 h** |
| `0x0079` | `CF.SetPoint` | RW | `raw / 16384.0` (Q14 normalized) | norm | Scaled by `0x06F7` / `0x0939` |
| `0x06F5` | `CF.SetpointMinLimit` | RO | `raw / 16384.0` | norm | Min setpoint |
| `0x06F6` | `CF.SetpointMaxLimit` | RO | `raw / 16384.0` | norm | Max setpoint |
| `0x0939` | `DevP.PHdMax` | RO | `(raw - 2147483648) / 1024.0` | m | `2147490304` → **6.50 m** |
| `0x093B` | `DevP.VolFlowLimMax` | RO | `(raw - 2147483648) / 1024.0` | m³/h | `2147487744` → **4.00 m³/h** |
| `0x093F` | `DevP.VolFlowLim_0_9` | RO | `(raw - 2147483648) / 1024.0` | m³/h | Flow limit 0.9 |
| `0x017D` | `CF.VolumeFlowNom` | RW | `raw / 1024.0` | m³/h | Nominal flow |
| `0x02BA` | `CF.MaxFlowLimit` | RW | `(raw - 2147483648) / 1024.0` | m³/h | Max flow limit |
| `0x0BC9` / `0xDF06` | *(Factor Setpoint)* | RO / RW | `raw / 4096.0` (Q12) | factor | Assisted-setup factor |

---

## 4. Device Info, Status, Active Influences & Real Fault Decoding

### Crucial Corrections vs. Initial Reverse-Engineering Guesses
- **`0x0191` is NOT a fault!** It is **`PressureIndependentValve`** (*Druckunabhängiges Ventil*, RW boolean: `1` = enabled, `0` = disabled). On Stratos PICO, when `0x0191 == 1`, the app displays `"Druckunabhängiges Ventil"` under **Active Influences** (*Aktive Einflüsse*), never as a fault.
- **`0x025A` is NOT a fault!** It is **`CF.NightSetback`** (*Nachtabsenkungsbetrieb*, RW boolean: `1` = enabled, `0` = disabled). Also displayed under Active Influences when active.
- **`0x0B36` is NOT fault history!** It is **`DevP.UUID`** (16-byte device UUID).

### Real Fault & Warning Mechanism (`0x0C8B` + `0x0C8C`)
1. Poll **`0x0C8B` (`Failure.List`)**: returns a CBOR array of currently active failure IDs.
   - `[]` (empty array) = **zero active errors and zero active warnings**.
2. For each ID in `0x0C8B`, issue a CoAP GET to **`0x0C8C`** with that ID as CBOR request argument (`CoAPReadRequest(0x0C8C, requestArgument: id)`).
3. `0x0C8C` returns a CBOR array containing a CBOR map with integer keys:
   - Key `2` → **`userTroubleCode`** (integer)
   - Key `6` → **`stationId`** (array, first element; on multi-pump / non-Pico)
4. Classification (`PumpMessage.fromUserTroubleCode`):
   - `userTroubleCode < 500` → **Error / Fehler** (e.g. `401..421`, or Stratos PICO `E04..E36`)
   - `userTroubleCode >= 500` → **Warning / Warnung** (e.g. `550..555+`)

### Static Device Identity Registers
| CoAP ID | CFP Name | Type | Description |
|---|---|---|---|
| `0x0B36` | `DevP.UUID` | byte[16] | 16-byte device UUID |
| `0x0B3D` | `DevP.TypeLabelPumpName` | string | Pump model name (e.g. `"Stratos PICO"`) |
| `0x0B3F` | `DevP.TypeLabelPumpType` | string | Pump family (e.g. `"Stratos PICO"`) |
| `0x0B44` | `DevP.SwVersionBundle` | string | Firmware version (e.g. `"01.02.22"`) |
| `0x0F02` | `DevP.Name` | string (RW) | User-configured pump name |
| `0x0F03` | `DevP.Location` | string (RW) | User-configured pump location |

### Active Influences Registers (`0x067F`, `0x0680`, `0x0681`)
- **`0x067F`** (`PumpState.ActiveInfluences.Override`): override states (`faultPresent`, `overrideOff`, `pumpVentingIsRunning`, `dejamming`, `dryRunRecovery`, `overrideMax`, `overrideMin`, `overrideManual`, `pumpOffRequirement`, `setpointAnalogueInput`, `contextMenuPumpOnOff`, `overrideDeviationSpeed`, `thermalDisinfectionActive`, `nightSetback`, `autoControlDay`, `noActiveOverride`, `otherOverride`).
- **`0x0680`** (`PumpState.ActiveInfluences.FlowRelated`): hydraulic/power limitations (`maxPumpCurve`, `maxFlow`, `minFlow`, `userSpeedLimitationMax/Min`, `limitedOperationMotorTemperature`, `limitedOperationVoltageMain`, `limitedOperationCurrentMotorPhase`, `limitedOperationVoltageDCLink`, `limitedOperationSpeedReference`, `limitedOperationPowerMains`).

---

## 5. Control Modes (`0x0001` & `0x0BC4..0x0BC6`)

Extracted directly from `BasicControlMode::number` (`basic_control_modes.dart`):

| `0x0001` Value | BasicControlMode | German Name | Stratos PICO |
|---|---|---|---|
| `0` (`0x00`) | `motorOff` | Motor aus | — |
| `1` (`0x01`) | `constantSpeed` | Konstantdrehzahl (`n-const`) | Yes |
| `2` (`0x02`) | `minimumMotorSpeed` | Minimale Motordrehzahl | — |
| `3` (`0x03`) | `maximumMotorSpeed` | Maximale Motordrehzahl | — |
| `8` (`0x08`) | `differentialPressureC` | Differenzdruck konstant (`Δp-c`) | **Yes (live)** |
| `9` (`0x09`) | `differentialPressureC` | Differenzdruck konstant (`Δp-c` var.) | — |
| `10` (`0x0A`) | `differentialPressureV` | Differenzdruck variabel (`Δp-v`) | Yes |
| `16` (`0x10`) | `differentialPressureC` | Differenzdruck konstant (`Δp-c` var.) | — |
| `17` (`0x11`) | `differentialPressureV` | Differenzdruck variabel (`Δp-v` var.) | — |
| `25` (`0x19`) | `constantFlow` | Konstanter Volumenstrom (`Q-const`) | — |
| `26` (`0x1A`) | `accumulativeFlow` | Multi-Flow Adaptation | — |
| `32` (`0x20`) | `constantTemperature` | Konstante Temperatur (`T-const`) | PICO-Z |
| `33` (`0x21`) | `temperatureDifference` | Temperaturdifferenz (`ΔT-const`) | — |
| `40` (`0x28`) | `dynamicAdaptation` | Dynamic Adapt plus | Yes |
| `48` (`0x30`) | `pidControl` | PID-Regelung | — |
| `255` (`0xFF`) | `notAvailable` | Nicht verfügbar | — |

Related configuration registers:
- **`0x0BC4`** (RW): `ApplicationType` (`0..2`)
- **`0x0BC5`** (RW): `SubApplicationType` (`0..6`)
- **`0x0BC6`** (RW): `InstallationType` (`0..15`, e.g. underfloor heating / radiator / circulation)

---

## 6. Verified Writable CoAP Control Commands (Stratos PICO)

All writes use `CoAPWriteRequest` (`0.03 PUT` to `/<HEX4>` with CBOR payload):

| Feature | CoAP ID | Read Status ID | CBOR Write Payload | Meaning |
|---|---|---|---|---|
| **Pump ON / OFF** | `0x0669` | `0x0669` | uint `1` / `0` | `1` = Pumpe EIN, `0` = Pumpe AUS |
| **Setpoint (normalized)** | `0x0079` | `0x0079` | uint `round((target / scale) * 16384)` | Delivery head / speed / temp setpoint |
| **Setpoint Factor** | `0xDF06` | `0x0BC9` | uint `round(factor * 4096)` | Assisted-setup factor (Q12) |
| **Control Mode** | `0x0001` | `0x0001` | uint (`1`, `8`, `10`, `40`, …) | See Section 5 (`setPicoCFID` also writes `0x0BC6`) |
| **Air Venting (Entlüftung)** | `0x0605` | `0x060F` (status: `4`=running, `3`=idle), `0x060A` (remaining s) | uint `1` / `0` | `1` = Start venting, `0` = Stop venting |
| **Manual Restart** | `0x05C9` | `0x05D3` (status) | uint `1` / `0` | `1` = Start manual restart, `0` = Stop |
| **Reset Energy Counter** | `0x0A22` | `0x0A23` (`/ 128` kWh) | uint `0` | Resets resettable kWh counter to `0` |
| **Night Setback** | `0x025A` | `0x025A` | uint `1` / `0` | `1` = Nachtabsenkung EIN, `0` = AUS |
| **Pressure Independent Valve** | `0x0191` | `0x0191` | uint `1` / `0` | `1` = Druckunabhängiges Ventil EIN, `0` = AUS |
| **Override / Basic Function** | `0x066A` | `0x066A` | uint `0..4` | `0`=Auto, `1`=Motor AUS, `2`=Min, `3`=Max, `4`=Manuell |
| **Pump Name / Location** | `0x0F02` / `0x0F03` | `0x0F02` / `0x0F03` | UTF-8 text string | Custom name / location |
| **HMI Settings** | `0x0C13` / `0x0C15` / `0x0C16` / `0x0C18` | same | uint | Brightness, Country, Language, UnitSet |
