#include "wilo_protocol.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> hex_to_bytes(const std::string &hex) {
  assert(hex.size() % 2 == 0);
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  auto nibble = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    assert(false && "invalid hex char");
    return 0;
  };
  for (size_t i = 0; i < hex.size(); i += 2) {
    out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
  }
  return out;
}

void assert_near(double actual, double expected, double tol) {
  if (std::fabs(actual - expected) > tol) {
    std::cerr << "Expected " << expected << " +- " << tol << ", got " << actual << "\n";
    assert(false);
  }
}

void test_bootstrap_and_auth_frames() {
  using namespace wilo_protocol;
  assert(bytes_to_hex(build_bootstrap_mtu_req().data(), build_bootstrap_mtu_req().size()) == "0103010001");
  assert(bytes_to_hex(build_bootstrap_flags_req().data(), build_bootstrap_flags_req().size()) == "0103010003");
  assert(bytes_to_hex(build_bootstrap_addr_req().data(), build_bootstrap_addr_req().size()) == "0103010005");
  assert(bytes_to_hex(build_pin_request_frame().data(), build_pin_request_frame().size()) == "0503010001");

  // PIN 9712 = 0x25F0 -> little-endian f0 25 (matches dump packet [1959])
  auto auth = build_auth_pin_frame(9712);
  assert(bytes_to_hex(auth.data(), auth.size()) == "0503030003f025");

  // Parse pump bootstrap replies from dump packets [1566], [1567], [1571], [1576], [1961]
  WiloEnvelope env;
  auto b1 = hex_to_bytes("0103030002b400");
  assert(parse_wilo_envelope(b1.data(), b1.size(), &env));
  auto r1 = parse_bootstrap_or_auth_reply(env);
  assert(r1.kind == BootstrapReply::Kind::MAX_FRAME_SIZE && r1.value16 == 180);

  auto b2 = hex_to_bytes("010302000613");
  assert(parse_wilo_envelope(b2.data(), b2.size(), &env));
  auto r2 = parse_bootstrap_or_auth_reply(env);
  assert(r2.kind == BootstrapReply::Kind::CLIENT_ADDRESS && r2.value8 == 0x13);

  auto b3 = hex_to_bytes("050302000205");
  assert(parse_wilo_envelope(b3.data(), b3.size(), &env));
  auto r3 = parse_bootstrap_or_auth_reply(env);
  assert(r3.kind == BootstrapReply::Kind::PIN_REQUIRED && r3.value8 == 0x05);

  auto b4 = hex_to_bytes("0503010004");
  assert(parse_wilo_envelope(b4.data(), b4.size(), &env));
  auto r4 = parse_bootstrap_or_auth_reply(env);
  assert(r4.kind == BootstrapReply::Kind::AUTH_SUCCESS);
}

void test_query_and_write_frames() {
  using namespace wilo_protocol;

  // Single-key query from dump packet [1966]
  auto f_single = build_query_frame(0x0001, {0x0B44}, 0x13);
  assert(bytes_to_hex(f_single.data(), f_single.size()) ==
         "1f031700420100010b44b430423434113ce106e7132100ff190b44");

  // Multi-key query from dump packet [2069]
  auto f_multi = build_query_frame(0x000C, {0x0F02, 0x0B3D, 0x0B3F, 0x0939, 0x093B, 0x0669}, 0x13);
  assert(bytes_to_hex(f_multi.data(), f_multi.size()) ==
         "1f0327004203000cf000b446303030113ce106e7132100ff86190f02190b3d190b3f19093919093b190669");

  // Dynamic client address 0x2A must appear in CoAP option 2048
  auto f_dyn = build_query_frame(0x0001, {0x0B44}, 0x2A);
  assert(bytes_to_hex(f_dyn.data(), f_dyn.size()) ==
         "1f031700420100010b44b430423434113ce106e72a2100ff190b44");

  // Indexed GET frame for 0x0C8C FailureDetail with failure_id=7
  auto f_fail = build_indexed_read_frame(0x0021, KEY_FAILURE_DETAIL, 7, 0x13);
  WiloEnvelope env;
  assert(parse_wilo_envelope(f_fail.data(), f_fail.size(), &env));
  CoapMessage coap;
  assert(parse_coap_message(env.payload.data(), env.payload.size(), &coap));
  assert(coap.code == COAP_CODE_GET);
  assert(coap.uri_path == "0C8C");
  CborValue arg;
  assert(decode_cbor(coap.payload.data(), coap.payload.size(), &arg));
  assert(arg.type == CborValue::Type::UNSIGNED && arg.unsigned_value == 7);

  // Write frame (CoAP PUT to 0x0079 with CBOR uint 6827)
  auto f_write = build_write_frame(0x0042, KEY_SETPOINT_NORM, encode_cbor_uint(6827), 0x13, COAP_CODE_PUT);
  assert(parse_wilo_envelope(f_write.data(), f_write.size(), &env));
  assert(parse_coap_message(env.payload.data(), env.payload.size(), &coap));
  assert(coap.code == COAP_CODE_PUT);
  assert(coap.message_id == 0x0042);
  assert(coap.uri_path == "0079");
  assert(coap.has_src_addr && coap.src_addr == 0x13);
  assert(coap.has_dst_addr && coap.dst_addr == 0x00);
  CborValue val;
  assert(decode_cbor(coap.payload.data(), coap.payload.size(), &val));
  assert(val.type == CborValue::Type::UNSIGNED && val.unsigned_value == 6827);

  // Write frame with non-zero uri_index (second URI-Path segment "i3")
  auto f_write_idx = build_write_frame(0x0043, KEY_SETPOINT_NORM, encode_cbor_uint(4096), 0x13, COAP_CODE_PUT, 3);
  assert(parse_wilo_envelope(f_write_idx.data(), f_write_idx.size(), &env));
  assert(parse_coap_message(env.payload.data(), env.payload.size(), &coap));
  assert(coap.uri_path == "0079/i3");
}

void test_coap_sequence_00ff_regression() {
  using namespace wilo_protocol;
  auto frame = hex_to_bytes("1f031200624500ff0b44c13ce106e7002113ff1927ee");
  WiloEnvelope env;
  assert(parse_wilo_envelope(frame.data(), frame.size(), &env));
  CoapMessage coap;
  assert(parse_coap_message(env.payload.data(), env.payload.size(), &coap));
  assert(coap.message_id == 0x00FF);
  assert(coap.is_success());
  CborValue val;
  assert(decode_cbor(coap.payload.data(), coap.payload.size(), &val));
  assert(val.type == CborValue::Type::UNSIGNED && val.unsigned_value == 10222);
  assert(format_software_version(val.unsigned_value) == "01.02.22");
}

void test_coap_404_error_handling() {
  using namespace wilo_protocol;
  auto frame = hex_to_bytes("1f031700628400070b3ab430423341113ce106e7002113ff190b3a");
  WiloEnvelope env;
  assert(parse_wilo_envelope(frame.data(), frame.size(), &env));
  CoapMessage coap;
  assert(parse_coap_message(env.payload.data(), env.payload.size(), &coap));
  assert(coap.message_id == 0x0007);
  assert(!coap.is_success());
  assert(coap.code_class() == 4 && coap.code_detail() == 4);
  assert(coap.uri_path == "0B3A");
}

void test_real_live_and_config_decoding() {
  using namespace wilo_protocol;

  // Real live batch response from dump packet [3198]
  auto live_frame = hex_to_bytes(
      "1f034d0062440099f000c13ce106e7002113ffa9"
      "1909391a80001a00"
      "19093b1a80001000"
      "1909dc1a00945a50"
      "1908fd1908c5"
      "1908fe1a8000035b"
      "1909d919937e"
      "1903a21958f7"
      "1909ee1820"
      "190a23190f78");

  WiloEnvelope env;
  assert(parse_wilo_envelope(live_frame.data(), live_frame.size(), &env));
  CoapMessage coap;
  assert(parse_coap_message(env.payload.data(), env.payload.size(), &coap));
  assert(coap.is_success() && coap.message_id == 0x0099);

  CborValue map;
  assert(decode_cbor(coap.payload.data(), coap.payload.size(), &map));
  assert(map.type == CborValue::Type::MAP && map.map_values.size() == 9);

  assert_near(decode_biased_u32_q10(map.find_uint_key(KEY_MAX_HEAD)->unsigned_value), 6.5, 0.001);
  assert_near(decode_biased_u32_q10(map.find_uint_key(KEY_MAX_FLOW)->unsigned_value), 4.0, 0.001);
  assert_near(decode_operating_hours(map.find_uint_key(KEY_OPERATING_SECONDS)->unsigned_value), 2700.68, 0.01);
  assert_near(decode_u16_q10(map.find_uint_key(KEY_ACTUAL_HEAD)->unsigned_value), 2.1924, 0.001);
  assert_near(decode_biased_u32_q10(map.find_uint_key(KEY_ACTUAL_FLOW)->unsigned_value), 0.8389, 0.001);
  assert_near(decode_biased_u16_q1(map.find_uint_key(KEY_MOTOR_SPEED)->unsigned_value), 2495.0, 0.01);
  assert_near(decode_power_watts(map.find_uint_key(KEY_ELECTRICAL_POWER)->unsigned_value), 16.0, 0.01);
  assert_near(decode_energy_kwh(map.find_uint_key(KEY_ELECTRICAL_ENERGY)->unsigned_value), 30.9375, 0.001);

  // Medium temperature 0x03A2 = 0x58F7 = 22775 -> (22775 - 14335) / 256 = 32.96875 C
  double temp_c = 0.0;
  assert(decode_medium_temp_c(map.find_uint_key(KEY_MEDIUM_TEMP)->unsigned_value, &temp_c));
  assert_near(temp_c, 32.96875, 0.001);
  // Live pump 20132 -> (20132 - 14335) / 256 = 22.6445 C
  assert(decode_medium_temp_c(20132, &temp_c));
  assert_near(temp_c, 22.6445, 0.001);
  // Invalid sentinel 0xFFFFFFFF must be rejected
  assert(!decode_medium_temp_c(WILO_INVALID_U32, &temp_c));

  // Setpoint calculation & encoding using real 0x06F5/0x06F6/0x06F7 maps
  auto scale_cbor = hex_to_bytes("a401f96c1a08f946000af946001828f90000");
  CborValue scale_map;
  assert(decode_cbor(scale_cbor.data(), scale_cbor.size(), &scale_map));
  // min_map: {1: 2731, 8: 1365, 10: 1365, 40: 0}
  auto min_cbor = hex_to_bytes("a401190aab081905550a190555182800");
  CborValue min_map;
  assert(decode_cbor(min_cbor.data(), min_cbor.size(), &min_map));
  // max_map: {1: 16384, 8: 16384, 10: 16384, 40: 16384}
  auto max_cbor = hex_to_bytes("a401194000081940000a1940001828194000");
  CborValue max_map;
  assert(decode_cbor(max_cbor.data(), max_cbor.size(), &max_map));

  double phys_sp = 0.0;
  assert(compute_physical_setpoint(6007, 8, scale_map, &phys_sp));
  assert_near(phys_sp, 2.20, 0.005);
  assert_near(decode_setpoint_percent(6007), 36.6638, 0.01);

  // Encode 2.50 m in mode 8 (scale=6.0) -> round(2.5 / 6.0 * 16384) = 6827
  uint16_t raw_sp = 0;
  assert(encode_physical_setpoint(2.50, 8, scale_map, &min_map, &max_map, &raw_sp));
  assert(raw_sp == 6827);
  // Below min (0.1 m) clamps to min_map[8] = 1365 (0.50 m)
  assert(encode_physical_setpoint(0.10, 8, scale_map, &min_map, &max_map, &raw_sp));
  assert(raw_sp == 1365);
  // Above max (9.0 m) clamps to max_map[8] = 16384 (6.00 m)
  assert(encode_physical_setpoint(9.00, 8, scale_map, &min_map, &max_map, &raw_sp));
  assert(raw_sp == 16384);
  // Dynamic Adapt (mode 40, scale 0.0) must fail cleanly
  assert(!encode_physical_setpoint(2.50, 40, scale_map, &min_map, &max_map, &raw_sp));

  // Control mode roundtrip
  assert(control_mode_to_string(8) == "Constant Pressure (dp-c)");
  uint16_t mode_id = 0;
  assert(control_mode_from_string("Constant Pressure (dp-c)", &mode_id) && mode_id == 8);
  assert(control_mode_from_string("Variable Pressure (dp-v)", &mode_id) && mode_id == 10);
  assert(control_mode_from_string("Constant Speed (n-const)", &mode_id) && mode_id == 1);
  assert(control_mode_from_string("Dynamic Adapt", &mode_id) && mode_id == 40);
  assert(!control_mode_from_string("Invalid Mode", &mode_id));
}

void test_failure_detail_and_compact_json() {
  using namespace wilo_protocol;

  // CBOR [{2: 404}] -> Error 404 (Pumpenwelle blockiert)
  auto err_cbor = hex_to_bytes("81a102190194");
  CborValue err_val;
  assert(decode_cbor(err_cbor.data(), err_cbor.size(), &err_val));
  FailureDetail d1;
  assert(parse_failure_detail(err_val, &d1));
  assert(d1.trouble_code == 404 && d1.is_error);

  // CBOR [{2: 551}] -> Warning 551 (Warnung Unterspannung)
  auto warn_cbor = hex_to_bytes("81a102190227");
  CborValue warn_val;
  assert(decode_cbor(warn_cbor.data(), warn_cbor.size(), &warn_val));
  FailureDetail d2;
  assert(parse_failure_detail(warn_val, &d2));
  assert(d2.trouble_code == 551 && !d2.is_error);

  assert(format_failure_summary({}, 0) == "Keine");
  assert(format_failure_summary({d1}, 0) == "Fehler 404: Pumpenwelle blockiert");
  assert(format_failure_summary({d2}, 0) == "Warnung 551: Warnung Unterspannung");

  // Compact JSON omits 0x0B36 UUID and enforces <= 250 chars
  std::map<uint16_t, CborValue> vals;
  CborValue uuid;
  uuid.type = CborValue::Type::BYTES;
  uuid.string_value = "00000000000000000000000000000000";
  vals[KEY_DEVICE_UUID] = uuid;
  for (uint16_t k = 1; k <= 30; k++) {
    CborValue v;
    v.type = CborValue::Type::UNSIGNED;
    v.unsigned_value = 123456789ULL + k;
    vals[k] = v;
  }
  const std::string compact = key_map_to_compact_json(vals, 250, true);
  assert(compact.size() <= 250);
  assert(compact.find("0x0B36") == std::string::npos);
  assert(compact.front() == '{' && compact.back() == '}');
}

}  // namespace

int main() {
  test_bootstrap_and_auth_frames();
  test_query_and_write_frames();
  test_coap_sequence_00ff_regression();
  test_coap_404_error_handling();
  test_real_live_and_config_decoding();
  test_failure_detail_and_compact_json();
  std::cout << "All wilo_protocol C++ unit tests passed!\n";
  return 0;
}
