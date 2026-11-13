"""Create local, strict WiiCompiled experiments from a hash-checked DOL."""
import json
from pathlib import Path
import re
import struct
import subprocess

if __package__:
    from .prepare import ROOT, EXPECTED_SHA1, digest, inspect_dol
else:
    from prepare import ROOT, EXPECTED_SHA1, digest, inspect_dol


def read_symbols(text, sections):
    result = {}
    for line in text.splitlines():
        if "type:function" not in line:
            continue
        match = re.match(r"(\S+) = [^:]+:0x([0-9a-fA-F]+);.*?size:0x([0-9a-fA-F]+)", line)
        if not match:
            raise ValueError(f"Unrecognized function symbol: {line}")
        name, address, size = match.groups()
        address, size = int(address, 16), int(size, 16)
        if address % 4 or size % 4 or not size or not any(
            s["kind"] == "text" and s["address"] <= address and address + size <= s["address"] + s["size"]
            for s in sections
        ):
            raise ValueError(f"Function outside executable sections: {name}")
        if address in result and result[address] != (name, size):
            raise ValueError(f"Duplicate function address: {address:08X}")
        result[address] = (name, size)
    if not result:
        raise ValueError("No function symbols")
    return result


def decode_init(data):
    """Narrow straight-line startup evaluator; unknown instructions fail closed."""
    registers = {}
    for (word,) in struct.iter_unpack(">I", data):
        if word == 0x4E800020:  # blr
            if not all(r in registers for r in (2, 13)):
                raise ValueError("SDA registers not initialized")
            return registers[2], registers[13]
        op, rt, ra, imm = word >> 26, (word >> 21) & 31, (word >> 16) & 31, word & 65535
        signed = imm if imm < 32768 else imm - 65536
        if op in (14, 15) and (ra == 0 or ra in registers):
            registers[rt] = ((registers.get(ra, 0) if ra else 0) + (signed << (16 if op == 15 else 0))) & 0xFFFFFFFF
        elif op == 24 and rt in registers:
            registers[ra] = registers[rt] | imm
        else:
            raise ValueError("Unrecognized startup instruction; inspect before extending evaluator")
    raise ValueError("Startup function has no return")


def main():
    lock = json.loads((ROOT / "references.lock.json").read_text())
    for repository in lock["repositories"]:
        actual = subprocess.check_output(["git", "-C", str(ROOT / repository["path"]), "rev-parse", "HEAD"], text=True).strip()
        if actual != repository["commit"]:
            raise ValueError(f"Reference commit mismatch: {repository['name']}")
    dol = ROOT / "private/extracted/RK5E01/DATA/sys/main.dol"
    if digest(dol, "sha1") != EXPECTED_SHA1:
        raise ValueError("DOL version mismatch")
    data = dol.read_bytes()
    layout = inspect_dol(data)
    symbols = read_symbols((ROOT / "references/key/config/RK5E01/symbols.txt").read_text(), layout["sections"])
    init = [(a, size) for a, (name, size) in symbols.items() if name == "__init_registers"]
    if len(init) != 1:
        raise ValueError("Expected one __init_registers")
    address, size = init[0]
    section = next(s for s in layout["sections"] if s["address"] <= address < s["address"] + s["size"])
    offset = section["offset"] + address - section["address"]
    r2, r13 = decode_init(data[offset:offset + size])
    target = ROOT / "private/profiles"
    target.mkdir(parents=True, exist_ok=True)
    (ROOT / "private/empty-native").mkdir(exist_ok=True)
    (target / "RK5E01.map").write_text("".join(f"{a:08X} {name}\n" for a, (name, _) in sorted(symbols.items())))
    for mode in ("leaf", "entry", "mapped"):
        profile = f'''schema_version: 1
workspace_root: ../..
project:
  id: key-rk5e01-{mode}
  display_name: KEY RK5E01 feasibility {mode}
  game_id: RK5E01
memory:
  base: 0x80000000
  size: 0x01800000
  sda_base: 0x{r13:08X}
  sda2_base: 0x{r2:08X}
inputs:
  dol:
    path: private/extracted/RK5E01/DATA/sys/main.dol
    sha256: {digest(dol)}
translation:
  allow_unsupported_instructions: false
'''
        if mode == "mapped":
            profile += "  function_map:\n    path: private/profiles/RK5E01.map\n"
        profile += f'''runtime:
  native_registration_root: private/empty-native
  native_abi_directories: []
output:
  root: private/generated/{mode}
  functions: functions
  runtime_config: RuntimeConfig.h
  data_initializer: data_sections_init.cpp
  base_manifest: base/base_manifest.json
'''
        (target / f"{mode}.yml").write_text(profile, encoding="utf-8")
    report = {"symbol_functions": len(symbols), "sda2_r2": f"0x{r2:08X}", "sda_r13": f"0x{r13:08X}",
              "entry": f"0x{layout['entry_point']:08X}", "init_registers": f"0x{address:08X}",
              "scope": "Static startup analysis only; MEM2/runtime and indirect call coverage unverified"}
    (ROOT / "private/reports/profile.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
