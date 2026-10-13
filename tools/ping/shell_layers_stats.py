# -*- coding: utf-8 -*-
"""從 G-code 看磚（x<52）每一層有沒有實心類擠出（Top surface／Bottom surface／Internal solid infill／Bridge），
印出實心層的層號區段與中間層的型別統計。用法：python shell_layers_stats.py <gcode>"""
import re, sys, collections
path = sys.argv[1]
SOLID = {"Top surface", "Bottom surface", "Internal solid infill", "Bridge", "Internal Bridge", "Bridge infill"}
rx, ry, re_ = re.compile(r"X(-?[\d.]+)"), re.compile(r"Y(-?[\d.]+)"), re.compile(r"E(-?[\d.]+)")
x = y = None; cur_type = None; layer = 0
per_layer = collections.defaultdict(collections.Counter)
for line in open(path, encoding="utf-8", errors="replace"):
    if line.startswith(";LAYER_CHANGE"): layer += 1; continue
    if line.startswith(";TYPE:"): cur_type = line.strip()[6:]; continue
    if not line.startswith("G1"): continue
    m = rx.search(line); n = ry.search(line); e = re_.search(line)
    if m: x = float(m.group(1))
    if n: y = float(n.group(1))
    if e and float(e.group(1)) > 0 and (m or n) and x is not None and x < 52 and cur_type:
        per_layer[layer][cur_type] += 1
solid_layers = sorted(L for L, c in per_layer.items() if any(t in SOLID for t in c))
print("layers:", layer, "| layers with solid-type extrusion on tile:", len(solid_layers))
def runs(v):
    out = []; s = None; prev = None
    for k in v:
        if s is None: s = prev = k
        elif k == prev + 1: prev = k
        else: out.append((s, prev)); s = prev = k
    if s is not None: out.append((s, prev))
    return out
print("solid layer runs:", runs(solid_layers))
mid = [L for L in per_layer if 6 < L < layer - 5]
mid_types = collections.Counter()
for L in mid: mid_types.update(per_layer[L].keys())
print("types seen in middle layers (7..%d):" % (layer - 6), dict(mid_types))
for L in (1, 2, 3, 4, 5, layer - 4, layer - 3, layer - 2, layer - 1, layer):
    print("  layer %3d: %s" % (L, dict(per_layer.get(L, {}))))
