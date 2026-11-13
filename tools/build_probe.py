"""Build the isolated boot probe from validated, private translated functions."""
import hashlib
import json
from pathlib import Path
import subprocess
import re
import struct
import bisect
import argparse
import os
if __package__:
    from .prepare_ax import prepare as prepare_ax, write_if_changed
else:
    from prepare_ax import prepare as prepare_ax, write_if_changed
if __package__:
    from .native_config import prepare as prepare_native_config
    from .disc_layout import prepare as prepare_disc_layout
else:
    from native_config import prepare as prepare_native_config
    from disc_layout import prepare as prepare_disc_layout

def unsafe_rfi_functions(dol_bytes, addresses, metadata, source_root):
    symbols = (ROOT / "references/key/config/RK5E01/symbols.txt").read_text()
    spans = sorted((int(a,16), int(s,16), name) for name,a,s in
                   re.findall(r"^(\S+) = [^:]+:0x([0-9A-Fa-f]+);[^\n]*type:function size:0x([0-9A-Fa-f]+)", symbols, re.M))
    starts = [s[0] for s in spans]
    owners = set()
    words = struct.unpack_from(">64I", dol_bytes)
    for section in range(7):
        off, base, size = words[section], words[18+section], words[36+section]
        for rel in range(0,size,4):
            if struct.unpack_from(">I",dol_bytes,off+rel)[0] == 0x4C000064:
                idx=bisect.bisect_right(starts,base+rel)-1
                if idx>=0 and base+rel<spans[idx][0]+spans[idx][1]: owners.add(spans[idx][0])
    unsafe=set()
    for address in addresses:
        text=(source_root/metadata[address]["relativePath"]).read_text()
        inline={int(a,16) for a in re.findall(r"// inline leaf 0x([0-9A-Fa-f]+)",text)}
        if address in owners or inline & owners:
            unsafe.add(address)
    return unsafe

def reject_unsafe_rfi(dol_bytes, addresses, metadata, source_root):
    bad=unsafe_rfi_functions(dol_bytes,addresses,metadata,source_root)
    if bad: raise ValueError(f"Unsafe rfi lowering in {[hex(a) for a in sorted(bad)]}; requires native platform service or translator fix")

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = "a73cd2f5b1768268451aa28efa649bcf95461b25a481fc4b6cd053c941cf9079"
RTC_FIXTURE = "946771200"  # 2000-01-02 local civil time, explicit reproducible input.


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--explore", action="store_true", help="Build/run to inspect the next stop; does not claim checkpoint validation")
    parser.add_argument("--graphics", action="store_true", help="Build/run the native D3D12 backend")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--dsp-coefficients",type=Path,help="Local DSP coefficient file; copied only into private/boot")
    args=parser.parse_args()
    dol = ROOT / "private/extracted/RK5E01/DATA/sys/main.dol"
    if hashlib.sha256(dol.read_bytes()).hexdigest() != EXPECTED:
        raise ValueError("DOL hash mismatch")
    prepare_native_config(dol)
    prepare_disc_layout()
    ax_sources=prepare_ax(args.dsp_coefficients)
    tmd = (ROOT / "private/extracted/RK5E01/DATA/tmd.bin").read_bytes()
    if len(tmd) < 0x18C or struct.unpack_from(">I", tmd)[0] != 0x10001 or struct.unpack_from(">Q", tmd, 0x184)[0] != 0x100000038:
        raise ValueError("Expected RSA-2048 TMD requesting IOS56 for this boot profile")
    metadata = json.loads((ROOT / "private/reports/mapped-metadata.json").read_text())
    by_address = {f["entryPoint"]: f for f in metadata["functions"]}
    # Original boot/data functions only. Missing services are traps, never success stubs.
    profile = json.loads((ROOT / "config/RK5E01/probe-functions.json").read_text())
    if profile["dol_sha256"] != EXPECTED: raise ValueError("Probe profile version mismatch")
    addresses = [int(a, 16) for a in profile["functions"]]
    names = {int(line.split()[0], 16): line.split()[1] for line in
             (ROOT / "private/profiles/RK5E01.map").read_text().splitlines()}
    reject_unsafe_rfi(dol.read_bytes(), addresses, by_address, ROOT / "private/generated/mapped/functions")
    out = ROOT / "private/probe-build-input"
    out.mkdir(parents=True, exist_ok=True)
    sources = []
    declarations, rows = [], []
    for address in addresses:
        record = by_address[address]
        source = ROOT / "private/generated/mapped/functions" / record["relativePath"]
        if hashlib.sha256(source.read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError(f"Generated source differs from metadata: {source.name}")
        sources.append(source.as_posix())
        declarations.append(f'extern "C" void func_{address:08X}(CpuContext*);')
        label=json.dumps(names.get(address, f"discovered_{address:08X}"),ensure_ascii=True)
        rows.append(f'{{0x{address:08X}u, func_{address:08X}, {label}}},')
    symbol_text = (ROOT / "references/key/config/RK5E01/symbols.txt").read_text()
    bss_table = int(re.search(r"^_bss_init_info = [^:]+:0x([0-9a-fA-F]+);", symbol_text, re.M).group(1), 16)
    write_if_changed(out / "probe_registry.inc",f"static constexpr uint32_t bss_init_table = 0x{bss_table:08X}u;\n" + "\n".join(declarations) + "\nstatic const Registration registrations[] = {\n" + "\n".join(rows) + "\n};\n")
    write_if_changed(out / "sources.cmake",f'set(KEY_REGISTRY_DIR "{out.as_posix()}")\nset(KEY_GENERATED_SOURCES\n' + "".join(f'  "{p}"\n' for p in sources) + ")\n")
    build = ROOT / ("private/build/graphics" if args.graphics else "private/build/probe")
    clang = ROOT / ".tools/llvm-mingw-20261104-ucrt-x86_64/bin/clang++.exe"
    ninja = ROOT / ".tools/ninja/ninja.exe"
    runtime_dirs=[clang.parent]
    if args.graphics:
        runtime_dirs += [ROOT/"private/build/aurora/_deps/dawn_prebuilt-src/bin",
                         ROOT/"private/build/aurora/_deps/sdl3_prebuilt-src/x86_64-w64-mingw32/bin",
                         build/"_deps/png-build",build/"_deps/zlib-build"]
    os.environ["PATH"]=os.pathsep.join(map(str,runtime_dirs))+os.pathsep+os.environ["PATH"]
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                    f"-DCMAKE_CXX_COMPILER={clang.as_posix()}", f"-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}",
                    f"-DCMAKE_C_COMPILER={(clang.parent/'clang.exe').as_posix()}",
                    f"-DCMAKE_RC_COMPILER={(clang.parent/'windres.exe').as_posix()}",
                    f"-DKEY_ENABLE_GRAPHICS={'ON' if args.graphics else 'OFF'}",
                    f"-DKEY_AX_SOURCES={ax_sources.as_posix()}",
                    f"-DCMAKE_BUILD_TYPE={'Release' if args.graphics else 'Debug'}", f"-DKEY_PROBE_SOURCES={(out / 'sources.cmake').as_posix()}"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "6"], check=True)
    if not args.explore:
        subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"], check=True)
    wrong = out / "synthetic-wrong.dol"
    wrong.write_bytes(bytes(9340736))
    rejected = subprocess.run([str(build / "key-boot-probe.exe"), str(wrong), RTC_FIXTURE], capture_output=True, text=True, timeout=15)
    if rejected.returncode != 2 or "Unsupported DOL SHA-256" not in rejected.stderr:
        raise RuntimeError("Wrong-DOL rejection check failed")
    print("PASS: same-size synthetic wrong DOL rejected before guest execution")
    if not args.explore:
        print("PASS: local build and synthetic contracts; use tools/install_local.py to assemble the private GUI executable")
        return
    with (ROOT / "private/reports/boot-probe.log").open("w") as log:
        try:
            result = subprocess.run([str(build / "key-boot-probe.exe"), str(dol), RTC_FIXTURE], stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            print(f"Probe exit: {result.returncode}; see private/reports/boot-probe.log (exit 2 is a diagnostic stop, not boot success)")
        except subprocess.TimeoutExpired:
            log.write(f"TIMEOUT: probe exceeded {args.timeout} seconds\n")
            raise
    print("Exploration only: no menu/playability success asserted")
    if result.returncode not in (0,2): raise RuntimeError("Unexpected native process exit during exploration")


if __name__ == "__main__":
    main()
