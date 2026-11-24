#!/usr/bin/env python3
"""Offline decoder for Wilo-Smart Connect module BT Apple PacketLogger (.pklg) dumps.

Parses HCI/L2CAP/ATT packets, Wilo credit flow control, transport bootstrap,
PIN authentication, and RFC 7252 CoAP + CBOR telemetry/write messages.

Highlights write/control commands (CoAP POST/PUT/DELETE on non-0xF000 keys)
so captures of changing settings in Wilo-Assistant can be decoded immediately.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

WILO_BATCH_KEY = 0xF000

KNOWN_KEYS: dict[int, str] = {
    0x0001: "control_mode",
    0x0079: "setpoint_norm_q14",
    0x0191: "status_flag_0191",
    0x025A: "status_flag_025a",
    0x03A2: "live_unknown_03a2",
    0x0669: "run_config_0669",
    0x06F5: "setpoint_min_map",
    0x06F6: "setpoint_max_map",
    0x06F7: "setpoint_scale_map",
    0x08FD: "actual_head_m",
    0x08FE: "actual_flow_m3h",
    0x0939: "max_head_m",
    0x093B: "max_flow_m3h",
    0x09D9: "motor_speed_rpm",
    0x09DC: "operating_seconds",
    0x09EE: "electrical_power_w",
    0x0A23: "electrical_energy_kwh",
    0x0B36: "fault_history",
    0x0B3D: "pump_type",
    0x0B3F: "location_note",
    0x0B42: "serial_number",
    0x0B44: "software_version",
    0x0BC4: "status_0bc4",
    0x0BC5: "status_0bc5",
    0x0BC6: "pump_status_code",
    0x0C8B: "active_alarms",
    0x0F02: "device_name",
    0xDF02: "module_info_df02",
    0xDF03: "module_info_df03",
    0xDF05: "module_info_df05",
    0xDF06: "module_info_df06",
}


def decode_cbor(buf: bytes, offset: int = 0) -> tuple[Any, int]:
    if offset >= len(buf):
        raise ValueError("unexpected end of CBOR input")
    initial = buf[offset]
    offset += 1
    major = initial >> 5
    ai = initial & 0x1F

    def read_u(additional: int, off: int) -> tuple[int, int]:
        if additional <= 23:
            return additional, off
        if additional == 24:
            return buf[off], off + 1
        if additional == 25:
            return struct.unpack_from(">H", buf, off)[0], off + 2
        if additional == 26:
            return struct.unpack_from(">I", buf, off)[0], off + 4
        if additional == 27:
            return struct.unpack_from(">Q", buf, off)[0], off + 8
        raise ValueError(f"unsupported CBOR additional info {additional}")

    if major == 0:
        return read_u(ai, offset)
    if major == 1:
        val, offset = read_u(ai, offset)
        return -1 - val, offset
    if major == 2:
        n, offset = read_u(ai, offset)
        return buf[offset : offset + n], offset + n
    if major == 3:
        n, offset = read_u(ai, offset)
        return buf[offset : offset + n].decode("utf-8", "replace"), offset + n
    if major == 4:
        n, offset = read_u(ai, offset)
        arr = []
        for _ in range(n):
            item, offset = decode_cbor(buf, offset)
            arr.append(item)
        return arr, offset
    if major == 5:
        n, offset = read_u(ai, offset)
        mapping = {}
        for _ in range(n):
            k, offset = decode_cbor(buf, offset)
            v, offset = decode_cbor(buf, offset)
            mapping[k] = v
        return mapping, offset
    if major == 6:
        _tag, offset = read_u(ai, offset)
        return decode_cbor(buf, offset)
    if major == 7:
        if ai == 20:
            return False, offset
        if ai == 21:
            return True, offset
        if ai in (22, 23):
            return None, offset
        if ai == 25:
            return struct.unpack_from(">e", buf, offset)[0], offset + 2
        if ai == 26:
            return struct.unpack_from(">f", buf, offset)[0], offset + 4
        if ai == 27:
            return struct.unpack_from(">d", buf, offset)[0], offset + 8
    raise ValueError(f"unsupported CBOR type major={major} ai={ai}")


def parse_coap(data: bytes) -> dict[str, Any]:
    if len(data) < 4:
        raise ValueError("CoAP packet shorter than 4 bytes")
    ver = (data[0] >> 6) & 3
    ctype = (data[0] >> 4) & 3
    tkl = data[0] & 0x0F
    code = data[1]
    mid = struct.unpack_from(">H", data, 2)[0]
    token = data[4 : 4 + tkl]
    off = 4 + tkl
    opt_num = 0
    options: list[tuple[int, bytes]] = []
    payload = b""
    while off < len(data):
        if data[off] == 0xFF:
            payload = data[off + 1 :]
            break
        delta = data[off] >> 4
        olen = data[off] & 0x0F
        off += 1
        if delta == 13:
            delta = 13 + data[off]
            off += 1
        elif delta == 14:
            delta = 269 + struct.unpack_from(">H", data, off)[0]
            off += 2
        if olen == 13:
            olen = 13 + data[off]
            off += 1
        elif olen == 14:
            olen = 269 + struct.unpack_from(">H", data, off)[0]
            off += 2
        opt_num += delta
        options.append((opt_num, data[off : off + olen]))
        off += olen
    uri = "/".join(v.decode("ascii", "replace") for n, v in options if n == 11)
    return {
        "version": ver,
        "type": ctype,
        "code": code,
        "code_str": f"{code >> 5}.{code & 31:02d}",
        "mid": mid,
        "token": token,
        "uri": uri,
        "options": options,
        "payload": payload,
    }


def decode_physical(key: int, val: Any, context: dict[int, Any]) -> str | None:
    if not isinstance(val, int):
        return None
    if key == 0x08FD:
        return f"{val / 1024.0:.3f} m"
    if key in (0x08FE, 0x0939, 0x093B):
        signed = struct.unpack("<i", struct.pack("<I", (val - 0x80000000) & 0xFFFFFFFF))[0]
        unit = "m" if key == 0x0939 else "m³/h"
        return f"{signed / 1024.0:.3f} {unit}"
    if key == 0x09D9:
        rpm = ((val & 0xFFFF) - 0x8000) / 2.0
        return f"{rpm:.1f} RPM"
    if key == 0x09DC:
        return f"{val / 3600.0:.2f} h ({val} s)"
    if key == 0x09EE:
        return f"{val / 2.0:.1f} W"
    if key == 0x0A23:
        return f"{val / 128.0:.3f} kWh"
    if key == 0x0B44:
        return f"v{val // 10000:02d}.{(val // 100) % 100:02d}.{val % 100:02d}"
    if key == 0x0079:
        pct = (val / 16384.0) * 100.0
        mode = context.get(0x0001)
        scale_map = context.get(0x06F7)
        if isinstance(mode, int) and isinstance(scale_map, dict) and mode in scale_map:
            phys = (val / 16384.0) * float(scale_map[mode])
            return f"{phys:.2f} ({pct:.1f}%)"
        return f"{pct:.1f}%"
    return None


@dataclass
class DumpAnalysis:
    device_macs: list[str] = field(default_factory=list)
    client_addr: int | None = None
    max_frame_size: int | None = None
    auth_pin: int | None = None
    auth_ok: bool = False
    tx_credits_granted: list[int] = field(default_factory=list)
    rx_credit_writes: int = 0
    latest_values: dict[int, Any] = field(default_factory=dict)
    write_commands: list[dict[str, Any]] = field(default_factory=list)
    coap_errors: list[dict[str, Any]] = field(default_factory=list)
    coap_transactions: int = 0


def analyze_pklg(path: Path, verbose: bool = False) -> DumpAnalysis:
    raw = path.read_bytes()
    pos = 0
    records = []
    while pos + 13 <= len(raw):
        rec_len = struct.unpack_from("<I", raw, pos)[0]
        ts_sec, ts_usec = struct.unpack_from("<II", raw, pos + 4)
        ptype = raw[pos + 12]
        payload = raw[pos + 13 : pos + 4 + rec_len]
        records.append((len(records), ts_sec + ts_usec / 1e6, ptype, payload))
        pos += 4 + rec_len

    result = DumpAnalysis()
    t0: float | None = None
    target_handles: set[int] = set()

    for _idx, ts, ptype, pl in records:
        if ptype == 0x01 and len(pl) >= 14 and pl[0] == 0x3E and pl[2] in (0x01, 0x0A):
            status = pl[3]
            if status == 0:
                handle = struct.unpack_from("<H", pl, 4)[0]
                addr = ":".join(f"{b:02X}" for b in reversed(pl[8:14]))
                result.device_macs.append(addr)
                target_handles.add(handle)
                if t0 is None:
                    t0 = ts

    if t0 is None and records:
        t0 = records[0][1]

    # L2CAP reassembly per (handle, direction)
    l2_buf: dict[tuple[int, int], bytearray] = {}
    l2_exp: dict[tuple[int, int], int] = {}
    l2_cid: dict[tuple[int, int], int] = {}

    pending_coap: dict[int, dict[str, Any]] = {}
    wilo_rx_reasm = bytearray()

    for idx, ts, ptype, pl in records:
        if ptype not in (0x02, 0x03) or len(pl) < 4:
            continue
        hflags, dlen = struct.unpack_from("<HH", pl, 0)
        conn_handle = hflags & 0x0FFF
        pb = (hflags >> 12) & 0x03
        if target_handles and conn_handle not in target_handles:
            continue
        frag = pl[4 : 4 + dlen]
        key = (conn_handle, ptype)
        if pb in (0, 2):
            if len(frag) < 4:
                continue
            l2len, cid = struct.unpack_from("<HH", frag, 0)
            l2_buf[key] = bytearray(frag[4:])
            l2_exp[key] = l2len
            l2_cid[key] = cid
        elif pb == 1 and key in l2_buf:
            l2_buf[key].extend(frag)
        else:
            continue

        if len(l2_buf[key]) < l2_exp[key] or l2_exp[key] == 0:
            continue

        msg = bytes(l2_buf[key][: l2_exp[key]])
        cid = l2_cid[key]
        l2_buf[key].clear()
        l2_exp[key] = 0
        if cid != 0x0004 or not msg:
            continue

        op = msg[0]
        dt = ts - (t0 or ts)

        # RX credit grants (client -> pump Write Request of 0x18)
        if ptype == 0x02 and op == 0x12 and len(msg) == 4 and msg[3] == 0x18:
            result.rx_credit_writes += 1
            continue

        # TX credit grants (pump -> client Indication of 1 byte)
        if ptype == 0x03 and op == 0x1D and len(msg) == 4:
            result.tx_credits_granted.append(msg[3])
            if verbose:
                print(f"[{idx:4d}] {dt:7.3f}s RX IND   tx_credits += {msg[3]} (0x{msg[3]:02X})")
            continue

        if op not in (0x52, 0x1B) or len(msg) < 4:
            continue

        val = msg[3:]
        direction = "TX" if ptype == 0x02 else "RX"

        # Handle Wilo envelope reassembly on RX
        if direction == "RX":
            if wilo_rx_reasm:
                wilo_rx_reasm.extend(val)
                decl = struct.unpack_from("<H", wilo_rx_reasm, 2)[0]
                if len(wilo_rx_reasm) < 4 + decl:
                    continue
                val = bytes(wilo_rx_reasm[: 4 + decl])
                wilo_rx_reasm.clear()
            elif len(val) >= 4:
                decl = struct.unpack_from("<H", val, 2)[0]
                if len(val) < 4 + decl:
                    wilo_rx_reasm.extend(val)
                    continue

        if len(val) < 4 or val[1] != 0x03:
            continue

        ch = val[0]
        decl_len = struct.unpack_from("<H", val, 2)[0]
        body = val[4 : 4 + decl_len]

        if ch == 0x01 and direction == "RX" and body:
            if body[0] == 0x02 and len(body) >= 3:
                result.max_frame_size = struct.unpack_from("<H", body, 1)[0]
            elif body[0] == 0x06 and len(body) >= 2:
                result.client_addr = body[1]
            if verbose:
                print(f"[{idx:4d}] {dt:7.3f}s RX CTRL  {val.hex()}")
        elif ch == 0x05 and body:
            if direction == "TX" and body[0] == 0x03 and len(body) >= 3:
                result.auth_pin = struct.unpack_from("<H", body, 1)[0]
                if verbose:
                    print(f"[{idx:4d}] {dt:7.3f}s TX AUTH  PIN={result.auth_pin:04d} ({val.hex()})")
            elif direction == "RX" and body[0] == 0x04:
                result.auth_ok = True
                if verbose:
                    print(f"[{idx:4d}] {dt:7.3f}s RX AUTH  OK ({val.hex()})")
            elif verbose:
                print(f"[{idx:4d}] {dt:7.3f}s {direction} AUTH  {val.hex()}")
        elif ch == 0x1F:
            coap = parse_coap(body)
            cbor_val = decode_cbor(coap["payload"])[0] if coap["payload"] else None
            if direction == "TX":
                result.coap_transactions += 1
                pending_coap[coap["mid"]] = {
                    "dt": dt,
                    "code": coap["code"],
                    "code_str": coap["code_str"],
                    "uri": coap["uri"],
                    "token": coap["token"].hex(),
                    "cbor": cbor_val,
                    "raw_hex": val.hex(),
                }
                # Detect write/control commands: any non-GET request whose URI is not F000
                if coap["code"] != 0x01 and coap["uri"].upper() != "F000":
                    result.write_commands.append(
                        {
                            "idx": idx,
                            "dt": dt,
                            "code": coap["code_str"],
                            "uri": coap["uri"],
                            "mid": coap["mid"],
                            "cbor": cbor_val,
                            "payload_hex": coap["payload"].hex(),
                            "frame_hex": val.hex(),
                            "response": None,
                        }
                    )
                if verbose:
                    print(
                        f"[{idx:4d}] {dt:7.3f}s TX CoAP  mid=0x{coap['mid']:04X} {coap['code_str']} "
                        f"/{coap['uri']} cbor={cbor_val!r}"
                    )
            else:
                req = pending_coap.get(coap["mid"])
                if verbose:
                    print(
                        f"[{idx:4d}] {dt:7.3f}s RX CoAP  mid=0x{coap['mid']:04X} {coap['code_str']} "
                        f"cbor={cbor_val!r}"
                    )
                # Attach response to any matching write command
                for w in reversed(result.write_commands):
                    if w["mid"] == coap["mid"] and w["response"] is None:
                        w["response"] = {"code": coap["code_str"], "cbor": cbor_val}
                        break
                if (coap["code"] >> 5) != 2:
                    result.coap_errors.append(
                        {
                            "mid": coap["mid"],
                            "code": coap["code_str"],
                            "uri": req["uri"] if req else coap["uri"],
                            "cbor": cbor_val,
                        }
                    )
                elif req is not None:
                    if isinstance(cbor_val, dict) and req["uri"].upper() == "F000":
                        for k, v in cbor_val.items():
                            if isinstance(k, int):
                                result.latest_values[k] = v
                    else:
                        try:
                            target_key = int(req["uri"], 16)
                            result.latest_values[target_key] = cbor_val
                        except ValueError:
                            pass

    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Wilo Smart Connect BT .pklg captures")
    parser.add_argument("pklg", type=Path, help="Path to Apple PacketLogger .pklg file")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print every decoded Wilo/CoAP packet")
    args = parser.parse_args()

    res = analyze_pklg(args.pklg, verbose=args.verbose)
    print(f"=== Wilo BT Capture Summary: {args.pklg.name} ===")
    print(f"Connected MAC(s):   {', '.join(res.device_macs) or 'unknown'}")
    print(f"Max Frame Size:     {res.max_frame_size}")
    print(f"Assigned CoAP Addr: {f'0x{res.client_addr:02X}' if res.client_addr is not None else 'unknown'}")
    print(f"Session PIN:        {f'{res.auth_pin:04d}' if res.auth_pin is not None else 'none'} (auth_ok={res.auth_ok})")
    print(f"TX Credit Grants:   {res.tx_credits_granted} (total={sum(res.tx_credits_granted)})")
    print(f"RX Credit Writes:   {res.rx_credit_writes}")
    print(f"CoAP Requests:      {res.coap_transactions}")

    print("\n=== Decoded Registers (Latest Values) ===")
    for key in sorted(res.latest_values):
        val = res.latest_values[key]
        name = KNOWN_KEYS.get(key, "unknown")
        phys = decode_physical(key, val, res.latest_values)
        phys_str = f"  ==> {phys}" if phys else ""
        print(f"  0x{key:04X} ({name:22s}): {val!r}{phys_str}")

    if res.coap_errors:
        print("\n=== CoAP Non-2xx Responses ===")
        for err in res.coap_errors:
            print(f"  /{err['uri']} -> {err['code']} payload={err['cbor']!r}")

    print("\n=== Detected Write / Control Commands (Non-GET, Non-F000) ===")
    if not res.write_commands:
        print("  (none found in this capture)")
    else:
        for w in res.write_commands:
            print(
                f"  [{w['idx']}] +{w['dt']:.2f}s {w['code']} /{w['uri']} "
                f"cbor={w['cbor']!r} payload={w['payload_hex']} resp={w['response']}"
            )
            print(f"      raw frame: {w['frame_hex']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
