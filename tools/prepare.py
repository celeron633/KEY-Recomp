"""Local-only disc extraction and DOL inspection; requires Python 3.10+."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA1 = "f4280a2b0709bfde4132be22e9f7fa8ecfa2a130"


def digest(path: Path, algorithm: str = "sha256") -> str:
    h = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def inspect_dol(data: bytes) -> dict:
    if len(data) < 0x100:
        raise ValueError("Truncated DOL header")
    words = struct.unpack_from(">64I", data)
    sections = []
    for i in range(18):
        offset, address, size = words[i], words[18 + i], words[36 + i]
        if not size:
            continue
        if offset < 0x100 or offset + size > len(data):
            raise ValueError("DOL section outside file")
        if address + size > 0x100000000:
            raise ValueError("DOL address overflow")
        for section in sections:
            if max(address, section["address"]) < min(address + size, section["address"] + section["size"]):
                raise ValueError("Overlapping DOL memory sections")
            if max(offset, section["offset"]) < min(offset + size, section["offset"] + section["size"]):
                raise ValueError("Overlapping DOL file sections")
        sections.append(dict(kind="text" if i < 7 else "data", index=i if i < 7 else i - 7,
                             offset=offset, address=address, size=size))
    bss, bss_size, entry = words[54:57]
    if bss + bss_size > 0x100000000:
        raise ValueError("BSS address overflow")
    # The DOL BSS envelope may span initialized sections (including this game).
    # A loader must preserve those sections when clearing BSS.
    overlaps = [s["index"] for s in sections if s["kind"] == "data" and bss_size
                and max(bss, s["address"]) < min(bss + bss_size, s["address"] + s["size"])]
    if entry % 4 or not any(s["kind"] == "text" and s["address"] <= entry < s["address"] + s["size"] for s in sections):
        raise ValueError("Entry point not in executable section")
    return dict(entry_point=entry, bss_address=bss, bss_size=bss_size,
                bss_overlapping_data_indices=overlaps, sections=sections)


def run(*args: str) -> str:
    result = subprocess.run(args, check=True, capture_output=True, encoding="utf-8", errors="replace")
    return result.stdout


def prepare(image: Path, dolphin: Path, reuse: bool = False) -> dict:
    header = json.loads(run(str(dolphin), "header", "-i", str(image), "-j"))
    if header.get("game_id") != "RK5E01" or header.get("revision") != 0:
        raise ValueError("Only RK5E01 revision 0 is supported")
    destination = ROOT / "private/extracted/RK5E01"
    if not reuse:
        if destination.exists():
            raise ValueError("Extraction directory already exists; use --reuse to inspect it without overwriting")
        destination.parent.mkdir(parents=True, exist_ok=True)
        run(str(dolphin), "extract", "-i", str(image), "-o", str(destination), "-g", "-q")
    candidates = [destination / "DATA/sys/main.dol", destination / "sys/main.dol"]
    dols = [p for p in candidates if p.is_file()]
    if len(dols) != 1:
        raise ValueError("Expected exactly one extracted main.dol; extraction may be incomplete")
    dol = dols[0]
    sha1 = digest(dol, "sha1")
    if sha1 != EXPECTED_SHA1:
        raise ValueError(f"DOL does not match reference RK5E01 Rev 0: SHA-1 {sha1}")
    files = sorted(p for p in destination.rglob("*") if p.is_file())
    report = dict(schema_version=1, disc=header, extraction_reused=reuse,
                  note="Reuse validates DOL only; full filesystem integrity requires fresh extraction and disc verification.",
                  dol_path=dol.relative_to(ROOT).as_posix(), dol_sha1=sha1,
                  dol_sha256=digest(dol), dol=inspect_dol(dol.read_bytes()),
                  file_count=len(files), total_bytes=sum(p.stat().st_size for p in files),
                  rel_files=[p.relative_to(destination).as_posix() for p in files if p.suffix.lower() == ".rel"])
    output = ROOT / "private/reports/inventory.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--dolphin-tool", type=Path, default=Path(r"D:\Program Files\Dolphin-x64\DolphinTool.exe"))
    parser.add_argument("--reuse", action="store_true")
    args = parser.parse_args()
    try:
        result = prepare(args.image.resolve(), args.dolphin_tool.resolve(), args.reuse)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Preparation failed: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
