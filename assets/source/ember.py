"""Original Ember-family pixel art, authored for Digivice (CC0-1.0).

Every pose is hand-composed on a 32 × 32 indexed canvas. Index 0 is
transparent; the remaining colors are shared across the three stages.
Only the tiny project-local Canvas rasterizer is required.
"""
from pixel import Canvas

PALETTE = [
    '#000000', '#15223b', '#943a40', '#ef6543',
    '#ffae45', '#ffdc87', '#263653', '#fff4cf',
    '#e84965', '#ff8792', '#ba473d', '#f38b3e',
    '#603449', '#dc5740', '#ce783f', '#804148',
]
POSES = ('idle', 'attack', 'hurt', 'sleep', 'care', 'celebrate')


def _pose(pose):
    if pose not in POSES:
        raise ValueError('Unknown creature pose: ' + str(pose))
    return pose


def _eyes(c, pose, x, y, gap=7):
    """A warm, wide-set face; closed lids are drawn, never recolored."""
    right = x + gap
    if pose == 'sleep':
        c.line(x, y + 1, x + 2, y + 1, 1)
        c.line(right, y + 1, right + 2, y + 1, 1)
        c.dot(x, y, 1)
        c.dot(right + 2, y, 1)
    elif pose == 'hurt':
        c.line(x, y, x + 2, y + 1, 1)
        c.line(x + 2, y + 1, x, y + 2, 1)
        c.line(right + 2, y, right, y + 1, 1)
        c.line(right, y + 1, right + 2, y + 2, 1)
    elif pose in ('care', 'celebrate'):
        c.line(x, y + 1, x + 1, y, 1)
        c.dot(x + 2, y + 1, 1)
        c.line(right, y + 1, right + 1, y, 1)
        c.dot(right + 2, y + 1, 1)
    else:
        c.rect(x, y, x + 2, y + 2, 1)
        c.rect(right, y, right + 2, y + 2, 1)
        c.dot(x, y, 7)
        c.dot(right, y, 7)
        if pose == 'attack':
            c.line(x - 1, y - 2, x + 2, y - 1, 1)
            c.line(right, y - 1, right + 3, y - 2, 1)


def _cinder(pose):
    pose = _pose(pose)
    c = Canvas()
    sleeping = pose == 'sleep'
    # A hooked coal-tail gives even the smallest stage a unique silhouette.
    tail = ([(21, 21), (25, 19), (27, 15), (27, 24), (23, 26), (19, 25)]
            if not sleeping else [(20, 23), (26, 22), (27, 25), (22, 27), (18, 26)])
    c.polygon(tail, 1)
    if sleeping:
        c.polygon([(21, 24), (25, 24), (25, 25), (22, 26), (20, 25)], 3)
    else:
        c.polygon([(22, 22), (25, 21), (26, 19), (26, 23), (23, 25), (21, 24)], 3)
        c.line(24, 23, 25, 22, 4)
    c.polygon([(9, 24), (14, 24), (15, 28), (8, 28), (8, 26)], 1)
    c.polygon([(17, 24), (21, 24), (23, 27), (22, 28), (17, 28)], 1)
    c.rect(10, 26, 13, 27, 2)
    c.rect(18, 26, 21, 27, 2)
    c.dot(9, 27, 5)
    c.dot(22, 27, 5)
    # Asymmetric three-tip flame crown and a pear-shaped coal body.
    crown = ([(9, 14), (9, 10), (12, 12), (13, 6), (16, 9), (20, 4), (21, 10), (24, 8), (23, 16)]
             if not sleeping else [(9, 16), (10, 12), (13, 14), (16, 10), (18, 14), (23, 12), (23, 18)])
    c.polygon(crown, 1)
    if sleeping:
        c.polygon([(11, 16), (11, 14), (14, 16), (16, 13), (18, 16), (22, 14), (21, 18)], 4)
    else:
        c.polygon([(10, 14), (10, 12), (13, 14), (14, 9), (17, 12), (19, 8), (20, 12), (22, 11), (21, 16)], 4)
        c.line(15, 12, 16, 14, 5)
    c.polygon([(10, 13), (20, 13), (23, 17), (24, 23), (21, 26), (10, 26), (7, 23), (8, 17)], 1)
    c.polygon([(11, 14), (19, 14), (22, 18), (23, 22), (20, 25), (11, 25), (8, 22), (9, 18)], 3)
    c.polygon([(8, 21), (11, 23), (21, 23), (23, 21), (23, 23), (20, 25), (11, 25)], 2)
    c.polygon([(12, 21), (19, 21), (20, 23), (18, 24), (13, 24), (11, 23)], 4)
    c.line(11, 15, 17, 15, 4)
    c.dot(10, 16, 4)
    if pose == 'attack':
        c.polygon([(8, 18), (5, 16), (4, 17), (4, 21), (8, 23), (10, 21)], 1)
        c.polygon([(6, 18), (5, 19), (8, 21), (9, 20)], 4)
        c.polygon([(22, 17), (25, 15), (27, 16), (27, 20), (24, 22), (22, 21)], 1)
        c.polygon([(24, 18), (26, 17), (26, 19), (24, 20)], 4)
    elif pose == 'celebrate':
        c.polygon([(8, 18), (5, 13), (4, 14), (4, 18), (7, 22), (10, 21)], 1)
        c.line(5, 16, 8, 20, 4)
        c.polygon([(22, 18), (26, 12), (27, 14), (27, 18), (24, 22), (21, 21)], 1)
        c.line(26, 15, 23, 20, 4)
    elif pose in ('care', 'sleep'):
        c.line(9, 21, 12, 23, 1)
        c.line(21, 21, 18, 23, 1)
        c.line(10, 21, 12, 22, 4)
        c.line(20, 21, 18, 22, 4)
    elif pose == 'hurt':
        c.line(8, 19, 11, 22, 1)
        c.line(22, 19, 19, 22, 1)
    else:
        c.line(8, 20, 10, 22, 1)
        c.line(22, 20, 20, 22, 1)
    _eyes(c, pose, 11, 17, 6)
    c.dot(9, 20, 9)
    c.dot(21, 20, 9)
    if pose == 'sleep':
        c.dot(15, 22, 1)
    elif pose == 'attack':
        c.rect(14, 21, 16, 22, 1)
        c.dot(15, 22, 7)
    else:
        c.line(14, 21, 16, 21, 1)
    return c.grid


def _scoria(pose):
    pose = _pose(pose)
    c = Canvas()
    # Forked, mineral-edged tail and grounded digitigrade feet.
    c.polygon([(20, 21), (24, 22), (25, 18), (27, 15), (27, 25), (23, 27), (19, 25)], 1)
    c.polygon([(22, 23), (25, 24), (26, 20), (26, 24), (23, 26), (21, 25)], 4)
    c.polygon([(10, 22), (15, 23), (14, 28), (7, 28), (8, 26)], 1)
    c.polygon([(17, 23), (21, 22), (23, 26), (24, 28), (17, 28)], 1)
    c.rect(10, 25, 13, 27, 2)
    c.rect(18, 25, 21, 27, 2)
    c.line(8, 27, 10, 27, 5)
    c.line(21, 27, 23, 27, 5)
    # Wide jagged flame shoulders; motion changes their outer contour.
    if pose in ('attack', 'celebrate'):
        left = [(11, 16), (7, 12), (4, 8 if pose == 'celebrate' else 12), (4, 18), (6, 18), (7, 21), (12, 22)]
        right = [(20, 16), (24, 12), (27, 8 if pose == 'celebrate' else 12), (27, 18), (25, 18), (24, 21), (19, 22)]
        c.polygon(left, 1)
        c.polygon(right, 1)
        c.polygon([(10, 17), (6, 14), (5, 13), (5, 17), (7, 17), (8, 20), (11, 21)], 3)
        c.polygon([(21, 17), (25, 14), (26, 13), (26, 17), (24, 17), (23, 20), (20, 21)], 3)
        c.line(6, 15, 9, 18, 5)
        c.line(25, 15, 22, 18, 5)
    else:
        c.polygon([(10, 15), (6, 16), (5, 20), (4, 22), (8, 21), (7, 24), (12, 22)], 1)
        c.polygon([(21, 15), (25, 16), (26, 20), (27, 22), (23, 21), (24, 24), (19, 22)], 1)
        c.polygon([(9, 17), (7, 17), (6, 20), (9, 19), (9, 22), (11, 21)], 3)
        c.polygon([(22, 17), (24, 17), (25, 20), (22, 19), (22, 22), (20, 21)], 3)
        c.dot(7, 18, 5)
        c.dot(24, 18, 5)
    c.polygon([(11, 15), (20, 15), (22, 21), (20, 25), (11, 25), (9, 21)], 1)
    c.polygon([(12, 17), (19, 17), (21, 21), (19, 24), (12, 24), (10, 21)], 3)
    c.polygon([(15, 17), (18, 19), (18, 22), (15, 24), (13, 21)], 4)
    c.line(15, 19, 16, 21, 5)
    # Tall split horns frame the horizontal face mask, unlike Cinder's crown.
    c.polygon([(9, 13), (8, 5), (11, 7), (12, 4), (15, 10), (17, 10), (20, 4), (21, 7), (24, 5), (23, 13)], 1)
    c.polygon([(10, 12), (10, 8), (12, 10), (12, 7), (14, 12)], 4)
    c.polygon([(18, 12), (20, 7), (20, 10), (22, 8), (22, 12)], 4)
    c.polygon([(10, 10), (21, 10), (24, 13), (22, 18), (19, 20), (12, 20), (9, 18), (7, 13)], 1)
    c.polygon([(11, 11), (20, 11), (23, 13), (21, 17), (18, 19), (13, 19), (10, 17), (8, 13)], 3)
    c.polygon([(10, 13), (14, 12), (16, 13), (18, 12), (22, 13), (20, 17), (11, 17)], 4)
    c.line(13, 11, 18, 11, 5)
    _eyes(c, pose, 10, 14, 8)
    c.dot(9, 17, 9)
    c.dot(22, 17, 9)
    if pose == 'attack':
        c.rect(14, 17, 17, 18, 1)
        c.dot(14, 17, 7)
        c.dot(17, 17, 7)
    elif pose == 'sleep':
        c.dot(15, 18, 1)
    else:
        c.line(14, 18, 17, 18, 1)
    # Hands rest together, guard the face, or rise independently of shoulders.
    if pose in ('care', 'sleep'):
        c.polygon([(10, 20), (13, 21), (14, 23), (11, 23), (9, 21)], 1)
        c.polygon([(21, 20), (18, 21), (17, 23), (20, 23), (22, 21)], 1)
        c.line(11, 21, 12, 22, 5)
        c.line(20, 21, 19, 22, 5)
    elif pose == 'hurt':
        c.line(10, 21, 12, 18, 1)
        c.line(21, 21, 19, 18, 1)
        c.dot(12, 19, 5)
        c.dot(19, 19, 5)
    return c.grid


def _pyrel(pose):
    pose = _pose(pose)
    c = Canvas()
    # A three-point ceremonial flame cloak, long neck and sweeping crown.
    c.polygon([(13, 17), (19, 17), (23, 23), (26, 27), (20, 26), (20, 28), (16, 26), (12, 28), (12, 26), (6, 27), (10, 23)], 1)
    c.polygon([(14, 19), (18, 19), (21, 24), (23, 25), (19, 24), (19, 26), (16, 24), (13, 26), (13, 24), (9, 25), (12, 23)], 2)
    c.polygon([(15, 19), (17, 19), (19, 24), (16, 23), (13, 25)], 3)
    c.line(16, 20, 16, 23, 4)
    # The grand stage has broad featherless fire wings, narrow when sleeping.
    if pose in ('attack', 'celebrate'):
        wing_y = 5 if pose == 'celebrate' else 7
        c.polygon([(12, 17), (9, 11), (4, wing_y), (4, 17), (6, 16), (5, 21), (8, 20), (8, 24), (13, 21)], 1)
        c.polygon([(20, 17), (23, 11), (27, wing_y), (27, 17), (25, 16), (26, 21), (23, 20), (23, 24), (19, 21)], 1)
        c.polygon([(11, 18), (8, 13), (5, wing_y + 3), (5, 15), (7, 14), (7, 18), (10, 18), (10, 22), (12, 20)], 3)
        c.polygon([(21, 18), (24, 13), (26, wing_y + 3), (26, 15), (24, 14), (24, 18), (21, 18), (21, 22), (20, 20)], 3)
        c.line(6, 12 if pose == 'celebrate' else 13, 10, 17, 4)
        c.line(25, 12 if pose == 'celebrate' else 13, 21, 17, 4)
        c.dot(6, 11 if pose == 'celebrate' else 12, 5)
        c.dot(25, 11 if pose == 'celebrate' else 12, 5)
    elif pose == 'sleep':
        c.polygon([(12, 14), (8, 16), (8, 21), (10, 24), (14, 23), (16, 20)], 1)
        c.polygon([(20, 14), (24, 16), (24, 21), (22, 24), (18, 23), (16, 20)], 1)
        c.polygon([(12, 16), (9, 17), (10, 21), (12, 22), (14, 21)], 3)
        c.polygon([(20, 16), (23, 17), (22, 21), (20, 22), (18, 21)], 3)
        c.line(10, 18, 13, 21, 4)
        c.line(22, 18, 19, 21, 4)
    else:
        c.polygon([(12, 13), (8, 12), (4, 10), (5, 17), (7, 16), (6, 21), (9, 20), (9, 24), (14, 21)], 1)
        c.polygon([(20, 13), (24, 12), (27, 10), (26, 17), (24, 16), (25, 21), (22, 20), (22, 24), (18, 21)], 1)
        c.polygon([(11, 15), (8, 14), (6, 13), (6, 15), (9, 15), (8, 19), (11, 18), (11, 21), (13, 20)], 3)
        c.polygon([(21, 15), (24, 14), (25, 13), (25, 15), (22, 15), (23, 19), (20, 18), (20, 21), (19, 20)], 3)
        c.line(7, 14, 11, 16, 4)
        c.line(24, 14, 21, 16, 4)
    c.polygon([(12, 14), (20, 14), (21, 19), (19, 23), (16, 25), (12, 22), (10, 19)], 1)
    c.polygon([(13, 15), (19, 15), (20, 19), (18, 22), (16, 23), (13, 21), (11, 19)], 3)
    # The pale ember jewel is repeated from Scoria's chest as family identity.
    c.polygon([(16, 16), (19, 19), (16, 22), (13, 19)], 4)
    c.polygon([(16, 17), (17, 19), (16, 20), (15, 19)], 5)
    c.dot(16, 18, 7)
    c.polygon([(10, 11), (9, 5), (12, 7), (13, 3), (16, 6), (19, 3), (20, 7), (23, 5), (22, 11)], 1)
    c.polygon([(11, 10), (11, 7), (13, 9), (14, 5), (16, 8), (18, 5), (19, 9), (21, 7), (21, 10)], 4)
    c.dot(14, 6, 5)
    c.dot(18, 6, 5)
    c.polygon([(11, 8), (20, 8), (23, 11), (21, 15), (17, 18), (14, 18), (10, 15), (8, 11)], 1)
    c.polygon([(12, 9), (19, 9), (22, 11), (20, 14), (17, 17), (14, 17), (11, 14), (9, 11)], 3)
    c.polygon([(11, 10), (14, 10), (16, 12), (18, 10), (21, 10), (20, 14), (17, 16), (14, 16), (11, 14)], 4)
    c.line(13, 9, 18, 9, 5)
    _eyes(c, pose, 11, 12, 7)
    c.polygon([(15, 14), (17, 14), (16, 16)], 1)
    c.dot(16, 14, 5)
    if pose == 'care':
        c.line(11, 19, 14, 21, 1)
        c.line(20, 19, 18, 21, 1)
        c.dot(13, 20, 5)
        c.dot(19, 20, 5)
    elif pose == 'hurt':
        c.line(11, 19, 13, 16, 1)
        c.line(20, 19, 18, 16, 1)
        c.dot(13, 17, 5)
        c.dot(18, 17, 5)
    return c.grid


def creatures():
    """Return the family descriptors expected by the asset-pack builder."""
    return [
        {'id': 'cinder', 'name': 'Cinder', 'family': 'ember', 'stage': 1,
         'palette': PALETTE.copy(), 'draw': _cinder},
        {'id': 'scoria', 'name': 'Scoria', 'family': 'ember', 'stage': 2,
         'palette': PALETTE.copy(), 'draw': _scoria},
        {'id': 'pyrel', 'name': 'Pyrel', 'family': 'ember', 'stage': 3,
         'palette': PALETTE.copy(), 'draw': _pyrel},
    ]
