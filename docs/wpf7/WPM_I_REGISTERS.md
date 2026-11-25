# Stiebel Eltron WPF 7 / WPM i — Verified Elster CAN Register Map

This document summarizes the CAN bus registers verified on a **Stiebel Eltron WPF 7** ground-source heat pump with **WPM i** controller (`GERAETE_ID = 190-08`) against Elster CAN bus scans and Stiebel Eltron ComfortSoft WPM i definitions.

---

## 1. CAN Bus Addressing (20 kbps)

| Member | CAN ID | Role on WPM i |
|---|---|---|
| `KESSEL` | `0x180` | Main WPM i controller (temperatures, setpoints, faults, operating mode, summer flag) |
| `HK1` / `HEIZMODUL` | `0x301` | Heating circuit 1 (`VORLAUFSOLLTEMP` `0x0004`, `HEIZKURVE` fallback) |
| `MANAGER` | `0x480` | Heat pump manager (`SAMMEL_RELAISSTATUS`, `MASCHINENDRUCK`, `LAUFZEIT_*`, `STILLSTANDZEIT_0`, `HEIZKURVE`) |
| `BEDIENMODUL` | `0x601` | Display / FEK channel (`BRENNER` status, `HK1` display values) |
| `CAN_BRIDGE` | `0x680` | This ESP32-S3 node |

---

## 2. Verified Key Registers

| Index | Member | Name | Elster Type | Meaning & Scaling |
|---|---|---|---|---|
| `0x0001` | `0x180` | `FEHLERMELDUNG` | `et_default` | Active WPM i error code (`0` = no fault; see Section 5). Unblacklisted in `ElsterTable.h`. |
| `0x0004` | `0x301` | `VORLAUFSOLLTEMP` | `et_dec_val` | Calculated flow setpoint (`raw / 10.0` °C). |
| `0x0005`..`0x0008` | `0x180` | `RAUMSOLLTEMP_I..NACHT` | `et_dec_val` | Room temperature setpoints (`0.1` °C, RW). |
| `0x000c` | `0x180` | `AUSSENTEMP` | `et_dec_val` | Outside temperature (`°C`). |
| `0x0016` | `0x180` | `RUECKLAUFISTTEMP` | `et_dec_val` | Return temperature (`°C`). |
| `0x0022` | `0x180` | `HYSTERESEZEIT` | `et_default` | Mirrors `REGLERDYNAMIK` raw integer (`0x001e` = `30`; note `ElsterTable.h` decodes as `et_dec_val` `3.0`). |
| `0x010e` | `0x480` | `HEIZKURVE` | `et_cent_val` | Heating curve slope (`raw / 100.0`, e.g. `0.23`). |
| `0x01ac` | `0x180` | `BIVALENTPARALLELTEMPERATUR_HZG` | `et_dec_val` | Display `BIVALENZ-HZG` (`°C`, e.g. `-19.0 °C` — outdoor temperature threshold enabling backup heater). |
| `0x01ae` | `0x180` | `BIVALENZALTERNATIVTEMPERATUR_HZG` | `et_dec_val` | Display `GRENZE-HZG` (`0xa000` = `AUS` / disabled). |
| `0x01c4` | `0x480` | `LAUFZEIT_WP1` | `et_default` | Compressor total runtime (`h`). |
| `0x01cc` | `0x480` | `STILLSTANDZEIT_0` | `et_default` | Remaining compressor anti-short-cycle lockout (`min`, `0` = ready). |
| `0x01d6` | `0x180` | `WPVORLAUFIST` | `et_dec_val` | Heat pump flow temperature (`°C`). |
| `0x01d4` | `0x180` | `QUELLE_IST` | `et_dec_val` | Brine / source inlet temperature (`°C`). |
| `0x01e8` | `0x180` | `MAX_HEIZUNG_TEMP` | `et_dec_val` | Display `VORLAUFMAX-HZG` (`°C`, e.g. `60.0 °C`). |
| `0x0259` | `0x480` | `LAUFZEIT_DHC1` | `et_default` | Electric backup heater stage 1 runtime (`h`). |
| `0x025a` | `0x480` | `LAUFZEIT_DHC2` | `et_default` | Electric backup heater stage 2 runtime (`h`). |
| `0x0265` | `0x180` | `HEISSGASTEMP` | `et_dec_val` | Hot-gas discharge temperature (`°C`). |
| `0x0268` | `0x480` | `MASCHINENDRUCK` | `et_dec_val` | R410A high-pressure gauge transducer (`raw / 10.0` bar(g)). |
| `0x0a20` | `0x480` | `SAMMEL_RELAISSTATUS` | `et_default` | 16-bit relay bitmask (see Section 3). Unblacklisted in `ElsterTable.h`. |
| `0xfdab` | `0x480` | `ZWEITER_WE_STATUS` | `et_default` | Actually `cWP_EVU` (utility block input status on WPM i). |
| `0xfdaf` | `0x601` | `DAUERLAUF_PUFFERLADEPUMPE` | `et_little_endian` | Display `DAUERLAUF-PUMP` (`0` = `AUS`, `1` = `EIN`). |
| `0xfdb0` | `0x601` | `SCHALTWERKDYNAMIKZEIT` | `et_little_endian` | Display `REGLERDYNAMIK` (Kelvin-minute integral threshold, e.g. `30` — see Section 7). |
| `0xfdb1` | `0x601` | `STILLSTANDZEIT` | `et_little_endian` | Display `STILLSTANDZEIT` (`min`, e.g. `20`). |
| `0xfdb2` | `0x601` | `PUMPENZYKLEN` | `et_little_endian` | Display `PUMPENZYKLEN` (`0` = `AUS`, `1` = `EIN`). |
| `0xfdb3` | `0x601` | `GEBAEUDEART` | `et_little_endian` | Building thermal mass class (`1`..`3`, e.g. `2`). |
| `0xfdb4` | `0x180` | `SOMMERBETRIEB` | `et_little_bool` | Automatic summer shutoff configuration flag (`0x0100` = enabled, `0x0000` = disabled; **NOT** "currently in summer standby"). |

---

## 3. `0x0a20` (`SAMMEL_RELAISSTATUS`) Bitmask

Verified directly against WPM i status logic and live compressor runs:

| Bit | Mask | Function | Notes |
|---|---|---|---|
| 0 | `0x0001` | Electric Backup Heater Stage 1 (`DHC1`) | |
| 1 | `0x0002` | Electric Backup Heater Stage 2 (`DHC2`) | |
| 2 & 3 | `0x000C` | **Compressor (`Verdichter`)** | Active when **both** bits 2 and 3 are set (`(mask & 0x000C) == 0x000C`). |
| 4 | `0x0010` | DHW 3-Way Valve (`WW-Ventil`) | |
| 5 | `0x0020` | Heating Circuit Pump (`Heizkreispumpe`) | |
| 6 | `0x0040` | Buffer Charging Pump (`Pufferladepumpe`) | |
| 8 | `0x0100` | **Brine / Source Pump (`Quellenpumpe`)** | Energizes ~30 s **before** the compressor starts — ideal pre-start trigger for circulation pump boost! |
| 9 | `0x0200` | Cooling (`Kühlen`) | |

Typical live bitmask observed during a heating run on WPF 7: `0x016C` (`Bits 2, 3, 5, 6, 8` = Compressor + Heating Circuit Pump + Buffer Pump + Brine Pump).

---

## 4. `0x0268` (`MASCHINENDRUCK`) & R410A Saturation Curve

`0x0268` reports the R410A high-side gauge pressure in `0.1 bar(g)` steps (e.g., `11.2 bar` in standby, `20.0 bar` during a `33 °C` flow heating run).

The firmware converts gauge pressure `p_g` (`bar(g)`) to absolute pressure `p_abs = p_g + 1.01325 bar` and computes the **R410A condensing saturation temperature** via a 3rd-order polynomial in `lp = ln(p_abs)` fitted against NIST Chemistry WebBook R410A data over `0..65 °C` (`0.5..48 bar(g)`, max error `0.015 °C`):

```cpp
double lp = std::log((double)pressureBarGauge + 1.01325);
double t_kond = -48.038088 + lp * (15.509594 + lp * (3.389605 + lp * 0.128850));
```

From `T_kond`, two live refrigeration-health metrics are derived during compressor runs:
- **Condenser Approach (*Verflüssiger-Grädigkeit*):** `T_kond − T_VL` (typical healthy range: `1..5 K`).
- **Hot-Gas Superheat (*Heißgas-Überhitzung*):** `T_HG − T_kond`.

---

## 5. `0x0001` (`FEHLERMELDUNG`) WPM i Error Codes

| Code | Meaning (DE) |
|---|---|
| `0` | Kein Fehler |
| `1` | Fühlerfehler / Kurzschluss |
| `2` | Hochdruckstörung (HD) |
| `3` | Niederdruckstörung (ND) |
| `4` | Motorschutz Verdichter |
| `5` | Heißgastemperatur zu hoch |
| `6` | Frostschutz / Quellen-Untertemperatur |
| `7` | Volumenstrom / Durchflusswächter |
| `8` | Kommunikationsfehler CAN |
| `9` | Phasenfolge / Drehfeld |
| `10` | Fühler Außen (AT) |
| `11` | Fühler Vorlauf (VL) |
| `12` | Fühler Rücklauf (RL) |
| `13` | Fühler Quelle (Sole) |
| `14` | Fühler Warmwasser (WW) |
| `15` | Fühler Heißgas (HG) |
| `20` | HD-Wächter (E20) |
| `21` | ND-Wächter (E21) |
| `22` | Motorschutz / Verdichter (E22) |
| `23` | Quellen-Frostschutz (E23) |
| `24` | Verdichter-Anlaufstörung (E24) |
| `25` | Abtau- / Sicherheitsfehler (E25) |
| `26` | Fühlerbruch / Kurzschluss (E26) |
| `27` | Volumenstrom-Störung (E27) |
| `30` | Rücklauftemperatur zu hoch (E30) |
| `31` | Heißgastemperatur zu hoch (E31) |
| `130` | Kommunikationsfehler IWS/WPM (E130) |

---

## 6. Note on Room Sensor Simulation (`RAUMISTTEMP`)

On the **WPM i**, writing `RAUMISTTEMP` directly on the CAN bus (`0x601` or `0x301`) is **ignored** unless a real physical FEK remote control is registered with the WPM i. Instead, [homeassistant/packages/wpf7_raumfuehrung.yaml](../../homeassistant/packages/wpf7_raumfuehrung.yaml) implements a clean closed-loop **PI controller** that adjusts `RAUMSOLLTEMP_I` and `RAUMSOLLTEMP_NACHT` (`0x0005` / `0x0008`, rounded to `0.5 K` to protect EEPROM). Testing confirmed that a `-1.0 K` room setpoint shift moves the WPM i buffer setpoint by `-1.2 K` within `~25 s`.

---

## 7. Note on WPM i (`Software 9008`) Integral Switching (`REGLERDYNAMIK`)

Unlike newer WPM 3 / 3i controllers, the classic **WPM i** (`SOFTWARE-WPMI 9008`) does **not** expose *Anteil Vorlauf* or *Hysterese 1..5* menus. Instead, compressor cut-off is governed by the Kelvin-minute integral register **`0xfdb0` (`SCHALTWERKDYNAMIKZEIT`, display `REGLERDYNAMIK`)** together with `0xfdb1` (`STILLSTANDZEIT`).

- Higher `REGLERDYNAMIK` values (e.g. `30` for underfloor heating) integrate more Kelvin-minutes above the return setpoint before stopping the compressor, extending runtimes at the cost of higher peak flow temperatures at the end of a cycle.
- On parallel-buffer installations where an internal buffer-charging pump (`0x0040`) feeds the primary loop and an external circulation pump (e.g. Wilo Stratos PICO) drives the secondary heating circuit, matching the secondary flow `Q_sek` closely to the primary flow `Q_prim` (avoiding both under-flow short-circuiting and over-flow return mixing) is the cleanest lever to lengthen cycles and lower peak flow temperatures without changing WPM i factory dynamics.
