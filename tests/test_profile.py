import struct
import unittest
from tools.make_profile import decode_init, read_symbols


class ProfileTests(unittest.TestCase):
    def test_startup_signed_add_and_ori(self):
        # Synthetic lis r2,0x1234; ori r2,r2,0xabcd; lis r13,0x5678;
        # addi r13,r13,-16; blr. These bytes are not from a game.
        data = struct.pack(">5I", 0x3C401234, 0x6042ABCD, 0x3DA05678, 0x39ADFFF0, 0x4E800020)
        self.assertEqual(decode_init(data), (0x1234ABCD, 0x5677FFF0))

    def test_unknown_instruction_rejected(self):
        with self.assertRaises(ValueError):
            decode_init(struct.pack(">I", 0))

    def test_unknown_source_rejected(self):
        with self.assertRaises(ValueError):
            decode_init(struct.pack(">I", 0x38430001))

    def test_missing_register_rejected(self):
        with self.assertRaises(ValueError):
            decode_init(struct.pack(">I", 0x4E800020))

    def test_symbol_range_includes_size(self):
        sections = [{"kind": "text", "address": 0x1000, "size": 8}]
        self.assertEqual(read_symbols("f = .text:0x1000; // type:function size:0x8", sections), {0x1000: ("f", 8)})
        with self.assertRaises(ValueError):
            read_symbols("f = .text:0x1004; // type:function size:0x8", sections)

    def test_conflicting_symbol_rejected(self):
        with self.assertRaises(ValueError):
            read_symbols("f = .text:0x1000; // type:function size:0x4\ng = .text:0x1000; // type:function size:0x4",
                         [{"kind": "text", "address": 0x1000, "size": 8}])
