/* =====================================================================
   照片磚濾除・窄段「併哪一邊」單元測（開發中清單 #50 甲＋丁，Eric 2026-10-01 裁；牌 c-1001-LVL-03）
   跑法：node tools/ping/phototile_filter_test.js
   受測：
     · mesh_union.js enforceMinHorizontalWidth——第 5 參數＝標籤距離表（沒給＝色階序號差）
     · engine.js filterLabels——palette（四料＝調色盤色差表，③⑥ 兩次都要用到）、公開介面
     · index.html filterVerticalLabels——丁：模擬圖改呼叫引擎 filterLabels，不得自己串濾除鏈
   AIP 刀 1（2026-10-03，牌 c-1003-AIP-04）：濾除改左右寬＋上下高兩條（規格 R9-11 第二輪 Q10／Q11、實作計畫 Q1）——
     · mesh_union.js enforceMinVerticalHeight／countMinHeightViolations／snapRowsToBands、smoothLabelNoise 的上下視窗
     · engine.js filterLabels 的甲（全面分方向＋對齊層）／乙（只換小色塊那步）兩案、層高表對 19 支照片磚製程檔

   參考答案（oracle）：改判準前的產品函式逐字副本，只多「兩邊都有鄰段時往哪邊併」那一段——
     'longer'＝舊判準（併進較長的、一樣長併左）；'index'＝#45 原型（色階序號差最近，平手比長度）；
     'lab'＝#50 四料量測用的判準（調色盤平方 Lab 距離最近，平手比長度）。
   原型與量測在 scratch（不在本 repo），這裡內嵌副本＝測試自足。

   突變測試：PT_WEB_DIR＝另一份 phototile 資料夾（例如把判準改壞的副本），本測試必須轉紅。
   ===================================================================== */
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const WEB = process.env.PT_WEB_DIR || path.join(ROOT, 'resources', 'web', 'phototile');
const M = require(path.join(WEB, 'mesh_union.js'));
const E = require(path.join(WEB, 'engine.js'));
const INDEX_SRC = fs.readFileSync(path.join(WEB, 'index.html'), 'utf8');

let pass = 0, fail = 0;
function ok(cond, msg) {
  if (cond) { pass++; console.log('  ✅ ' + msg); }
  else { fail++; console.log('  ❌ ' + msg); }
}

/* ---------- oracle ---------- */
function oracle(labels, w, h, minCells, mode, D, K) {
  const out = new Uint8Array(labels);
  if (!(minCells > 1)) return out;
  const start = new Int32Array(w), len = new Int32Array(w), lab = new Int32Array(w);
  const prev = new Int32Array(w), next = new Int32Array(w);
  for (let y = 0; y < h; y++) {
    const row = y * w;
    let n = 0;
    for (let x = 0; x < w;) {
      const v = out[row + x];
      let x2 = x + 1;
      while (x2 < w && out[row + x2] === v) x2++;
      start[n] = x; len[n] = x2 - x; lab[n] = v;
      prev[n] = n - 1; next[n] = (x2 < w) ? n + 1 : -1;
      n++; x = x2;
    }
    if (n <= 1) continue;
    let head = 0, i = 0;
    while (i !== -1) {
      const p = prev[i], q = next[i];
      if (len[i] >= minCells || (p === -1 && q === -1)) { i = q; continue; }
      const pl = (p === -1) ? -1 : len[p];
      const ql = (q === -1) ? -1 : len[q];
      let goLeft = (pl >= ql);
      if (mode !== 'longer' && p !== -1 && q !== -1) {
        const d = mode === 'lab'
          ? D[lab[i] * K + lab[p]] - D[lab[i] * K + lab[q]]
          : Math.abs(lab[p] - lab[i]) - Math.abs(lab[q] - lab[i]);
        goLeft = d < 0 ? true : d > 0 ? false : (pl >= ql);
      }
      if (goLeft) {
        len[p] += len[i];
        next[p] = q; if (q !== -1) prev[q] = p;
        if (q !== -1 && lab[q] === lab[p]) { len[p] += len[q]; next[p] = next[q]; if (next[q] !== -1) prev[next[q]] = p; }
        i = p;
      } else {
        start[q] = start[i]; len[q] += len[i];
        prev[q] = p; if (p !== -1) next[p] = q;
        if (p !== -1 && lab[p] === lab[q]) { start[q] = start[p]; len[q] += len[p]; prev[q] = prev[p]; if (prev[p] !== -1) next[prev[p]] = q; }
        if (prev[q] === -1) head = q;
        i = (prev[q] !== -1) ? prev[q] : q;
      }
    }
    for (let r = head; r !== -1; r = next[r]) {
      const v = lab[r], x0 = start[r], x1 = start[r] + len[r];
      for (let x = x0; x < x1; x++) out[row + x] = v;
    }
  }
  return out;
}
/* oracle 用的扁平平方 Lab 距離（＝#50 量測的寫法）；產品用的是引擎的 labToneTable，兩邊各算各的 */
function flatLab(pal) {
  const K = pal.length, D = new Float64Array(K * K);
  for (let a = 0; a < K; a++) for (let b = 0; b < K; b++) {
    const A = pal[a].lab, B = pal[b].lab;
    D[a * K + b] = (A[0] - B[0]) ** 2 + (A[1] - B[1]) ** 2 + (A[2] - B[2]) ** 2;
  }
  return D;
}
/* 產品的距離表形狀（巢狀；mesh_union 只吃 tone[a][b]） */
function nestedLab(pal) {
  return pal.map(A => Float64Array.from(pal, B => (A.lab[0] - B.lab[0]) ** 2 + (A.lab[1] - B.lab[1]) ** 2 + (A.lab[2] - B.lab[2]) ** 2));
}

/* ---------- 小工具 ---------- */
const rowOf = segs => { const r = []; for (const [v, n] of segs) for (let i = 0; i < n; i++) r.push(v); return new Uint8Array(r); };
const runs = a => { const o = []; for (let x = 0; x < a.length;) { let y = x + 1; while (y < a.length && a[y] === a[x]) y++; o.push([a[x], y - x]); x = y; } return o; };
const same = (a, b) => { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; };
const diffCount = (a, b) => { let d = 0; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) d++; return d; };
const J = x => JSON.stringify(x);
function mulberry32(seed) {
  return () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
}
/* 比 countMinWidthViolations 嚴：列頭列尾的段也算（產品計數器刻意不算邊段；但併段本來就會併掉邊段） */
function strictViolations(L, w, h, minCells) {
  let bad = 0;
  for (let y = 0; y < h; y++) {
    const rs = runs(L.subarray(y * w, (y + 1) * w));
    if (rs.length > 1) for (const [, n] of rs) if (n < minCells) bad++;
  }
  return bad;
}
function newColours(src, out, w, h) {
  let bad = 0;
  for (let y = 0; y < h; y++) {
    const have = new Set(src.subarray(y * w, (y + 1) * w));
    for (let x = 0; x < w; x++) if (!have.has(out[y * w + x])) { bad++; break; }
  }
  return bad;
}

console.log('照片磚濾除・窄段併哪一邊（#50）');

/* ---------- 1. #45 那種一列：主體被 8 階切成 5 條各窄於 16 格的灰帶 ---------- */
console.log('— 1. #45 耳朵那列的合成例（計畫頁 §03 示意列）');
{
  const row = rowOf([[0, 120], [5, 14], [6, 10], [7, 15], [6, 12], [5, 14], [0, 120]]);
  const w = row.length, minCells = 16;
  const neu = M.enforceMinHorizontalWidth(row, w, 1, minCells);
  const old = oracle(row, w, 1, minCells, 'longer');
  ok(J(runs(old)) === J([[0, 305]]), '舊判準（只比長度）：5 條全輸給背景、整截主體被吃光＝' + J(runs(old)));
  ok(J(runs(neu.labels)) === J([[0, 120], [6, 65], [0, 120]]), '新判準：5 條併成一段 65 格（3.25 mm）、主體保住＝' + J(runs(neu.labels)));
  ok(neu.toneFlips > 0, `新判準確實改了方向（toneFlips ${neu.toneFlips} > 0）`);
  ok(M.countMinWidthViolations(neu.labels, w, 1, minCells) === 0 && strictViolations(neu.labels, w, 1, minCells) === 0, '最小寬違規 0');
}

/* ---------- 2. 平手、只有一邊、同色夾擊、門檻 ≤1 ---------- */
console.log('— 2. 邊界情況');
{
  const m = 16;
  const t1 = M.enforceMinHorizontalWidth(rowOf([[2, 20], [1, 5], [0, 30]]), 55, 1, m);
  ok(J(runs(t1.labels)) === J([[2, 20], [0, 35]]), '一樣近（序號差都是 1）→ 比長度、併進較長的右段＝' + J(runs(t1.labels)));
  ok(t1.toneFlips === 0, '平手時方向跟舊判準相同（toneFlips 0）');
  const t2 = M.enforceMinHorizontalWidth(rowOf([[2, 20], [1, 5], [0, 20]]), 45, 1, m);
  ok(J(runs(t2.labels)) === J([[2, 25], [0, 20]]), '一樣近又一樣長 → 併左＝' + J(runs(t2.labels)));
  const t3 = M.enforceMinHorizontalWidth(rowOf([[1, 5], [3, 40]]), 45, 1, m);
  ok(J(runs(t3.labels)) === J([[3, 45]]), '列頭只有右鄰 → 併右');
  const t4 = M.enforceMinHorizontalWidth(rowOf([[3, 40], [1, 5]]), 45, 1, m);
  ok(J(runs(t4.labels)) === J([[3, 45]]), '列尾只有左鄰 → 併左');
  const t5 = M.enforceMinHorizontalWidth(rowOf([[2, 30], [5, 4], [2, 30]]), 64, 1, m);
  ok(J(runs(t5.labels)) === J([[2, 64]]), '同色夾擊 → 併完左右同色自動合一＝' + J(runs(t5.labels)));
  const t6 = M.enforceMinHorizontalWidth(rowOf([[0, 3], [7, 2], [0, 3]]), 8, 1, 1);
  ok(J(runs(t6.labels)) === J([[0, 3], [7, 2], [0, 3]]) && t6.toneFlips === 0 && t6.mergedRuns === 0, 'minCells ≤ 1 → 原樣、toneFlips 0');
  const t7 = M.enforceMinHorizontalWidth(rowOf([[4, 10]]), 10, 1, m);
  ok(J(runs(t7.labels)) === J([[4, 10]]), '整列只有一段（比門檻窄也沒有鄰段可併）→ 原樣');
  // 遠近決定方向、而不是長度：左邊較長但遠、右邊較短但近
  const t8 = M.enforceMinHorizontalWidth(rowOf([[0, 60], [5, 6], [6, 20]]), 86, 1, m);
  ok(J(runs(t8.labels)) === J([[0, 60], [6, 26]]) && t8.toneFlips === 1, '左長但遠（差 5 階）、右短但近（差 1 階）→ 併右（toneFlips 1）＝' + J(runs(t8.labels)));
}

/* ---------- 3. 四料跨色相：序號會併錯、色差併對 ---------- */
console.log('— 3. 四料跨色相（序號相鄰≠顏色相近）');
{
  // 依 L* 由亮到暗排（＝產品調色盤的排序）；淺灰與淺橘亮度差不多 ⇒ 序號相鄰
  const pal = [{ lab: [95, 0, 0] }, { lab: [78, 0, 0] }, { lab: [76, 18, 45] }, { lab: [62, 0, 0] }, { lab: [60, 30, 60] }];
  const row = rowOf([[1, 40], [2, 6], [4, 40]]), w = row.length, m = 16;
  const byIdx = M.enforceMinHorizontalWidth(row, w, 1, m);
  const byLab = M.enforceMinHorizontalWidth(row, w, 1, m, nestedLab(pal));
  ok(J(runs(byIdx.labels)) === J([[1, 46], [4, 40]]), '序號差：淺橘（2）併進序號差 1 的淺灰（1）＝併錯色相');
  ok(J(runs(byLab.labels)) === J([[1, 40], [4, 46]]), '色差：淺橘併進色差小的橘（4）＝併對');
  ok(same(byLab.labels, oracle(row, w, 1, m, 'lab', flatLab(pal), pal.length)), '產品色差判準＝#50 量測 oracle 逐格相同');
}

/* ---------- 4. 隨機 2,000 列 ---------- */
console.log('— 4. 隨機 2,000 列（100 張 × 20 列；固定亂數種子）');
{
  const rnd = mulberry32(50);
  let rows = 0, idxDiff = 0, labDiff = 0, violP = 0, violS = 0, newC = 0, flips = 0, divergeRows = 0, oldDiffRows = 0;
  for (let img = 0; img < 100; img++) {
    const w = 40 + Math.floor(rnd() * 360), h = 20, minCells = 2 + Math.floor(rnd() * 19);
    const K = img % 2 ? 2 + Math.floor(rnd() * 7) : 2 + Math.floor(rnd() * 63);   // 雙料 2–8 階／四料最多 64 色
    const pal = Array.from({ length: K }, () => ({ lab: [rnd() * 100, rnd() * 160 - 80, rnd() * 160 - 80] }))
      .sort((a, b) => b.lab[0] - a.lab[0]);
    const L = new Uint8Array(w * h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w;) {
      const v = Math.floor(rnd() * K), n = 1 + Math.floor(rnd() * (rnd() < 0.7 ? 12 : 60));
      for (let k = 0; k < n && x < w; k++, x++) L[y * w + x] = v;
    }
    const pi = M.enforceMinHorizontalWidth(L, w, h, minCells);
    const pl = M.enforceMinHorizontalWidth(L, w, h, minCells, nestedLab(pal));
    const oi = oracle(L, w, h, minCells, 'index');
    const ol = oracle(L, w, h, minCells, 'lab', flatLab(pal), K);
    const oo = oracle(L, w, h, minCells, 'longer');
    for (let y = 0; y < h; y++) {
      const s = y * w, e = s + w;
      if (!same(pi.labels.subarray(s, e), oi.subarray(s, e))) idxDiff++;
      if (!same(pl.labels.subarray(s, e), ol.subarray(s, e))) labDiff++;
      if (!same(pi.labels.subarray(s, e), pl.labels.subarray(s, e))) divergeRows++;
      if (!same(pi.labels.subarray(s, e), oo.subarray(s, e))) oldDiffRows++;
    }
    rows += h;
    violP += M.countMinWidthViolations(pi.labels, w, h, minCells) + M.countMinWidthViolations(pl.labels, w, h, minCells);
    violS += strictViolations(pi.labels, w, h, minCells) + strictViolations(pl.labels, w, h, minCells);
    newC += newColours(L, pi.labels, w, h) + newColours(L, pl.labels, w, h);
    flips += pi.toneFlips + pl.toneFlips;
  }
  ok(rows === 2000, `跑了 ${rows} 列`);
  ok(idxDiff === 0, `序號差（雙料）＝#45 原型逐格相同（不同的列 ${idxDiff}）`);
  ok(labDiff === 0, `色差（四料）＝#50 量測判準逐格相同（不同的列 ${labDiff}）`);
  ok(violP === 0 && violS === 0, `最小寬違規 0（產品計數 ${violP}、含邊段 ${violS}）`);
  ok(newC === 0, `不會冒出該列原本沒有的顏色（${newC} 列）`);
  // 陰性對照：資料要真的走到新判準（否則上面幾條是空轉的綠燈）
  ok(oldDiffRows > 200 && flips > 0, `資料有鑑別力：新判準跟舊判準不同的列 ${oldDiffRows}、改方向 ${flips} 次`);
  ok(divergeRows > 100, `資料有鑑別力：序號差與色差選得不同的列 ${divergeRows}`);
}

/* ---------- AIP 刀 1 用的小工具 ---------- */
/* 轉置：w×h → h×w（T[x*h+y]＝L[y*w+x]）。上下方向的參考答案＝把左右那支 oracle 用在轉置後的圖上——
   兩邊各算各的，產品的 enforceMinVerticalHeight 抄歪了就對不上。 */
function transpose(L, w, h) {
  const T = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) T[x * h + y] = L[y * w + x];
  return T;
}
const cellsOf = (mm, c) => Math.max(1, Math.ceil(mm / c - 1e-9));
/* 對齊層的參考實作（不呼叫產品的 snapRowsToBands）：從最後一列往上每 b 列一條帶，帶內每欄取最多（平手取靠下的），
   整條用 oracle 修左右寬 */
function snapRef(L, w, h, b, mc, mode, D, K) {
  const out = new Uint8Array(L);
  for (let y1 = h; y1 > 0; y1 -= b) {
    const y0 = Math.max(0, y1 - b), row = new Uint8Array(w);
    for (let x = 0; x < w; x++) {
      const cnt = new Map();
      for (let y = y1 - 1; y >= y0; y--) cnt.set(L[y * w + x], (cnt.get(L[y * w + x]) || 0) + 1);
      let best = -1, bc = 0;
      for (let y = y1 - 1; y >= y0; y--) { const v = L[y * w + x]; if (cnt.get(v) > bc) { bc = cnt.get(v); best = v; } }
      row[x] = best;
    }
    const f = oracle(row, w, 1, mc, mode, D, K);
    for (let y = y0; y < y1; y++) out.set(f, y * w);
  }
  return out;
}
/* 每一個上下換色的位置都要落在帶界上（從底面數 b 的倍數）＝上下段都是整條帶 */
function offBandEdges(L, w, h, b) {
  let bad = 0;
  for (let y = 1; y < h; y++) if ((h - y) % b !== 0) for (let x = 0; x < w; x++) if (L[y * w + x] !== L[(y - 1) * w + x]) bad++;
  return bad;
}

/* ---------- 4b. 上下方向（AIP 刀 1）---------- */
console.log('— 4b. 上下方向＝左右那支轉置後逐格相同（AIP 刀 1，第二輪 Q10）');
{
  const rnd = mulberry32(1003);
  let cols = 0, idxDiff = 0, labDiff = 0, cntDiff = 0, viol = 0, changed = 0;
  for (let g = 0; g < 60; g++) {
    const w = 8 + Math.floor(rnd() * 40), h = 30 + Math.floor(rnd() * 200), minCells = 2 + Math.floor(rnd() * 8);
    const K = 2 + Math.floor(rnd() * 12);
    const pal = Array.from({ length: K }, () => ({ lab: [rnd() * 100, rnd() * 160 - 80, rnd() * 160 - 80] }));
    const L = new Uint8Array(w * h);
    for (let x = 0; x < w; x++) for (let y = 0; y < h;) {
      const v = Math.floor(rnd() * K), n = 1 + Math.floor(rnd() * (rnd() < 0.7 ? 6 : 30));
      for (let k = 0; k < n && y < h; k++, y++) L[y * w + x] = v;
    }
    const T = transpose(L, w, h);
    const vi = M.enforceMinVerticalHeight(L, w, h, minCells);
    const vl = M.enforceMinVerticalHeight(L, w, h, minCells, nestedLab(pal));
    if (!same(vi.labels, transpose(oracle(T, h, w, minCells, 'index'), h, w))) idxDiff++;
    if (!same(vl.labels, transpose(oracle(T, h, w, minCells, 'lab', flatLab(pal), K), h, w))) labDiff++;
    if (M.countMinHeightViolations(L, w, h, minCells) !== M.countMinWidthViolations(T, h, w, minCells)) cntDiff++;
    viol += M.countMinHeightViolations(vi.labels, w, h, minCells) + M.countMinHeightViolations(vl.labels, w, h, minCells);
    changed += vi.changedPixels;
    cols += w;
  }
  ok(idxDiff === 0, `序號差：上下＝左右 oracle 轉置逐格相同（不同的圖 ${idxDiff}／60）`);
  ok(labDiff === 0, `色差：上下＝左右 oracle 轉置逐格相同（不同的圖 ${labDiff}／60）`);
  ok(cntDiff === 0, `上下違規計數＝轉置後的左右計數（不同的圖 ${cntDiff}）`);
  ok(viol === 0, `修完上下違規 0（${viol}）`);
  ok(changed > 1000, `資料有鑑別力：上下修改了 ${changed} 格（${cols} 欄）`);
  // 手算例：一欄 [0×40, 3×2, 0×40]、門檻 4 ⇒ 同色夾擊併回 0；[0×30, 5×3, 6×20] ⇒ 併進近的 6（往下）
  const col = segs => { const r = []; for (const [v, n] of segs) for (let i = 0; i < n; i++) r.push(v); return new Uint8Array(r); };
  const a = M.enforceMinVerticalHeight(col([[0, 40], [3, 2], [0, 40]]), 1, 82, 4);
  ok(J(runs(a.labels)) === J([[0, 82]]), '上下同色夾擊 → 併回同一色');
  const b2 = M.enforceMinVerticalHeight(col([[0, 30], [5, 3], [6, 20]]), 1, 53, 4);
  ok(J(runs(b2.labels)) === J([[0, 30], [6, 23]]), '上近下遠 → 併進顏色近的下面那段＝' + J(runs(b2.labels)));
  // smoothLabelNoise 的上下視窗
  const z = new Uint8Array(40 * 40);
  const s1 = M.smoothLabelNoise(z, 40, 40, 2, 0.05, 0.05, 1.0, 'median');
  const s2 = M.smoothLabelNoise(z, 40, 40, 2, 0.05, 0.05, 1.0, 'median', 0.2);
  ok(s1.radiusX === 10 && s1.radiusY === 10 && s2.radiusX === 10 && s2.radiusY === 2,
    `平滑視窗：沒給上下＝跟左右同（${s1.radiusX}/${s1.radiusY}）；給一層 0.2 mm ⇒ 上下半徑 2（${s2.radiusX}/${s2.radiusY}）`);
}

/* ---------- 4c. 對齊層（甲案的再修）---------- */
console.log('— 4c. 對齊層 snapRowsToBands（甲：左右、上下同時成立）');
{
  const rnd = mulberry32(31);
  let refDiff = 0, hv = 0, edges = 0, n = 0;
  for (let g = 0; g < 40; g++) {
    const w = 30 + Math.floor(rnd() * 120), h = 20 + Math.floor(rnd() * 90), b = 1 + Math.floor(rnd() * 6), mc = 2 + Math.floor(rnd() * 12);
    const K = 2 + Math.floor(rnd() * 10);
    const pal = Array.from({ length: K }, () => ({ lab: [rnd() * 100, rnd() * 160 - 80, rnd() * 160 - 80] }));
    const L = new Uint8Array(w * h);
    for (let i = 0; i < L.length; i++) L[i] = rnd() < 0.85 && i ? L[i - 1] : Math.floor(rnd() * K);
    const useLab = g % 2 === 0;
    const r = M.snapRowsToBands(L, w, h, b, mc, useLab ? nestedLab(pal) : undefined);
    if (!same(r.labels, snapRef(L, w, h, b, mc, useLab ? 'lab' : 'index', flatLab(pal), K))) refDiff++;
    hv += strictViolations(r.labels, w, h, mc) + M.countMinHeightViolations(r.labels, w, h, b);
    edges += offBandEdges(r.labels, w, h, b);
    n += r.changedPixels;
  }
  ok(refDiff === 0, `＝參考實作逐格相同（不同的圖 ${refDiff}／40）`);
  ok(hv === 0, `左右（含邊段）、上下違規都 0（${hv}）`);
  ok(edges === 0, `上下換色一律落在帶界（從底面數）上（${edges}）`);
  ok(n > 10000, `資料有鑑別力：改了 ${n} 格`);
  // 手算：h＝10、帶 4 ⇒ 帶＝[6,10)、[2,6)、[0,2)（貼頂那條不滿）；平手取靠下的
  const L = new Uint8Array([1, 1, 2, 2, 1, 1, 2, 2, 9, 9].map(v => v));   // 1 欄 10 列
  const r = M.snapRowsToBands(L, 1, 10, 4, 1);
  ok(J([...r.labels]) === J([1, 1, 1, 1, 1, 1, 9, 9, 9, 9]) && r.bands === 3,
    '帶從熱床往上切、平手取靠下：' + J([...r.labels]));
}

/* ---------- 5. 引擎 filterLabels 的接線（AIP 刀 1：甲／乙）---------- */
console.log('— 5. 引擎 filterLabels：甲、乙兩案逐格＝參考鏈（②③⑤ 都要吃到同一把尺）');
{
  ok(typeof E.filterLabels === 'function' && E.filterLabels === E._internals.filterLabels, '引擎公開 filterLabels（與 _internals 同一支）');
  const rnd = mulberry32(1001);
  const w = 160, h = 120, img = { w, h };
  const K = 12;
  const pal = Array.from({ length: K }, (_, i) => ({ lab: [95 - i * 7, (i % 3) * 25 - 25, (i % 4) * 20 - 30] }));
  // 大塊＋斜條＋雜點：讓 ②③⑤ 都有兩邊都有鄰段的窄段
  const raw = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let v = ((x >> 5) + (y >> 4)) % K;
    if (((x + 2 * y) % 23) < 3) v = (v + 1 + (y % 5)) % K;
    if (rnd() < 0.02) v = Math.floor(rnd() * K);
    raw[y * w + x] = v;
  }
  const Pd = { width: 8, height: 6, noiseMm: 0.5, nozzle: 0.4, mode: 'dual' };   // 0.05 mm/格；左右 max(0.5,0.8)＝16 格、上下 0.2＝4 格
  const Pq = Object.assign({}, Pd, { mode: 'quad' });                              // 四料一層 0.25＝5 格
  function chain(P, variant, strategy, mode) {
    const sx = P.width / w, sz = P.height / h;
    const layer = P.mode === 'quad' ? 0.25 : 0.2;
    const mc = cellsOf(Math.max(P.noiseMm, 2 * P.nozzle), sx), mv = cellsOf(layer, sz);
    const D = flatLab(pal);
    const H = L => oracle(L, w, h, mc, mode, D, K);
    const V = L => transpose(oracle(transpose(L, w, h), h, w, mv, mode, D, K), h, w);
    const sm = M.smoothLabelNoise(raw, w, h, K, sx, sz, P.noiseMm, strategy, variant === 'jia' ? layer : undefined);
    const t = V(H(sm.labels));
    const op = M.openLabelsMinWidth(t, w, h, mc, variant === 'jia' ? mv : cellsOf(2 * P.nozzle, sz));
    const base = op.degenerate ? t : op.labels;
    return { fin: variant === 'jia' ? snapRef(base, w, h, mv, mc, mode, D, K) : H(base), degenerate: op.degenerate, mc, mv };
  }
  for (const variant of ['jia', 'yi']) {
    const tag = variant === 'jia' ? '甲' : '乙';
    const dv = P => Object.assign({}, P, { devVariant: variant });
    const qLab = E.filterLabels(raw, img, dv(Pq), K, 'mode', pal);
    const qIdx = E.filterLabels(raw, img, dv(Pq), K, 'mode');
    const dIdx = E.filterLabels(raw, img, dv(Pd), K, 'median', pal);
    const cLab = chain(Pq, variant, 'mode', 'lab'), cIdx = chain(Pq, variant, 'mode', 'index'), cMed = chain(Pd, variant, 'median', 'index');
    ok(!cLab.degenerate, `${tag}：合成圖的開運算沒有退化（⑤ 真的有跑）`);
    ok(same(qLab.labels, cLab.fin), `${tag}：四料帶調色盤＝色差判準的參考鏈逐格相同（差 ${diffCount(qLab.labels, cLab.fin)} 格）`);
    ok(qLab.stats.toneMetric === 'lab' && qLab.stats.widthToneFlips > 0 && qLab.stats.heightToneFlips > 0,
      `${tag}：四料帶調色盤 toneMetric＝${qLab.stats.toneMetric}、widthToneFlips＝${qLab.stats.widthToneFlips}、heightToneFlips＝${qLab.stats.heightToneFlips}`);
    ok(same(qIdx.labels, cIdx.fin) && qIdx.stats.toneMetric === 'index', `${tag}：四料沒帶調色盤＝序號差（差 ${diffCount(qIdx.labels, cIdx.fin)} 格）`);
    ok(same(dIdx.labels, cMed.fin) && dIdx.stats.toneMetric === 'index', `${tag}：雙料（median）就算帶了 palette 也照用序號差（差 ${diffCount(dIdx.labels, cMed.fin)} 格）`);
    ok(!same(qLab.labels, qIdx.labels), `${tag}：陰性對照：色差與序號在合成圖上確實不同`);
    const all = [qLab, qIdx, dIdx];
    ok(all.every(r => r.stats.minWidthViolations === 0), `${tag}：左右違規都 0`);
    if (variant === 'jia') {
      ok(all.every(r => r.stats.minHeightViolations === 0), `甲：上下違規都 0（${all.map(r => r.stats.minHeightViolations)}）`);
      ok(offBandEdges(dIdx.labels, w, h, cMed.mv) === 0 && offBandEdges(qLab.labels, w, h, cLab.mv) === 0, '甲：上下換色都落在層的帶界上');
    }
    ok(qLab.stats.filter === 'axis-' + variant, `${tag}：stats.filter＝${qLab.stats.filter}`);
  }
  const jd = E.filterLabels(raw, img, Pd, K, 'median'), yd = E.filterLabels(raw, img, Object.assign({}, Pd, { devVariant: 'yi' }), K, 'median');
  ok(jd.stats.filter === 'axis-jia' && !same(jd.labels, yd.labels), '沒帶 devVariant＝甲；甲、乙在合成圖上確實不同');
  // 門檻：左右＝max(雜訊濾除欄, 2×口徑)、上下＝這台的一層
  const th = (o, s) => E.filterLabels(raw, img, Object.assign({}, Pd, o), K, s || 'median').stats;
  const t1 = th({ noiseMm: 1.0 }), t2 = th({ noiseMm: 1.0, nozzle: 0.6 }), t3 = th({ noiseMm: 1.0, nozzle: 1.0 }), t4 = th({ noiseMm: 0 }), t5 = th({ mode: 'quad', nozzle: 0.6 }, 'mode');
  ok(t1.minWidthMm === 1.0 && t2.minWidthMm === 1.2 && t3.minWidthMm === 2.0 && t4.minWidthMm === 0.8,
    `左右門檻 0.4／0.6／1.0 口徑＝${t1.minWidthMm}／${t2.minWidthMm}／${t3.minWidthMm}；雜訊濾除 0 ⇒ ${t4.minWidthMm}（2×口徑照守）`);
  ok(t1.minHeightMm === 0.2 && t2.minHeightMm === 0.3 && t3.minHeightMm === 0.5 && t5.minHeightMm === 0.35 && t4.smoothedPixels === 0,
    `上下門檻＝一層：雙料 ${t1.minHeightMm}／${t2.minHeightMm}／${t3.minHeightMm}、四料 0.6 口徑 ${t5.minHeightMm}；雜訊濾除 0 只關平滑`);
  let threw = null;
  try { E.filterLabels(raw, img, Object.assign({}, Pd, { nozzle: 0.8 }), K, 'median'); } catch (e) { threw = e; }
  ok(threw && threw.code === 'bad_request', '沒有層高的口徑（0.8）大聲報錯，不靜默亂猜');
  const keys = ['filter', 'changedPixels', 'smoothedPixels', 'changedAreaMm2', 'widthChangedPixels', 'widthMergedRuns', 'minWidthMm',
                'heightChangedPixels', 'heightMergedRuns', 'minHeightMm', 'layerMm', 'toneMetric', 'widthToneFlips', 'heightToneFlips',
                'openedAwayPixels', 'openDegenerate', 'refixChangedPixels', 'layerBands', 'minWidthViolations', 'minHeightViolations',
                'thresholdMm', 'strategy'];
  ok(keys.every(k => k in t1), '統計欄位齊（頁面狀態文字與 3MF 內嵌統計照讀）：缺 ' + J(keys.filter(k => !(k in t1))));
}

/* ---------- 5b. 甲乙的分別：一層高的橫向細節 ---------- */
console.log('— 5b. 甲留得住 0.3 mm 高的橫帶、乙照舊擋掉（計畫 §04 的取捨）');
{
  const w = 200, h = 200, img = { w, h };
  const raw = new Uint8Array(w * h);
  for (let y = 80; y < 86; y++) for (let x = 40; x < 160; x++) raw[y * w + x] = 1;    // 6 列＝0.3 mm
  for (let y = 140; y < 142; y++) for (let x = 40; x < 160; x++) raw[y * w + x] = 2;  // 2 列＝0.1 mm（比一層薄）
  const P = { width: 10, height: 10, noiseMm: 1.0, nozzle: 0.4, mode: 'dual' };
  const cnt = (L, v) => L.reduce((a, x) => a + (x === v), 0);
  const jia = E.filterLabels(raw, img, P, 3, 'median').labels;
  const yi = E.filterLabels(raw, img, Object.assign({}, P, { devVariant: 'yi' }), 3, 'median').labels;
  ok(cnt(jia, 1) >= 4 * 100, `甲：0.3 mm 那條留下來（${cnt(jia, 1)} 格；對齊層後是整層 0.2 mm 高）`);
  ok(cnt(yi, 1) === 0, `乙：0.3 mm 那條被擋掉（${cnt(yi, 1)} 格）`);
  ok(cnt(jia, 2) === 0 && cnt(yi, 2) === 0, '比一層還薄的 0.1 mm 那條，兩案都併掉');
}

/* ---------- 6. 丁：模擬圖不得自己串濾除鏈 ---------- */
console.log('— 6. 丁的結構守衛（index.html）');
function fnBody(src, name) {
  const at = src.search(new RegExp('function\\s+' + name + '\\s*\\('));
  if (at < 0) return null;
  let i = src.indexOf('{', at), depth = 0;
  for (let j = i; j < src.length; j++) {
    if (src[j] === '{') depth++;
    else if (src[j] === '}' && --depth === 0) return src.slice(at, j + 1);
  }
  return null;
}
function guardPage(src) {
  const bad = [];
  for (const fn of ['smoothLabelNoise', 'enforceMinHorizontalWidth', 'enforceMinVerticalHeight', 'snapRowsToBands', 'filterSmallComponents', 'openLabelsMinWidth'])
    if (new RegExp('PhotoTileMesh\\s*\\.\\s*' + fn + '\\s*\\(').test(src)) bad.push('頁面自己呼叫 PhotoTileMesh.' + fn);
  const body = fnBody(src, 'filterVerticalLabels');
  if (!body) bad.push('找不到 filterVerticalLabels');
  else if (!/\.filterLabels\s*\(/.test(body)) bad.push('filterVerticalLabels 沒有呼叫引擎 filterLabels');
  else if (!/\bmode\s*:/.test(body)) bad.push('filterVerticalLabels 沒帶 mode（引擎靠它查上下那一層的層高；AIP 刀 1）');
  else if (/devVariant/.test(src)) bad.push('頁面帶了 devVariant（開發用，只給三欄對照工具）');
  const calls = [...src.matchAll(/(function\s+)?filterVerticalLabels\s*\(([^)]*)\)/g)].filter(m => !m[1]).map(m => m[2].split(',').map(s => s.trim()));
  const quad = calls.filter(a => a[2] === "'mode'" || a[2] === '"mode"');
  if (!quad.length) bad.push('找不到四料（mode）的呼叫');
  for (const a of quad) if (a.length < 4 || !a[3]) bad.push('四料呼叫沒帶調色盤：filterVerticalLabels(' + a.join(',') + ')');
  return bad;
}
{
  const bad = guardPage(INDEX_SRC);
  ok(bad.length === 0, '現行 index.html 過守衛' + (bad.length ? '：' + bad.join('；') : ''));
  /* 陽性對照：#50 之前（T069 出貨線 8c09adca3a）那一段的逐字副本——守衛必須抓得到 */
  const OLD = [
    'function filterVerticalLabels(labels,paletteSize,strategy){',
    '  if(!window.PhotoTileMesh || !img){ updateNoiseStat(null); return new Uint8Array(labels); }',
    '  const sx=params.width/img.w, sz=params.height/img.h;',
    '  const smooth=PhotoTileMesh.smoothLabelNoise(labels,img.w,img.h,paletteSize,sx,sz,params.noiseMm,strategy);',
    '  const minWidthMm=2*params.nozzle;',
    '  const wide=PhotoTileMesh.enforceMinHorizontalWidth(smooth.labels,img.w,img.h,',
    '    Math.max(1,Math.round(minWidthMm/sx)));',
    '  const result=PhotoTileMesh.filterSmallComponents(wide.labels,img.w,img.h,sx,sz,params.noiseMm,',
    '    {maxPasses:Math.max(8,Math.min(24,paletteSize+2))});',
    '  return result.labels;',
    '}',
    "    labArr=filterVerticalLabels(labArr,palette.length,'mode');",
    "  const labArr=filterVerticalLabels(rawLabels,K,'median');"
  ].join('\n');
  const oldBad = guardPage(OLD);
  ok(oldBad.length >= 5, `陽性對照：#50 之前那段被抓到 ${oldBad.length} 條（直呼三支＋沒走引擎＋四料沒帶調色盤）`);
  ok(/mesh_union\.js\?v=\d{8}/.test(INDEX_SRC) && /engine\.js\?v=\d{8}/.test(INDEX_SRC), 'index.html 載 mesh_union.js／engine.js 都帶 ?v= 版本字串（WebView 快取，SOP WebView2 §N）');
  const noMode = INDEX_SRC.replace(/,\s*mode:slotCount===4\?'quad':'dual'\}/, '}');
  ok(noMode !== INDEX_SRC && guardPage(noMode).some(s => /沒帶 mode/.test(s)), '陽性對照：拿掉模擬圖那一處的 mode，守衛抓得到');
}

/* ---------- 7. 層高表＝照片磚製程檔（AIP 刀 1）---------- */
console.log('— 7. 引擎層高表＝19 支「同進照片磚」製程檔的 layer_height');
{
  const PROC = path.join(ROOT, 'resources', 'profiles', 'PING', 'process');
  const files = fs.readdirSync(PROC).filter(f => f.includes('同進照片磚') && f.endsWith('.json'));
  ok(files.length >= 19, `找到 ${files.length} 支同進照片磚製程檔（10-03 是 19 支）`);
  const bad = [], seen = new Set();
  for (const f of files) {
    const j = JSON.parse(fs.readFileSync(path.join(PROC, f), 'utf8'));
    const printer = (j.compatible_printers || [])[0] || '';
    const m = /^(F[DF])\d+.*\s(\d\.\d) nozzle$/.exec(printer);
    if (!m) { bad.push(f + '：認不出機型／口徑 ' + printer); continue; }
    const mode = m[1] === 'FF' ? 'quad' : 'dual', nozzle = Number(m[2]), lh = Number(j.layer_height);
    seen.add(mode + nozzle);
    if (E.layerHeightMm(mode, nozzle) !== lh) bad.push(`${f}：製程 ${lh}、引擎表 ${E.layerHeightMm(mode, nozzle)}`);
  }
  ok(bad.length === 0, '每一支都跟引擎表相同' + (bad.length ? '：' + bad.join('；') : ''));
  const tbl = E.layerTable(), miss = [];
  for (const mode of Object.keys(tbl)) for (const nz of Object.keys(tbl[mode])) if (!seen.has(mode + Number(nz))) miss.push(mode + ' ' + nz);
  ok(miss.length === 0, '引擎表每一格都有製程檔對得到' + (miss.length ? '：缺 ' + miss.join('、') : ''));
}

console.log(`\n${pass} 過、${fail} 敗`);
process.exit(fail ? 1 : 0);
