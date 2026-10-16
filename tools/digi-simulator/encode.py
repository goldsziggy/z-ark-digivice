#!/usr/bin/env python3
"""Encode actual device RGB frames, with an always-visible simulation disclosure."""
import argparse,json,pathlib,subprocess
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('raw');p.add_argument('output');p.add_argument('--title',default='Digi-Modify | Manual critical');a=p.parse_args()
font='/System/Library/Fonts/Supplemental/Arial.ttf'
f=ImageFont.truetype(font,22);small=ImageFont.truetype(font,17);title=ImageFont.truetype(font,27)
events=[json.loads(x) for x in pathlib.Path(a.raw+'.events.jsonl').read_text().splitlines()]
proc=subprocess.Popen(['/opt/homebrew/bin/ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pixel_format','rgb24','-video_size','640x760','-framerate','20','-i','-','-f','s16le','-ar','44100','-ac','1','-i',a.raw+'.s16le','-c:a','aac','-b:a','128k','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',a.output],stdin=subprocess.PIPE)
with open(a.raw,'rb') as frames:
 n=0
 while data:=frames.read(412*412*3):
  if len(data)!=412*412*3:raise ValueError('truncated runtime frame')
  canvas=Image.new('RGB',(640,760),(9,17,25));d=ImageDraw.Draw(canvas)
  d.text((320,23),a.title,font=title,fill='#edf6d9',anchor='mt')
  d.rounded_rectangle((110,66,530,102),10,fill='#5e4423')
  d.text((320,72),'Simulated card trigger',font=f,fill='#ffe2a4',anchor='mt')
  im=Image.frombytes('RGB',(412,412),data).resize((576,576),Image.Resampling.NEAREST);canvas.paste(im,(32,114))
  active=[e for e in events if e['ms']<=n*50];e=active[-1] if active else events[0]
  summary=e['event'].upper().replace('SYNTHETIC FIXTURE','SYNTHETIC BATTLE • REAL GAME CODE')
  if e['tier']:summary+='  |  TIER '+str(e['tier'])+'/3'
  d.text((320,697),summary,font=small,fill='#9dd7d4',anchor='mt')
  d.text((320,729),'Runtime + synth audio • Hardware NFC/audio unverified',font=small,fill='#bdc6ce',anchor='mt')
  proc.stdin.write(canvas.tobytes());n+=1
proc.stdin.close();assert proc.wait()==0
print(json.dumps({'frames':n,'fps':20,'seconds':n/20,'output':a.output}))
