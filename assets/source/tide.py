"""Original Tide creatures, authored as editable, dependency-free pixel shapes.

CC0-1.0. Rill, Brine and Pelagia are original characters for this project.
All six poses are hand composed on the same transparent 32 x 32 canvas.
"""

from pixel import Canvas


# 0 is transparent. Keep the first ten semantic colours consistent across species.
PALETTE = [
    "#000000", "#102c49", "#15788f", "#22c4c7",
    "#80ece0", "#d6fff0", "#082139", "#fffdf4",
    "#ed786e", "#ffc89a", "#115b78", "#4dd9f0",
    "#408da1", "#a8cfce", "#f5a99a", "#236eaf",
]
POSES = {"idle", "attack", "hurt", "sleep", "care", "celebrate"}


def _shape(c, points, colour, outline=1):
    c.polygon(points, colour)
    if outline is not None:
        for a, b in zip(points, points[1:] + points[:1]):
            c.line(a[0], a[1], b[0], b[1], outline)


def _oval(c, x, y, rx, ry, colour):
    c.ellipse(x, y, rx, ry, 1)
    c.ellipse(x, y, max(0, rx - 1), max(0, ry - 1), colour)


def _eye(c, x, y, pose, right=False):
    """A compact three-pixel face feature, with actual eyelid/pupil poses."""
    if pose == "sleep":
        c.line(x, y + 1, x + 2, y + 1, 1)
        c.dot(x + (2 if right else 0), y, 1)
    elif pose == "hurt":
        c.dot(x, y, 1)
        c.dot(x + 1, y + 1, 1)
        c.dot(x, y + 2, 1)
    elif pose in ("care", "celebrate"):
        c.line(x, y + 1, x + 1, y, 1)
        c.line(x + 1, y, x + 2, y + 1, 1)
    else:
        c.rect(x, y, x + 2, y + 2, 7)
        c.rect(x + 1, y + 1, x + 2, y + 2, 6)
        c.dot(x, y, 5)
        if pose == "attack":
            c.line(x, y - 1, x + 2, y, 1)


def _mouth(c, x, y, pose):
    if pose == "attack":
        c.rect(x, y, x + 2, y + 1, 1)
        c.dot(x, y, 7)
    elif pose == "celebrate":
        c.rect(x, y, x + 2, y + 1, 1)
        c.dot(x + 1, y + 1, 8)
    elif pose == "hurt":
        c.line(x, y, x + 2, y, 1)
    else:
        c.dot(x, y, 1)
        c.dot(x + 1, y + 1, 1)
        c.dot(x + 2, y, 1)


def rill(pose="idle"):
    """A small tide-pool tadpole: forked tail, coral forelock and cheek gills."""
    pose = pose if pose in POSES else "idle"
    c = Canvas()

    # The forked tail changes its spread and pitch independently of the body.
    if pose == "sleep":
        tail = [(11, 18), (7, 20), (5, 19), (5, 24), (9, 25), (13, 22)]
    elif pose == "attack":
        tail = [(12, 15), (8, 14), (4, 11), (5, 17), (4, 21), (9, 20), (12, 21)]
    elif pose == "celebrate":
        tail = [(12, 17), (8, 15), (5, 12), (5, 18), (4, 22), (10, 22), (13, 20)]
    elif pose == "hurt":
        tail = [(12, 17), (7, 16), (5, 17), (7, 20), (5, 23), (10, 22), (13, 21)]
    else:
        tail = [(12, 16), (8, 15), (5, 13), (6, 18), (4, 23), (10, 22), (13, 21)]
    _shape(c, tail, 3)
    c.line(7, 19, 11, 19, 4)
    c.dot(7, 18, 4)

    # Three pointed gill lobes and a swept coral forelock, rather than a round blob.
    _shape(c, [(12, 13), (8, 11), (9, 8), (12, 10), (12, 6), (15, 9), (15, 13)], 8)
    c.line(11, 10, 13, 12, 9)
    _shape(c, [(15, 10), (16, 6), (20, 4), (19, 8), (22, 7), (21, 12)], 8)
    c.line(17, 8, 19, 6, 9)
    _shape(c, [(10, 14), (12, 11), (16, 10), (21, 10), (24, 13),
               (25, 18), (24, 22), (21, 24), (15, 24), (12, 22), (10, 18)], 3)
    _shape(c, [(11, 17), (14, 20), (22, 20), (24, 18), (23, 22),
               (20, 23), (15, 23), (12, 21)], 2, None)
    _shape(c, [(14, 19), (18, 18), (22, 19), (22, 22), (20, 23), (16, 22)], 5, None)
    c.line(14, 12, 18, 11, 4)
    c.line(12, 14, 13, 13, 4)
    c.dot(21, 12, 4)

    # Near fin is folded against the belly for care, tucked for sleep, thrust for attack.
    if pose == "attack":
        _shape(c, [(22, 19), (25, 17), (27, 15), (27, 19), (24, 22)], 4)
        c.dot(26, 17, 5)
    elif pose == "celebrate":
        _shape(c, [(23, 19), (25, 14), (27, 12), (27, 18), (25, 21)], 4)
    elif pose == "care":
        _shape(c, [(23, 18), (24, 22), (20, 22), (18, 20), (21, 20)], 4)
    elif pose in ("sleep", "hurt"):
        _shape(c, [(23, 19), (25, 21), (24, 24), (22, 22)], 2)
    else:
        _shape(c, [(23, 18), (26, 19), (27, 22), (24, 22), (22, 20)], 4)

    _eye(c, 15, 14, pose)
    _eye(c, 21, 14, pose, True)
    _mouth(c, 18, 18, pose)
    c.dot(13, 18, 8)
    c.dot(24, 17, 8)
    # A pair of small pectoral gill marks stays legible even at native scale.
    c.dot(11, 16, 10)
    c.dot(12, 18, 10)
    return c.grid


def brine(pose="idle"):
    """An armoured reef ray with sail-shaped fins and an asymmetric coral crest."""
    pose = pose if pose in POSES else "idle"
    c = Canvas()

    if pose == "sleep":
        left = [(12, 13), (8, 16), (7, 24), (11, 26), (14, 20)]
        right = [(20, 13), (25, 17), (25, 24), (21, 26), (18, 20)]
    elif pose == "attack":
        left = [(12, 12), (6, 9), (4, 8), (4, 16), (8, 19), (13, 20)]
        right = [(20, 12), (24, 10), (27, 7), (27, 16), (24, 19), (19, 20)]
    elif pose == "celebrate":
        left = [(12, 13), (8, 9), (5, 4), (4, 13), (7, 18), (13, 21)]
        right = [(20, 13), (24, 9), (26, 4), (27, 13), (24, 18), (19, 21)]
    elif pose == "care":
        left = [(12, 13), (7, 15), (5, 20), (10, 23), (15, 20)]
        right = [(20, 13), (25, 15), (27, 20), (22, 23), (17, 20)]
    elif pose == "hurt":
        left = [(12, 12), (8, 12), (6, 16), (5, 21), (10, 23), (14, 20)]
        right = [(20, 12), (25, 11), (26, 16), (25, 22), (20, 24), (18, 20)]
    else:
        left = [(12, 12), (8, 10), (4, 8), (5, 17), (8, 21), (13, 23)]
        right = [(20, 12), (24, 10), (27, 8), (27, 17), (24, 21), (19, 23)]
    _shape(c, left, 3)
    _shape(c, right, 3)
    # Fan rays pick up the wing pose rather than leaving fixed dots outside it.
    for wing, inner in ((left, (11, 17)), (right, (21, 17))):
        tip = wing[2]
        c.line(tip[0], tip[1] + 1, inner[0], inner[1], 2)
        c.line(wing[1][0], wing[1][1] + 1, inner[0], inner[1] - 1, 4)
        c.dot(wing[1][0], wing[1][1] + 1, 9)

    _shape(c, [(12, 22), (11, 27), (15, 25), (17, 28), (19, 25), (23, 27), (21, 21)], 2)
    c.line(16, 24, 17, 26, 4)
    _shape(c, [(13, 11), (12, 7), (15, 8), (17, 3), (19, 7), (22, 5), (21, 12)], 8)
    c.line(17, 6, 17, 9, 9)
    c.dot(20, 8, 9)

    # The carapace is a faceted hexagon with four distinct shell ridges.
    _shape(c, [(13, 9), (19, 9), (22, 12), (22, 20), (20, 24),
               (16, 26), (12, 23), (10, 19), (10, 13)], 3)
    _shape(c, [(11, 17), (16, 20), (21, 17), (21, 21),
               (19, 24), (16, 25), (13, 22)], 2, None)
    _shape(c, [(14, 17), (18, 17), (20, 20), (18, 23), (16, 24), (13, 21)], 5, None)
    c.line(13, 11, 19, 11, 4)
    c.dot(12, 12, 4)
    c.line(12, 20, 14, 22, 10)
    c.line(20, 20, 19, 22, 10)
    c.line(14, 23, 15, 24, 4)
    c.dot(19, 23, 4)

    _eye(c, 12, 13, pose)
    _eye(c, 18, 13, pose, True)
    _mouth(c, 15, 17, pose)
    c.dot(11, 17, 8)
    c.dot(21, 17, 8)

    # An interlocking shell clasp becomes a held pearl in the care pose.
    if pose == "care":
        _shape(c, [(11, 19), (14, 20), (15, 22), (12, 22)], 4)
        _shape(c, [(21, 19), (18, 20), (17, 22), (20, 22)], 4)
        _oval(c, 16, 21, 2, 2, 9)
        c.dot(15, 20, 7)
    else:
        c.line(15, 21, 17, 21, 13)
        c.dot(16, 20, 7)
    return c.grid


def pelagia(pose="idle"):
    """A crowned sea-dragon, with a hooked tail and split coral dorsal sail."""
    pose = pose if pose in POSES else "idle"
    c = Canvas()

    # A curled, segmented tail makes the adult silhouette longer and angular.
    tail = [(17, 19), (18, 23), (15, 27), (10, 28), (6, 26),
            (4, 22), (5, 18), (8, 17), (7, 21), (9, 24), (12, 24), (14, 21)]
    if pose == "attack":
        tail = [(17, 19), (18, 23), (14, 27), (9, 27), (5, 24),
                (4, 20), (6, 17), (8, 17), (7, 21), (10, 23), (13, 23), (14, 20)]
    elif pose == "sleep":
        tail = [(17, 20), (18, 24), (14, 28), (9, 28), (5, 26),
                (4, 23), (6, 21), (8, 22), (8, 24), (12, 25), (14, 22)]
    _shape(c, tail, 3)
    c.line(7, 25, 10, 27, 2)
    c.line(10, 27, 14, 26, 2)
    c.line(9, 24, 12, 25, 4)
    c.dot(6, 22, 4)
    c.line(12, 26, 13, 27, 10)
    c.line(8, 25, 8, 26, 10)

    # Far fin is a broad sail; the near fin below provides the active gesture.
    if pose == "sleep":
        far_fin = [(15, 13), (11, 14), (9, 20), (12, 23), (16, 20)]
    elif pose == "celebrate":
        far_fin = [(16, 13), (10, 8), (7, 3), (6, 11), (10, 16), (15, 20)]
    elif pose == "attack":
        far_fin = [(16, 13), (10, 8), (5, 7), (6, 12), (11, 17), (15, 20)]
    elif pose == "hurt":
        far_fin = [(15, 13), (11, 12), (8, 14), (9, 19), (14, 21)]
    else:
        far_fin = [(16, 13), (11, 9), (7, 7), (6, 14), (10, 18), (15, 21)]
    _shape(c, far_fin, 2)
    c.line(far_fin[1][0], far_fin[1][1] + 1, 14, 17, 4)
    c.line(far_fin[2][0] + 1, far_fin[2][1] + 2, 12, 16, 3)

    _shape(c, [(13, 17), (11, 13), (13, 11), (12, 7), (15, 9),
               (17, 6), (20, 10), (19, 17)], 8)
    c.line(14, 11, 16, 15, 9)
    c.line(15, 10, 16, 8, 9)

    # Serpentine neck with a scalloped pale throat, distinct from the younger stages.
    _shape(c, [(18, 10), (23, 12), (22, 17), (20, 19), (22, 22),
               (21, 25), (18, 27), (14, 25), (13, 22), (14, 18), (14, 14)], 3)
    _shape(c, [(20, 15), (21, 17), (18, 20), (20, 23), (18, 25),
               (16, 24), (15, 21), (17, 18)], 5, None)
    c.line(16, 20, 18, 21, 13)
    c.line(17, 23, 19, 23, 13)
    c.line(14, 22, 15, 24, 2)
    c.dot(17, 26, 2)
    c.line(15, 14, 16, 12, 4)

    # Forked crown and long brow project beyond the neck without filling the canvas.
    _shape(c, [(17, 11), (17, 5), (19, 7), (21, 3), (22, 7), (25, 5), (24, 11)], 8)
    c.line(21, 6, 21, 9, 9)
    c.dot(19, 8, 9)
    c.dot(23, 8, 9)
    _shape(c, [(18, 9), (23, 9), (25, 11), (25, 13), (27, 14),
               (26, 17), (23, 18), (19, 17), (17, 15), (17, 12)], 3)
    _shape(c, [(20, 15), (24, 15), (26, 14), (26, 16), (23, 17), (20, 16)], 5, None)
    c.line(19, 10, 22, 10, 4)
    c.dot(18, 12, 4)

    _eye(c, 21, 12, pose, True)
    c.dot(26, 14, 1)
    if pose in ("attack", "celebrate"):
        c.line(24, 16, 26, 16, 1)
        c.dot(25, 16, 7)
    elif pose == "hurt":
        c.line(24, 16, 25, 16, 1)
    else:
        c.dot(24, 16, 1)
    c.dot(19, 15, 8)
    c.dot(18, 14, 8)

    if pose == "attack":
        near_fin = [(18, 19), (21, 18), (26, 18), (27, 20), (23, 21), (20, 23)]
    elif pose == "celebrate":
        near_fin = [(18, 20), (23, 18), (27, 17), (26, 22), (22, 24), (19, 23)]
    elif pose == "care":
        near_fin = [(18, 19), (22, 21), (23, 24), (20, 24), (17, 22)]
    elif pose == "sleep":
        near_fin = [(18, 19), (21, 22), (22, 26), (19, 25), (17, 22)]
    elif pose == "hurt":
        near_fin = [(18, 19), (21, 21), (20, 25), (17, 23)]
    else:
        near_fin = [(18, 19), (22, 20), (25, 23), (24, 26), (20, 25), (17, 22)]
    _shape(c, near_fin, 3)
    c.line(near_fin[1][0], near_fin[1][1], 19, 22, 4)
    c.dot(18, 21, 5)
    if pose == "care":
        _oval(c, 24, 23, 2, 2, 9)
        c.dot(23, 22, 7)
    return c.grid


def creatures():
    """Builder-facing schema; each draw returns a fresh palette-index matrix."""
    return [
        {"id": "rill", "name": "Rill", "family": "tide", "stage": 1,
         "palette": PALETTE.copy(), "draw": rill},
        {"id": "brine", "name": "Brine", "family": "tide", "stage": 2,
         "palette": PALETTE.copy(), "draw": brine},
        {"id": "pelagia", "name": "Pelagia", "family": "tide", "stage": 3,
         "palette": PALETTE.copy(), "draw": pelagia},
    ]
