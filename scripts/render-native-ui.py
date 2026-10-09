#!/usr/bin/env python3
"""Render actual firmware screens from a verified local pack; outputs stay private.
Requires existing CMake/C++/libjpeg and macOS sips/Swift. Installs nothing.
"""
import argparse, hashlib, html, json, os, pathlib, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pack',required=True,type=pathlib.Path)
p.add_argument('--manifest',required=True,type=pathlib.Path)
p.add_argument('--output',required=True,type=pathlib.Path)
p.add_argument('--battle-sequences',action='store_true',help='Replay committed native turns with exact local sprites')
a=p.parse_args(); repo=pathlib.Path(__file__).resolve().parents[1]
pack=a.pack.resolve(); out=a.output.resolve()
if out==repo or repo in out.parents: raise SystemExit('Private output must be outside the repository')
os.umask(0o077);out.mkdir(parents=True,exist_ok=True,mode=0o700);out.chmod(0o700)
manifest=json.loads(a.manifest.read_text()); verified=[]
for item in manifest['files']:
 name=item['target']
 if pathlib.Path(name).name!=name: raise SystemExit('Unsafe pack target')
 file=pack/name
 if file.is_symlink() or not file.is_file(): raise SystemExit('Pack entry unavailable')
 digest=hashlib.sha256(file.read_bytes()).hexdigest()
 if file.stat().st_size!=item['bytes'] or digest!=item['sha256']: raise SystemExit('Pack digest mismatch: '+name)
 verified.append({'name':name,'sha256':digest})
build=repo/'build'/'native-ui-audit'
def run(args):
 r=subprocess.run([str(x) for x in args],cwd=repo,capture_output=True,text=True)
 with (out/'build.log').open('a') as log: log.write(r.stdout+r.stderr)
 if r.returncode: raise SystemExit(r.stderr or r.stdout)
 return r
run(['cmake','-S',repo,'-B',build,'-DBUILD_TESTING=OFF'])
run(['cmake','--build',build,'--target','digivice-game','digivice-nearby','-j','4'])
sources=['scripts/render-native-ui.cpp','firmware/runtime/device_ui.cpp','firmware/runtime/battle_presentation.cpp','firmware/runtime/setup_ui.cpp','firmware/runtime/network.cpp','firmware/runtime/starter.cpp','firmware/runtime/evolution_choice.cpp','firmware/runtime/sprite.cpp','firmware/runtime/background_decode.cpp']
compiler=['c++','-std=c++17','-Wall','-Wextra','-Wpedantic','-Werror','-Icore','-Ifirmware/runtime','-I/opt/homebrew/include']
run(compiler+sources+[build/'libdigivice-game.a',build/'libdigivice-nearby.a',build/'libdigivice-combat.a','-L/opt/homebrew/lib','-ljpeg','-o',build/'render-native-ui'])
r=run([build/'render-native-ui',pack,out]+(['--battle-sequences'] if a.battle_sequences else []))
for file in out.glob('*.ppm'):run(['sips','-s','format','png',file,'--out',file.with_suffix('.png')])
run(['swift','-module-cache-path',out/'.swift-cache',repo/'scripts/native-ui-contact-sheet.swift',out])
entries=[row.split('\t') for row in (out/'screens.tsv').read_text().splitlines()[1:]]
gallery='<!doctype html><meta charset="utf-8"><title>Native UI audit</title><style>body{background:#09141d;color:#ecf6d9;font:16px system-ui;margin:24px}main{display:grid;grid-template-columns:repeat(auto-fit,436px);gap:16px}figure{margin:0}img{width:412px;height:412px;image-rendering:pixelated}figcaption{padding:10px}</style><h1>Native renderer / actual local pack</h1><p>412×412. Synthetic states; physical touch and screen acceptance pending.</p><main>'
for row in entries: gallery+=f'<figure><img src="{html.escape(row[0])}" alt="{html.escape(row[1])}"><figcaption>{html.escape(row[1])}</figcaption></figure>'
(out/'index.html').write_text(gallery+'</main>')
evidence={'privateOnly':True,'hostOnly':True,'screens':len(entries),'packFilesVerified':len(verified),'inputs':verified,'sources':{name:hashlib.sha256((repo/name).read_bytes()).hexdigest() for name in sources+['firmware/runtime/device_ui.hpp','firmware/runtime/sprite_bounds.hpp','firmware/runtime/local_form_facing.hpp','scripts/render-native-ui.py','scripts/native-ui-contact-sheet.swift','core/game.cpp','core/game.hpp','core/forms.cpp','core/combat.cpp','core/nearby_match.cpp','core/trade.cpp','core/trade.hpp','firmware/runtime/trade_protocol.hpp']},'nativeChecks':['Sprite::open+decode CRC/frame bounds','inspectBackgroundJpeg','writeBackgroundBlock 60 percent RGB565','Controller::render framebuffer canaries and every off-circle pixel'],'limitations':['libjpeg host decode, not ESP ROM JPEG decoder','Synthetic game state only; no device/save access','Missing exact-form artwork uses a neutral ART MISSING indicator','Pixels prove layout, not physical touch, brightness or frame timing'],'result':r.stdout.strip()}
(out/'visual-evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
print(json.dumps({'output':str(out),'screens':len(entries),'packFilesVerified':len(verified),'status':'PASS'}))
