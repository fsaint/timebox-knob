"""1:1 cardboard net for the knob stand (tray + lid + 30° ramp). Letter pages, mm units."""
import numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, Polygon
from matplotlib.backends.backend_pdf import PdfPages

PAGE_W, PAGE_H = 215.9, 279.4   # Letter
CUT = dict(color="k", lw=0.9)
FOLD = dict(color="k", lw=0.9, ls=(0, (4, 3)), zorder=5)
GLUE = dict(facecolor="#dddddd", edgecolor="k", lw=0.6)

# --- dimensions (mm) : same as 3D v4 ---
TX, TY, TZ = 103, 97, 23      # tray outer
T = 2                         # cardboard thickness allowance
LX, LY, LZ = TX + 2*T, TY + 2*T, 12   # lid slides over tray
TAB = 14
DEV = 40                      # opening under the device: the Ø48 fixed base plate rests on the rim
RAMP_W, RAMP_L = 58, 62       # ramp slant face
RISE, RUN = RAMP_L*np.sin(np.radians(30)), RAMP_L*np.cos(np.radians(30))

def page(pdf, title, draw):
    fig = plt.figure(figsize=(8.5, 11))
    ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, PAGE_W); ax.set_ylim(0, PAGE_H); ax.set_aspect("equal"); ax.axis("off")
    ax.text(12, PAGE_H-10, title, fontsize=13, weight="bold", va="top")
    ax.text(12, PAGE_H-17, "Print at 100 % / actual size. Solid = cut, dashed = fold (score first), grey = glue tab. "
            "Check: bar below must measure 50 mm.", fontsize=7.5, va="top")
    ax.plot([12, 62], [PAGE_H-22, PAGE_H-22], color="k", lw=1.5); ax.plot([12, 12], [PAGE_H-24, PAGE_H-20], color="k", lw=1); ax.plot([62, 62], [PAGE_H-24, PAGE_H-20], color="k", lw=1)
    ax.text(37, PAGE_H-27, "50 mm", fontsize=7, ha="center", va="top")
    draw(ax); pdf.savefig(fig); plt.close(fig)

def rect(ax, x, y, w, h, style=CUT, **kw): ax.add_patch(Rectangle((x, y), w, h, fill=False, **{**style, **kw}))
def fold(ax, x0, y0, x1, y1): ax.plot([x0, x1], [y0, y1], **FOLD)
def tab(ax, pts): ax.add_patch(Polygon(pts, closed=True, **GLUE))
def label(ax, x, y, s, size=7, rot=0): ax.text(x, y, s, fontsize=size, ha="center", va="center", rotation=rot)

def cross_net(ax, ox, oy, W, D, H, tabs, name, walls_extra=None):
    """Open box net: base W×D at (ox,oy), walls H on 4 sides, glue tabs on the two side walls' ends."""
    # base
    rect(ax, ox, oy, W, D, style=dict(color="k", lw=0.001)); label(ax, ox+W/2, oy+D/2, name, 9)
    # walls
    for (x, y, w, h, side) in [(ox, oy+D, W, H, "back"), (ox, oy-H, W, H, "front"), (ox-H, oy, H, D, "left"), (ox+W, oy, H, D, "right")]:
        rect(ax, x, y, w, h); label(ax, x+w/2, y+h/2, side + " wall", 6, 90 if side in ("left", "right") else 0)
    fold(ax, ox, oy, ox+W, oy); fold(ax, ox, oy+D, ox+W, oy+D); fold(ax, ox, oy, ox, oy+D); fold(ax, ox+W, oy, ox+W, oy+D)
    # tabs on the left/right walls (top & bottom ends), fold to glue inside front/back walls
    for x in (ox-H, ox+W):
        tab(ax, [(x+1.5, oy+D), (x+H-1.5, oy+D), (x+H-3, oy+D+tabs), (x+3, oy+D+tabs)])
        tab(ax, [(x+1.5, oy), (x+H-1.5, oy), (x+H-3, oy-tabs), (x+3, oy-tabs)])
        fold(ax, x, oy+D, x+H, oy+D); fold(ax, x, oy, x+H, oy)
    # outer cut outline is implied by the rectangles; add explicit cut lines where walls meet base corners
    if walls_extra: walls_extra(ax, ox, oy, W, D, H)

with PdfPages("./cardboard_template.pdf") as pdf:
    # ---------- PAGE 1 : tray ----------
    def tray_extra(ax, ox, oy, W, D, H):
        # piezo sound slots, back-right of base
        px, py = ox + W - 24, oy + D - 20
        for dx in (-9, -4.5, 0, 4.5, 9): rect(ax, px+dx-0.8, py-11, 1.6, 22)
        label(ax, px, py+15, "piezo slots", 5.5)
        # USB-C window in back wall, left bay (window touches the fold = floor level)
        rect(ax, ox + 26 - 6.5, oy + D, 13, 9); label(ax, ox+26, oy+D+13, "USB-C", 5.5)
        # finger notch, front wall top edge
        rect(ax, ox + W/2 - 9, oy - H, 18, 4); label(ax, ox+W/2, oy-H+7, "finger notch", 5.5)
        # bay layout hints (not cut)
        ax.text(ox+32, oy+28, "battery\n62×54", fontsize=6, ha="center", color="#777")
        ax.text(ox+26, oy+D-18, "charger\n51×25, USB-C to back", fontsize=5.5, ha="center", color="#777")
        ax.text(ox+W-17, oy+28, "amp", fontsize=6, ha="center", color="#777")
        
    def p1(ax): cross_net(ax, (PAGE_W-TX)/2, (PAGE_H-TY)/2 - 12, TX, TY, TZ, TAB, "TRAY  base 103 × 97", tray_extra)
    page(pdf, "Knob stand — cardboard net 1/3 : TRAY (walls 23 mm)", p1)

    # ---------- PAGE 2 : lid ----------
    def lid_extra(ax, ox, oy, W, D, H):
        # drop hole for cables, under the ramp, slightly behind centre
        ax.add_patch(Circle((ox+W/2, oy+D/2+8), 20, fill=False, **CUT)); label(ax, ox+W/2, oy+D/2+8, "cable hole Ø40", 6)
        # ramp footprint (glue guide, not cut)
        rect(ax, ox+W/2-RAMP_W/2, oy+D/2-RUN/2+8, RAMP_W, RUN, style=dict(color="#999", lw=0.6, ls=":"))
        label(ax, ox+W/2, oy+D/2-RUN/2+4, "glue ramp here (dotted)", 5.5)
    def p2(ax): cross_net(ax, (PAGE_W-LX)/2, (PAGE_H-LY)/2 - 12, LX, LY, LZ, 10, "LID  107 × 101 (slides over tray)", lid_extra)
    page(pdf, "Knob stand — cardboard net 2/3 : LID (skirt 12 mm)", p2)

    # ---------- PAGE 3 : 30° ramp ----------
    def p3(ax):
        ox, oy = (PAGE_W-RAMP_W)/2 - 20, 60
        # strip: bottom (RUN) - slant (RAMP_L) - back (RISE), vertical stack, plus glue tab
        y = oy
        rect(ax, ox, y, RAMP_W, RUN); label(ax, ox+RAMP_W/2, y+RUN/2, "bottom (glue to lid)", 7)
        ax.add_patch(Circle((ox+RAMP_W/2, y+RUN/2), 20, fill=False, **CUT)); label(ax, ox+RAMP_W/2, y+RUN/2-24, "cable hole Ø40", 5.5)
        y += RUN; fold(ax, ox, y, ox+RAMP_W, y)
        rect(ax, ox, y, RAMP_W, RAMP_L); label(ax, ox+RAMP_W/2, y+RAMP_L-6, "slant face 30°", 7)
        ax.add_patch(Circle((ox+RAMP_W/2, y+RAMP_L/2), DEV/2, fill=False, **CUT)); label(ax, ox+RAMP_W/2, y+RAMP_L/2, f"opening Ø{DEV}\nØ48 base plate sits on\nthe rim, connectors\nhang through", 6)
        y += RAMP_L; fold(ax, ox, y, ox+RAMP_W, y)
        rect(ax, ox, y, RAMP_W, RISE); label(ax, ox+RAMP_W/2, y+RISE/2, "back", 7)
        y += RISE; fold(ax, ox, y, ox+RAMP_W, y)
        tab(ax, [(ox+2, y), (ox+RAMP_W-2, y), (ox+RAMP_W-4, y+10), (ox+4, y+10)])
        # two triangular end caps with tabs, to the right
        tx, ty = ox + RAMP_W + 22, oy
        for i in range(2):
            tri = [(tx, ty), (tx+RUN, ty), (tx+RUN, ty+RISE)]
            ax.add_patch(Polygon(tri, closed=True, fill=False, **CUT))
            # tabs along bottom and vertical edges
            tab(ax, [(tx+2, ty), (tx+RUN-2, ty), (tx+RUN-4, ty-8), (tx+4, ty-8)])
            tab(ax, [(tx+RUN, ty+2), (tx+RUN, ty+RISE-2), (tx+RUN+8, ty+RISE-4), (tx+RUN+8, ty+4)])
            label(ax, tx+RUN*0.62, ty+RISE*0.3, f"end cap {i+1}", 6)
            ty += RISE + 20
        ax.text(ox, oy-14, "Assembly: fold the strip into a wedge (bottom → slant → back), glue the tab under the bottom.\n"
                "Glue the two end caps into the open sides. Then glue the wedge onto the lid over its Ø40 hole.\n"
                "Device: its fixed base plate sits on the rim of the Ø40 opening (double-sided tape or its M2.5 screws);\n"
                "only the base touches cardboard, so the knob turns freely.", fontsize=6.5, va="top")
    page(pdf, "Knob stand — cardboard net 3/3 : 30° RAMP (wedge)", p3)
print("ok")
