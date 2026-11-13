import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import disc_layout


class DiscLayoutTests(unittest.TestCase):
    def fixture(self, root):
        (root/"asset").mkdir()
        data=bytearray(4*1024*1024)
        data[:4]=b"WBFS"
        struct.pack_into(">I",data,4,len(data)//512)
        data[8:10]=bytes([9,21])
        data[12]=1
        struct.pack_into(">H",data,512+256,1)
        start=2*1024*1024
        data[start:start+6]=b"RK5E01" # Identifier only; rest of the disc is synthesized.
        struct.pack_into(">II",data,start+0x40000,1,0x40020//4)
        struct.pack_into(">II",data,start+0x40020,0x50000//4,0)
        struct.pack_into(">II",data,start+0x502B8,0x8000//4,0x100000//4)
        path=root/"asset/synthetic.wbfs"
        path.write_bytes(data)
        return path

    def test_metadata_extraction_without_partition_keys(self):
        base=Path(__file__).resolve().parents[1]/"private/tests"
        base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temp:
            root=Path(temp).resolve()
            self.assertTrue(root.is_relative_to(base.resolve()))
            image=self.fixture(root)
            before=image.read_bytes()
            with patch.object(disc_layout,"ROOT",root): disc_layout.prepare()
            self.assertEqual(image.read_bytes(),before)
            geometry=struct.unpack(">4sIQQ",(root/"private/boot/geometry.bin").read_bytes())
            self.assertEqual(geometry,(b"KRD0",1,4699979776,0x50000))
            self.assertEqual((root/"private/boot/system-area.bin").stat().st_size,0x50000)

    def test_missing_metadata_block_rejected(self):
        base=Path(__file__).resolve().parents[1]/"private/tests"
        base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temp:
            root=Path(temp).resolve()
            self.assertTrue(root.is_relative_to(base.resolve()))
            image=self.fixture(root)
            with image.open("r+b") as f:
                f.seek(512+256); f.write(b"\0\0")
            with patch.object(disc_layout,"ROOT",root), self.assertRaises(ValueError): disc_layout.prepare()
