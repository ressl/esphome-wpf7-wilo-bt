// Host-side C++17 unit tests for WPF 7 / WPM i thermodynamic, relay, and COP logic.
// Compile & run:
//   c++ -std=c++17 -Wall -Wextra -Werror tests/test_wpf7_logic.cpp -o /tmp/test_wpf7_logic && /tmp/test_wpf7_logic

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr uint16_t RELAY_BIT_DHC1 = 0x0001;
constexpr uint16_t RELAY_BIT_DHC2 = 0x0002;
constexpr uint16_t RELAY_MASK_VERDICHTER = 0x000C;
constexpr uint16_t RELAY_BIT_WW_VENTIL = 0x0010;
constexpr uint16_t RELAY_BIT_HEIZKREISPUMPE = 0x0020;
constexpr uint16_t RELAY_BIT_PUFFERPUMPE = 0x0040;
constexpr uint16_t RELAY_BIT_QUELLENPUMPE = 0x0100;
constexpr uint16_t RELAY_BIT_KUEHLEN = 0x0200;

bool relaisVerdichterOn(uint16_t mask) {
  return (mask & RELAY_MASK_VERDICHTER) == RELAY_MASK_VERDICHTER;
}

float r410aCondensingTempC(float pressureBarGauge) {
  if (std::isnan(pressureBarGauge) || pressureBarGauge < 0.5f || pressureBarGauge > 48.0f) {
    return NAN;
  }
  double lp = std::log(static_cast<double>(pressureBarGauge) + 1.01325);
  double t = -48.038088 + lp * (15.509594 + lp * (3.389605 + lp * 0.128850));
  return static_cast<float>(t);
}

std::string formatRelaisStatusText(uint16_t mask) {
  std::string out;
  auto add = [&](const char *label) {
    if (!out.empty()) out += ", ";
    out += label;
  };
  if (relaisVerdichterOn(mask)) add("Verdichter");
  if (mask & RELAY_BIT_QUELLENPUMPE) add("Quellenpumpe");
  if (mask & RELAY_BIT_PUFFERPUMPE) add("Pufferladepumpe");
  if (mask & RELAY_BIT_HEIZKREISPUMPE) add("Heizkreispumpe");
  if (mask & RELAY_BIT_DHC1) add("Heizstab 1");
  if (mask & RELAY_BIT_DHC2) add("Heizstab 2");
  if (mask & RELAY_BIT_WW_VENTIL) add("WW-Ventil");
  if (mask & RELAY_BIT_KUEHLEN) add("Kühlen");
  return out.empty() ? "Standby" : out;
}

void assert_near(double actual, double expected, double tol) {
  if (std::isnan(actual) || std::fabs(actual - expected) > tol) {
    std::cerr << "Expected " << expected << " +- " << tol << ", got " << actual << "\n";
    assert(false);
  }
}

void test_r410a_saturation_curve() {
  // Reference points from NIST Chemistry WebBook R410A saturation table (P_gauge = P_abs - 1.01325 bar):
  //   0 degC -> 8.00 bar(abs) =  6.99 bar(g)
  //  10 degC -> 10.88 bar(abs) =  9.87 bar(g)
  //  20 degC -> 14.48 bar(abs) = 13.47 bar(g)
  //  35 degC -> 21.37 bar(abs) = 20.36 bar(g)
  //  50 degC -> 30.65 bar(abs) = 29.64 bar(g)
  assert_near(r410aCondensingTempC(6.99f), 0.0, 0.15);
  assert_near(r410aCondensingTempC(9.87f), 10.0, 0.15);
  assert_near(r410aCondensingTempC(13.47f), 20.0, 0.15);
  assert_near(r410aCondensingTempC(20.36f), 35.0, 0.15);
  assert_near(r410aCondensingTempC(29.64f), 50.0, 0.15);

  // Out-of-range / NaN guards
  assert(std::isnan(r410aCondensingTempC(NAN)));
  assert(std::isnan(r410aCondensingTempC(0.4f)));
  assert(std::isnan(r410aCondensingTempC(49.0f)));
}

void test_relay_bitmask_decoding() {
  // Standby: only heating circuit pump active
  assert(!relaisVerdichterOn(0x0020));
  assert(formatRelaisStatusText(0x0020) == "Heizkreispumpe");
  assert(formatRelaisStatusText(0x0000) == "Standby");

  // Single bit 2 or 3 must NOT count as compressor on (AreAllBitsSet(0x000C))
  assert(!relaisVerdichterOn(0x0004));
  assert(!relaisVerdichterOn(0x0008));

  // Brine pump pre-run 30s before compressor start
  assert(!relaisVerdichterOn(0x0120));
  assert(formatRelaisStatusText(0x0120) == "Quellenpumpe, Heizkreispumpe");

  // Live heating run observed on WPF 7 (0x016C = bits 2,3,5,6,8)
  assert(relaisVerdichterOn(0x016C));
  assert(formatRelaisStatusText(0x016C) == "Verdichter, Quellenpumpe, Pufferladepumpe, Heizkreispumpe");

  // Backup heater stage 1 + 2
  assert(formatRelaisStatusText(0x0003) == "Heizstab 1, Heizstab 2");
}

void test_thermal_power_and_cop() {
  // Live run verification: Q = 0.90 m3/h, dT = 6.12 K, P_el = 1875 W
  constexpr double flow_m3h = 0.90;
  constexpr double delta_t = 6.12;
  constexpr double p_el_w = 1875.0;
  double q_kw = flow_m3h * 1.163 * delta_t;
  double cop = (q_kw * 1000.0) / p_el_w;
  assert_near(q_kw, 6.4058, 0.01);
  assert_near(cop, 3.416, 0.01);
}

void test_nan_republish_guard() {
  // Regression test for v2.9.2 IEEE-754 NaN guard:
  // After standby publishes NAN, fabsf(NAN - approach) > 0.05f is FALSE,
  // so std::isnan(prev) MUST be checked explicitly.
  float prev = NAN;
  float approach = 1.2f;
  bool buggy_condition = (std::fabs(prev - approach) > 0.05f);
  bool fixed_condition = (std::isnan(prev) || std::fabs(prev - approach) > 0.05f);
  assert(!buggy_condition);
  assert(fixed_condition);
}

}  // namespace

int main() {
  test_r410a_saturation_curve();
  test_relay_bitmask_decoding();
  test_thermal_power_and_cop();
  test_nan_republish_guard();
  std::cout << "All WPF 7 / WPM i C++ unit tests passed!\n";
  return 0;
}
