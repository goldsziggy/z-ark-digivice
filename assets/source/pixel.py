"""Small integer-only pixel drawing vocabulary for the original editable art.

Coordinates are inclusive. Palette index zero means transparency. No external
image, font, sprite, tracing source, antialiasing, or runtime dependency is used.
"""

class Canvas:
    def __init__(self, w=32, h=32):
        self.w, self.h = w, h
        self.grid = [[0 for _ in range(w)] for _ in range(h)]

    def dot(self, x, y, color):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.grid[y][x] = color

    def rect(self, x0, y0, x1, y1, color):
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                self.grid[y][x] = color

    def ellipse(self, cx, cy, rx, ry, color):
        if rx == 0 or ry == 0:
            self.rect(cx-rx, cy-ry, cx+rx, cy+ry, color)
            return
        for y in range(cy-ry, cy+ry+1):
            for x in range(cx-rx, cx+rx+1):
                if (x-cx)**2 * ry**2 + (y-cy)**2 * rx**2 <= rx**2 * ry**2:
                    self.dot(x, y, color)

    def line(self, x0, y0, x1, y1, color):
        dx, dy = abs(x1-x0), -abs(y1-y0)
        sx, sy = 1 if x0 < x1 else -1, 1 if y0 < y1 else -1
        error = dx + dy
        while True:
            self.dot(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            twice = 2 * error
            if twice >= dy:
                error += dy
                x0 += sx
            if twice <= dx:
                error += dx
                y0 += sy

    def polygon(self, points, color):
        # Pixel-centre even/odd fill plus Bresenham edges, deterministic on ints.
        minimum = min(p[1] for p in points)
        maximum = max(p[1] for p in points)
        for y in range(minimum, maximum + 1):
            xs = []
            for a, b in zip(points, points[1:] + points[:1]):
                if (a[1] <= y < b[1]) or (b[1] <= y < a[1]):
                    xs.append(a[0] + (y-a[1]) * (b[0]-a[0]) / (b[1]-a[1]))
            xs.sort()
            for left, right in zip(xs[::2], xs[1::2]):
                for x in range(int(left), int(right) + 1):
                    self.dot(x, y, color)
        for a, b in zip(points, points[1:] + points[:1]):
            self.line(*a, *b, color)


def translate(grid, dx=0, dy=0):
    canvas = Canvas(len(grid[0]), len(grid))
    for y, row in enumerate(grid):
        for x, value in enumerate(row):
            canvas.dot(x+dx, y+dy, value)
    return canvas.grid


def face(canvas, pose, x=16, y=17, gap=4):
    """Shared family eye language, with distinct visible pose expressions."""
    for eye in [x-gap, x+gap]:
        if pose == 'sleep':
            canvas.line(eye-1, y+1, eye+1, y+1, 1)
            canvas.dot(eye-2, y, 1)
        elif pose == 'hurt':
            canvas.line(eye-1, y, eye+1, y+2, 1)
            canvas.line(eye+1, y, eye-1, y+2, 1)
        elif pose in ('care', 'celebrate'):
            canvas.dot(eye, y, 1)
            canvas.dot(eye-1, y+1, 1)
            canvas.dot(eye+1, y+1, 1)
        else:
            canvas.rect(eye-1, y-1, eye+1, y+2, 1)
            canvas.rect(eye, y, eye+1, y+1, 6)
            canvas.dot(eye-1, y-1, 7)
            if pose == 'attack':
                canvas.line(eye-2, y-2, eye+1, y-1, 1)
    canvas.dot(x, y+3, 1)
    if pose in ('care', 'celebrate'):
        canvas.rect(x-1, y+3, x+1, y+4, 1)
        canvas.dot(x, y+4, 8)
    canvas.dot(x-gap-2, y+3, 8)
    canvas.dot(x+gap+2, y+3, 8)
