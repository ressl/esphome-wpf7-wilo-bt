#include "wilo_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wilo_protocol {

namespace {

void append_cbor_uint(std::vector<uint8_t> *out, uint64_t value) {
  if (value <= 23) {
    out->push_back(static_cast<uint8_t>(value));
  } else if (value <= 0xFF) {
    out->push_back(0x18);
    out->push_back(static_cast<uint8_t>(value));
  } else if (value <= 0xFFFF) {
    out->push_back(0x19);
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(value & 0xFF));
  } else if (value <= 0xFFFFFFFFULL) {
    out->push_back(0x1A);
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out->push_back(static_cast<uint8_t>(value & 0xFF));
  } else {
    out->push_back(0x1B);
    for (int shift = 56; shift >= 0; shift -= 8) {
      out->push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
  }
}

void append_coap_option(std::vector<uint8_t> *out, uint16_t *prev_opt, uint16_t opt_num, const uint8_t *val,
                        size_t val_len) {
  const uint16_t delta = opt_num - *prev_opt;
  *prev_opt = opt_num;

  uint8_t delta_nibble = 0;
  uint8_t delta_ext[2] = {0, 0};
  size_t delta_ext_len = 0;
  if (delta < 13) {
    delta_nibble = static_cast<uint8_t>(delta);
  } else if (delta < 269) {
    delta_nibble = 13;
    delta_ext[0] = static_cast<uint8_t>(delta - 13);
    delta_ext_len = 1;
  } else {
    delta_nibble = 14;
    const uint16_t adj = delta - 269;
    delta_ext[0] = static_cast<uint8_t>((adj >> 8) & 0xFF);
    delta_ext[1] = static_cast<uint8_t>(adj & 0xFF);
    delta_ext_len = 2;
  }

  uint8_t len_nibble = 0;
  uint8_t len_ext[2] = {0, 0};
  size_t len_ext_len = 0;
  if (val_len < 13) {
    len_nibble = static_cast<uint8_t>(val_len);
  } else if (val_len < 269) {
    len_nibble = 13;
    len_ext[0] = static_cast<uint8_t>(val_len - 13);
    len_ext_len = 1;
  } else {
    len_nibble = 14;
    const uint16_t adj = static_cast<uint16_t>(val_len - 269);
    len_ext[0] = static_cast<uint8_t>((adj >> 8) & 0xFF);
    len_ext[1] = static_cast<uint8_t>(adj & 0xFF);
    len_ext_len = 2;
  }

  out->push_back(static_cast<uint8_t>((delta_nibble << 4) | len_nibble));
  out->insert(out->end(), delta_ext, delta_ext + delta_ext_len);
  out->insert(out->end(), len_ext, len_ext + len_ext_len);
  if (val_len > 0) {
    out->insert(out->end(), val, val + val_len);
  }
}

bool decode_cbor_unsigned(uint8_t additional_info, const uint8_t *data, size_t len, size_t *offset, uint64_t *value) {
  if (additional_info <= 23) {
    *value = additional_info;
    return true;
  }
  if (additional_info == 24) {
    if (*offset + 1 > len) {
      return false;
    }
    *value = data[(*offset)++];
    return true;
  }
  if (additional_info == 25) {
    if (*offset + 2 > len) {
      return false;
    }
    *value = (static_cast<uint64_t>(data[*offset]) << 8) | data[*offset + 1];
    *offset += 2;
    return true;
  }
  if (additional_info == 26) {
    if (*offset + 4 > len) {
      return false;
    }
    *value = (static_cast<uint64_t>(data[*offset]) << 24) | (static_cast<uint64_t>(data[*offset + 1]) << 16) |
             (static_cast<uint64_t>(data[*offset + 2]) << 8) | static_cast<uint64_t>(data[*offset + 3]);
    *offset += 4;
    return true;
  }
  if (additional_info == 27) {
    if (*offset + 8 > len) {
      return false;
    }
    uint64_t decoded = 0;
    for (int i = 0; i < 8; i++) {
      decoded = (decoded << 8) | data[*offset + i];
    }
    *offset += 8;
    *value = decoded;
    return true;
  }
  return false;
}

bool decode_cbor_item(const uint8_t *data, size_t len, size_t *offset, CborValue *value, int depth) {
  if (*offset >= len || depth > 8) {
    return false;
  }

  const uint8_t initial = data[(*offset)++];
  const uint8_t major_type = initial >> 5;
  const uint8_t additional_info = initial & 0x1F;

  uint64_t raw = 0;
  switch (major_type) {
    case 0:
      if (!decode_cbor_unsigned(additional_info, data, len, offset, &raw)) {
        return false;
      }
      value->type = CborValue::Type::UNSIGNED;
      value->unsigned_value = raw;
      return true;
    case 1:
      if (!decode_cbor_unsigned(additional_info, data, len, offset, &raw)) {
        return false;
      }
      value->type = CborValue::Type::NEGATIVE;
      value->signed_value = -1 - static_cast<int64_t>(raw);
      return true;
    case 2:
    case 3: {
      if (!decode_cbor_unsigned(additional_info, data, len, offset, &raw)) {
        return false;
      }
      if (raw > len - *offset) {
        return false;
      }
      if (major_type == 2) {
        value->type = CborValue::Type::BYTES;
        value->string_value = bytes_to_hex(data + *offset, static_cast<size_t>(raw));
      } else {
        value->type = CborValue::Type::STRING;
        value->string_value.assign(reinterpret_cast<const char *>(data + *offset), static_cast<size_t>(raw));
      }
      *offset += static_cast<size_t>(raw);
      return true;
    }
    case 4: {
      if (!decode_cbor_unsigned(additional_info, data, len, offset, &raw)) {
        return false;
      }
      if (raw > len - *offset) {
        return false;
      }
      value->type = CborValue::Type::ARRAY;
      value->array_values.clear();
      value->array_values.reserve(static_cast<size_t>(raw));
      for (uint64_t i = 0; i < raw; i++) {
        CborValue child;
        if (!decode_cbor_item(data, len, offset, &child, depth + 1)) {
          return false;
        }
        value->array_values.push_back(std::move(child));
      }
      return true;
    }
    case 5: {
      if (!decode_cbor_unsigned(additional_info, data, len, offset, &raw)) {
        return false;
      }
      if (raw * 2 > len - *offset) {
        return false;
      }
      value->type = CborValue::Type::MAP;
      value->map_values.clear();
      value->map_values.reserve(static_cast<size_t>(raw));
      for (uint64_t i = 0; i < raw; i++) {
        CborValue key;
        CborValue child;
        if (!decode_cbor_item(data, len, offset, &key, depth + 1) ||
            !decode_cbor_item(data, len, offset, &child, depth + 1)) {
          return false;
        }
        value->map_values.emplace_back(std::move(key), std::move(child));
      }
      return true;
    }
    case 6: {
      // Semantic tag: unwrap tag number and decode tagged item
      if (!decode_cbor_unsigned(additional_info, data, len, offset, &raw)) {
        return false;
      }
      return decode_cbor_item(data, len, offset, value, depth + 1);
    }
    case 7:
      switch (additional_info) {
        case 20:
          value->type = CborValue::Type::BOOL;
          value->bool_value = false;
          return true;
        case 21:
          value->type = CborValue::Type::BOOL;
          value->bool_value = true;
          return true;
        case 22:
        case 23:
          value->type = CborValue::Type::NIL;
          return true;
        case 25: {
          if (*offset + 2 > len) {
            return false;
          }
          const uint16_t half = (static_cast<uint16_t>(data[*offset]) << 8) | data[*offset + 1];
          *offset += 2;
          value->type = CborValue::Type::FLOAT;
          value->float_value = decode_half_float(half);
          return true;
        }
        case 26: {
          if (*offset + 4 > len) {
            return false;
          }
          uint32_t bits = (static_cast<uint32_t>(data[*offset]) << 24) |
                          (static_cast<uint32_t>(data[*offset + 1]) << 16) |
                          (static_cast<uint32_t>(data[*offset + 2]) << 8) | static_cast<uint32_t>(data[*offset + 3]);
          *offset += 4;
          float value_f = 0.0f;
          std::memcpy(&value_f, &bits, sizeof(value_f));
          value->type = CborValue::Type::FLOAT;
          value->float_value = value_f;
          return true;
        }
        case 27: {
          if (*offset + 8 > len) {
            return false;
          }
          uint64_t bits = 0;
          for (int i = 0; i < 8; i++) {
            bits = (bits << 8) | data[*offset + i];
          }
          *offset += 8;
          double value_d = 0.0;
          std::memcpy(&value_d, &bits, sizeof(value_d));
          value->type = CborValue::Type::FLOAT;
          value->float_value = value_d;
          return true;
        }
        default:
          return false;
      }
    default:
      return false;
  }
}

}  // namespace

bool CborValue::as_number(double *out) const {
  if (out == nullptr) {
    return false;
  }
  switch (this->type) {
    case Type::UNSIGNED:
      *out = static_cast<double>(this->unsigned_value);
      return true;
    case Type::NEGATIVE:
      *out = static_cast<double>(this->signed_value);
      return true;
    case Type::FLOAT:
      if (std::isfinite(this->float_value)) {
        *out = this->float_value;
        return true;
      }
      return false;
    default:
      return false;
  }
}

const CborValue *CborValue::find_uint_key(uint64_t key) const {
  if (this->type != Type::MAP) {
    return nullptr;
  }
  for (const auto &entry : this->map_values) {
    if (entry.first.type == Type::UNSIGNED && entry.first.unsigned_value == key) {
      return &entry.second;
    }
  }
  return nullptr;
}

std::vector<uint8_t> build_wilo_envelope(uint8_t channel, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> frame;
  frame.reserve(4 + payload.size());
  frame.push_back(channel);
  frame.push_back(WILO_ENVELOPE_VERSION);
  const uint16_t len = static_cast<uint16_t>(payload.size());
  frame.push_back(static_cast<uint8_t>(len & 0xFF));
  frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

std::vector<uint8_t> build_bootstrap_mtu_req() { return build_wilo_envelope(WILO_CHANNEL_TRANSPORT, {0x01}); }

std::vector<uint8_t> build_bootstrap_flags_req() { return build_wilo_envelope(WILO_CHANNEL_TRANSPORT, {0x03}); }

std::vector<uint8_t> build_bootstrap_addr_req() { return build_wilo_envelope(WILO_CHANNEL_TRANSPORT, {0x05}); }

std::vector<uint8_t> build_pin_request_frame() { return build_wilo_envelope(WILO_CHANNEL_AUTH, {0x01}); }

std::vector<uint8_t> build_auth_pin_frame(uint16_t pin) {
  return build_wilo_envelope(WILO_CHANNEL_AUTH,
                             {0x03, static_cast<uint8_t>(pin & 0xFF), static_cast<uint8_t>((pin >> 8) & 0xFF)});
}

std::vector<uint8_t> encode_cbor_uint(uint64_t value) {
  std::vector<uint8_t> out;
  append_cbor_uint(&out, value);
  return out;
}

std::vector<uint8_t> encode_cbor_query_keys(const std::vector<uint16_t> &keys) {
  std::vector<uint8_t> payload;
  if (keys.empty()) {
    return payload;
  }
  if (keys.size() == 1) {
    append_cbor_uint(&payload, keys.front());
    return payload;
  }
  if (keys.size() <= 23) {
    payload.push_back(static_cast<uint8_t>(0x80 | keys.size()));
  } else {
    payload.push_back(0x98);
    payload.push_back(static_cast<uint8_t>(keys.size()));
  }
  for (uint16_t key : keys) {
    append_cbor_uint(&payload, key);
  }
  return payload;
}

std::vector<uint8_t> build_coap_envelope(uint8_t coap_code, uint16_t message_id, uint16_t primary_key,
                                         uint8_t client_addr, const std::vector<uint8_t> &cbor_payload,
                                         uint16_t uri_index) {
  std::vector<uint8_t> coap;
  coap.reserve(28 + cbor_payload.size());

  // CoAP header: Ver=1, Type=CON (0), TKL=2 -> 0x42
  coap.push_back(0x42);
  coap.push_back(coap_code);
  coap.push_back(static_cast<uint8_t>((message_id >> 8) & 0xFF));
  coap.push_back(static_cast<uint8_t>(message_id & 0xFF));
  // 2-byte Token = primary_key (big-endian)
  coap.push_back(static_cast<uint8_t>((primary_key >> 8) & 0xFF));
  coap.push_back(static_cast<uint8_t>(primary_key & 0xFF));

  uint16_t prev_opt = 0;
  char uri_buf[5];
  std::snprintf(uri_buf, sizeof(uri_buf), "%04X", primary_key);
  append_coap_option(&coap, &prev_opt, COAP_OPT_URI_PATH, reinterpret_cast<const uint8_t *>(uri_buf), 4);

  if (uri_index != 0) {
    char idx_buf[8];
    const int n = std::snprintf(idx_buf, sizeof(idx_buf), "i%X", static_cast<unsigned>(uri_index));
    if (n > 0) {
      append_coap_option(&coap, &prev_opt, COAP_OPT_URI_PATH, reinterpret_cast<const uint8_t *>(idx_buf),
                         static_cast<size_t>(n));
    }
  }

  const uint8_t content_format = COAP_CONTENT_FORMAT_CBOR;
  append_coap_option(&coap, &prev_opt, COAP_OPT_CONTENT_FORMAT, &content_format, 1);

  append_coap_option(&coap, &prev_opt, COAP_OPT_WILO_SRC_ADDR, &client_addr, 1);

  const uint8_t dst_addr = 0x00;
  append_coap_option(&coap, &prev_opt, COAP_OPT_WILO_DST_ADDR, &dst_addr, 1);

  if (!cbor_payload.empty()) {
    coap.push_back(0xFF);
    coap.insert(coap.end(), cbor_payload.begin(), cbor_payload.end());
  }

  return build_wilo_envelope(WILO_CHANNEL_COAP, coap);
}

std::vector<uint8_t> build_query_frame(uint16_t message_id, const std::vector<uint16_t> &keys, uint8_t client_addr) {
  const bool single = keys.size() == 1;
  const uint16_t primary_key = single ? keys.front() : WILO_BATCH_KEY;
  const uint8_t coap_code = single ? COAP_CODE_GET : COAP_CODE_PUT;
  return build_coap_envelope(coap_code, message_id, primary_key, client_addr, encode_cbor_query_keys(keys));
}

std::vector<uint8_t> build_indexed_read_frame(uint16_t message_id, uint16_t key, uint64_t argument,
                                              uint8_t client_addr) {
  return build_coap_envelope(COAP_CODE_GET, message_id, key, client_addr, encode_cbor_uint(argument));
}

std::vector<uint8_t> build_write_frame(uint16_t message_id, uint16_t key, const std::vector<uint8_t> &cbor_payload,
                                       uint8_t client_addr, uint8_t coap_code, uint16_t uri_index) {
  return build_coap_envelope(coap_code, message_id, key, client_addr, cbor_payload, uri_index);
}

bool parse_wilo_envelope(const uint8_t *data, size_t len, WiloEnvelope *out) {
  if (data == nullptr || out == nullptr || len < 4) {
    return false;
  }
  const uint16_t declared_len = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
  if (len < static_cast<size_t>(4) + declared_len) {
    return false;
  }
  out->channel = data[0];
  out->version = data[1];
  out->declared_length = declared_len;
  out->payload.assign(data + 4, data + 4 + declared_len);
  return true;
}

BootstrapReply parse_bootstrap_or_auth_reply(const WiloEnvelope &env) {
  BootstrapReply reply;
  if (env.version != WILO_ENVELOPE_VERSION || env.payload.empty()) {
    return reply;
  }

  if (env.channel == WILO_CHANNEL_TRANSPORT) {
    const uint8_t op = env.payload[0];
    if (op == 0x02 && env.payload.size() >= 3) {
      reply.kind = BootstrapReply::Kind::MAX_FRAME_SIZE;
      reply.value16 = static_cast<uint16_t>(env.payload[1]) | (static_cast<uint16_t>(env.payload[2]) << 8);
    } else if (op == 0x04 && env.payload.size() >= 3) {
      reply.kind = BootstrapReply::Kind::TRANSPORT_FLAGS;
      reply.value16 = static_cast<uint16_t>(env.payload[1]) | (static_cast<uint16_t>(env.payload[2]) << 8);
    } else if (op == 0x06 && env.payload.size() >= 2) {
      reply.kind = BootstrapReply::Kind::CLIENT_ADDRESS;
      reply.value8 = env.payload[1];
    }
  } else if (env.channel == WILO_CHANNEL_AUTH) {
    const uint8_t op = env.payload[0];
    if (op == 0x02 && env.payload.size() >= 2) {
      reply.kind = BootstrapReply::Kind::PIN_REQUIRED;
      reply.value8 = env.payload[1];
    } else if (op == 0x04) {
      reply.kind = BootstrapReply::Kind::AUTH_SUCCESS;
      reply.value8 = env.payload.size() >= 2 ? env.payload[1] : 0;
    } else if (op == 0x05 || op == 0x06 || op == 0xFF) {
      reply.kind = BootstrapReply::Kind::AUTH_ERROR;
      reply.value8 = env.payload.size() >= 2 ? env.payload[1] : op;
    }
  }
  return reply;
}

bool parse_coap_message(const uint8_t *data, size_t len, CoapMessage *out) {
  if (data == nullptr || out == nullptr || len < 4) {
    return false;
  }

  const uint8_t b0 = data[0];
  const uint8_t ver = (b0 >> 6) & 0x03;
  const uint8_t type = (b0 >> 4) & 0x03;
  const uint8_t tkl = b0 & 0x0F;
  if (ver != 1 || tkl > 8 || static_cast<size_t>(4) + tkl > len) {
    return false;
  }

  CoapMessage msg;
  msg.version = ver;
  msg.type = type;
  msg.code = data[1];
  msg.message_id = (static_cast<uint16_t>(data[2]) << 8) | data[3];
  msg.token.assign(data + 4, data + 4 + tkl);

  size_t off = 4 + tkl;
  uint16_t opt_num = 0;
  while (off < len) {
    if (data[off] == 0xFF) {
      off++;
      if (off >= len) {
        // RFC 7252: 0xFF marker with zero-length payload is malformed
        return false;
      }
      msg.payload.assign(data + off, data + len);
      *out = std::move(msg);
      return true;
    }

    const uint8_t hdr = data[off++];
    uint16_t delta = (hdr >> 4) & 0x0F;
    uint16_t opt_len = hdr & 0x0F;
    if (delta == 15 || opt_len == 15) {
      return false;
    }
    if (delta == 13) {
      if (off + 1 > len) {
        return false;
      }
      delta = static_cast<uint16_t>(13 + data[off++]);
    } else if (delta == 14) {
      if (off + 2 > len) {
        return false;
      }
      delta = static_cast<uint16_t>(269 + ((static_cast<uint16_t>(data[off]) << 8) | data[off + 1]));
      off += 2;
    }

    if (opt_len == 13) {
      if (off + 1 > len) {
        return false;
      }
      opt_len = static_cast<uint16_t>(13 + data[off++]);
    } else if (opt_len == 14) {
      if (off + 2 > len) {
        return false;
      }
      opt_len = static_cast<uint16_t>(269 + ((static_cast<uint16_t>(data[off]) << 8) | data[off + 1]));
      off += 2;
    }

    if (off + opt_len > len) {
      return false;
    }
    opt_num = static_cast<uint16_t>(opt_num + delta);

    if (opt_num == COAP_OPT_URI_PATH) {
      if (!msg.uri_path.empty()) {
        msg.uri_path.push_back('/');
      }
      msg.uri_path.append(reinterpret_cast<const char *>(data + off), opt_len);
    } else if (opt_num == COAP_OPT_WILO_SRC_ADDR && opt_len >= 1) {
      msg.src_addr = data[off];
      msg.has_src_addr = true;
    } else if (opt_num == COAP_OPT_WILO_DST_ADDR && opt_len >= 1) {
      msg.dst_addr = data[off];
      msg.has_dst_addr = true;
    }

    off += opt_len;
  }

  *out = std::move(msg);
  return true;
}

bool decode_cbor(const uint8_t *data, size_t len, CborValue *value) {
  if (data == nullptr || value == nullptr || len == 0) {
    return false;
  }
  size_t offset = 0;
  if (!decode_cbor_item(data, len, &offset, value, 0)) {
    return false;
  }
  return offset == len;
}

float decode_half_float(uint16_t bits) {
  const uint16_t exp = (bits >> 10) & 0x1F;
  const uint16_t mant = bits & 0x03FF;
  const float sign = (bits & 0x8000) ? -1.0f : 1.0f;

  if (exp == 0) {
    if (mant == 0) {
      return sign * 0.0f;
    }
    return sign * std::ldexp(static_cast<float>(mant), -24);
  }
  if (exp == 31) {
    if (mant == 0) {
      return sign * INFINITY;
    }
    return NAN;
  }
  return sign * std::ldexp(static_cast<float>(mant + 1024), static_cast<int>(exp) - 25);
}

bool is_invalid_raw_u32(uint64_t raw) { return raw == WILO_INVALID_U32; }

double decode_biased_u32_q10(uint64_t raw) {
  const uint32_t u32 = static_cast<uint32_t>(raw);
  const int32_t signed_q10 = static_cast<int32_t>(u32 - 0x80000000U);
  return static_cast<double>(signed_q10) / 1024.0;
}

double decode_u16_q10(uint64_t raw) { return static_cast<double>(raw) / 1024.0; }

double decode_biased_u16_q1(uint64_t raw) {
  const int32_t signed_q1 = static_cast<int32_t>(static_cast<uint16_t>(raw)) - 0x8000;
  return static_cast<double>(signed_q1) / 2.0;
}

bool decode_medium_temp_c(uint64_t raw, double *out_temp_c) {
  if (out_temp_c == nullptr || is_invalid_raw_u32(raw)) {
    return false;
  }
  // Exact Wilo-Assistant 6.13.0 formula: Helper::convertDecimalToDouble(raw, 256, 14335)
  *out_temp_c = static_cast<double>(static_cast<int64_t>(raw) - 14335LL) / 256.0;
  return true;
}

double decode_power_watts(uint64_t raw) { return static_cast<double>(raw) / 2.0; }

double decode_energy_kwh(uint64_t raw) { return static_cast<double>(raw) / 128.0; }

double decode_operating_hours(uint64_t raw) { return static_cast<double>(raw) / 3600.0; }

double decode_setpoint_percent(uint64_t raw) { return (static_cast<double>(raw) / 16384.0) * 100.0; }

bool compute_physical_setpoint(uint64_t raw_setpoint, uint64_t control_mode, const CborValue &scale_map,
                               double *out_value) {
  if (out_value == nullptr) {
    return false;
  }
  const CborValue *scale_entry = scale_map.find_uint_key(control_mode);
  if (scale_entry == nullptr) {
    return false;
  }
  double max_scale = 0.0;
  if (!scale_entry->as_number(&max_scale) || max_scale <= 0.0) {
    return false;
  }
  *out_value = (static_cast<double>(raw_setpoint) / 16384.0) * max_scale;
  return true;
}

bool encode_physical_setpoint(double physical_value, uint64_t control_mode, const CborValue &scale_map,
                              const CborValue *min_map, const CborValue *max_map, uint16_t *out_raw_q14) {
  if (out_raw_q14 == nullptr || !std::isfinite(physical_value)) {
    return false;
  }
  const CborValue *scale_entry = scale_map.find_uint_key(control_mode);
  if (scale_entry == nullptr) {
    return false;
  }
  double max_scale = 0.0;
  if (!scale_entry->as_number(&max_scale) || max_scale <= 0.0) {
    return false;
  }

  double min_q14 = 0.0;
  double max_q14 = 16384.0;
  if (min_map != nullptr) {
    if (const CborValue *e = min_map->find_uint_key(control_mode); e != nullptr) {
      double v = 0.0;
      if (e->as_number(&v) && v >= 0.0 && v <= 16384.0) {
        min_q14 = v;
      }
    }
  }
  if (max_map != nullptr) {
    if (const CborValue *e = max_map->find_uint_key(control_mode); e != nullptr) {
      double v = 0.0;
      if (e->as_number(&v) && v >= min_q14 && v <= 16384.0) {
        max_q14 = v;
      }
    }
  }

  // Exact Wilo-Assistant formula: round((physical_value / max_scale) * 16384.0)
  const double raw_f = std::round((physical_value / max_scale) * 16384.0);
  const double clamped = std::clamp(raw_f, min_q14, max_q14);
  *out_raw_q14 = static_cast<uint16_t>(clamped);
  return true;
}

bool parse_failure_detail(const CborValue &value, FailureDetail *out) {
  if (out == nullptr) {
    return false;
  }
  const CborValue *map = nullptr;
  if (value.type == CborValue::Type::ARRAY && !value.array_values.empty() &&
      value.array_values.front().type == CborValue::Type::MAP) {
    map = &value.array_values.front();
  } else if (value.type == CborValue::Type::MAP) {
    map = &value;
  }
  if (map == nullptr) {
    return false;
  }
  // Key 2 = userTroubleCode (verified from PumpDetailsDataHandler.handlePumpData L10493)
  const CborValue *code_val = map->find_uint_key(2);
  if (code_val == nullptr) {
    return false;
  }
  uint32_t code = 0;
  if (code_val->type == CborValue::Type::UNSIGNED) {
    code = static_cast<uint32_t>(code_val->unsigned_value);
  } else if (code_val->type == CborValue::Type::STRING && !code_val->string_value.empty()) {
    unsigned long parsed = 0;
    if (std::sscanf(code_val->string_value.c_str(), "%lu", &parsed) != 1) {
      return false;
    }
    code = static_cast<uint32_t>(parsed);
  } else {
    return false;
  }
  out->trouble_code = code;
  // Exact PumpMessage.fromUserTroubleCode rule: < 500 = Error, >= 500 = Warning
  out->is_error = code < 500;
  return true;
}

const char *trouble_code_description(uint32_t code) {
  switch (code) {
    case 401:
      return "Netzspannung instabil";
    case 402:
      return "Unterspannung";
    case 403:
      return "Überspannung";
    case 404:
      return "Pumpenwelle blockiert";
    case 405:
      return "Elektronik-Übertemperatur";
    case 406:
      return "Motor-Übertemperatur";
    case 407:
      return "Motor-Kontaktfehler";
    case 408:
      return "Starke Rückströmung";
    case 409:
      return "Softwareupdate erforderlich";
    case 410:
      return "Interner Elektronikfehler";
    case 411:
      return "Phasenausfall";
    case 412:
      return "Trockenlauf / Kein Medium";
    case 413:
      return "Saugdruck zu hoch";
    case 414:
      return "Leckage / Mindestdruck unterschritten";
    case 415:
      return "Saugdruck zu niedrig";
    case 420:
      return "Defekt Motor/Elektronik";
    case 421:
      return "Interner Servicefehler";
    case 550:
      return "Warnung Rückströmung";
    case 551:
      return "Warnung Unterspannung";
    case 552:
      return "Warnung Durchströmung";
    case 553:
      return "Warnung reduzierte Leistung";
    case 554:
      return "MFA Partner-Warnung";
    case 555:
      return "Sensorwert fehlerhaft";
    default:
      return nullptr;
  }
}

std::string format_failure_summary(const std::vector<FailureDetail> &details, size_t unresolved_count) {
  if (details.empty() && unresolved_count == 0) {
    return "Keine";
  }
  std::string out;
  for (const auto &d : details) {
    if (!out.empty()) {
      out += "; ";
    }
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "%s %u", d.is_error ? "Fehler" : "Warnung",
                  static_cast<unsigned>(d.trouble_code));
    out += prefix;
    if (const char *desc = trouble_code_description(d.trouble_code); desc != nullptr) {
      out += ": ";
      out += desc;
    }
  }
  if (unresolved_count > 0) {
    if (!out.empty()) {
      out += "; ";
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%u aktive Meldung(en)", static_cast<unsigned>(unresolved_count));
    out += buf;
  }
  if (out.size() > 250) {
    out.resize(247);
    out += "...";
  }
  return out;
}

std::string format_software_version(uint64_t raw) {
  const unsigned major = static_cast<unsigned>(raw / 10000ULL);
  const unsigned minor = static_cast<unsigned>((raw / 100ULL) % 100ULL);
  const unsigned patch = static_cast<unsigned>(raw % 100ULL);
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%02u.%02u.%02u", major, minor, patch);
  return buf;
}

std::string control_mode_to_string(uint64_t mode) {
  switch (mode) {
    case 0:
      return "Motor Off";
    case 1:
      return "Constant Speed (n-const)";
    case 2:
      return "Min Speed";
    case 3:
      return "Max Speed";
    case 8:
    case 9:
    case 16:
      return "Constant Pressure (dp-c)";
    case 10:
    case 17:
      return "Variable Pressure (dp-v)";
    case 25:
      return "Constant Flow (Q-const)";
    case 26:
      return "Multi-Flow Adaptation";
    case 32:
      return "Constant Temperature (T-const)";
    case 33:
      return "Temperature Difference (dT-const)";
    case 40:
      return "Dynamic Adapt";
    case 48:
      return "PID Control";
    case 255:
      return "Not Available";
    default: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "Mode %llu", static_cast<unsigned long long>(mode));
      return buf;
    }
  }
}

bool control_mode_from_string(const std::string &name, uint16_t *out_mode) {
  if (out_mode == nullptr) {
    return false;
  }
  if (name == "Constant Speed (n-const)") {
    *out_mode = 1;
    return true;
  }
  if (name == "Constant Pressure (dp-c)") {
    *out_mode = 8;
    return true;
  }
  if (name == "Variable Pressure (dp-v)") {
    *out_mode = 10;
    return true;
  }
  if (name == "Dynamic Adapt") {
    *out_mode = 40;
    return true;
  }
  return false;
}

std::string cbor_to_json(const CborValue &value) {
  char buffer[32];
  switch (value.type) {
    case CborValue::Type::UNSIGNED:
      return std::to_string(value.unsigned_value);
    case CborValue::Type::NEGATIVE:
      return std::to_string(value.signed_value);
    case CborValue::Type::FLOAT:
      if (std::isnan(value.float_value)) {
        return "null";
      }
      if (!std::isfinite(value.float_value)) {
        return value.float_value > 0 ? "\"Infinity\"" : "\"-Infinity\"";
      }
      std::snprintf(buffer, sizeof(buffer), "%.10g", value.float_value);
      return buffer;
    case CborValue::Type::BOOL:
      return value.bool_value ? "true" : "false";
    case CborValue::Type::NIL:
      return "null";
    case CborValue::Type::STRING:
    case CborValue::Type::BYTES:
      return "\"" + escape_json(value.string_value) + "\"";
    case CborValue::Type::ARRAY: {
      std::string json = "[";
      for (size_t i = 0; i < value.array_values.size(); i++) {
        if (i != 0) {
          json += ",";
        }
        json += cbor_to_json(value.array_values[i]);
      }
      json += "]";
      return json;
    }
    case CborValue::Type::MAP: {
      std::string json = "{";
      bool first = true;
      for (const auto &entry : value.map_values) {
        if (!first) {
          json += ",";
        }
        first = false;
        std::string key_json;
        if (entry.first.type == CborValue::Type::UNSIGNED) {
          key_json = key_to_hex(static_cast<uint16_t>(entry.first.unsigned_value));
        } else if (entry.first.type == CborValue::Type::STRING) {
          key_json = entry.first.string_value;
        } else {
          key_json = cbor_to_json(entry.first);
        }
        json += "\"" + escape_json(key_json) + "\":" + cbor_to_json(entry.second);
      }
      json += "}";
      return json;
    }
  }
  return "null";
}

std::string key_map_to_json(const std::map<uint16_t, CborValue> &values) {
  std::string json = "{";
  bool first = true;
  for (const auto &entry : values) {
    if (!first) {
      json += ",";
    }
    first = false;
    json += "\"" + key_to_hex(entry.first) + "\":" + cbor_to_json(entry.second);
  }
  json += "}";
  return json;
}

std::string key_map_to_compact_json(const std::map<uint16_t, CborValue> &values, size_t max_len, bool omit_uuid) {
  if (max_len < 2) {
    return "{}";
  }
  std::string json = "{";
  bool first = true;
  for (const auto &entry : values) {
    if (omit_uuid && entry.first == KEY_DEVICE_UUID) {
      continue;
    }
    std::string item = "\"" + key_to_hex(entry.first) + "\":" + cbor_to_json(entry.second);
    const size_t extra = (first ? 0 : 1) + item.size() + 1;  // comma + item + closing '}'
    if (json.size() + extra > max_len) {
      continue;
    }
    if (!first) {
      json += ",";
    }
    first = false;
    json += item;
  }
  json += "}";
  return json;
}

std::string escape_json(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          escaped += buffer;
        } else {
          escaped += c;
        }
        break;
    }
  }
  return escaped;
}

std::string key_to_hex(uint16_t key) {
  char buffer[7];
  std::snprintf(buffer, sizeof(buffer), "0x%04X", key);
  return buffer;
}

std::string bytes_to_hex(const uint8_t *data, size_t len) {
  static const char *HEX = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out.push_back(HEX[(data[i] >> 4) & 0x0F]);
    out.push_back(HEX[data[i] & 0x0F]);
  }
  return out;
}

}  // namespace wilo_protocol
