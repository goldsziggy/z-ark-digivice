"""Original 16px device glyphs and indexed battle effects; no image inputs."""
from pixel import Canvas

PALETTE=['#000000','#14223e','#294667','#4381ad','#65c4de','#c5eff1',
         '#536196','#fff3d1','#f18e70','#ffcc79','#728cbd','#a9cbd2',
         '#9cd587','#64b88d','#d2a3e2','#fffbe9']

def heart(c,x,y,color=8):
    c.polygon([(x-4,y-2),(x-2,y-4),(x,y-2),(x+2,y-4),(x+4,y-2),
               (x+4,y),(x,y+4),(x-4,y)],color)
    c.line(x-3,y-2,x-2,y-3,9)


def icon(name):
    c=Canvas(16,16)
    if name in ('heart','care'):
        heart(c,7,8)
        if name=='care':
            c.line(2,13,11,13,4);c.line(11,13,13,10,4)
            c.dot(13,2,7);c.line(12,3,14,3,7);c.dot(13,4,7)
    elif name=='feed':
        c.polygon([(2,8),(13,8),(12,12),(10,14),(5,14),(3,12)],3)
        c.line(3,8,12,8,5);c.line(5,11,10,11,4)
        c.rect(5,4,10,6,9);c.dot(7,3,8);c.dot(10,2,7)
    elif name=='play':
        c.polygon([(7,1),(9,5),(14,6),(10,9),(11,14),(7,11),(3,14),(4,9),(1,6),(6,5)],9)
        c.line(7,4,7,8,7);c.dot(6,6,7);c.dot(10,10,8)
    elif name=='rest':
        c.ellipse(7,8,6,6,4);c.ellipse(10,5,5,5,0)
        c.line(4,10,6,12,5);c.dot(12,10,9);c.dot(13,3,7)
    elif name=='steps':
        c.ellipse(5,6,2,3,4);c.rect(3,10,6,12,3)
        c.ellipse(11,9,2,3,9);c.rect(9,2,12,4,8)
        c.dot(4,4,5);c.dot(10,7,7)
    elif name=='card':
        c.rect(3,2,12,13,3);c.rect(4,3,11,12,4)
        c.rect(5,5,8,8,9);c.line(6,5,6,8,8)
        c.line(5,10,10,10,2);c.line(7,1,12,1,5)
        c.line(2,13,7,13,9)
    elif name=='wifi':
        c.line(2,5,4,3,4);c.line(4,3,11,3,4);c.line(11,3,13,5,4)
        c.line(4,8,6,6,5);c.line(6,6,9,6,5);c.line(9,6,11,8,5)
        c.line(6,10,7,9,7);c.line(7,9,9,10,7);c.rect(7,12,8,13,9)
    elif name=='download':
        c.rect(7,2,8,9,4);c.line(4,7,7,10,5);c.line(8,10,11,7,5)
        c.line(2,10,2,13,3);c.line(2,13,13,13,3);c.line(13,13,13,10,3)
        c.dot(8,3,7)
    elif name=='update':
        c.line(3,6,3,4,4);c.line(3,4,5,2,4);c.line(5,2,10,2,4)
        c.line(10,2,13,5,4);c.polygon([(10,5),(14,5),(14,1)],5)
        c.line(12,9,12,11,9);c.line(12,11,10,13,9);c.line(10,13,5,13,9)
        c.line(5,13,2,10,9);c.polygon([(1,10),(5,10),(1,14)],8)
    elif name=='recovery':
        c.polygon([(7,1),(13,4),(12,10),(7,14),(2,10),(1,4)],3)
        c.polygon([(7,3),(11,5),(10,9),(7,12),(4,9),(3,5)],4)
        c.line(4,7,6,9,7);c.line(6,9,10,5,7)
    elif name=='back':
        c.line(8,3,3,8,5);c.line(3,8,8,13,5);c.line(4,8,13,8,4)
        c.line(8,4,4,8,4);c.line(4,8,8,12,4)
    elif name=='settings':
        c.rect(6,1,9,3,3);c.rect(6,12,9,14,3);c.rect(1,6,3,9,3);c.rect(12,6,14,9,3)
        c.line(3,3,5,5,3);c.line(10,10,12,12,3);c.line(3,12,5,10,3);c.line(10,5,12,3,3)
        c.ellipse(7,7,5,5,4);c.ellipse(7,7,2,2,0);c.dot(6,3,5)
    elif name=='battery':
        c.rect(1,4,12,11,4);c.rect(2,5,11,10,0);c.rect(13,6,14,9,3)
        c.rect(3,6,4,9,12);c.rect(6,6,7,9,12);c.rect(9,6,10,9,9)
    return c.grid


def asset(name, family, size, frames, frame_ms):
    return dict(name=name.title(),family=family,stage=0,width=size,height=size,
                palette=PALETTE,animations={'idle':dict(frameMs=frame_ms,frames=frames)})


def icons():
    return {name:asset(name,'ui',16,[icon(name)],180)
            for name in ['care','feed','play','rest','steps','card','wifi','download',
                         'update','recovery','back','settings','battery','heart']}


def effects():
    result={}
    frames=[]
    for radius in [2,4,6,3]:
        c=Canvas(16,16)
        c.line(7-radius,7,7+radius,7,9);c.line(7,7-radius,7,7+radius,9)
        c.line(7-radius+1,7-radius+1,7+radius-1,7+radius-1,8)
        c.line(7+radius-1,7-radius+1,7-radius+1,7+radius-1,8)
        c.rect(6,6,8,8,7);frames.append(c.grid)
    result['spark']=asset('Spark','effect',16,frames,90)
    frames=[]
    for radius in [13,11,9,6,3,1]:
        c=Canvas()
        c.ellipse(15,15,radius,radius,4)
        if radius>1:c.ellipse(15,15,radius-1,radius-1,0)
        c.line(15-radius+1,13,15-radius+1,16,5)
        c.dot(15+radius,15,9);c.dot(15,15-radius,7)
        if radius<7:
            c.line(14,15,16,15,7);c.line(15,14,15,16,7)
        frames.append(c.grid)
    result['capture']=asset('Capture','effect',32,frames,90)
    frames=[]
    for shift in [-3,0,3]:
        c=Canvas(16,16)
        c.line(2+shift,12,11+shift,3,8)
        c.line(3+shift,13,12+shift,4,9)
        c.line(4+shift,13,12+shift,5,7)
        frames.append(c.grid)
    result['hit']=asset('Hit','effect',16,frames,70)
    frames=[]
    for y in [10,8,7,6]:
        c=Canvas(16,16);heart(c,7,y);frames.append(c.grid)
    result['heart']=asset('Heart','effect',16,frames,160)
    return result
