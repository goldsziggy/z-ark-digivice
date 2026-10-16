#!/usr/bin/env python3
"""Regression gate for selected compiled Xtensa main-task call paths.

These are entry-frame subtotals, not whole-program worst-case bounds. Deeper
library/virtual calls, interrupts and spills still require hardware high-water
observation. Requires pyelftools from the existing ESP-IDF Python environment.
"""
import argparse,json,subprocess
from elftools.elf.elffile import ELFFile
from pathlib import Path

def analyze(path,demangler):
    with path.open('rb') as stream:
        elf=ELFFile(stream)
        symbols=[s for s in elf.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and s['st_size']]
        names=subprocess.run([demangler],input='\n'.join(s.name for s in symbols),capture_output=True,text=True,check=True).stdout.splitlines()
        assert len(names)==len(symbols)
        frames={}
        for symbol,name in zip(symbols,names):
            if not isinstance(symbol['st_shndx'],int):continue
            section=elf.get_section(symbol['st_shndx']);offset=symbol['st_value']-section['sh_addr']
            code=section.data()[offset:offset+3];word=int.from_bytes(code,'little')
            if len(code)==3 and word&0xff==0x36:
                frames[name]={'address':hex(symbol['st_value']),'frame_bytes':(word>>12)*8}
    selected={}
    def pick(key,needle):
        matches=[(name,item) for name,item in frames.items() if needle in name and 'lambda' not in name]
        assert len(matches)==1,(key,matches)
        name,item=matches[0];selected[key]={'function':name,**item};return key
    specs={
      'main':'app_main','console':'readLine(', 'poll':'HandheldRuntime::poll()',
      'ui':'HandheldRuntime::pollInterface(', 'care_auto':'HandheldRuntime::pollCareAndAuto(',
      'care_minute':'HandheldRuntime::pollCareMinute(', 'auto_ready':'deviceui::autoFightReady(',
      'intent':'HandheldRuntime::interfaceIntent(', 'live_apply':'livebattle::applyAction(',
      'step':'digivice::applyAutoStep(', 'engine':'anonymous namespace)::autoEngine(',
      'apply':'digivice::apply(digivice::State', 'valid':'digivice::isValid(digivice::State',
      'nfc_poll':'nfcdiag::Device::poll(', 'nfc_test':'nfcdiag::Device::pollTest(',
      'session_poll':'nfcdiag::Session::poll(', 'nfc_status':'nfcdiag::Device::status()',
      'render':'deviceui::Controller::render(',
    }
    for key,needle in specs.items():pick(key,needle)
    common=['main','console','poll','ui','care_auto']
    paths={
      'current_auto_readiness':common+['auto_ready','step','engine','apply','valid'],
      'legacy_auto_readiness':common+['auto_ready','apply','engine','apply','valid'],
      'current_auto_dispatch':common+['intent','live_apply','step','engine','apply','valid'],
      'care_commit':common+['care_minute','apply','valid'],
      'nfc_session_poll':['main','console','poll','nfc_poll','nfc_test','session_poll'],
      'nfc_status':['main','console','poll','nfc_poll','nfc_test','nfc_status'],
      'nfc_render':['main','console','poll','ui','render'],
    }
    totals={name:sum(selected[key]['frame_bytes'] for key in path) for name,path in paths.items()}
    assert selected['care_auto']['frame_bytes']<=2048,'Care State leaked into Auto dispatcher frame'
    assert selected['care_minute']['frame_bytes']>=12000,'Expected isolated care State frame not found'
    assert totals['current_auto_readiness']<=44*1024,totals
    assert totals['current_auto_dispatch']<=44*1024,totals
    assert totals['legacy_auto_readiness']<=56*1024,totals
    assert totals['care_commit']<=30*1024,totals
    assert totals['nfc_session_poll']<=2048 and totals['nfc_status']<=2048 and totals['nfc_render']<=4096,totals
    return dict(elf=str(path),configured_stack_bytes=65536,selected_frames=selected,paths=paths,frame_subtotals=totals,
      scope='Static entry-frame subtotals only; deeper callbacks/library/interrupt/spill usage not included. Hardware high-water and stable Auto boot still required.',passed=True)
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf',type=Path);parser.add_argument('--demangler',required=True);args=parser.parse_args()
    print(json.dumps(analyze(args.elf,args.demangler),indent=2))
