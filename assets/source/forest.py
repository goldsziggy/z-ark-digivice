"""Original forest family and wild lantern moth, edited as indexed pixel forms."""
from pixel import Canvas, face

FOREST = ['#000000', '#14223e', '#255f60', '#43b56f', '#92e790', '#d6f7b0',
          '#183e5b', '#fff4d2', '#f58c83', '#ffcc84', '#3a8993', '#6dcdb6',
          '#9ba6ee', '#5374ad', '#f0dc7a', '#fffbea']
GOLD = ['#000000', '#14223e', '#416348', '#88b656', '#d5e774', '#fff4ad',
        '#243c61', '#fffbe5', '#ed8a77', '#ffc887', '#529986', '#8bd6a0',
        '#afb6e8', '#6286b7', '#f3cf64', '#fff7cf']
LUMINOUS = ['#000000', '#14223e', '#245977', '#45a8a0', '#93e4be', '#d7fbd0',
            '#283969', '#fffbea', '#f59b90', '#ffd08c', '#527bbc', '#91b4ee',
            '#c3a5f0', '#836bba', '#f5d982', '#fff8d5']
WILD = ['#000000', '#14223e', '#98537c', '#d87867', '#f5b266', '#ffe2a0',
        '#3c386b', '#fff6d8', '#9e82d0', '#d2b4f0', '#557dbc', '#8cb8d2',
        '#b5d9b9', '#7dad9a', '#ffe6a0', '#fff8d8']


def leaf(c, outer, inner, vein):
    c.polygon(outer, 1)
    c.polygon(inner, 3)
    c.line(*vein, 4)


def mote(pose):
    c = Canvas()
    attack = pose == 'attack'
    # Two unequal leaf ears give Mote its recognisable sprout silhouette.
    leaf(c, [(8,13),(6,7),(7,3),(11,6),(13,13)],
         [(9,11),(8,7),(8,5),(10,7),(11,12)], (8,6,10,10))
    leaf(c, [(18,12),(20,5),(24,4),(24,8),(21,14)],
         [(20,11),(21,7),(23,6),(22,9)], (22,7,21,10))
    c.ellipse(16,23,6,5,1); c.ellipse(16,22,5,4,2)
    c.ellipse(16,22,4,4,3); c.ellipse(16,22,2,3,5)
    c.rect(11,26,14,28,1); c.rect(18,26,21,28,1)
    c.rect(12,26,14,27,3); c.rect(18,26,20,27,3)
    c.ellipse(16,16,9,8,1); c.ellipse(16,16,8,7,2)
    c.ellipse(15,15,7,6,3); c.ellipse(14,13,5,3,4)
    c.polygon([(9,17),(12,14),(15,17),(16,21),(11,21)],5)
    c.polygon([(17,17),(20,14),(23,17),(21,21),(17,21)],5)
    c.line(12,11,15,10,5)
    for x, direction in [(9,-1),(23,1)]:
        y = 18 if pose == 'celebrate' else 23
        if attack and direction == 1: y = 18
        c.line(x-direction,y,x+direction*2,y-1,1)
        c.dot(x+direction,y-1,3)
    face(c,pose,16,16,4)
    return c.grid


def glint(pose):
    c=Canvas()
    # Layered leaf collar, a forward crest, and longer hind limbs.
    for outer,inner,vein in [
        ([(8,14),(4,8),(5,4),(10,6),(13,13)],[(8,12),(6,8),(6,6),(9,8)],(6,6,10,11)),
        ([(18,12),(21,4),(26,3),(25,10),(22,15)],[(20,11),(23,6),(25,5),(23,10)],(24,5,21,11)),
        ([(13,13),(13,6),(17,3),(19,8),(18,13)],[(15,11),(15,7),(17,5),(17,10)],(16,6,16,10))]:
        leaf(c,outer,inner,vein)
    c.polygon([(8,18),(5,25),(11,24),(9,28),(16,25),(23,28),(21,23),(27,24),(23,17)],1)
    c.polygon([(9,19),(7,23),(12,22),(11,26),(16,23),(22,26),(20,22),(25,23),(22,18)],3)
    c.line(8,23,12,20,4);c.line(20,20,23,23,4)
    c.ellipse(16,23,5,5,1);c.ellipse(16,23,4,4,2)
    c.polygon([(13,21),(17,20),(20,23),(17,27),(14,26)],5)
    c.rect(11,27,14,29,1);c.rect(19,26,22,28,1)
    c.line(12,27,14,27,4);c.line(20,26,22,26,4)
    c.ellipse(16,15,8,7,1);c.ellipse(16,15,7,6,2)
    c.ellipse(15,14,6,5,3);c.ellipse(15,12,4,3,4)
    c.polygon([(10,15),(13,14),(16,17),(18,14),(22,15),(21,20),(12,20)],5)
    c.rect(15,10,17,11,14);c.dot(16,9,7)
    if pose in ('attack','celebrate'):
        c.polygon([(22,20),(26,14),(28,13),(27,18),(24,23)],1)
        c.line(24,19,27,15,4)
    else:
        c.line(9,22,7,25,1);c.line(23,21,24,25,1)
    face(c,pose,16,15,4)
    return c.grid


def lumen(pose):
    c=Canvas()
    # Wide moth-like woodland mantle and branching luminous antlers.
    raised=pose in ('attack','celebrate')
    for side in (-1,1):
        points=[(16+side*5,15),(16+side*11,10 if raised else 15),
                (16+side*12,20),(16+side*10,25),(16+side*5,23)]
        c.polygon(points,1)
        c.polygon([(16+side*6,16),(16+side*10,13 if raised else 17),
                   (16+side*10,21),(16+side*9,23),(16+side*5,21)],3)
        c.line(16+side*6,17,16+side*10,21,4)
        c.line(16+side*6,19,16+side*8,23,11)
        c.line(16+side*5,10,16+side*7,5,1)
        c.line(16+side*7,5,16+side*10,3,1)
        c.line(16+side*7,5,16+side*6,2,1)
        c.dot(16+side*10,3,14);c.dot(16+side*6,2,5)
    c.polygon([(11,19),(20,19),(23,27),(19,26),(16,29),(12,26),(9,27)],1)
    c.polygon([(12,20),(19,20),(21,25),(18,24),(16,27),(13,24),(11,25)],3)
    c.polygon([(14,21),(18,21),(18,24),(16,26),(14,24)],5)
    c.ellipse(16,14,8,7,1);c.ellipse(16,14,7,6,2)
    c.ellipse(15,13,6,5,3)
    c.polygon([(10,12),(13,11),(16,14),(19,11),(22,12),(21,19),(11,19)],5)
    c.polygon([(12,10),(10,7),(14,8),(16,4),(18,8),(22,7),(20,10)],1)
    c.polygon([(13,9),(13,8),(15,9),(16,6),(17,9),(20,8),(19,9)],14)
    c.dot(16,8,7)
    face(c,pose,16,14,4)
    c.polygon([(15,21),(16,20),(17,21),(16,23)],14)
    c.dot(16,21,7)
    return c.grid


def flicker(pose):
    c=Canvas()
    folded=pose=='sleep'
    raised=pose in ('attack','celebrate')
    for side in (-1,1):
        reach=7 if folded else 12
        top=10 if folded else (5 if raised else 7)
        c.polygon([(16+side*2,14),(16+side*7,top),(16+side*reach,top+1),
                   (16+side*reach,17),(16+side*8,19),(16+side*10,25),(16+side*4,25),(16+side*2,20)],1)
        c.polygon([(16+side*3,14),(16+side*7,top+2),(16+side*(reach-1),top+3),
                   (16+side*(reach-1),16),(16+side*7,17),(16+side*4,21)],3)
        c.polygon([(16+side*4,15),(16+side*8,top+4),(16+side*(reach-2),top+5),
                   (16+side*(reach-2),14),(16+side*7,16)],4)
        c.ellipse(16+side*7,13 if not folded else 15,2,2,2)
        c.dot(16+side*7,12 if not folded else 14,5)
        c.polygon([(16+side*4,19),(16+side*8,21),(16+side*8,23),(16+side*4,23)],8)
        c.dot(16+side*6,22,9)
        c.line(16+side*2,11,16+side*4,6,1)
        c.dot(16+side*4,6,5)
    c.ellipse(16,21,3,6,1);c.ellipse(16,21,2,5,2)
    c.rect(15,19,17,22,4);c.line(15,24,17,24,5)
    c.ellipse(16,14,5,5,1);c.ellipse(16,14,4,4,4)
    c.ellipse(15,12,2,1,5)
    face(c,pose,16,14,2)
    return c.grid


def creatures():
    return [
        dict(id='mote',name='Mote',family='forest',stage=1,palette=FOREST,draw=mote),
        dict(id='glint',name='Glint',family='forest',stage=2,palette=GOLD,draw=glint),
        dict(id='lumen',name='Lumen',family='forest',stage=3,palette=LUMINOUS,draw=lumen),
        dict(id='flicker',name='Flicker',family='wild',stage=1,palette=WILD,draw=flicker),
    ]
