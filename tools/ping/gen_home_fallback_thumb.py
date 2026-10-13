#!/usr/bin/env python3
"""首頁「最近打開文件」不帶縮圖時的 fallback（resources/web/homepage/img/d.png）產生器。

出處：回報中心 #98（劉勝賢）→ Eric 2026-09-07 令「用軟體 icon、去色淺化」(commit 97f181142f，整顆 icon 撐滿 184px)
      → Eric 2026-09-08：「徽章不要顯示那麼大，旁邊要留白，會感覺有點壓迫」，四案比較後裁**丙**＝
        去掉 icon 底板、只留 P 字形、單一中灰、約 46%，四周露出卡片灰底（#E4E4E4，home.css .FileImg）。

做法＝對既有資產做**確定性影像處理**（沒有重畫、沒有 AI 生成——CIS 核心原則 1）：
  1. 讀 app icon resources/images/OrcaSlicer.png（256×256；是 app icon 不是商標，方框位置本來就該用方形 icon）
  2. 取「低飽和」像素（P 字＋外框 bevel 都是灰白；橘底板飽和度高）→ 去掉與影像邊框相連的那塊（外框）⇒ 只剩 P
  3. 用飽和度做柔邊（S 40→70 線性淡出），等比縮到 184×0.46≈85px 的長邊、置中於 184×184 透明畫布，單色 #B4B4B4
  4. alpha 承載形狀，顏色恆定 ⇒ 全圖 R==G==B（零殘留彩度）

用法（repo 根）：python tools/ping/gen_home_fallback_thumb.py            # 產出並印 md5
              python tools/ping/gen_home_fallback_thumb.py --check    # 只比對，不寫（exit 1＝不一致）
兩線（ping/v3.5／release/v3.6）的 d.png 應逐位元組相同：產一次、複製過去、比 md5。
"""
import hashlib, io, os, sys
from collections import deque
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC  = os.path.join(ROOT, 'resources', 'images', 'OrcaSlicer.png')
DST  = os.path.join(ROOT, 'resources', 'web', 'homepage', 'img', 'd.png')
TILE = 184          # home.css .FileImg / .FileItem img 的實際尺寸
SCALE = 0.46        # 丙案：P 字長邊佔卡片 46%
COLOR = (0xB4, 0xB4, 0xB4)   # 中灰；卡片底 #E4E4E4 上看得見、又不搶有圖的鄰居
SAT_HARD, SAT_SOFT = 70, 40  # HSV S（0–255）：≥70 視為橘底板；40–70 線性柔邊

def render() -> bytes:
    src = Image.open(SRC).convert('RGBA')
    W, H = src.size
    hsv = src.convert('HSV').load(); alpha = src.split()[3].load()
    lowsat = [[1 if alpha[x, y] > 0 and hsv[x, y][1] < SAT_HARD else 0 for x in range(W)] for y in range(H)]
    # 去掉與邊框相連的低飽和區（icon 外框 bevel），剩下的就是 P 字
    seen = [[0] * W for _ in range(H)]; dq = deque()
    for y in range(H):
        for x in range(W):
            if (x in (0, W - 1) or y in (0, H - 1)) and lowsat[y][x] and not seen[y][x]:
                seen[y][x] = 1; dq.append((x, y))
    while dq:
        x, y = dq.popleft()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < W and 0 <= ny < H and lowsat[ny][nx] and not seen[ny][nx]:
                seen[ny][nx] = 1; dq.append((nx, ny))
    soft = Image.new('L', (W, H), 0); sl = soft.load()
    for y in range(H):
        for x in range(W):
            if lowsat[y][x] and not seen[y][x]:
                s = hsv[x, y][1]
                sl[x, y] = int(max(0, min(255, (SAT_HARD - s) / (SAT_HARD - SAT_SOFT) * 255)))
    bb = soft.getbbox(); glyph = soft.crop(bb)
    gw, gh = glyph.size; k = TILE * SCALE / max(gw, gh)
    nw, nh = int(round(gw * k)), int(round(gh * k))
    mask = glyph.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new('RGBA', (TILE, TILE), (0, 0, 0, 0))
    ink = Image.new('RGBA', (nw, nh), COLOR + (255,)); ink.putalpha(mask)
    canvas.alpha_composite(ink, ((TILE - nw) // 2, (TILE - nh) // 2))
    buf = io.BytesIO(); canvas.save(buf, 'PNG', optimize=True); return buf.getvalue()

def main() -> int:
    data = render(); md5 = hashlib.md5(data).hexdigest()
    if '--check' in sys.argv:
        cur = open(DST, 'rb').read() if os.path.exists(DST) else b''
        ok = cur == data; print(('一致 ' if ok else '不一致 ') + md5); return 0 if ok else 1
    with open(DST, 'wb') as f: f.write(data)
    im = Image.open(DST); px = im.load()
    assert im.size == (TILE, TILE) and all(px[x, y][0] == px[x, y][1] == px[x, y][2] for x in range(TILE) for y in range(TILE) if px[x, y][3])
    print(f'寫入 {os.path.relpath(DST, ROOT)}  {len(data)} B  md5 {md5}  形狀 bbox {im.split()[3].getbbox()}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
