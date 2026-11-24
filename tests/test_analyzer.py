import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import analyze_wilo_pklg as analyzer  # noqa: E402


class TestWiloAnalyzer(unittest.TestCase):
    def test_decode_cbor_primitives_and_map(self):
        # CBOR map {0x09EE: 32, 0x0A23: 3960, 0x0B44: 10222}
        raw = bytes.fromhex("a31909ee1820190a23190f78190b441927ee")
        val, end = analyzer.decode_cbor(raw)
        self.assertEqual(end, len(raw))
        self.assertEqual(val, {0x09EE: 32, 0x0A23: 3960, 0x0B44: 10222})

    def test_parse_coap_response(self):
        # CoAP 2.05 Content response with mid=0x00FF and CBOR uint 10222 (v01.02.22)
        pkt = bytes.fromhex("624500ff0b44c13ce106e7002113ff1927ee")
        coap = analyzer.parse_coap(pkt)
        self.assertEqual(coap["code_str"], "2.05")
        self.assertEqual(coap["mid"], 0x00FF)
        cbor_val, _ = analyzer.decode_cbor(coap["payload"])
        self.assertEqual(cbor_val, 10222)

    def test_decode_physical_units(self):
        ctx = {
            0x0001: 8,
            0x06F7: {1: 4200.0, 8: 6.0, 10: 6.0, 40: 0.0},
        }
        self.assertEqual(analyzer.decode_physical(0x0B44, 10222, ctx), "v01.02.22")
        self.assertEqual(analyzer.decode_physical(0x09EE, 32, ctx), "16.0 W")
        self.assertEqual(analyzer.decode_physical(0x0A23, 3960, ctx), "30.938 kWh")
        self.assertEqual(analyzer.decode_physical(0x09D9, 0x937E, ctx), "2495.0 RPM")
        self.assertEqual(analyzer.decode_physical(0x08FD, 2560, ctx), "2.500 m")
        self.assertEqual(analyzer.decode_physical(0x08FE, 0x800003A0, ctx), "0.906 m³/h")
        self.assertTrue(analyzer.decode_physical(0x0079, 6007, ctx).startswith("2.20 "))


if __name__ == "__main__":
    unittest.main()
