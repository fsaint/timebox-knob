"""Knob stand v2: cradle lid + electronics tray (battery, charger, piezo)."""
import numpy as np, trimesh
from trimesh.creation import cylinder, box
from trimesh.transformations import rotation_matrix, translation_matrix
OUT = "./"   # run from enclosure/src; outputs land here

# ---------------- parameters (mm) ----------------
dev_d, grip_h, tol = 48, 5.5, 0.6      # grip_h = height of FIXED base plate (collar stops 1.5 mm short)           # device
tilt, wall, floor_t, knob_gap, cable_w = 30, 2.4, 3, 1.5, 16
cradle_y, cradle_z = 4, 17                 # cup floor centre on lid (y: + = back)
pad_w, pad_d = 60, 66                      # wedge footprint on lid

in_x, in_y, in_z = 98, 92, 18              # x widened so the piezo's 41 mm ears fit the back-right bay              # tray interior
twall, tfloor, lid_t, clr = 2.4, 2.5, 2.5, 0.4
boss, boss_hole, lid_hole = 7, 2.5, 3.4    # M3 self-tap corner bosses
usb_w, usb_h = 13, 9                       # charger USB-C window in back wall

out_x, out_y = in_x + 2*twall, in_y + 2*twall
tray_h = tfloor + in_z + lid_t             # lid drops flush into the top

cup_h = grip_h - knob_gap
ledge = 4                                   # width of the ring the base sits on
cup_od, cup_id = dev_d + tol + 2*wall, dev_d + tol
T = translation_matrix([0, cradle_y, cradle_z]) @ rotation_matrix(np.radians(tilt), [1, 0, 0])

def cyl(d, h, z0=0.0, sections=128):
    m = cylinder(radius=d/2, height=h, sections=sections); m.apply_translation([0, 0, h/2 + z0]); return m
def cube(size, pos):
    m = box(extents=size); m.apply_translation(np.array(pos) + np.array(size)/2); return m
def tilted(m): m = m.copy(); m.apply_transform(T); return m
def union(parts): return trimesh.boolean.union(parts, engine="manifold")
def diff(a, parts): return trimesh.boolean.difference([a] + parts, engine="manifold")
def finish(m, name):
    m.merge_vertices()
    if not m.is_watertight:
        m.process(validate=True); trimesh.repair.fill_holes(m)
    print(f"{name:12s} watertight={m.is_watertight} vol={m.volume/1000:.1f}cm3 bbox={np.round(m.extents,1)}")
    m.export(OUT + name + ".stl"); m.export(OUT + name + ".3mf"); return m

# ---------------- LID (cradle) : z=0 is lid top, plate below ----------------
lid_x, lid_y = in_x - clr, in_y - clr
plate = cube([lid_x, lid_y, lid_t], [-lid_x/2, -lid_y/2, -lid_t])
wedge = trimesh.util.concatenate([
    tilted(cyl(cup_od, cup_h + floor_t, -floor_t)),
    cube([pad_w, pad_d, 0.2], [-pad_w/2, -pad_d/2 + 6, -0.1]),
]).convex_hull
lid = union([plate, wedge])
corners = [(sx*(in_x/2 - boss/2), sy*(in_y/2 - boss/2)) for sx in (-1, 1) for sy in (-1, 1)]
lid_cuts = [
    tilted(cyl(cup_id, 60)),                                                     # device pocket
    tilted(cyl(dev_d - 2*ledge, 60, -floor_t - 1)),                              # open floor: ring ledge only
    cyl(dev_d - 2*ledge - 4, cradle_z + lid_t + 2, -lid_t - 1).apply_translation([0, cradle_y + 6, 0]),  # straight drop into box
] + [cyl(lid_hole, 10, -lid_t - 1).apply_translation([cx, cy, 0]) for cx, cy in corners]
lid = finish(diff(lid, lid_cuts), "lid")

# ---------------- TRAY : z=0 is desk ----------------
tray = cube([out_x, out_y, tray_h], [-out_x/2, -out_y/2, 0])
tray = diff(tray, [cube([in_x, in_y, in_z + lid_t + 1], [-in_x/2, -in_y/2, tfloor])])
# corner bosses up to the lid ledge
bosses = [cube([boss, boss, in_z], [cx - boss/2, cy - boss/2, tfloor]) for cx, cy in corners]
# bay ribs (1.6 mm tall) : battery front-left, amp strip right, charger back-left, piezo back-right
rib_h, rib_t = 1.6, 1.2
bx0, by0 = -in_x/2, -in_y/2                 # inner front-left corner
bat_x, bat_y = 64, 56
ribs = [
    cube([rib_t, bat_y + rib_t, rib_h], [bx0 + bat_x, by0, tfloor]),           # battery | amp strip
    cube([bat_x, rib_t, rib_h], [bx0, by0 + bat_y, tfloor]),           # battery | charger
    cube([rib_t, in_y - bat_y, rib_h], [bx0 + 53, by0 + bat_y, tfloor]),  # charger | piezo
]
tray = union([tray] + bosses + ribs)
# piezo centre (back-right bay) and sound slots through the floor
pz_c = (bx0 + 53 + (in_x - 53)/2, by0 + bat_y + (in_y - bat_y)/2)
slots = [cube([1.6, 22, tfloor + 2], [pz_c[0] - 0.8 + dx, pz_c[1] - 11, -1]) for dx in (-9, -4.5, 0, 4.5, 9)]
tray_cuts = slots + [
    cyl(boss_hole, in_z, tfloor + 3).apply_translation([cx, cy, 0]) for cx, cy in corners
] + [
    # charger USB-C window, back wall, left bay
    cube([usb_w, twall + 2, usb_h], [bx0 + 53/2 - usb_w/2, in_y/2 - 1, tfloor]),
    # power switch (Adafruit 805) actuator slot, left wall, charger bay; body glued behind the wall
    cube([twall + 2, 7, 4.5], [-out_x/2 - 1, by0 + bat_y + 2, tfloor + 6]),
    # finger notch to lift the lid, front wall
    cube([18, twall + 2, lid_t + 1.5], [-9, -out_y/2 - 1, tray_h - lid_t - 1.5]),
]
tray = finish(diff(tray, tray_cuts), "tray")

# ---------------- preview ----------------
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
def add(ax, m, color, alpha=1.0):
    pc = Poly3DCollection(m.vertices[m.faces], alpha=alpha, linewidths=0.05)
    pc.set_facecolor(color); pc.set_edgecolor((0, 0, 0, 0.12)); ax.add_collection3d(pc)

lid_on = lid.copy(); lid_on.apply_translation([0, 0, tray_h])
lid_up = lid.copy(); lid_up.apply_translation([0, 0, tray_h + 30])
device = tilted(cyl(dev_d, 33, 0, 96)); device.apply_translation([0, 0, tray_h])
bat = cube([62, 54, 7], [bx0 + 1, by0 + 1, tfloor]); chg = cube([51, 25, 10], [bx0 + 1, in_y/2 - 26, tfloor])
amp = cube([22.5, 17.7, 10], [bx0 + bat_x + 2, by0 + 4, tfloor]); pz = cyl(30, 2.5, tfloor).apply_translation([*pz_c, 0])

fig = plt.figure(figsize=(15, 6), dpi=130)
for i, (title, el, az, exploded) in enumerate([("assembled", 22, -55, False), ("exploded", 30, -55, True), ("tray layout (top)", 88, -90, None)]):
    ax = fig.add_subplot(1, 3, i + 1, projection="3d")
    add(ax, tray, "#5b8def")
    for m, c in ((bat, "#e0a84a"), (chg, "#4abf8a"), (amp, "#c75ad4"), (pz, "#888888")): add(ax, m, c)
    if exploded is False: add(ax, lid_on, "#5b8def"); add(ax, device, "#333333", 0.35)
    elif exploded: add(ax, lid_up, "#5b8def")
    ax.set_xlim(-60, 60); ax.set_ylim(-60, 60); ax.set_zlim(0, 120)
    ax.set_box_aspect((1, 1, 1)); ax.view_init(el, az); ax.set_title(title); ax.set_axis_off()
plt.tight_layout(); plt.savefig(OUT + "enclosure.png")
