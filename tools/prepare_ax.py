"""Prepare a private, address-independent AX mixer from the pinned GPL reference.

Only generic DSP algorithms are selected. No MKW addresses, HLE stubs, game
program, firmware or coefficient bytes enter the public project source.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import shutil

ROOT=Path(__file__).resolve().parents[1]
REF=ROOT/'references/WiiCompiled'
COMMIT='ffbde831317c2a3fc7841f53e7244db83f024ff2'
NOTICE='// Derived from patchzyy/Wiicompiled '+COMMIT+' (GPL-3.0).\n// Adapted by KEY-Recomp: synchronous checked memory, fixed verified AX version, strict failures.\n'
def write_if_changed(path,text):
    if not path.exists() or path.read_text()!=text:path.write_text(text)

def method(source,name):
    # Bound selection to a class-level method and balance braces after masking
    # strings/comments. This importer deliberately supports only the pinned source.
    match=re.search(r'^    (?![ \t])(?:static )?[^\n{};]*\b'+name+r'\([^;]*?\)\s*(?:const\s*)?\{',source,re.M)
    if not match: raise ValueError('Missing AX method '+name)
    start=match.start();brace=source.index('{',match.start(),match.end());depth=0
    masked=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),source)
    for end in range(brace,len(source)):
        if masked[end]=='{':depth+=1
        if masked[end]=='}':
            depth-=1
            if not depth:return source[start:end+1]
    raise ValueError('Unbalanced AX method '+name)

COEFFICIENT_SHA256='d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179'
def prepare(coefficient_source=None):
    actual=subprocess.check_output(['git','-C',str(REF),'rev-parse','HEAD'],text=True).strip()
    if actual!=COMMIT:raise ValueError('AX reference revision mismatch')
    directory=REF/'runtime/src/hle/audio'
    names=['ax_internal.h','ax_mix.cpp','ax_mix_kernels.h']
    for name in names:
        expected=subprocess.check_output(['git','-C',str(REF),'show',f'{COMMIT}:runtime/src/hle/audio/{name}'])
        if expected.replace(b'\r\n',b'\n')!=(directory/name).read_bytes().replace(b'\r\n',b'\n'):
            raise ValueError('Modified AX reference '+name)
    out=ROOT/'private/generated/ax';out.mkdir(parents=True,exist_ok=True)
    coefficients=ROOT/'private/boot/dsp_coef.bin'
    local_source=Path(coefficient_source) if coefficient_source else Path('D:/Program Files/Dolphin-x64/Sys/GC/dsp_coef.bin')
    candidate=coefficients if coefficients.exists() else local_source
    if len(candidate.read_bytes())!=4096 or hashlib.sha256(candidate.read_bytes()).hexdigest()!=COEFFICIENT_SHA256:
        raise ValueError('Unexpected local DSP coefficient table; no table is bundled or downloaded')
    if not coefficients.exists():
        coefficients.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(candidate,coefficients)
    original=(directory/'ax_internal.h').read_text()
    types=original[original.index('enum class AXCommandLayout'):]
    write_if_changed(out/'ax_types.h',NOTICE+'#pragma once\n#include "ax_adapter.h"\nnamespace AxDspHle {\n'+types)
    write_if_changed(out/'ax_mix_kernels.h',NOTICE+(directory/'ax_mix_kernels.h').read_text())
    source=(directory/'ax_mix.cpp').read_text()
    methods='DecodeCommand UsesStashedPbAddress OutputCarriesVolume CanReadWords ScanCommandList ExecuteCommandList ReadAddress MixBuffers SetupProcessing AddToLR AddSubToLR ConvertMixerControl GenerateVolumeRamp ProcessPBList BaseCoefficients VoiceCoefficients LoadPBUpdates ApplyPBUpdatesForMs AdvanceBuffers CopyGuestToStruct CopyStructToGuest PBSegmentsForLayout ReadPBWithLayout WritePBWithLayout RunCompressor ProcessVoice ResampleAudio Resample MixAdd LowPassFilter BiquadFilter MixAUXSamples UploadAUXMixLRSC OutputSamples OutputWMSamples'.split()
    parts=[]
    for name in methods:
        body=method(source,name)
        if name=='ResampleAudio':body='    template<typename Reader>\n'+body
        if name=='ExecuteCommandList':body=body.replace('        m_auxOutCount = 0;\n','')
        if name=='ProcessPBList':
            body=body.replace('while (pbAddr && guard++ < 256) {','while (pbAddr) {\n            if(++guard>256)throw std::runtime_error("Cyclic/oversized AX PB chain");')
        if name=='VoiceCoefficients':body=body.replace('return nullptr;','throw std::runtime_error("Invalid AX coefficient bank");')
        if name=='ProcessVoice':body=body.replace('        Accelerator accel;','        if(pb.src_type>2 || pb.coef_select>3 || (pb.audio_addr.sample_format!=0 && pb.audio_addr.sample_format!=10 && pb.audio_addr.sample_format!=25))throw std::runtime_error("Unsupported AX voice encoding");\n        Accelerator accel;')
        if name=='ApplyPBUpdatesForMs':body=body.replace('                continue;','                throw std::runtime_error("AX PB update offset out of range");')
        parts.append(body)
    enum_start=source.index('    enum Cmd :');enum_end=source.index('    static DecodedCommand DecodeCommand',enum_start)
    declarations=source[enum_start:enum_end]
    generated=NOTICE+'#include "ax_processor.h"\n#include "ax_adapter.h"\n#include "ax_types.h"\n#include "ax_mix_kernels.h"\nnamespace AxDspHle {\nclass Processor {\npublic:\n'+declarations+'''
    struct CommandListScan {uint32_t outputCommands=0,auxInputCount=0;std::array<uint32_t,24> auxInputAddrs{};};
    struct PBSegment {size_t guestOffset,structOffset,byteCount;};
    uint32_t m_cmdListSize=0,m_compressorPos=0,m_ucodeCrc=0x4cc52064;
    PBLayout m_pbLayout=PBLayout::SkipUpdatesOnly;
    bool m_hasCoeffs=true;
    std::array<int16_t,0x800> m_coeffs{};
    std::array<uint16_t,512> m_cmdList{};
    std::array<std::array<int,96>,kAxRegularBusCount> m_bus{};
    std::array<std::array<int,18>,kAxWiimoteBusCount> m_wmBus{};
    uint16_t m_lastMainVolume=0x8000;
    std::array<uint16_t,3> m_lastAuxVolumes{0x8000,0x8000,0x8000};
    void ReadPB(uint32_t addr,AXPBWii& pb) {ReadPBWithLayout(addr,pb,m_pbLayout);}
    void WritePB(uint32_t addr,const AXPBWii& pb)const {WritePBWithLayout(addr,pb,m_pbLayout);}
    void WriteAuxOutBuffer(uint32_t a,const int* b,uint32_t n){WriteGuestS32Buffer(a,b,n);}
    void ReadAuxInBuffer(uint32_t a,int* b,uint32_t n){ReadGuestS32Buffer(a,b,n);}
    [[noreturn]] void ReportUnknownCommand(uint16_t,uint32_t){throw std::runtime_error("Unknown AX command");}
'''+ '\n\n'.join(parts)+'''
};
static Processor processor;
void initialize_processor(std::span<const int16_t> coefficients) {
    if(coefficients.size()!=0x800)throw std::runtime_error("Invalid DSP coefficient extent");
    processor=Processor{};std::copy(coefficients.begin(),coefficients.end(),processor.m_coeffs.begin());
}
unsigned process_commands(uint32_t address,uint32_t words) {
    if(!words || words>512)throw std::runtime_error("Invalid AX command-list extent");
    key::memory().gpu_view(address,words*2);
    processor.m_cmdListSize=words;
    for(uint32_t i=0;i<words;++i)processor.m_cmdList[i]=MixRead16(address+i*2);
    Processor::CommandListScan scan;
    if(!processor.ScanCommandList(AXCommandLayout::New,&scan))throw std::runtime_error("Malformed AX command list");
    processor.ExecuteCommandList(AXCommandLayout::New,words);
    return scan.outputCommands;
}
}
'''
    if 'MKW_GADDR' in generated or 'RuntimeConfigFile' in generated:raise ValueError('Game-specific dependency escaped AX selection')
    write_if_changed(out/'ax_processor.cpp',generated)
    (out/'source-manifest.json').write_text(json.dumps({'commit':COMMIT,'license':'GPL-3.0','source_sha256':{n:hashlib.sha256((directory/n).read_bytes()).hexdigest() for n in names}},indent=2)+'\n')
    return out

if __name__=='__main__':print(prepare())
