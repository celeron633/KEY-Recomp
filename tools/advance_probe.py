"""Expand only observed, hash-verified translated functions; stop at semantic gaps."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from datetime import datetime
from build_probe import unsafe_rfi_functions

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--limit",type=int,default=20)
    parser.add_argument("--graphics",action="store_true")
    args=parser.parse_args()
    if not 1<=args.limit<=100: raise ValueError("Limit must be between 1 and 100")
    profile_path=ROOT/"config/RK5E01/probe-functions.json"
    metadata={f["entryPoint"]:f for f in json.loads((ROOT/"private/reports/mapped-metadata.json").read_text())["functions"]}
    known=set(metadata)
    dol=(ROOT/"private/extracted/RK5E01/DATA/sys/main.dol").read_bytes()
    out=ROOT/"private/reports/advance"/datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True,exist_ok=False)
    steps=[]
    for i in range(args.limit):
        with (out/f"{i:03}-build.log").open("w") as log:
            proc=subprocess.run([sys.executable,str(ROOT/"tools/build_probe.py"),"--explore"]+(["--graphics"] if args.graphics else []),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        if proc.returncode:
            steps.append({"action":"build/execution gap","log":f"{i:03}-build.log"})
            print(f"BUILD/EXECUTION GAP: {out/f'{i:03}-build.log'}",flush=True)
            break
        text=(ROOT/"private/reports/boot-probe.log").read_text()
        shutil.copyfile(ROOT/"private/reports/boot-probe.log",out/f"{i:03}-run.log")
        stop=next((line for line in reversed(text.splitlines()) if line.startswith("STOP:")),"No diagnostic stop")
        print(stop,flush=True)
        match=re.fullmatch(r"STOP: Unregistered guest function/service 0x([0-9A-Fa-f]{8})",stop)
        if not match:
            steps.append({"stop":stop,"action":"semantic review required"})
            break
        address=int(match[1],16)
        profile=json.loads(profile_path.read_text())
        current={int(a,16) for a in profile["functions"]}
        native={int(a,16) for a in profile.get("native_services",{})}
        if address not in known or address in current:
            steps.append({"stop":stop,"action":"no verified new translation"})
            break
        if i+1==args.limit:
            steps.append({"stop":stop,"action":"iteration limit; no untested edit"})
            break
        candidates={address}
        frontier={address}
        for _ in range(2):
            frontier={callee for caller in frontier if caller not in native
                      for callee in metadata[caller]["build"]["directCallDependencies"] if callee in known}-candidates
            candidates.update(frontier)
        candidates-=native|current
        unsafe=unsafe_rfi_functions(dol,candidates,metadata,ROOT/"private/generated/mapped/functions")
        if address in unsafe or address in native:
            steps.append({"stop":stop,"action":"native/control-flow review required"})
            break
        candidates-=unsafe
        current.update(candidates)
        profile["functions"]=[f"0x{a:08X}" for a in sorted(current)]
        profile_path.write_text(json.dumps(profile,indent=2)+"\n")
        steps.append({"stop":stop,"action":"added verified translation and two levels of direct dependencies","added":[f"0x{a:08X}" for a in sorted(candidates)],"excluded_unsafe":[f"0x{a:08X}" for a in sorted(unsafe)]})
        (out/"steps.json").write_text(json.dumps(steps,indent=2)+"\n")
    (out/"steps.json").write_text(json.dumps(steps,indent=2)+"\n")
    print(f"Evidence: {out}",flush=True)


if __name__=="__main__": main()
