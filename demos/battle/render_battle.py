"""Render a cinematic UI target from verified game-core event data.

Original fixture sprites, presentation-only animation, RGB565 source framebuffer.
No hardware capture, geometry certification, new game rules, or device access.
Requires existing Pillow and ffmpeg. Output media is intentionally outside Git.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import argparse, json, math, subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT.parent / 'deliverables' / 'battle'
W, H, FPS, DURATION = 800, 1040, 24, 16
SW = 480
SX, SY = 160, 182
TAU = math.tau
FONT = '/System/Library/Fonts/Supplemental/Arial.ttf'
BOLD = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
MONO = '/System/Library/Fonts/Menlo.ttc'
fonts = {}
EVENT_DATA=json.loads(Path(__file__).with_name('battle-events.json').read_text())
EVENTS={event['id']:event for event in EVENT_DATA['events']}
COMBAT=EVENTS['spark-attack-and-retaliation']
CAPTURE=EVENTS['capture-success']
ENEMY_START=COMBAT['before']['wildHp']
ENEMY_END=COMBAT['after']['wildHp']
PLAYER_START=COMBAT['before']['hp']
PLAYER_END=COMBAT['after']['hp']
def font(n, bold=False, mono=False):
    key=(n,bold,mono)
    if key not in fonts: fonts[key] = ImageFont.truetype(MONO if mono else BOLD if bold else FONT,n)
    return fonts[key]
def clamp(x,a=0,b=1): return max(a,min(b,x))
def mix(a,b,t): return a+(b-a)*clamp(t)
def ease(t):
    t=clamp(t); return t*t*(3-2*t)
def text(d, xy, value, size=16, color='#effcff', bold=False, anchor='mm', mono=False):
    d.text(xy,str(value),font=font(size,bold,mono),fill=color,anchor=anchor)
def rounded(d, box, color, radius=10, outline=None, width=1):
    d.rounded_rectangle(tuple(map(round,box)),radius,fill=color,outline=outline,width=width)
def bezier(points, count=32):
    a,b,c,d=points
    return [((1-t)**3*a[0]+3*(1-t)**2*t*b[0]+3*(1-t)*t*t*c[0]+t**3*d[0],
             (1-t)**3*a[1]+3*(1-t)**2*t*b[1]+3*(1-t)*t*t*c[1]+t**3*d[1])
            for t in [i/count for i in range(count+1)]]
def glow(im,xy,r,color,strength=150):
    layer=Image.new('RGBA',im.size)
    d=ImageDraw.Draw(layer)
    for i in range(int(r),0,-3):
        a=int(strength*(1-i/r)**2)
        d.ellipse((xy[0]-i,xy[1]-i,xy[0]+i,xy[1]+i),fill=(*color,a))
    im.alpha_composite(layer)

def make_shell():
    # A broad shoulder surrounding the round display, tapering only below it.
    s=2
    im=Image.new('RGB',(W*s,H*s))
    d=ImageDraw.Draw(im)
    for y in range(H*s):
        t=y/(H*s)
        c=(int(13-4*t),int(22-6*t),int(34-7*t))
        d.line((0,y,W*s,y),fill=c)
    shell=[((285,120),(340,105),(460,105),(515,120)),
           ((515,120),(638,149),(690,222),(688,350)),
           ((688,350),(689,492),(642,590),(598,669)),
           ((598,669),(552,750),(496,841),(444,889)),
           ((444,889),(416,915),(384,915),(356,889)),
           ((356,889),(304,841),(248,750),(202,669)),
           ((202,669),(158,590),(111,492),(112,350)),
           ((112,350),(110,222),(162,149),(285,120))]
    pts=[p for seg in shell for p in bezier(seg)]
    # Visible lower edge establishes body depth without assigning dimensions.
    for dy,c in [(19,'#080d13'),(14,'#151e28'),(9,'#34434e')]:
        d.polygon([(int(x*s),int((y+dy)*s)) for x,y in pts],fill=c)
    mask=Image.new('L',im.size)
    ImageDraw.Draw(mask).polygon([(int(x*s),int(y*s)) for x,y in pts],fill=255)
    face=Image.new('RGB',im.size)
    fd=ImageDraw.Draw(face)
    for y in range(H*s):
        t=clamp((y/s-110)/800)
        c=tuple(int(mix(a,b,t)) for a,b in zip((83,109,123),(38,54,68)))
        fd.line((0,y,W*s,y),fill=c)
    im.paste(face,(0,0),mask)
    d=ImageDraw.Draw(im)
    d.line([(int(x*s),int(y*s)) for x,y in pts],fill='#8ba8b3',width=2)
    # Muted bronze shoulder trims; no open side loops or finger holes.
    d.line([(155*s,293*s),(173*s,227*s),(222*s,177*s)],fill='#d6b887',width=5*s)
    d.line([(645*s,293*s),(627*s,227*s),(578*s,177*s)],fill='#d6b887',width=5*s)
    center=(400,422)
    for r,c in [(257,'#17252e'),(252,'#b3c4ba'),(248,'#677e81'),(245,'#091923'),(241,'#09121e')]:
        d.ellipse(((center[0]-r)*s,(center[1]-r)*s,(center[0]+r)*s,(center[1]+r)*s),fill=c)
    # Three raised controls, not holes. Main button centered on tapered grip.
    for x,y,r in [(315,738,21),(485,738,21),(400,794,28)]:
        d.ellipse(((x-r)*s,(y-r+5)*s,(x+r)*s,(y+r+5)*s),fill='#14212b')
        d.ellipse(((x-r)*s,(y-r)*s,(x+r)*s,(y+r)*s),fill='#bbc7c0',outline='#dae2d7',width=2)
        d.arc(((x-r+4)*s,(y-r+4)*s,(x+r-4)*s,(y+r-4)*s),195,330,fill='#758c8e',width=2*s)
    d.polygon([(394*s,787*s),(409*s,794*s),(394*s,801*s)],fill='#4a6570')
    d.line((309*s,738*s,321*s,738*s),fill='#4a6570',width=3*s)
    d.line((479*s,738*s,491*s,738*s),fill='#4a6570',width=3*s)
    d.line((485*s,732*s,485*s,744*s),fill='#4a6570',width=3*s)
    # Short status light and discreet product markings.
    d.rounded_rectangle((386*s,707*s,414*s,711*s),radius=2*s,fill='#75e6da')
    text(d,(400*s,140*s),'D I G I V I C E',11*s,'#d3dcd9',True)
    text(d,(400*s,860*s),'FIELD UNIT',10*s,'#a5b7bc',True)
    im=im.resize((W,H),Image.Resampling.LANCZOS)
    d=ImageDraw.Draw(im)
    text(d,(400,38),'A LITTLE WORLD. A BIG ADVENTURE.',18,'#e2edef',True)
    text(d,(400,69),'D I G I V I C E  /  B A T T L E  S T U D Y',10,'#8da9b5')
    text(d,(400,971),'480 × 480  ·  RGB565 COLOR',14,'#d9e8e8',True)
    text(d,(400,999),'VISUAL TARGET · NOT A HARDWARE FPS TEST',10,'#809eac')
    return im

PACK=json.loads((ROOT/'assets/starter-v1.json').read_text())
# Presentation palette: original sprites retain their silhouettes and animation frames.
MOTE={'.':(0,0,0,0),'o':'#173c4a','g':'#48c797','l':'#acff8a','c':'#fff3b0',
      'e':'#092e40','w':'#f3fff9','r':'#ff9b78'}
FLICKER={'.':(0,0,0,0),'o':'#642c61','m':'#fa6176','y':'#ffae6b','b':'#fff1b1',
         'a':'#c3548f','n':'#ffc184','e':'#401a55','w':'#fff8d4','r':'#ffa666'}
SPRITES={}
for name,palette in [('mote',MOTE),('flicker',FLICKER)]:
    specs=PACK['sprites'][name]
    frames=[]
    for rows in specs['frames']:
        sprite=Image.new('RGBA',(16,16))
        sd=ImageDraw.Draw(sprite)
        for y,row in enumerate(rows):
            for x,p in enumerate(row):
                if p!='.': sd.point((x,y),fill=palette.get(p,PACK['palette'][p]))
        frames.append(sprite)
    SPRITES[name]=frames

def sprite(im,name,x,y,scale,t,flash=0,alpha=1,squash=1):
    a=SPRITES[name][int(t*3)%len(SPRITES[name])].copy()
    if flash:
        tint=Image.new('RGBA',a.size,(255,255,229,255))
        tint.putalpha(a.getchannel('A'))
        a=Image.blend(a,tint,clamp(flash))
    a=a.resize((round(16*scale/squash),round(16*scale*squash)),Image.Resampling.NEAREST)
    if alpha<1: a.putalpha(a.getchannel('A').point(lambda v:round(v*alpha)))
    im.alpha_composite(a,(round(x-a.width/2),round(y-a.height/2)))

def hp(d,x,y,w,name,hp,maxhp,color):
    text(d,(x,y),name,14,'#f0ffef',True,anchor='lm')
    text(d,(x+w,y),f'{round(hp)}/{maxhp}',12,'#cfe4ea',anchor='rm',mono=True)
    rounded(d,(x,y+13,x+w,y+23),'#071522',5,outline='#325066')
    if hp>0: rounded(d,(x+2,y+15,x+2+(w-4)*hp/maxhp,y+21),color,3)

def star(d,x,y,r,c):
    d.polygon([(x,y-r),(x+r*.27,y-r*.27),(x+r,y),(x+r*.27,y+r*.27),
               (x,y+r),(x-r*.27,y+r*.27),(x-r,y),(x-r*.27,y-r*.27)],fill=c)

def make_background():
    im=Image.new('RGBA',(SW,SW))
    d=ImageDraw.Draw(im)
    for y in range(SW):
        q=y/SW
        d.line((0,y,SW,y),fill=(round(mix(10,27,q)),round(mix(18,30,q)),round(mix(44,67,q)),255))
    glow(im,(341,156),140,(43,104,163),35)
    d=ImageDraw.Draw(im)
    d.ellipse((275,108,329,162),fill='#a5e9e0')
    d.ellipse((260,99,316,154),fill='#111f3e')
    for i in range(35):
        x=(i*83+27)%480;y=(i*47+69)%266
        if y<80: continue
        d.rectangle((x,y,x+1,y+1),fill='#6ca6b7')
    d.polygon([(0,247),(52,187),(117,228),(174,150),(245,238),(338,176),(411,237),(480,182),(480,480),(0,480)],fill='#172d4b')
    d.polygon([(0,283),(61,235),(112,267),(212,208),(288,281),(375,235),(480,264),(480,480),(0,480)],fill='#1d3c52')
    d.polygon([(0,336),(116,293),(219,322),(382,274),(480,319),(480,480),(0,480)],fill='#205265')
    d.polygon([(0,375),(128,330),(230,350),(344,331),(480,365),(480,480),(0,480)],fill='#172f45')
    # Floating arena stones, limited large shapes give the sprites clean contrast.
    d.ellipse((64,285,238,318),fill='#122737')
    d.ellipse((68,279,235,307),fill='#3a7281')
    d.ellipse((71,279,232,298),fill='#5a9d9a')
    d.ellipse((269,255,399,282),fill='#142d42')
    d.ellipse((267,247,399,271),fill='#786f98')
    d.ellipse((271,247,396,263),fill='#a090b6')
    for x,y,c in [(50,295,'#2ba69e'),(438,283,'#7268a9'),(410,324,'#ce759e'),(87,369,'#53b7b0')]:
        d.polygon([(x,y-18),(x+7,y),(x+3,y+9),(x-5,y+4)],fill=c)
        d.line((x,y-16,x+1,y+3),fill='#a4e8ce',width=1)
    return im

SHELL=make_shell()
BACKGROUND=make_background()
MASK=Image.new('L',(SW,SW));ImageDraw.Draw(MASK).ellipse((0,0,479,479),fill=255)
LUT=[(v>>3)*255//31 for v in range(256)]+[(v>>2)*255//63 for v in range(256)]+[(v>>3)*255//31 for v in range(256)]

def screen(t):
    im=BACKGROUND.copy()
    d=ImageDraw.Draw(im)
    # Subtle ambient fireflies, independent of game state.
    for i in range(9):
        x=45+(i*61)%390;y=164+(i*39)%177+math.sin(t*1.7+i)*5
        k=int(110+70*math.sin(t*2+i))
        d.rectangle((x,y,x+2,y+2),fill=(94,k,179,255))
    # One core attack is animated in two beats, then its capture event.
    attacking=5.0<=t<7.8
    retaliating=7.8<=t<10
    capture=10<=t<13.5
    success=t>=13.5
    mx,my=145,250+math.sin(t*4)*3
    fx,fy=334,215+math.sin(t*4+1.4)*4
    mote_flash=flicker_flash=0
    mote_alpha=flicker_alpha=1
    mote_scale=6.8;flicker_scale=5.7
    enemy_hp=ENEMY_START if t<6.65 else mix(ENEMY_START,ENEMY_END,(t-6.65)/.42)
    player_hp=PLAYER_START if t<8.88 else mix(PLAYER_START,PLAYER_END,(t-8.88)/.35)
    if 3<t<5.6:
        glow(im,(mx,my),83,(57,233,171),int(75+25*math.sin(t*10)))
        d=ImageDraw.Draw(im)
        a=t*2.4
        for i in range(7):
            x=mx+math.cos(a+i*TAU/7)*64;y=my+math.sin(a+i*TAU/7)*39
            star(d,x,y,3,'#d5ff9b')
    if attacking:
        dash=math.sin(clamp((t-5.3)/1.1)*math.pi)
        mx+=dash*40;my-=dash*13
        if 5.9<t<6.72:
            p=clamp((t-5.9)/.77)
            qx=mix(194,fx,p);qy=mix(231,fy,p)-math.sin(p*math.pi)*24
            glow(im,(qx,qy),39,(44,237,245),145)
            d=ImageDraw.Draw(im)
            for j in range(7):
                r=11-j
                x=qx-j*11;y=qy+j*1.4
                d.ellipse((x-r,y-r,x+r,y+r),fill=('#e4fff1' if j<2 else '#4fe1d0'))
            star(d,qx,qy,16,'#fffbc0')
        if 6.64<t<7.2:
            p=(t-6.64)/.56
            flicker_flash=max(0,1-p)*.9
            fx+=math.sin(p*32)*7*(1-p)
            d=ImageDraw.Draw(im)
            d.ellipse((fx-17-p*48,fy-17-p*48,fx+17+p*48,fy+17+p*48),outline='#d6ffcf',width=max(1,int(5*(1-p))))
            for i in range(12):
                a=i*TAU/12;rr=24+p*64
                star(d,fx+math.cos(a)*rr,fy+math.sin(a)*rr,5*(1-p)+2,'#e4ffad' if i%2 else '#58ffe0')
    if retaliating:
        fx-=math.sin(clamp((t-7.9)/1.0)*math.pi)*21
        if 8.22<t<8.94:
            p=(t-8.22)/.72
            qx=mix(302,mx,p);qy=mix(213,my,p)-math.sin(p*math.pi)*10
            glow(im,(qx,qy),33,(255,126,129),105)
            d=ImageDraw.Draw(im)
            for j in range(5):
                star(d,qx+j*13,qy-j*3,9-j,'#ffd394' if j==0 else '#f9789e')
        if 8.89<t<9.4:
            p=(t-8.89)/.51;mote_flash=(1-p)*.8
            mx+=math.sin(p*34)*5*(1-p)
            d=ImageDraw.Draw(im)
            for i in range(8):
                a=i*TAU/8;rr=15+p*43
                star(d,mx+math.cos(a)*rr,my+math.sin(a)*rr,3,'#ffc5ac')
    if capture:
        p=clamp((t-10.5)/1.25)
        glow(im,(334,215),105,(113,177,255),round(130*p))
        if t>10.6:
            d=ImageDraw.Draw(im)
            for r in [55,66]:
                d.arc((334-r,215-r,334+r,215+r),int(t*170)%360,int(t*170)%360+250,fill='#9deaff',width=2)
        if t>11.45:
            q=clamp((t-11.45)/.7)
            flicker_scale*=1-.95*q
            flicker_flash=q;flicker_alpha=1-q
        if t>12.1:
            flicker_alpha=0
            d=ImageDraw.Draw(im)
            orb=12+3*math.sin(t*14)
            d.ellipse((334-orb,215-orb,334+orb,215+orb),fill='#ebffdd',outline='#8cfff0',width=3)
    if success:
        flicker_alpha=0
        mx=mix(145,240,ease((t-13.5)/.55));my=250-abs(math.sin((t-13.5)*4))*9
        glow(im,(240,260),115,(86,241,173),80)
        d=ImageDraw.Draw(im)
        for i in range(17):
            x=80+(i*53)%322;y=163+(i*37)%198+(t-13.5)*13
            star(d,x,y,3+math.sin(t*3+i),['#ffdca1','#95ffda','#dfb5ff'][i%3])
    sprite(im,'mote',mx,my,mote_scale,t,mote_flash,mote_alpha,1+.015*math.sin(t*4))
    if flicker_alpha>0:
        sprite(im,'flicker',fx,fy,flicker_scale,t,flicker_flash,flicker_alpha)
    d=ImageDraw.Draw(im)
    if 6.7<t<7.6:
        text(d,(335,150-(t-6.7)*23),f'-{ENEMY_START-ENEMY_END}',27,'#e7ffb7',True)
    if 8.95<t<9.8:
        text(d,(146,178-(t-8.95)*22),f'-{PLAYER_START-PLAYER_END}',25,'#ffb7a6',True)
    text(d,(240,49),'LITTLE FOREST',15,'#a1bacd',True)
    if not success:
        hp(d,257,93,142,'FLICKER',enemy_hp,27,'#ff977e')
        hp(d,77,333,174,'MOTE',player_hp,100,'#80efba')
    else:
        rounded(d,(94,92,386,165),'#0c1b30',16,outline='#35556b')
        text(d,(240,114),'CAPTURE COMPLETE',20,'#e9ffc5',True)
        text(d,(240,145),'Flicker joined your collection',13,'#c2e5df')
        gains=CAPTURE['numericDeltas']
        text(d,(240,331),f"+{gains['captures']} DIGIMON    +{gains['bond']} BOND",15,'#acffcc',True)
    label=('A WILD FLICKER!' if t<3 else 'SPARK · ATTACK UP' if t<5 else 'SPARK BURST!' if t<7.8
           else 'FLICKER STRIKES' if t<10 else 'CAPTURE!' if t<13.5 else 'NEW DIGIMON!')
    accent='#c1ffbe' if t<7.8 else '#ffbb9c' if t<10 else '#c9c5ff' if t<13.5 else '#b9ffcf'
    rounded(d,(115,378,365,419),'#0b1b2e',13,outline='#4b7487')
    text(d,(240,399),label,17,accent,True)
    text(d,(240,446),'SPARK CARD ACTIVE' if 3<=t<6.65 else 'WILD ENCOUNTER' if t<10 else 'A NEW ADVENTURE',10,'#a6bcd0',True)
    # Quantize every displayed RGB channel to the board's 5/6/5 render space.
    return im.convert('RGB').point(LUT)

def render():
    OUT.mkdir(parents=True,exist_ok=True)
    # Input snapshots are checked, not recalculated by this renderer.
    prefix=''
    for event in EVENT_DATA['events']:
        prefix+=f"{event['action']} {event['value']}\n"
        actual=json.loads(subprocess.check_output([str(ROOT/'build/digivice-core'),'--replay',str(EVENT_DATA['seed'])],input=prefix.encode()))
        assert actual==event['after'], 'Battle ledger differs from shared core replay'
    video=OUT/'Digivice_Triangle_Battle.mp4'
    screenvideo=OUT/'Digivice_Battle_Screen.mp4'
    def encoder(path,w,h):
        return subprocess.Popen(['ffmpeg','-y','-v','error','-f','rawvideo','-pix_fmt','rgb24',
            '-s',f'{w}x{h}','-r',str(FPS),'-i','-','-an','-c:v','libx264','-preset','medium',
            '-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(path)],stdin=subprocess.PIPE)
    pipe=encoder(video,W,H);screenpipe=encoder(screenvideo,SW,SW)
    for frame in range(FPS*DURATION):
        t=frame/FPS
        lcd=screen(t)
        im=SHELL.copy();im.paste(lcd,(SX,SY),MASK)
        round_lcd=Image.new('RGB',(SW,SW),'#070e1a');round_lcd.paste(lcd,(0,0),MASK)
        pipe.stdin.write(im.tobytes());screenpipe.stdin.write(round_lcd.tobytes())
        if frame in [0,round(4*FPS),round(6.8*FPS),round(9.1*FPS),round(12.4*FPS),round(14.4*FPS)]:
            im.save(OUT/f'preview-{t:04.1f}.png')
        if frame==round(7.1*FPS): im.save(OUT/'Digivice_Battle_Preview.png')
    pipe.stdin.close();screenpipe.stdin.close()
    assert pipe.wait()==0 and screenpipe.wait()==0
    subprocess.run(['ffmpeg','-y','-v','error','-i',str(screenvideo),'-filter_complex',
        'fps=12,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=3',
        '-loop','0',str(OUT/'Digivice_Battle_Screen.gif')],check=True)
    print(json.dumps({'durationSeconds':DURATION,'presentationFps':FPS,'displayPixels':[480,480],
        'rgb565Source':True,'simulation':True,'files':[p.name for p in OUT.glob('Digivice*')]}))

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--preview',type=float)
    args=ap.parse_args()
    if args.preview is not None:
        OUT.mkdir(parents=True,exist_ok=True)
        im=SHELL.copy();im.paste(screen(args.preview),(SX,SY),MASK)
        im.save(OUT/'preview-check.png')
    else: render()
