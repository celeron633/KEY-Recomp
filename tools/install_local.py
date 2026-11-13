"""Assemble an explicitly private, local game executable from the native build.

This is not a public release packager. It copies only named build outputs and
runtime DLLs, and leaves the user's extracted game data in its existing location.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct

ROOT=Path(__file__).resolve().parents[1]
def main():
    dol=ROOT/'private/extracted/RK5E01/DATA/sys/main.dol'
    if hashlib.sha256(dol.read_bytes()).hexdigest()!='a73cd2f5b1768268451aa28efa649bcf95461b25a481fc4b6cd053c941cf9079':
        raise ValueError('Unsupported local game data')
    sources={
        'KEY-Recomp.exe':'private/build/graphics/key-boot-probe.exe',
        'webgpu_dawn.dll':'private/build/aurora/_deps/dawn_prebuilt-src/bin/webgpu_dawn.dll',
        'dxcompiler.dll':'private/build/aurora/_deps/dawn_prebuilt-src/bin/dxcompiler.dll',
        'dxil.dll':'private/build/aurora/_deps/dawn_prebuilt-src/bin/dxil.dll',
        'SDL3.dll':'private/build/aurora/_deps/sdl3_prebuilt-src/x86_64-w64-mingw32/bin/SDL3.dll',
        'libpng16.dll':'private/build/graphics/_deps/png-build/libpng16.dll',
        'libzlib1.dll':'private/build/graphics/_deps/zlib-build/libzlib1.dll',
    }
    exe=(ROOT/sources['KEY-Recomp.exe']).read_bytes()
    pe=struct.unpack_from('<I',exe,0x3C)[0]
    if exe[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',exe,pe+4)[0]!=0x8664 or struct.unpack_from('<H',exe,pe+24+68)[0]!=2:
        raise ValueError('Expected a Windows x64 GUI executable')
    for source in sources.values():
        if not (ROOT/source).is_file():raise FileNotFoundError(source)
    out=ROOT/'private/game';out.mkdir(parents=True,exist_ok=True)
    manifest={'purpose':'PRIVATE local build only; contains user-generated game code. Do not distribute.','files':{}}
    for name,source in sources.items():
        shutil.copyfile(ROOT/source,out/name)
        manifest['files'][name]={'source':source,'sha256':hashlib.sha256((out/name).read_bytes()).hexdigest()}
    (out/'local-build.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(out/'KEY-Recomp.exe')

if __name__=='__main__':main()
