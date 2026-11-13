import struct
import unittest
from tools.native_config import encode_settings, product_seed, sysconf


class NativeConfigTests(unittest.TestCase):
    def test_sysconf_structure_and_user_defaults(self):
        data=sysconf()
        self.assertEqual(len(data),0x4000)
        self.assertEqual(data[:4],b"SCv0")
        self.assertEqual(data[-4:],b"SCed")
        count=struct.unpack_from(">H",data,4)[0]
        offsets=struct.unpack_from(">"+"H"*(count+1),data,6)
        self.assertEqual(list(offsets),sorted(set(offsets)))
        decoded={}
        for p,end in zip(offsets,offsets[1:]):
            descriptor=data[p]
            self.assertEqual(descriptor>>5,3)
            n=(descriptor&31)+1
            self.assertEqual(p+1+n+1,end)
            decoded[data[p+1:p+1+n].decode()]=data[p+1+n]
        self.assertEqual(decoded["IPL.LNG"],1)
        self.assertEqual(decoded["IPL.AR"],1)

    def test_synthetic_format_seed_round_trip(self):
        seed=0x12345678  # Arbitrary synthetic test value, not game/console data.
        output=encode_settings(["AREA=TEST","VIDEO=TEST"],seed)
        decoded=bytearray()
        for b in output:
            decoded.append(b^(seed&255) if b else 0)
            seed=((seed<<1)|(seed>>31))&0xffffffff
        self.assertIn(b"AREA=TEST\r\n",decoded)
        self.assertIn(b"VIDEO=TEST\r\n",decoded)
        self.assertEqual(len(output),256)

    def test_wrong_executable_refuses_derivation(self):
        with self.assertRaises(ValueError): product_seed(b"synthetic")

    def test_oversized_setting_rejected(self):
        with self.assertRaises(ValueError): encode_settings(["X="+"a"*300],0x12345678)
