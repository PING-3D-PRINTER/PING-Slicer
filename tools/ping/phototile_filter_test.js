/* =====================================================================
   照片磚濾除・窄段「併哪一邊」單元測（開發中清單 #50 甲＋丁，Eric 2026-10-01 裁；牌 c-1001-LVL-03）
   跑法：node tools/ping/phototile_filter_test.js
   受測：
     · mesh_union.js enforceMinHorizontalWidth——第 5 參數＝標籤距離表（沒給＝色階序號差）
     · engine.js filterLabels——palette（四料＝調色盤色差表，③⑥ 兩次都要用到）、公開介面
     · index.html filterVerticalLabels——丁：模擬圖改呼叫引擎 filterLabels，不得自己串濾除鏈

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

/* ---------- 5. 引擎 filterLabels 的接線 ---------- */
console.log('— 5. 引擎 filterLabels（③⑥ 兩次都要吃到同一把尺）');
{
  ok(typeof E.filterLabels === 'function' && E.filterLabels === E._internals.filterLabels, '引擎公開 filterLabels（與 _internals 同一支）');
  const rnd = mulberry32(1001);
  const w = 160, h = 120, img = { w, h };
  const P = { width: 8, height: 6, noiseMm: 0.5, nozzle: 0.4 };   // 0.05 mm/格 ⇒ 最小寬 16 格
  const K = 12;
  const pal = Array.from({ length: K }, (_, i) => ({ lab: [95 - i * 7, (i % 3) * 25 - 25, (i % 4) * 20 - 30] }));
  // 大塊＋斜條＋雜點：讓 ③ 與 ⑥（開運算後再修）都有兩邊都有鄰段的窄段
  const raw = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let v = ((x >> 5) + (y >> 4)) % K;
    if (((x + 2 * y) % 23) < 3) v = (v + 1 + (y % 5)) % K;
    if (rnd() < 0.02) v = Math.floor(rnd() * K);
    raw[y * w + x] = v;
  }
  function chain(strategy, mode) {
    const sx = P.width / w, sz = P.height / h;
    const sm = M.smoothLabelNoise(raw, w, h, K, sx, sz, P.noiseMm, strategy);
    const minC = Math.max(1, Math.ceil(2 * P.nozzle / sx)), minV = Math.max(1, Math.ceil(2 * P.nozzle / sz));
    const D = flatLab(pal);
    const wide = oracle(sm.labels, w, h, minC, mode, D, K);
    const res = M.filterSmallComponents(wide, w, h, sx, sz, P.noiseMm, { maxPasses: Math.max(8, Math.min(24, K + 2)) });
    const op = M.openLabelsMinWidth(res.labels, w, h, minC, minV);
    return { fin: op.degenerate ? res.labels : oracle(op.labels, w, h, minC, mode, D, K), degenerate: op.degenerate };
  }
  const qLab = E.filterLabels(raw, img, P, K, 'mode', pal);
  const qIdx = E.filterLabels(raw, img, P, K, 'mode');
  const dIdx = E.filterLabels(raw, img, P, K, 'median', pal);
  const cLab = chain('mode', 'lab'), cIdx = chain('mode', 'index'), cOld = chain('mode', 'longer'), cMed = chain('median', 'index');
  ok(!cLab.degenerate, '合成圖的開運算沒有退化（⑥ 真的有跑）');
  ok(same(qLab.labels, cLab.fin), `四料帶調色盤＝色差判準的參考鏈逐格相同（差 ${diffCount(qLab.labels, cLab.fin)} 格）`);
  ok(qLab.stats.toneMetric === 'lab' && qLab.stats.widthToneFlips > 0, `四料帶調色盤：toneMetric＝${qLab.stats.toneMetric}、widthToneFlips＝${qLab.stats.widthToneFlips}`);
  ok(same(qIdx.labels, cIdx.fin) && qIdx.stats.toneMetric === 'index', '四料沒帶調色盤＝序號差（mesh 的預設）');
  ok(same(dIdx.labels, cMed.fin) && dIdx.stats.toneMetric === 'index', '雙料（median）就算帶了 palette 也照用序號差');
  ok(!same(qLab.labels, cOld.fin) && !same(qLab.labels, qIdx.labels), '陰性對照：合成圖上色差／序號／舊判準三者確實不同（測試有鑑別力）');
  ok(qLab.stats.minWidthViolations === 0 && qIdx.stats.minWidthViolations === 0 && dIdx.stats.minWidthViolations === 0, '三種都最小寬違規 0');
  const keys = ['removedComponents', 'changedPixels', 'smoothedPixels', 'changedAreaMm2', 'widthChangedPixels', 'widthMergedRuns',
                'minWidthMm', 'openedAwayPixels', 'openDegenerate', 'minWidthViolations', 'passes', 'thresholdMm', 'strategy'];
  ok(keys.every(k => k in qLab.stats), '既有統計欄位都還在（頁面狀態文字與 3MF 內嵌統計照讀）');
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
  for (const fn of ['smoothLabelNoise', 'enforceMinHorizontalWidth', 'filterSmallComponents', 'openLabelsMinWidth'])
    if (new RegExp('PhotoTileMesh\\s*\\.\\s*' + fn + '\\s*\\(').test(src)) bad.push('頁面自己呼叫 PhotoTileMesh.' + fn);
  const body = fnBody(src, 'filterVerticalLabels');
  if (!body) bad.push('找不到 filterVerticalLabels');
  else if (!/\.filterLabels\s*\(/.test(body)) bad.push('filterVerticalLabels 沒有呼叫引擎 filterLabels');
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
}

console.log(`\n${pass} 過、${fail} 敗`);
process.exit(fail ? 1 : 0);
