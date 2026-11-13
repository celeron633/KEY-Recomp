"""Run three strict translation experiments, retaining failures as evidence."""
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    dotnet = ROOT / ".tools/dotnet/dotnet.exe"
    cli = ROOT / "references/WiiCompiled/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"
    logs = ROOT / "private/reports"
    logs.mkdir(parents=True, exist_ok=True)
    if any((ROOT / f"private/generated/{mode}").exists() for mode in ("leaf", "entry", "mapped")):
        raise ValueError("Existing experiment output: preserve it elsewhere before a fresh run; no stale results accepted")
    results = []
    for mode, address in [("leaf", "0x80004040"), ("entry", "0x80004050"), ("mapped", "0x80004050")]:
        profile = ROOT / f"private/profiles/{mode}.yml"
        command = [str(dotnet), str(cli), "translate-recursive", address, "--project", str(profile),
                   "--threads", "4", "--output-metadata", str(logs / f"{mode}-metadata.json")]
        started = time.monotonic()
        with (logs / f"{mode}-translation.log").open("w", encoding="utf-8") as stream:
            completed = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        results.append({"mode": mode, "exit_code": completed.returncode,
                        "seconds": round(time.monotonic() - started, 2),
                        "cpp_files": len(list((ROOT / f"private/generated/{mode}").rglob("*.cpp")))})
        (logs / "translation-runs.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps(results[-1]), flush=True)
    # This is an experiment runner, never a release gate. Even exit 0 requires skip/coverage review.
    if any(result["exit_code"] for result in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
