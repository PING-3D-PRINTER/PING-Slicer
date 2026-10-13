# -*- coding: utf-8 -*-
"""從 G-code 統計「外牆迴圈起點（縫）」落在磚的正面／背面／其他（側邊或色塊交界）的比例。
迴圈起點＝在 ;TYPE:Outer wall 區間內，一段不擠出的移動（travel）之後第一個擠出移動的起點。
（Orca 只在型別改變時才寫 ;TYPE，同層多個色塊的外牆共用一個標記，所以不能只看標記後第一段。）
磚的 y 範圍只從外牆擠出點取（brim／塔不算）；塔在 +X≈60，用 x<52 排除。
用法：python seam_stats.py <gcode>"""
import re, sys, collections
path = sys.argv[1]
X_TILE_MAX = 52.0
rx, ry, re_ = re.compile(r"X(-?[\d.]+)"), re.compile(r"Y(-?[\d.]+)"), re.compile(r"E(-?[\d.]+)")
x = y = None
cur_type = None
layer = 0
travelled = False
wall_pts_y = []
seams = []
for line in open(path, encoding="utf-8", errors="replace"):
    if line.startswith(";LAYER_CHANGE"):
        layer += 1; travelled = False; continue
    if line.startswith(";TYPE:"):
        cur_type = line.strip()[6:]; travelled = False; continue
    if not line.startswith("G1"):
        continue
    m = rx.search(line); n = ry.search(line); e = re_.search(line)
    if m: x = float(m.group(1))
    if n: y = float(n.group(1))
    extruding = e is not None and float(e.group(1)) > 0 and (m or n)
    if cur_type != "Outer wall" or x is None or y is None or x >= X_TILE_MAX:
        continue
    if not extruding:
        if m or n:
            travelled = True; sx, sy = x, y          # travel 目標＝下一段擠出的起點
        continue
    wall_pts_y.append(y)
    if travelled:
        seams.append((layer, sx, sy)); travelled = False
if not wall_pts_y:
    sys.exit("no outer-wall extrusion on the tile")
ymin, ymax = min(wall_pts_y), max(wall_pts_y)
margin = 0.9
cat = collections.Counter()
for _, sx, sy in seams:
    if sy < ymin + margin: cat["front(y min)"] += 1
    elif sy > ymax - margin: cat["back(y max)"] += 1
    else: cat["other(side/boundary)"] += 1
tot = sum(cat.values()) or 1
print("tile outer-wall y range:", round(ymin, 2), round(ymax, 2), "| layers:", layer, "| outer-wall loop starts on tile:", len(seams))
for k in ("front(y min)", "back(y max)", "other(side/boundary)"):
    v = cat.get(k, 0); print("  %-22s %5d  %5.1f%%" % (k, v, 100.0 * v / tot))
