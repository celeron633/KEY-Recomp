import struct
import unittest
from tools.prepare import inspect_dol


def sample():
    data = bytearray(0x120)
    for offset, value in [(0, 0x100), (0x48, 0x80004000), (0x90, 0x20),
                          (0xD8, 0x80005000), (0xDC, 0x20), (0xE0, 0x80004000)]:
        struct.pack_into(">I", data, offset, value)
    return data


class DolTests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(inspect_dol(sample())["entry_point"], 0x80004000)

    def test_truncated(self):
        with self.assertRaises(ValueError):
            inspect_dol(bytes(255))

    def test_out_of_file(self):
        data = sample()
        struct.pack_into(">I", data, 0x90, 0x21)
        with self.assertRaises(ValueError):
            inspect_dol(data)

    def test_invalid_entry(self):
        data = sample()
        struct.pack_into(">I", data, 0xE0, 0x80005000)
        with self.assertRaises(ValueError):
            inspect_dol(data)

    def test_overlapping_sections(self):
        data = sample() + bytes(0x20)
        for offset, value in [(4, 0x120), (0x4C, 0x80004010), (0x94, 0x20)]:
            struct.pack_into(">I", data, offset, value)
        with self.assertRaises(ValueError):
            inspect_dol(data)

    def test_bss_envelope_can_span_initialized_data(self):
        data = sample() + bytes(0x20)
        for offset, value in [(0x1C, 0x120), (0x64, 0x80005000), (0xAC, 0x20)]:
            struct.pack_into(">I", data, offset, value)
        self.assertEqual(inspect_dol(data)["bss_overlapping_data_indices"], [0])

    def test_bss_overflow(self):
        data = sample()
        struct.pack_into(">I", data, 0xD8, 0xFFFFFFF0)
        with self.assertRaises(ValueError):
            inspect_dol(data)


if __name__ == "__main__":
    unittest.main()
