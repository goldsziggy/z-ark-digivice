#!/usr/bin/env python3
"""Build original indexed sprite packs; standard library only unless --previews.

The runtime representation is raw packed 4-bit palette indexes, high nibble
first, inside canonical base64; preview PNGs are tooling outputs, never inputs.
"""
import argparse
import base64
import hashlib
import importlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets' / 'source'
DEST = ROOT / 'assets' / 'packs'
sys.path.insert(0, str(SOURCE))
from pixel import Canvas, translate
import interface

HARD_LIMIT = 256 * 1024
ANIMATIONS = {
    'idle': (180, [(0,0),(0,-1),(1,-1),(1,0)]),
    'attack': (100, [(-1,0),(2,-1),(1,0)]),
    'hurt': (130, [(-1,0),(1,0)]),
    'sleep': (650, [(0,0),(0,1)]),
    'care': (260, [(0,0),(0,-1)]),
    'celebrate': (150, [(-1,-1),(0,-2),(1,-1),(0,0)]),
}


def rgb565(hex_color):
    red,green,blue=(int(hex_color[i:i+2],16) for i in (1,3,5))
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def unpack_color(color):
    # Replicate MSBs, matching a tiny native RGB565 -> RGB888 decoder.
    r,g,b=(color>>11)&31,(color>>5)&63,color&31
    return ((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))


def packed_frame(grid, width, height):
    assert len(grid)==height and all(len(row)==width for row in grid), 'invalid frame dimensions'
    flat=[index for row in grid for index in row]
    assert all(type(index) is int and 0<=index<16 for index in flat), 'palette index outside 4-bit range'
    raw=bytes((flat[i]<<4)|flat[i+1] for i in range(0,len(flat),2))
    assert len(raw)==width*height//2
    return base64.b64encode(raw).decode('ascii')


def move_without_clipping(grid, dx, dy):
    points=[(x,y) for y,row in enumerate(grid) for x,value in enumerate(row) if value]
    assert points, 'empty creature frame'
    xs,ys=zip(*points)
    dx=max(-min(xs),min(dx,len(grid[0])-1-max(xs)))
    dy=max(-min(ys),min(dy,len(grid)-1-max(ys)))
    return translate(grid,dx,dy)


def animate(spec):
    animations={}
    for pose,(frame_ms,offsets) in ANIMATIONS.items():
        frames=[]
        for index,(dx,dy) in enumerate(offsets):
            # Wind-up, extension, recovery are visibly different attack frames.
            source_pose='idle' if pose=='attack' and index==0 else pose
            frame=move_without_clipping(spec['draw'](source_pose),dx,dy)
            overlay=Canvas();overlay.grid=frame
            if pose=='sleep':
                x,y=(24,3) if index==0 else (26,1)
                overlay.line(x,y,x+2,y,5);overlay.line(x+2,y,x,y+2,5);overlay.line(x,y+2,x+2,y+2,5)
            elif pose=='care':
                # Small affection glint preserves the creature silhouette.
                x,y=(3,6) if index==0 else (27,5)
                overlay.dot(x,y,8);overlay.dot(x+2,y,8)
                overlay.line(x,y+1,x+2,y+1,8);overlay.dot(x+1,y+2,8)
            elif pose=='celebrate':
                x,y=[(3,8),(27,5),(26,24),(5,23)][index]
                overlay.line(x-1,y,x+1,y,7);overlay.line(x,y-1,x,y+1,7)
            frames.append(overlay.grid)
        assert len({json.dumps(frame) for frame in frames})==len(frames), f'duplicate {spec["id"]}/{pose} frames'
        animations[pose]=dict(frameMs=frame_ms,frames=frames)
    return dict(name=spec['name'],family=spec['family'],stage=spec['stage'],width=32,height=32,
                palette=spec['palette'],animations=animations)


def encode_asset(asset):
    assert len(asset['palette'])==16
    result={key:asset[key] for key in ('name','family','stage','width','height')}
    result.update(palette=[rgb565(color) for color in asset['palette']],transparentIndex=0,animations={})
    for name,animation in asset['animations'].items():
        assert 40<=animation['frameMs']<=2000
        assert 1<=len(animation['frames'])<=8
        result['animations'][name]=dict(frameMs=animation['frameMs'],frames=[
            packed_frame(grid,asset['width'],asset['height']) for grid in animation['frames']])
    return result


def canonical(data):
    return (json.dumps(data,separators=(',',':'),sort_keys=True,ensure_ascii=True)+'\n').encode('ascii')


def measure(pack,raw):
    categories={}
    for category in ('sprites','effects','icons'):
        items=pack[category]
        frames=sum(len(animation['frames']) for item in items.values() for animation in item['animations'].values())
        pixels=sum(item['width']*item['height']*len(animation['frames']) for item in items.values() for animation in item['animations'].values())
        categories[category]=dict(assets=len(items),frames=frames,pixels=pixels,packedPixelBytes=pixels//2,
                                  nativeRgb565Bytes=pixels*2,decodedRgbaBytes=pixels*4)
    return dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
                frames=sum(v['frames'] for v in categories.values()),
                pixels=sum(v['pixels'] for v in categories.values()),
                packedPixelBytes=sum(v['packedPixelBytes'] for v in categories.values()),
                nativeRgb565Bytes=sum(v['nativeRgb565Bytes'] for v in categories.values()),
                decodedBytes=sum(v['decodedRgbaBytes'] for v in categories.values()),categories=categories)


def build():
    packs={}
    for pack_id,module_name in [('starter-v2','forest'),('tide-v1','tide'),('ember-v1','ember')]:
        module=importlib.import_module(module_name)
        source_sprites={spec['id']:animate(spec) for spec in module.creatures()}
        pack=dict(formatVersion=1,packId=pack_id,version=1,license='CC0-1.0',paletteEncoding='rgb565',
                  sprites={key:encode_asset(value) for key,value in source_sprites.items()},effects={},icons={})
        if pack_id=='starter-v2':
            pack['effects']={key:encode_asset(value) for key,value in interface.effects().items()}
            pack['icons']={key:encode_asset(value) for key,value in interface.icons().items()}
        raw=canonical(pack)
        assert len(raw)<=HARD_LIMIT,f'{pack_id} exceeds hard 256 KiB limit'
        target=160*1024 if pack_id=='starter-v2' else 128*1024
        assert len(raw)<target,f'{pack_id} exceeds art budget target'
        packs[pack_id]=(pack,raw)
    return packs


def render_frame(asset,animation='idle',index=0):
    from PIL import Image
    raw=base64.b64decode(asset['animations'][animation]['frames'][index],validate=True)
    indexes=[nibble for byte in raw for nibble in (byte>>4,byte&15)]
    image=Image.new('RGBA',(asset['width'],asset['height']))
    image.putdata([(*unpack_color(asset['palette'][n]),255) if n else (0,0,0,0) for n in indexes])
    return image


def previews(packs):
    from PIL import Image,ImageDraw,ImageFont
    preview_dir=DEST/'previews';preview_dir.mkdir(parents=True,exist_ok=True)
    font=ImageFont.load_default()
    for pack_id,(pack,_) in packs.items():
        creatures=list(pack['sprites'].values())
        width,height=1072,80+len(creatures)*164
        image=Image.new('RGB',(width,height),'#10213c');draw=ImageDraw.Draw(image)
        draw.text((24,18),f'DIGIVICE / {pack_id.upper()} / ORIGINAL INDEXED ART',fill='#d8f0ed',font=font)
        draw.text((24,38),'Each creature: idle 4 | attack 3 | hurt 2 | sleep 2 | care 2 | celebrate 4. Shown at exactly 4x.',fill='#85b7c7',font=font)
        for i,asset in enumerate(creatures):
            y=70+i*164
            draw.text((18,y+6),asset['name'].upper(),fill='#ffcc79',font=font)
            draw.text((18,y+23),f'STAGE {asset["stage"]}',fill='#85b7c7',font=font)
            for j,pose in enumerate(ANIMATIONS):
                x=112+j*156
                draw.rectangle((x,y,x+143,y+143),fill='#192e4a',outline='#31516a')
                frame=render_frame(asset,pose,1 if pose=='attack' else 0).resize((128,128),Image.Resampling.NEAREST)
                image.paste(frame,(x+8,y+1),frame)
                draw.text((x+10,y+130),pose.upper(),fill='#a3cbd0',font=font)
        image.save(preview_dir/f'{pack_id}-contact.png')
        # All17animationframes, useful for inspecting individual pixel changes.
        frame_sheet=Image.new('RGB',(17*68+116,len(creatures)*94+44),'#10213c');fd=ImageDraw.Draw(frame_sheet)
        fd.text((16,14),f'{pack_id} / ALL FRAMES AT 2x / NO FILTERING',fill='#d8f0ed',font=font)
        for row,asset in enumerate(creatures):
            y=40+row*94;fd.text((12,y+25),asset['name'],fill='#ffcc79',font=font);column=0
            for state,animation in asset['animations'].items():
                for index in range(len(animation['frames'])):
                    x=116+column*68;pic=render_frame(asset,state,index).resize((64,64),Image.Resampling.NEAREST)
                    frame_sheet.paste(pic,(x,y),pic)
                    fd.text((x,y+67),f'{state[:3]} {index+1}',fill='#85b7c7',font=font);column+=1
        frame_sheet.save(preview_dir/f'{pack_id}-frames.png')
    roster=[(pack_id,asset) for pack_id,(pack,_) in packs.items() for asset in pack['sprites'].values()]
    image=Image.new('RGB',(1000,674),'#10213c');draw=ImageDraw.Draw(image)
    draw.text((25,18),'DIGIVICE / TEN ORIGINAL CREATURES / 32px native + 4x integer scale',fill='#d8f0ed',font=font)
    draw.text((25,39),'Original silhouettes, 16 RGB565 colours per sprite. Preview PNGs are never runtime assets.',fill='#85b7c7',font=font)
    for i,(pack_id,asset) in enumerate(roster):
        x=18+(i%5)*198;y=73+(i//5)*285
        draw.rectangle((x,y,x+184,y+264),fill='#192e4a',outline='#31516a')
        pic=render_frame(asset);big=pic.resize((128,128),Image.Resampling.NEAREST)
        image.paste(big,(x+28,y+15),big);image.paste(pic,(x+76,y+157),pic)
        draw.text((x+14,y+205),asset['name'].upper(),fill='#ffcc79',font=font)
        draw.text((x+14,y+224),f'{asset["family"]} / stage {asset["stage"]}',fill='#b8dce0',font=font)
        draw.text((x+14,y+242),pack_id,fill='#85b7c7',font=font)
    image.save(preview_dir/'creature-roster.png')
    # Human-review animation only. Reconstructed from the packed runtime bytes.
    animation_frames=[];durations=[]
    for state,(frame_ms,offsets) in ANIMATIONS.items():
        for frame_index in range(len(offsets)):
            animated=Image.new('RGB',(900,452),'#10213c');ad=ImageDraw.Draw(animated)
            ad.text((20,15),f'DIGIVICE / ORIGINAL CREATURES / {state.upper()} / FRAME {frame_index+1}',fill='#d8f0ed',font=font)
            ad.text((20,34),'32px indexed sprites at exactly 4x / RGB565 palette / nearest-neighbour',fill='#85b7c7',font=font)
            for i,(_,asset) in enumerate(roster):
                x=12+(i%5)*178;y=58+(i//5)*192
                ad.rectangle((x,y,x+164,y+179),fill='#192e4a',outline='#31516a')
                pic=render_frame(asset,state,frame_index).resize((128,128),Image.Resampling.NEAREST)
                animated.paste(pic,(x+18,y+7),pic)
                ad.text((x+14,y+151),asset['name'].upper(),fill='#ffcc79',font=font)
            animation_frames.append(animated);durations.append(frame_ms)
    animation_frames[0].save(preview_dir/'creature-animation.gif',save_all=True,
                            append_images=animation_frames[1:],duration=durations,loop=0,optimize=False,disposal=2)
    starter=packs['starter-v2'][0]
    sheet=Image.new('RGB',(860,360),'#10213c');draw=ImageDraw.Draw(sheet)
    draw.text((20,16),'DEVICE ICONS AT 4x / BATTLE EFFECTS AT 3x',fill='#d8f0ed',font=font)
    for i,(name,asset) in enumerate(starter['icons'].items()):
        x=18+(i%7)*120;y=42+(i//7)*96
        pic=render_frame(asset).resize((64,64),Image.Resampling.NEAREST);sheet.paste(pic,(x,y),pic)
        draw.text((x,y+68),name,fill='#85b7c7',font=font)
    for i,(name,asset) in enumerate(starter['effects'].items()):
        x=20+i*207;y=247
        pic=render_frame(asset).resize((asset['width']*3,asset['height']*3),Image.Resampling.NEAREST)
        sheet.paste(pic,(x,y),pic);draw.text((x+103,y+12),name,fill='#ffcc79',font=font)
    sheet.save(preview_dir/'interface-contact.png')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--include-test-fixtures',action='store_true',help='explicitly generate legacy toy creatures for isolated tests only')
    parser.add_argument('--check',action='store_true',help='verify committed pack/budget bytes exactly match source')
    preview_args=parser.add_mutually_exclusive_group()
    preview_args.add_argument('--previews',action='store_true',help='also render contact sheets and GIF; requires Pillow')
    preview_args.add_argument('--no-preview',action='store_true',help='explicit standard-library-only build (the default)')
    args=parser.parse_args()
    if not args.include_test_fixtures:
        parser.error('This builder only creates test creatures. Pass --include-test-fixtures for isolated tests; release gameplay uses its own local Digimon art.')
    packs=build()
    measured={pack_id:measure(pack,raw) for pack_id,(pack,raw) in packs.items()}
    budget=dict(formatVersion=1,measurement='exact generated bytes and pixel counts; excludes JSON parser/runtime overhead',
                hardPackLimitBytes=HARD_LIMIT,maximumFrame=dict(width=32,height=32,packedBytes=512,nativeRgb565Bytes=2048,rgbaBytes=4096),
                fourActiveFrames=dict(nativeRgb565Bytes=8192,rgbaBytes=16384),packs=measured)
    outputs={f'{pack_id}.json':raw for pack_id,(_,raw) in packs.items()}
    outputs['budget.json']=(json.dumps(budget,indent=2,sort_keys=True)+'\n').encode('ascii')
    DEST.mkdir(parents=True,exist_ok=True)
    for filename,raw in outputs.items():
        target=DEST/filename
        if args.check:
            if not target.exists() or target.read_bytes()!=raw:
                raise SystemExit(f'Out of date: {target.relative_to(ROOT)}. Run python3 scripts/build-assets.py --include-test-fixtures')
        else:target.write_bytes(raw)
    if args.previews:previews(packs)
    for pack_id,value in measured.items():
        print(f'{pack_id}: {value["bytes"]:,} JSON bytes; {value["frames"]} frames; {value["packedPixelBytes"]:,} packed pixel bytes; {value["decodedBytes"]:,} all-frame RGBA bytes')
    print('Reproducible pack check passed.' if args.check else 'Built deterministic original sprite packs.')


if __name__=='__main__':
    main()
