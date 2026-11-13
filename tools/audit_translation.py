"""Audit local translation metadata; command success alone is not readiness."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    reports = ROOT / "private/reports"
    metadata = json.loads((reports / "mapped-metadata.json").read_text())
    functions = metadata["functions"]
    addresses = {f["entryPoint"] for f in functions}
    symbols = {}
    for line in (ROOT / "private/profiles/RK5E01.map").read_text().splitlines():
        address, name = line.split(maxsplit=1)
        symbols[int(address, 16)] = name
    missing = sorted(symbols.keys() - addresses)
    unresolved = {}
    for function in functions:
        for target in function["build"]["directCallDependencies"]:
            if target not in addresses:
                unresolved.setdefault(f"0x{target:08X}", []).append({
                    "address": f"0x{function['entryPoint']:08X}",
                    "name": symbols.get(function["entryPoint"], "unknown")})
    indirect = []
    pattern = re.compile(r"\b(?:InvokeIndirect\w*|InvokeDirectCpu)\s*[<(]")
    # Dynamic dispatch inventory only; this does not prove any target is resolved.
    for function in functions:
        path = ROOT / "private/generated/mapped/functions" / function["relativePath"]
        text = path.read_text(encoding="utf-8")
        sites = [line.strip() for line in text.splitlines() if "InvokeIndirect" in line and pattern.search(line)]
        if sites:
            indirect.append({"address": f"0x{function['entryPoint']:08X}",
                             "name": symbols.get(function["entryPoint"], "unknown"), "site_count": len(sites)})
    log = (reports / "mapped-translation.log").read_text(encoding="utf-8")
    report = {"input_symbols": len(symbols), "translated_functions": len(functions),
              "additional_starts": len(addresses - symbols.keys()),
              "missing_symbols": [f"0x{a:08X}" for a in missing], "quality": metadata["quality"],
              "unresolved_direct_targets": unresolved,
              "skip_messages": [line for line in log.splitlines() if "skipped map entry" in line or "Skipped " in line],
              "indirect_dispatch_functions": len(indirect), "indirect_dispatch_sites": sum(f["site_count"] for f in indirect),
              "indirect_inventory": indirect,
              "runtime_ready": False,
              "limits": "Code generation only; runtime services, indirect targets, floating-point semantics and full compilation unverified."}
    (reports / "translation-audit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "indirect_inventory"}, indent=2))


if __name__ == "__main__":
    main()
