"""Create a synthetic PPC add function; no game bytes required."""
import hashlib
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def main():
    directory = ROOT / "private/synthetic"
    directory.mkdir(parents=True, exist_ok=True)
    (ROOT / "private/reports").mkdir(parents=True, exist_ok=True)
    data = bytearray(0x108)
    for offset, value in [(0, 0x100), (0x48, 0x80004000), (0x90, 8), (0xE0, 0x80004000),
                          (0x100, 0x7C632214), (0x104, 0x4E800020)]:
        struct.pack_into(">I", data, offset, value)
    (directory / "add.dol").write_bytes(data)
    (directory / "recomp.yml").write_text(f'''schema_version: 1
workspace_root: ../..
project:
  id: synthetic-add
memory:
  base: 0x80000000
  size: 0x01800000
  sda_base: 0x80008000
  sda2_base: 0x80008000
inputs:
  dol:
    path: private/synthetic/add.dol
    sha256: {hashlib.sha256(data).hexdigest()}
translation:
  allow_unsupported_instructions: false
runtime:
  native_registration_root: private/empty-native
  native_abi_directories: []
output:
  root: private/synthetic/generated
''')
    (ROOT / "private/empty-native").mkdir(exist_ok=True)


if __name__ == "__main__":
    main()
