"""Generate local user preferences; derive the product-format seed from the owned DOL."""
from pathlib import Path
import struct
import hashlib

ROOT = Path(__file__).resolve().parents[1]


def product_seed(dol: bytes) -> int:
    if hashlib.sha256(dol).hexdigest() != "a73cd2f5b1768268451aa28efa649bcf95461b25a481fc4b6cd053c941cf9079":
        raise ValueError("Unsupported DOL for local product settings derivation")
    h = struct.unpack_from(">64I", dol)
    address = 0x806B0F20  # Product settings parser in this hash-pinned executable.
    i = next(i for i in range(7) if h[18+i] <= address < h[18+i]+h[36+i])
    off = h[i] + address - h[18+i]
    hi, lo = struct.unpack_from(">I", dol, off)[0], struct.unpack_from(">I", dol, off+8)[0]
    if hi >> 26 != 15 or ((hi >> 21) & 31) != 6 or ((hi >> 16) & 31) != 0 or lo >> 26 != 14 or ((lo >> 21)&31) != 11 or ((lo >> 16)&31) != 6:
        raise ValueError("Product decoder constant pattern changed")
    signed = (lo & 0xFFFF) if not lo & 0x8000 else (lo & 0xFFFF)-0x10000
    return ((hi & 0xFFFF)*65536 + signed) & 0xFFFFFFFF


def encode_settings(lines: list[str], seed: int) -> bytes:
    out = bytearray()
    def encode(data, key):
        result = bytearray()
        for byte in data:
            result.append(byte ^ (key & 255))
            key = ((key << 1) | (key >> 31)) & 0xFFFFFFFF
        return result, key
    for line in lines:
        raw = (line + "\r\n").encode("ascii")
        for attempt in range(32):
            block, next_seed = encode(raw, seed)
            if 0 not in block:
                out.extend(block); seed = next_seed; break
            padding, seed = encode(b"\n", seed)
            out.extend(padding)
        else:
            raise ValueError("Cannot encode settings line without NUL")
    if len(out) > 256:
        raise ValueError("Settings exceeded format size")
    return bytes(out).ljust(256, b"\0")


def sysconf() -> bytes:
    items = [("IPL.LNG", 1), ("IPL.AR", 1), ("IPL.SND", 1), ("IPL.PGS", 0), ("IPL.E60", 0)]
    start = 6 + (len(items)+1)*2
    entries, offsets = bytearray(), []
    for name, value in items:
        offsets.append(start + len(entries))
        encoded = name.encode("ascii")
        entries += bytes([(3 << 5) | (len(encoded)-1)]) + encoded + bytes([value])
    offsets.append(start+len(entries))
    data = b"SCv0" + struct.pack(">H", len(items)) + struct.pack(">"+"H"*len(offsets), *offsets) + entries
    return data.ljust(0x3FFC, b"\0") + b"SCed"


def prepare(dol_path: Path) -> None:
    root = ROOT / "private/user/nand"
    config = root / "shared2/sys/SYSCONF"
    product = root / "title/00000001/00000002/data/setting.txt"
    for path in (config, product): path.parent.mkdir(parents=True, exist_ok=True)
    if not config.exists(): config.write_bytes(sysconf())
    if not product.exists():
        product.write_bytes(encode_settings([
            "AREA=USA", "MODEL=RVL-001(USA)", "DVD=0", "MPCH=0x7FFE", "CODE=LU",
            "SERNO=000000001", "VIDEO=NTSC", "GAME=US"], product_seed(dol_path.read_bytes())))


if __name__ == "__main__":
    prepare(ROOT / "private/extracted/RK5E01/DATA/sys/main.dol")
