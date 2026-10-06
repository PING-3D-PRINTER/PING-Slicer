/* =====================================================================
   PhotoTileAiFlow — 照片磚「複製提示詞 → 用自己的 AI 產圖 → 貼回工作室」（AIP 第二班刀 4；開發中清單 #22）

   規格＝照片磚_核心規格.md R9-11（Q1～Q10、v3 後九題、第二輪 Q10～Q13、實作計畫五題）；
   計畫頁＝00治理文件/計畫_照片磚AI產圖複製提示詞_實作_20261003.html §03 刀 4；
   動線與每一句文案＝原型 v4（Eric 2026-10-03「原型 OK」；範本 D:/_sa/aip/proto4_template.html）。
   提示詞的文字一律從款式庫讀（constants.aiFlow／toneRulesZh、各款 promptTemplateZh）——那些是
   tools/ping/phototile_aiflow_extract.js 在 node 裡執行原型自己的程式碼抽出來的，**不准在這裡手打**。

   分兩層：
     一、算式（不碰畫面；node 測試 tools/ping/phototile_aiflow_test.js 直接跑）：
         語言、形狀、像素換算、提示詞組法、換個說法、四項檢查、各機尺寸上限、我的款式。
     二、畫面：三步、貼回、結果、我的款式編輯。頁面（index.html）只交一個轉接物件（mount），
         收圖之後要做的事（色階數、壓平、換來源）仍在頁面的 ptAiAccept——金鑰直連與貼回走同一條。

   跟原型不同的地方（計畫頁「跟原型不同的地方」＋本棒讀碼補的）：
     ①語言：原型只認 4 種、其他會丟例外 ⇒ 產品 21 種都接：繁中／簡中→中文提示詞（簡中請 AI 用簡體回），其他→英文，缺／不認得→英文。
     ②我的款式描述裡的 {minPx}／{tones} 存成佔位字（原型存成當下數字，改尺寸不會跟著變）。
     ③我的款式不能跟內建款式同名（原型只比自己做的那幾款）。
     ④數字欄、文字欄裡按 Ctrl+V＝一般貼上（原型會把它攔成貼回圖）——在 index.html 的 paste 那一支。
     ⑤〔回到原圖〕之後，這次產的圖在三步畫面上方留一排縮圖，點一下就再送進來（原型說「點一下可以再用」但回到原圖後看不到那一排）。
     ⑥比例：磚跟著 AI 圖改高度、或裁成原本比例——裁切是把**裁好的圖**送進 C++（壓平與輸出才會是同一張），不是只在畫面上裁。
     ⑦濾除併進顏色最近的鄰段（#50 判準）——那在引擎，不在這裡；提示詞數字照原型（左右 max(雜訊濾除, 2×口徑)、上下一層）。
   ===================================================================== */
(function (root, factory) {
  const api = factory(root);
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root && root.document) root.PhotoTileAiFlow = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function (root) {
'use strict';

/* ============================== 一、算式 ============================== */
/* 門檻全部照原型 v4（resultHTML／analyze／sameAsPhoto／shape／DETAIL）；改了這裡，測試裡對原型的比對會紅。 */
const RATIO_TOL = 0.03;          // |ln(圖寬高比 ÷ 磚寬高比)| > 0.03 ＝比例不同
const SQUARE_TOL = 0.05;         // AI 圖 |ln 寬高比| < 0.05 ＝講「正方形」
const SHAPE_SPLIT = 1.2;         // 磚寬高比 ≥ 1.2 橫式、≤ 1/1.2 直式、其餘正方形（決定請 AI 給的圖形狀）
const SMALL_LONG_PX = 900;       // 長邊 < 900 px ＝圖太小（細節會糊）
const CLOSE_DE = 6;              // 色差 < 6 寫「很貼近」，其餘「貼近」
const DE_WARN_DEFAULT = 12;      // 顏色門檻＝款式庫 acceptance.deltaE.warn（R9-11 Q4）；讀不到才用這個
const SAME_N = 64, SAME_LUM_DIFF = 12, SAME_SHARE = 0.05;   // 貼成原圖：64×64 亮度格，差 > 12 的格不到 5%
const DETAIL_THIN_MAX_PX = 2;    // 上下最少 ≤ 2 px ＝「細的橫條也印得出來」那一句
const MINW_FACTOR = 2;           // 左右最少＝max(雜訊濾除, 2×口徑)（R9-11 第二輪 Q11 甲；同 engine.js minWidthMm）
const LEVELS_MAX = 8;            // 色階上限（Eric 2026-08-02 裁 B；同 #levelsIn max）
/* 各機尺寸上限（R9-11 v3 後九題 Q1～Q3、第二輪 Q13 甲）：寬＝盤面直徑 −50（留洗料塔的位置）、高＝機型檔可印高、
   delta 越往上搆得到的圈越小（limit_z 以上可達半徑縮小）。循環洗料塔＝index.html buildRequest 的 cycle（35／15／8，測試盯著兩邊一樣）。 */
const W_MARGIN = 50, SIZE_MIN = 20;
const TOWER = { size: 35, gap: 15, brim: 8 };
/* 回覆語言的英文名稱（原型只有 en／ja，其餘是產品補的；繁中／簡中／英／日的名稱從原型抽在款式庫 aiFlow.replyNames）。 */
const REPLY_EN = { en: 'English', ja: 'Japanese', ko: 'Korean', de: 'German', fr: 'French', es: 'Spanish', it: 'Italian',
  nl: 'Dutch', pl: 'Polish', pt: 'Portuguese', pt_BR: 'Brazilian Portuguese', ru: 'Russian', uk: 'Ukrainian', tr: 'Turkish',
  cs: 'Czech', hu: 'Hungarian', sv: 'Swedish', ca: 'Catalan', lt: 'Lithuanian', vi: 'Vietnamese', th: 'Thai' };

/* 介面語言 → 提示詞語言與回覆語言（計畫頁「跟原型不同的地方」：繁中、簡中→中文；其他→英文；回覆照介面語言）。
   code 是 App 的 current_language_code()（例 zh_TW、en_US、pt_BR）；舊宿主沒送＝空字串＝英文（規格 Q7「缺就英文」）。 */
function langInfo(code, af) {
  const names = (af && af.replyNames) || {};
  const parts = String(code == null ? '' : code).trim().replace(/-/g, '_').split('_');
  const l = (parts[0] || '').toLowerCase(), r = (parts[1] || '').toUpperCase();
  if (l === 'zh') {
    const key = (r === 'CN' || r === 'SG' || r === 'HANS') ? 'zh_CN' : 'zh_TW';
    return { code: key, prompt: 'zh', reply: names[key] };
  }
  const full = r ? l + '_' + (r.length === 2 ? r : parts[1]) : l;
  const reply = names[full] || REPLY_EN[full] || names[l] || REPLY_EN[l] || names.en || REPLY_EN.en;
  return { code: full, prompt: 'en', reply };
}
/* 請 AI 給的圖形狀（原型 shape()）：寬高比 ≥1.2 橫式 1536×1024、≤1/1.2 直式 1024×1536、其餘 1024×1024 */
function shapeKey(aspectWH) { return aspectWH >= SHAPE_SPLIT ? 'landscape' : (aspectWH <= 1 / SHAPE_SPLIT ? 'portrait' : 'square'); }
function shapeOf(aspectWH, af, lang) {
  const k = shapeKey(aspectWH), s = af.shapes[k], L = lang === 'zh' ? 'zh' : 'en';
  return { key: k, txt: s[L].txt, size: s[L].size, ui: s.ui, px: s.px };
}
/* mm → 像素：最細線寬（mm）÷ 磚寬（mm）× 請 AI 給的圖寬（px），最少 1（原型 minPx／minPxZ） */
function minPxOf(mm, tileW, px) { return Math.max(1, Math.round(mm / tileW * px)); }
function xMinMm(noiseMm, nozzle) { return Math.max(Number(noiseMm) || 0, MINW_FACTOR * (Number(nozzle) || 0.4)); }
function zMinMm(layerMm) { return +(Number(layerMm) || 0.2).toFixed(2); }   // 上下＝一層（第二輪 Q10 甲）
function fillT(t, map) { return Object.keys(map).reduce((s, k) => s.split('{' + k + '}').join(String(map[k])), String(t == null ? '' : t)); }
function upHex(hexes) { return (hexes || []).map(h => String(h).toUpperCase()); }
function detailLine(af, L, x, z) { return fillT(af.printDetail[L][z <= DETAIL_THIN_MAX_PX ? 'thin' : 'band'], { x, z }); }
/* 料色那一行：中文樣板在款式庫；英文那一行的正本在 matflow.js（aiPaletteLine，金鑰直連從 T061 起就用它），由呼叫端傳進來 */
function paletteLine(af, L, hexes, enLine) {
  const hx = upHex(hexes);
  if (L === 'zh') return fillT(af.paletteLineZh, { n: hx.length, hexes: hx.join(af.hexJoin.zh) });
  return typeof enLine === 'function' ? enLine(hx) : '';
}
function templateOf(style, L) { return L === 'zh' ? style.promptTemplateZh : style.promptTemplate; }
/* 提示詞組法（原型 buildPrompt；規格 R9-11「提示詞本體不另寫一套」）：
   前言 → 款式模板（{minPx}{tones} 代換）→ 品味規則（內建款式；不代換）／我的款式＝描述＋可印性規則 → 列印細節 → 料色那一行。
   preface:false ＝金鑰直連（開發者模式）用：同一份、只少「對聊天型 AI 講的話」。 */
function buildPrompt(o) {
  const C = o.lib.constants, af = C.aiFlow, L = o.lang === 'zh' ? 'zh' : 'en';
  const sh = shapeOf(o.aspect, af, L), x = minPxOf(o.xMinMm, o.tileW, sh.px), z = minPxOf(o.zMinMm, o.tileW, sh.px);
  const fill = t => fillT(t, { minPx: x, tones: o.tones });
  const parts = [];
  if (o.preface !== false) parts.push(fillT(af.chatPreface[L], { shape: sh.txt, size: sh.size, reply: o.reply }));
  if (o.mine) parts.push(fill(o.style.desc), fill(af.customRules[L]));
  else parts.push(fill(templateOf(o.style, L)), L === 'zh' ? C.toneRulesZh : C.toneRules);
  parts.push(detailLine(af, L, x, z), paletteLine(af, L, o.hexes, o.enPalette));
  return parts.join('\n\n');
}
/* 換個說法五句＋「請 AI 重產這個比例」（原型 FIX）：中文介面複製中文、其他複製英文 */
function phraseOf(af, key) { return key === 'shape' ? af.ratioPhrase : (af.retry || []).find(p => p.key === key) || null; }
function fixText(af, key, L, ctx) {
  const p = phraseOf(af, key); if (!p) return '';
  const Lx = L === 'zh' ? 'zh' : 'en', sh = ctx.shape || {};
  return fillT(p[Lx], { hexes: upHex(ctx.hexes).join(af.hexJoin[Lx]), tones: ctx.tones, minPx: ctx.minPx, shape: sh.txt, size: sh.size });
}
/* 複製之後又改了尺寸／口徑／濾除 ⇒ 提示詞裡的最細線寬變了（原型 sizeChangedNote，文案照原型） */
function sizeChangedNote(copied, now) {
  if (!copied || !copied.w) return '';
  if (copied.w !== now.w) return '尺寸從 ' + copied.w + ' 改成 ' + now.w + ' mm——提示詞裡的最細線寬跟著從 ' + copied.x + ' 變成 ' + now.x + ' px（上下 ' + copied.z + ' → ' + now.z + ' px）；要讓 AI 照新尺寸畫，重新複製再產一次。';
  if (copied.x !== now.x || copied.z !== now.z) return '口徑或上下濾除改了——提示詞裡的最細線寬從左右 ' + copied.x + '／上下 ' + copied.z + ' px 變成 ' + now.x + '／' + now.z + ' px；要讓 AI 照新設定畫，重新複製再產一次。';
  return '';
}

/* ---- 貼回後的四項檢查＋一項請人看（R9-11 Q4：一律只提示、不擋） ---- */
function aspectGap(a, b) { return Math.abs(Math.log(a / b)); }
function isMismatch(aspI, aspT) { return aspectGap(aspI, aspT) > RATIO_TOL; }
function ratioTxt(a) { const c = [[3, 2], [4, 3], [16, 9], [5, 4]]; for (const [p, q] of c) { if (Math.abs(Math.log(a / (p / q))) < RATIO_TOL) return p + ':' + q; if (Math.abs(Math.log(a / (q / p))) < RATIO_TOL) return q + ':' + p; } return a.toFixed(2) + ':1'; }
function cropPct(aI, aT) { return aI > aT ? '左右各裁掉 ' + Math.round((1 - aT / aI) * 50) + '%' : '上下各裁掉 ' + Math.round((1 - aI / aT) * 50) + '%'; }
function aiShapeLabel(aspI) { return Math.abs(Math.log(aspI)) < SQUARE_TOL ? '正方形' : (aspI > 1 ? '橫式' : '直式') + ' ' + ratioTxt(aspI); }
/* 亮度格（64×64 RGBA → 4096 個亮度；權重照原型，用 sRGB 值不轉線性） */
function lumGrid(rgba) { const n = SAME_N * SAME_N, o = new Float32Array(n); for (let i = 0; i < n; i++) o[i] = 0.2126 * rgba[i * 4] + 0.7152 * rgba[i * 4 + 1] + 0.0722 * rgba[i * 4 + 2]; return o; }
/* 是不是貼成原本的照片：比例差 > 0.03 一定不是；否則 64×64 亮度格差 > 12 的格不到 5% 就算「幾乎一樣」 */
function sameAsPhoto(aspI, aspPhoto, lumA, lumB) {
  if (aspectGap(aspI, aspPhoto) > RATIO_TOL) return false;
  let diff = 0; const n = Math.min(lumA.length, lumB.length);
  for (let i = 0; i < n; i++) if (Math.abs(lumA[i] - lumB[i]) > SAME_LUM_DIFF) diff++;
  return diff / n < SAME_SHARE;
}
/* 判斷（不含文案）：same 成立時其他幾項不看（原型同）；deltaE 沒有（還沒選料、模擬不出來）＝顏色那一項不出現。 */
function checkList(o) {
  const out = [];
  if (o.same) { out.push({ key: 'same', state: 'warn' }); return out; }
  if (!o.locked) out.push({ key: 'lock', state: 'warn' });
  else out.push({ key: 'shape', state: isMismatch(o.aspI, o.aspT) ? 'warn' : 'ok' });
  if (typeof o.deltaE === 'number' && isFinite(o.deltaE)) out.push({ key: 'color', state: o.deltaE >= (o.warnDE || DE_WARN_DEFAULT) ? 'warn' : 'ok' });
  out.push({ key: 'small', state: Math.max(o.w0, o.h0) < SMALL_LONG_PX ? 'warn' : 'ok' });
  out.push({ key: 'eye', state: 'eye' });
  return out;
}
function warnCount(list) { return list.filter(c => c.state === 'warn').length; }

/* ---- 各機尺寸上限（R9-11 v3 後九題 Q1～Q3；第二輪 Q13 甲＝右邊的塔頂端搆不到就改擺後面，C++ PingCycleTower 同一條） ---- */
function limitZ(g) { return g.pe + Math.sqrt(g.arm * g.arm - g.R * g.R) - g.arm; }
/* Klipper delta.py：Z ≤ limit_z 整個盤面都搆得到；超過之後可達半徑＝R − √(arm² − (arm − 超出量)²)。沒有幾何（認不得的機型）＝不收。 */
function reachAt(z, m) {
  const bedR = m.bedD / 2, g = m.geo;
  if (!g) return bedR;
  const lz = limitZ(g); if (z <= lz) return bedR;
  const a = z - lz; if (a >= g.arm) return 0;
  return Math.max(0, Math.min(bedR, g.R - Math.sqrt(g.arm * g.arm - (g.arm - a) * (g.arm - a))));
}
/* 磚置中；循環塔先試右邊，放不下或（reach）頂端搆不到改後面。磚角與塔身角都要在頂端搆得到。 */
function fitsAt(W, H, T, m, rule, tower) {
  const tw = tower || TOWER;
  if (W > m.bedD - W_MARGIN + 1e-6) return { ok: false, why: 'w' };
  if (H > m.maxH + 1e-6) return { ok: false, why: 'h' };
  const r = reachAt(H, m), bedR = m.bedD / 2;
  if (Math.hypot(W / 2, T / 2) > r + 1e-6) return { ok: false, why: 'top' };
  const half = tw.size / 2 + tw.brim, off = tw.gap + tw.brim + tw.size / 2, b = tw.size / 2;
  let beside = (W / 2 + off + half) <= bedR;
  if (beside && (rule || 'reach') === 'reach' && Math.hypot(W / 2 + off + b, b) > r + 1e-6) beside = false;
  const cx = beside ? W / 2 + off : 0, cy = beside ? 0 : T / 2 + off;
  if (Math.hypot(Math.abs(cx) + b, Math.abs(cy) + b) > r + 1e-6) return { ok: false, why: 'tower' };
  return { ok: true, beside };
}
/* thick(h)＝這個高度時的厚度（頁面：自己設的就用它，否則照料數預設＋防倒下限的自動值） */
function maxWLocked(asp, m, thick, rule) {
  let w = Math.floor(Math.min(m.bedD - W_MARGIN, m.maxH * asp) + 1e-9);
  for (; w > SIZE_MIN; w--) { const h = w / asp; if (fitsAt(w, h, thick(h), m, rule).ok) return w; }
  return SIZE_MIN;
}
function maxWFree(H, m, thick, rule) { let w = Math.floor(m.bedD - W_MARGIN); for (; w > SIZE_MIN; w--) { if (fitsAt(w, H, thick(H), m, rule).ok) return w; } return SIZE_MIN; }
function maxHFree(W, m, thick, rule) { let h = Math.floor(m.maxH); for (; h > SIZE_MIN; h--) { if (fitsAt(W, h, thick(h), m, rule).ok) return h; } return SIZE_MIN; }
/* 高到這台上限時最寬多少（只有 delta 頂端這一條會比「直徑 −50」更窄）；厚度用自動值（不看自己設的） */
function widestAtTop(m, thickAuto, rule) { const cw = m.bedD - W_MARGIN; for (let w = cw; w > SIZE_MIN; w--) { if (fitsAt(w, m.maxH, thickAuto(m.maxH), m, rule).ok) return w; } return SIZE_MIN; }
/* 擋住時講是哪一條（FBK-11；文案照原型 capReason） */
function capReason(W, H, T, pick, rule) {
  const m = pick.mdl, f = fitsAt(W, H, T, m, rule);
  if (f.why === 'w') return pick.name + ' 最寬 ' + (m.bedD - W_MARGIN) + ' mm（要留洗料塔的位置）';
  if (f.why === 'h') return pick.name + ' 最高 ' + m.maxH + ' mm（機型可印高度）';
  if (f.why === 'top') return '磚這麼高時，噴頭搆不到盤邊（機台越往上、搆得到的圈越小）';
  if (f.why === 'tower') return '磚這麼高時，旁邊的洗料塔印不到頂';
  return '';
}
function modelShort(model) { return String(model || '').replace(/\s*同進照片磚\s*$/, ''); }
/* 這次照哪一台算：宿主送的 models（system preset，依 printer_model 去重）裡，同料數、名字對得上進來時那台的（長名先比，
   「FD300 Pro」不會被當成 FD300）；對不上／不是從同進機進來＝同料數最小的那台（原型「不確定是哪台，先照最小的算」——
   也是匯出時 C++ 退回家族比對會切到的那台）。舊宿主沒送 models ＝ null ＝不加這一層（fail-open，800 mm 絕對上限照舊）。 */
function pickModel(models, mode, currentModel) {
  const list = (Array.isArray(models) ? models : []).filter(m => m && m.mode === mode && m.bedD > 0 && m.maxH > 0);
  if (!list.length) return null;
  if (currentModel) {
    const cur = String(currentModel);
    const hit = list.map(m => ({ m, s: modelShort(m.model) })).sort((a, b) => b.s.length - a.s.length)
      .find(x => x.s && (cur === x.s || cur.startsWith(x.s + ' ')));
    if (hit) return { mdl: hit.m, name: hit.s, unknown: false };
  }
  const small = list.slice().sort((a, b) => a.bedD - b.bedD || a.maxH - b.maxH)[0];
  return { mdl: small, name: modelShort(small.model), unknown: true };
}
function modelLabel(pick) { return pick.unknown ? '不確定是哪台，先照最小的 ' + pick.name + ' 算' : pick.mdl.model; }

/* ---- 我的款式（R9-11 Q9／Q10）：存在這台電腦（C++ phototile_mystyles_*，檔＝<data_dir>/phototile/my_styles.json） ---- */
const MINE_NAME_MAX = 20;
function clampTones(v, fallback) { return Math.max(2, Math.min(LEVELS_MAX, parseInt(v, 10) || fallback)); }
/* 名字、描述都要有；不能跟自己做的別款、也不能跟內建款式同名（計畫頁：原型只比自己做的那幾款） */
function validateMine(E, mine, builtinNames) {
  const miss = []; if (!String(E.name || '').trim()) miss.push('名字'); if (!String(E.desc || '').trim()) miss.push('畫風描述');
  if (miss.length) return '請先補上：' + miss.join('、') + '。';
  const nm = String(E.name).trim();
  if ((mine || []).some(s => String(s.name).trim() === nm && s.id !== E.id) || (builtinNames || []).some(n => String(n).trim() === nm))
    return '「' + nm + '」這個名字已經有一款了，換個名字。';
  return '';
}
/* 新增：從某一款的模板起頭——描述裡的 {minPx}／{tones} 保留成佔位字（產生提示詞時才換成當下的值） */
function mineDraftFrom(base, L) { return { id: null, base: base.id, name: '我的' + base.name, tones: base.tones, desc: templateOf(base, L) || '', err: '', showFull: false }; }
function mineRecord(E, mode, L, now) {
  const d = now || new Date();
  return { id: 'my-' + d.getTime().toString(36), name: String(E.name).trim().slice(0, MINE_NAME_MAX), tones: clampTones(E.tones, 3),
    desc: String(E.desc), base: E.base, mode, lang: L, created: d.toISOString().slice(0, 10) };
}
function mineValid(r) {
  return !!(r && typeof r.id === 'string' && /^my-[\w-]+$/.test(r.id) && typeof r.name === 'string' && r.name.trim()
    && typeof r.desc === 'string' && (r.mode === 'dual' || r.mode === 'quad') && Number.isInteger(r.tones) && r.tones >= 2 && r.tones <= LEVELS_MAX);
}
function parseMine(text) {
  let o; try { o = JSON.parse(text); } catch (e) { return { ok: false, list: [], why: '「我的款式」檔案不是合法 JSON' }; }
  const arr = o && Array.isArray(o.styles) ? o.styles : null;
  if (!arr) return { ok: false, list: [], why: '「我的款式」檔案的格式不對' };
  return { ok: true, list: arr.filter(mineValid), dropped: arr.filter(r => !mineValid(r)).length };
}
function serializeMine(list) { return JSON.stringify({ format: 'ping-phototile-mystyles', version: 1, styles: list || [] }, null, 1); }
function utf8Bytes(s) { return new TextEncoder().encode(s); }
function b64Bytes(bytes) { let s = ''; for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000)); return btoa(s); }
function bytesFromB64(b) { const s = atob(b || ''), out = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i); return out; }
function isPng(b) { const sig = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]; return !!b && b.length > 8 && sig.every((v, i) => b[i] === v); }

const core = {
  RATIO_TOL, SQUARE_TOL, SHAPE_SPLIT, SMALL_LONG_PX, CLOSE_DE, DE_WARN_DEFAULT, SAME_N, SAME_LUM_DIFF, SAME_SHARE, DETAIL_THIN_MAX_PX,
  MINW_FACTOR, LEVELS_MAX, W_MARGIN, SIZE_MIN, TOWER, REPLY_EN, MINE_NAME_MAX,
  langInfo, shapeKey, shapeOf, minPxOf, xMinMm, zMinMm, fillT, detailLine, paletteLine, templateOf, buildPrompt, phraseOf, fixText, sizeChangedNote,
  aspectGap, isMismatch, ratioTxt, cropPct, aiShapeLabel, lumGrid, sameAsPhoto, checkList, warnCount,
  limitZ, reachAt, fitsAt, maxWLocked, maxWFree, maxHFree, widestAtTop, capReason, modelShort, pickModel, modelLabel,
  clampTones, validateMine, mineDraftFrom, mineRecord, mineValid, parseMine, serializeMine, utf8Bytes, b64Bytes, bytesFromB64, isPng,
};
const doc = root && root.document;
if (!doc) return core;

/* ============================== 二、畫面 ============================== */
const GPT_URL = 'https://chatgpt.com/';
const RAW_CHUNK = 96 * 1024;            // 同 syncImageToHost：可被 3 整除＝base64 無中段 padding
const HOST_TIMEOUT_MS = 30000;
const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const $ = id => doc.getElementById(id);
let pg = null;                          // 頁面轉接物件（mount）
const S = {
  view: 'cards', styleId: null, step: 1, copied: false, away: false, back: false, showFull: false,
  copyWarn: '', pasteErr: '', hostErr: '', copiedWhat: '', recopied: false, copiedAt: null,
  results: [], cur: -1, fit: 'follow', busy: false, levelsBefore: null,
  mine: [], mineHost: false, mineNote: '', edit: null,
  capMsg: '', uiLang: '', photo: null, photoLum: null, stat: '',
  host: null, hostCrop: false,         // C++ 現在當來源的是清單裡哪一張（＋是不是裁過的）；換照片／回到原圖／本地款式重壓＝null
};
let seq = 0;

function lib() { return pg.lib(); }
function af() { return lib().constants.aiFlow; }
function L() { return langInfo(S.uiLang, af()).prompt; }
function mineById(id) { return S.mine.find(s => s.id === id) || null; }
function isMineId(id) { return /^my-/.test(String(id || '')); }
function styleOf(id) { return isMineId(id) ? mineById(id) : pg.styleById(id); }
function curStyle() { return S.styleId ? styleOf(S.styleId) : null; }
function cur() { return S.cur >= 0 ? S.results[S.cur] : null; }
function toast(t, ms) {
  const el = $('ptToast'); if (!el) return;
  el.textContent = t; el.classList.add('on');
  clearTimeout(toast._t); toast._t = setTimeout(() => el.classList.remove('on'), ms || 2600);
}
/* 這個款式現在提示詞用幾個色階：款式建議（題材別），壓不過這組料做得出的上限（同 ptApply、R6-7 附款） */
function tonesFor(st) { return Math.min(isMineId(st.id) ? st.tones : pg.tones(st), pg.levelCap(st.mode)); }
/* 磚「本身」的寬高比：鎖比例＝原照片的比例（AI 圖改了高度也不算）；解鎖＝寬×高 */
function baseAspect() {
  const p = pg.params();
  if (!pg.locked() && p.width > 0 && p.height > 0) return p.width / p.height;
  return S.photo ? S.photo.width / S.photo.height : (p.height > 0 ? p.width / p.height : 1);
}
function promptCtx(st) {
  const p = pg.params(), li = langInfo(S.uiLang, af());
  return { lib: lib(), style: st, mine: isMineId(st.id), lang: li.prompt, reply: li.reply, aspect: baseAspect(), tileW: p.width,
    xMinMm: xMinMm(p.noiseMm, p.nozzle), zMinMm: zMinMm(pg.layerMm(st.mode)), hexes: pg.colors(st.mode) || [], tones: tonesFor(st),
    enPalette: pg.enPalette };
}
function pxNow(st) {
  const c = promptCtx(st), sh = shapeOf(c.aspect, af(), c.lang);
  return { w: c.tileW, x: minPxOf(c.xMinMm, c.tileW, sh.px), z: minPxOf(c.zMinMm, c.tileW, sh.px), sh };
}
function promptFor(st, preface) { return buildPrompt(Object.assign(promptCtx(st), { preface: preface !== false })); }
/* 金鑰直連（開發者模式）要的生圖尺寸＝提示詞裡寫的那個形狀（R9-8 的三種尺寸） */
function apiSize() { return { landscape: '1536x1024', portrait: '1024x1536', square: '1024x1024' }[shapeKey(baseAspect())]; }
function fixFor(key) {
  const st = curStyle(); if (!st) return '';
  const c = promptCtx(st), n = pxNow(st);
  return fixText(af(), key, c.lang, { hexes: c.hexes, tones: c.tones, minPx: n.x, shape: n.sh });
}

/* ---- 各機上限：頁面接進改尺寸那一段（applySizeChange 呼叫 capFit） ---- */
function capPick(mode) { if (!pg) return null; const mc = pg.machineCap(); if (!mc || !Array.isArray(mc.models)) return null;
  const md = mode || pg.mode(); return pickModel(mc.models, md, (mc.current === md) ? mc.currentModel : null); }
function thickFn() { return h => pg.thickAt(h); }
function thickAutoFn() { return h => pg.thickAuto(h); }
function maxWidthNow(mode) {
  const pick = capPick(mode); if (!pick) return null;
  const p = pg.params();
  return pg.locked() ? maxWLocked(1 / pg.aspect(), pick.mdl, thickFn(), 'reach') : maxWFree(p.height, pick.mdl, thickFn(), 'reach');
}
/* 改寬或改高之後：超過這台上限就停在上限，旁邊一行講原因（Q2 硬上限，根 AGENTS 具名例外）。changed＝'width'|'height'。 */
function capFit(changed, prefix) {
  S.capMsg = '';
  const pick = capPick(); if (!pick) return;
  const p = pg.params(), th = thickFn(), m = pick.mdl;
  if (pg.locked()) {
    const hw = pg.aspect(), asp = 1 / hw, wm = maxWLocked(asp, m, th, 'reach');
    if (p.width > wm + 1e-9) {
      S.capMsg = (prefix || '已改成') + (prefix ? '寬改成 ' : '寬 ') + wm + ' mm——' + capReason(wm + 1, (wm + 1) / asp, th((wm + 1) / asp), pick, 'reach');
      p.width = wm; p.height = Math.round(wm * hw * 10) / 10;
    }
    return;
  }
  if (changed === 'height') {
    const hm = maxHFree(p.width, m, th, 'reach');
    if (p.height > hm + 1e-9) { S.capMsg = (prefix || '已改成') + (prefix ? '高改成 ' : '高 ') + hm + ' mm——' + capReason(p.width, hm + 1, th(hm + 1), pick, 'reach'); p.height = hm; }
    return;
  }
  const wm = maxWFree(p.height, m, th, 'reach');
  if (p.width > wm + 1e-9) { S.capMsg = (prefix || '已改成') + (prefix ? '寬改成 ' : '寬 ') + wm + ' mm——' + capReason(wm + 1, p.height, th(p.height), pick, 'reach'); p.width = wm; }
}
/* 尺寸卡那一行：「這台（FD300 同進照片磚）：寬最多 250 mm、高最多 300 mm…」＋擋住時的原因（黃字）。沒有 models＝回 false，頁面照舊寫自己的。 */
function capInfo(hintEl) {
  const pick = capPick(); if (!pick) return null;
  const p = pg.params(), m = pick.mdl, cw = m.bedD - W_MARGIN;
  const Wmax = maxWidthNow(), wt = widestAtTop(m, thickAutoFn(), 'reach');
  const hMax = pg.locked() ? Math.max(SIZE_MIN, Math.floor(Wmax * pg.aspect() * 10) / 10) : maxHFree(p.width, m, thickFn(), 'reach');
  const txt = '這台（' + modelLabel(pick) + '）：寬最多 ' + cw + ' mm、高最多 ' + m.maxH + ' mm'
    + (pg.locked() && Wmax < cw ? '；這個比例最寬 ' + Wmax + ' mm' : '')
    + (wt < cw ? '；磚很高時噴頭搆不到盤邊、寬再收（高 ' + m.maxH + ' mm 時最寬 ' + wt + ' mm）' : '');
  if (hintEl) { hintEl.style.color = ''; hintEl.innerHTML = esc(txt) + (S.capMsg ? '<br><span class="afWarnTx">⚠ ' + esc(S.capMsg) + '</span>' : ''); }
  return { maxW: Wmax, maxH: hMax };
}
/* 換機器／換料數／改厚度之後，目前的尺寸若已超過就收回來 */
function capRecheck(prefix) {
  const pick = capPick(); if (!pick) return;
  const p = pg.params();
  if (!fitsAt(p.width, p.height, pg.thickAt(p.height), pick.mdl, 'reach').ok) pg.resize(prefix || '');
  else pg.sizeBar();
}

/* ---- 宿主往返（C++ 刀 3 的協定；回呼掛在 window.PINGPhotoTile） ---- */
let impWait = null, originWait = null, mineSaveWait = null;
function sendChunks(prefix, begin, bytes, alive) {
  return (async () => {
    const chunks = Math.ceil(bytes.length / RAW_CHUNK);
    pg.send(prefix + 'begin', Object.assign({ size: bytes.length, chunks }, begin));
    for (let i = 0; i < chunks; i++) {
      if (!alive()) return;
      pg.send(prefix + 'chunk', { index: i, base64: b64Bytes(bytes.subarray(i * RAW_CHUNK, Math.min(bytes.length, (i + 1) * RAW_CHUNK))) });
      await new Promise(r => setTimeout(r, 0));
    }
    if (alive()) pg.send(prefix + 'end', {});
  })();
}
function hostWait(slot, onTimeout) {
  return new Promise(resolve => {
    const id = 'af-' + (++seq) + '-' + Date.now().toString(36);
    const t = setTimeout(() => { if (slot.get() && slot.get().id === id) { slot.set(null); resolve(onTimeout); } }, HOST_TIMEOUT_MS);
    const prev = slot.get(); if (prev) prev.done({ ok: false, stale: true });
    slot.set({ id, done: r => { clearTimeout(t); resolve(r); } });
  });
}
const impSlot = { get: () => impWait, set: v => { impWait = v; } };
/* 貼回的圖送進 C++（只收 PNG）；沒有宿主（瀏覽器直開）＝不送，直接算本機收下 */
function hostImport(bytes) {
  if (!pg.bridge()) return Promise.resolve({ ok: true, local: true });
  const p = hostWait(impSlot, { ok: false, message: '主程式沒有回應，這次沒有換圖——再貼一次試試。' });
  const id = impWait.id;
  sendChunks('phototile_ai_import_', { id }, bytes, () => impWait && impWait.id === id);
  return p;
}
function onImported(m) { if (!impWait || !m || m.id !== impWait.id) return; const w = impWait; impWait = null; w.done({ ok: !!m.ok, message: m.message || '' }); }

/* ---- 收圖：貼上／拖放／選擇檔案／〔貼上圖片〕／金鑰直連，全部到這裡 ---- */
async function acceptBlob(blob, how) {
  if (!blob || !/^image\//.test(blob.type || '')) { S.pasteErr = 'notimage'; render(); return; }
  let bmp; try { bmp = await createImageBitmap(blob); } catch (e) { S.pasteErr = 'notimage'; render(); return; }
  let png = null;
  if (blob.type === 'image/png') { const b = new Uint8Array(await blob.arrayBuffer()); if (isPng(b)) png = b; }
  addResult(bmp, png, how, curStyle(), false);
}
async function canvasPng(bmp, rect) {
  const r = rect || { x: 0, y: 0, w: bmp.width, h: bmp.height };
  const c = doc.createElement('canvas'); c.width = Math.max(1, Math.round(r.w)); c.height = Math.max(1, Math.round(r.h));
  c.getContext('2d').drawImage(bmp, r.x, r.y, r.w, r.h, 0, 0, c.width, c.height);
  const blob = await new Promise(res => c.toBlob(res, 'image/png'));
  const bytes = new Uint8Array(await blob.arrayBuffer());
  return { bytes, bmp: rect ? await createImageBitmap(c) : bmp };
}
function photoLum() {
  if (!S.photo) return null;
  if (!S.photoLum) S.photoLum = lumOfBitmap(S.photo);
  return S.photoLum;
}
/* 縮到 64×64 要用高品質平滑：預設的雙線性在 1200→64 這種倍率只取樣幾個像素（條紋貓實測同一張圖兩次縮出來 13% 的格不同、
   「貼成原圖」整個抓不到）；high＝先做 mipmap 平均，同一張圖每次都縮成同一份。 */
function lumOfBitmap(b) { const c = doc.createElement('canvas'); c.width = SAME_N; c.height = SAME_N; const x = c.getContext('2d'); x.imageSmoothingQuality = 'high'; x.drawImage(b, 0, 0, SAME_N, SAME_N); return lumGrid(x.getImageData(0, 0, SAME_N, SAME_N).data); }
/* 新的一張：進清單、成為目前這張、照目前的比例規則（磚跟著圖）送進 C++；imported＝金鑰直連（C++ 已經換好來源）。 */
function addResult(bmp, png, how, st, imported) {
  const r = { id: ++seq, bmp, png, how, style: st ? st.id : S.styleId, w: bmp.width, h: bmp.height, crops: {}, used: false };
  r.same = S.photo ? sameAsPhoto(bmp.width / bmp.height, S.photo.width / S.photo.height, lumOfBitmap(bmp), photoLum()) : false;
  S.results.push(r);
  if (imported) { S.host = r; S.hostCrop = false; }
  S.pasteErr = ''; S.hostErr = ''; S.copiedWhat = ''; S.recopied = false; S.fit = 'follow'; S.view = 'ai'; S.step = 4;
  if (r.style) S.styleId = r.style;
  return useResult(r, '磚跟著 AI 圖的比例，', null);
}
/* 把清單裡這一張換成目前的來源：裁切模式先裁好再送（壓平與輸出吃的就是裁好那張）；成功才換，失敗原本的圖不動。
   undo＝失敗時要退回的那一步（例：切裁切失敗＝切回跟著圖）。 */
async function useResult(r, prefix, undo) {
  const st = styleOf(r.style) || curStyle(); if (!st) return;
  const crop = pg.locked() && S.fit === 'crop' && isMismatch(r.w / r.h, baseAspect());
  let bytes = null, bmp = r.bmp;
  const fail = msg => {
    S.hostErr = msg;
    if (!r.used) { const i = S.results.indexOf(r); if (i >= 0) S.results.splice(i, 1); }
    if (undo) undo();
    if (!cur()) S.step = S.copied ? 3 : 1;
    render();
  };
  try {
    if (crop) {
      const aT = baseAspect(), key = aT.toFixed(4);
      if (!r.crops[key]) {
        const aI = r.w / r.h, rect = aI > aT ? { x: (r.w - r.h * aT) / 2, y: 0, w: r.h * aT, h: r.h } : { x: 0, y: (r.h - r.w / aT) / 2, w: r.w, h: r.w / aT };
        r.crops[key] = await canvasPng(r.bmp, rect);
      }
      bytes = r.crops[key].bytes; bmp = r.crops[key].bmp;
    } else if (S.host !== r || S.hostCrop) {
      if (!r.png) r.png = (await canvasPng(r.bmp)).bytes;
      bytes = r.png;
    }
  } catch (e) { fail('這張圖轉不成 PNG，這次沒有換圖。'); return; }
  if (crop && S.host === r && S.hostCrop === aspKey()) bytes = null;
  S.stat = '';
  if (bytes) { S.busy = true; pg.busyText('收圖中…'); render(); }
  const res = bytes ? await hostImport(bytes) : { ok: true };
  S.busy = false; if (bytes) pg.busyText('');
  if (res.stale) { if (!r.used) { const i = S.results.indexOf(r); if (i >= 0) S.results.splice(i, 1); } return; }
  if (!res.ok) { fail('⚠ ' + (res.message || '這次沒有換圖。')); return; }
  r.used = true; S.host = r; S.hostCrop = crop ? aspKey() : false; S.cur = S.results.indexOf(r);
  if (S.levelsBefore === null) S.levelsBefore = pg.params().klevels;
  pg.accept(bmp, st, prefix || '');
  render();
}
function aspKey() { return baseAspect().toFixed(4); }
async function pasteButton() {
  const nav = root.navigator;
  if (!nav.clipboard || !nav.clipboard.read) { S.pasteErr = 'denied'; render(); return; }
  try {
    const items = await nav.clipboard.read();
    for (const it of items) { const t = it.types.find(x => x.startsWith('image/')); if (t) return acceptBlob(await it.getType(t), '貼上'); }
    S.pasteErr = items.some(it => it.types.includes('text/plain')) ? 'text' : 'empty'; render();
  } catch (e) { S.pasteErr = 'denied'; render(); }
}
async function copyText(t) {
  try { await root.navigator.clipboard.writeText(t); return true; } catch (e) { /* 退回下面那條 */ }
  try { const ta = doc.createElement('textarea'); ta.value = t; ta.style.position = 'fixed'; ta.style.opacity = '0'; doc.body.appendChild(ta); ta.select();
    const ok = doc.execCommand('copy'); ta.remove(); return ok; } catch (e) { return false; }
}
async function copyPhoto() {
  if (!S.photo) return false;
  try { const c = doc.createElement('canvas'); c.width = S.photo.width; c.height = S.photo.height; c.getContext('2d').drawImage(S.photo, 0, 0);
    const blob = await new Promise(r => c.toBlob(r, 'image/png'));
    await root.navigator.clipboard.write([new root.ClipboardItem({ 'image/png': blob })]); return true; } catch (e) { return false; }
}
/* 開 ChatGPT：用 App 既有的 common_openurl（讀最外層 url、用系統預設瀏覽器開）；window.open 在 App 裡會把整個工作室換掉，只在瀏覽器直開時用 */
function openGpt() {
  const wx = root.wx;
  if (wx && typeof wx.postMessage === 'function') { wx.postMessage(JSON.stringify({ sequence_id: String(Date.now()), command: 'common_openurl', url: GPT_URL })); return; }
  try { root.open(GPT_URL, '_blank', 'noopener'); } catch (e) { /* 開不了就算了，網址寫在按鈕上 */ }
}

/* ---- 〔回到原圖〕：C++ 來源換回原照片（phototile_source_origin）；這次產的圖留在清單上 ---- */
const originSlot = { get: () => originWait, set: v => { originWait = v; } };
async function backToPhoto() {
  if (pg.bridge()) {
    const p = hostWait(originSlot, { ok: false, message: '主程式沒有回應，這次沒有換回原圖。' });
    pg.send('phototile_source_origin', {});
    const r = await p; if (r.stale) return;
    if (!r.ok) { toast('⚠ ' + (r.message || '這次沒有換回原圖。'), 3600); return; }
  }
  pg.restorePhoto(S.photo, S.levelsBefore);
  S.levelsBefore = null; S.cur = -1; S.host = null; S.stat = ''; S.step = S.copied ? 3 : 1; S.fit = 'follow'; S.hostErr = '';
  render(); toast('已回到原圖；這次產的圖留在款式那邊，點一下可以再用');
}
function onOrigin(m) { if (!originWait) return; const w = originWait; originWait = null; w.done({ ok: !!(m && m.ok), message: (m && m.message) || '' }); }

/* ---- 我的款式：讀、存（宿主只負責完整地存讀；內容是這裡定的 JSON） ---- */
const mineSlot = { get: () => mineSaveWait, set: v => { mineSaveWait = v; } };
function loadMine() { if (pg.bridge()) pg.send('phototile_mystyles_load', {}); }
function onMineLoaded(m) {
  if (!m || !m.ok) { S.mineHost = false; S.mineNote = (m && m.message) || ''; render(); return; }
  S.mineHost = true; S.mineNote = '';
  if (!m.exists) { pg.renderCards(); return; }
  try { const r = parseMine(new TextDecoder().decode(bytesFromB64(m.base64)));
    if (r.ok) S.mine = r.list; else { S.mineHost = false; S.mineNote = r.why + '，這次沒有載入（原檔未動）。'; }
  } catch (e) { S.mineHost = false; S.mineNote = '「我的款式」讀不回來，這次沒有載入（原檔未動）。'; }
  pg.renderCards(); render();
}
async function saveMine() {
  if (!pg.bridge() || !S.mineHost) return { ok: false, local: true };
  const p = hostWait(mineSlot, { ok: false, message: '主程式沒有回應，「我的款式」這次沒有存檔。' });
  const id = mineSaveWait.id;
  sendChunks('phototile_mystyles_save_', {}, utf8Bytes(serializeMine(S.mine)), () => mineSaveWait && mineSaveWait.id === id);
  return p;
}
function onMineSaved(m) { if (!mineSaveWait) return; const w = mineSaveWait; mineSaveWait = null; w.done({ ok: !!(m && m.ok), message: (m && m.message) || '' }); }

/* ============================== 畫面 ============================== */
const mfSw = h => '<span class="mfSw" style="background:' + esc(h) + '"></span>';
function matsHtml(st) {
  const inf = pg.matInfo(st.mode); if (!inf) return '還沒選';
  return inf.colors.map((h, i) => mfSw(h) + ' ' + esc((inf.mats[i] && inf.mats[i].label) || h)).join(' × ');
}
function usableOf(st) {
  if (isMineId(st.id)) return { ok: true };
  const need = pg.minTile(st), p = pg.params();
  if (p.width >= need) return { ok: true, need };
  const wm = maxWidthNow(st.mode);
  return (wm !== null && wm < need) ? { ok: false, cant: true, need, wm } : { ok: false, need };
}
function stepsHtml() {
  const st = curStyle(), mine = isMineId(st.id), u = usableOf(st), n = pxNow(st), p = pg.params(), s = S.step, pick = capPick(st.mode);
  const cls = k => 'afStep' + (s === k ? ' cur' : '') + ((k === 1 && S.copied) ? ' done' : '');
  const errs = { text: '剪貼簿裡是文字（可能是剛剛複製的提示詞），不是圖片。到 AI 那邊的圖上按右鍵 →「複製圖片」，再回來貼；或下載後按〔選擇檔案…〕。',
    empty: '剪貼簿裡沒有圖片。到 AI 那邊的圖上按右鍵 →「複製圖片」，再回來貼。',
    denied: '這個畫面不讓按鈕直接讀剪貼簿——請直接按 Ctrl+V，或按〔選擇檔案…〕。', notimage: '這個檔案不是圖片（可以用 PNG、JPG、WebP）。' };
  const langTag = '<span class="afLang">提示詞：' + (L() === 'zh' ? '中文' : 'English') + '（跟著介面語言）</span>';
  const note = sizeChangedNote(S.copiedAt, n);
  let h = '';
  if (S.results.length) h += histHtml(false);
  if (!u.ok) h += u.cant ? '<div class="afBanner">這款要把寬度放大到 ' + u.need + ' mm 才印得出細節，但這台（' + esc(pick ? pick.name : '') + '）這個比例最寬 ' + u.wm + ' mm——換大一點的機器，或選別的款式</div>'
    : '<div class="afBanner">這款要把寬度放大到 ' + u.need + ' mm 才印得出細節（目前 ' + p.width + ' mm）<button class="btn afSm" data-af="enlarge">放大到 ' + u.need + ' mm</button></div>';
  h += '<div class="afSum">磚 ' + p.width + '×' + p.height + ' mm（<button class="mfLnk" data-af="focusSize">改尺寸</button>；先定尺寸再複製，AI 畫的細節才會剛好）・提示詞已照這一格寫好：'
    + (mine ? '我的款式「' + esc(st.name) + '」' : esc(st.name)) + '・' + tonesFor(st) + ' 個色階・只用你選的這組料（' + matsHtml(st) + '）・' + esc(n.sh.ui) + '・線條不會細到印不出來'
    + langTag + ' <button class="mfLnk" data-af="toggleFull">' + (S.showFull ? '收起全文' : '看全文') + '</button></div>';
  h += '<div class="afSteps">';
  h += '<div class="' + cls(1) + '"><div class="t"><span class="n">' + (S.copied ? '✓' : '1') + '</span>複製提示詞</div>'
    + '<div class="afActs"><button class="btn' + (S.copied ? '' : ' primary') + '" data-af="copyPrompt" id="afCopyPrompt">' + (S.copied ? '再複製一次' : '複製提示詞') + '</button></div>'
    + (S.copyWarn ? '<div class="mfFlash warn">' + esc(S.copyWarn) + '</div>' : '')
    + (S.copied && !note ? '<div class="afOk">✓ 已放進剪貼簿，到 ChatGPT 的對話框按 Ctrl+V 貼上。</div>' : '')
    + (note ? '<div class="mfFlash warn">' + esc(note) + '</div>' : '')
    + '<div class="d sub">貼回圖時，色階數會改成 ' + tonesFor(st) + ' 階（這款的設定；目前 ' + p.klevels + ' 階）。</div></div>';
  h += '<div class="' + cls(2) + '"><div class="t"><span class="n">2</span>到 ChatGPT 產圖</div>'
    + '<div class="afActs"><button class="btn ghost afSm" data-af="openGPT">開啟 ChatGPT ↗</button><span class="sub">要先登入（免費帳號即可）</span></div>'
    + '<ol class="afGuide"><li>把你的照片拖進對話框（照片不在手邊：<button class="mfLnk" data-af="copyPhoto">複製這張照片</button>再到對話框 Ctrl+V）</li>'
    + '<li>按 Ctrl+V 貼上提示詞，送出</li><li>圖出來後，在圖上按右鍵「複製圖片」（或下載）</li></ol></div>';
  h += '<div class="' + cls(3) + '"><div class="t"><span class="n">3</span>把圖貼回來</div>'
    + '<div class="afDrop" data-afdrop="1"><div class="afActs"><button class="btn primary" data-af="pasteBtn">貼上圖片</button>'
    + '<button class="btn" data-af="pickFile">選擇檔案…</button></div><span class="hint">' + (S.back ? '<b>回來了？</b>把剛剛複製的圖貼上。' : '或把圖拖到這裡；在工作室按 Ctrl+V 也可以') + '</span></div></div>';
  h += '</div>';
  if (S.pasteErr) h += '<div class="mfFlash warn">' + errs[S.pasteErr] + '</div>';
  if (S.hostErr) h += '<div class="mfFlash warn">' + esc(S.hostErr) + '</div>';
  h += '<div class="afFine">Gemini、Copilot 等會產圖的 AI 也可以用（照做程度不一）；沒登入的 ChatGPT 不能上傳照片、也不能產圖，沒帳號可免費註冊。免費帳號每天能產的張數有限、各家規定常變。'
    + '照片是傳到你自己的 AI 帳號，PING 不經手。有的免費工具會在圖角加小標誌（浮水印），它會跟著印出來。</div>';
  if (S.showFull) h += '<pre class="afPrompt" id="afPromptPre">' + esc(promptFor(st)) + '</pre>';
  return h;
}
function histHtml(withAgain) {
  return '<div class="afHist"><span class="lb">這次產的：</span>' + S.results.map((x, i) => '<button class="afThumb' + (i === S.cur ? ' cur' : '') + '" data-af="pick" data-i="' + i + '"><canvas data-afth="' + i + '" width="54" height="36"></canvas>第 ' + (i + 1) + ' 張</button>').join('')
    + (withAgain ? '<span class="afAgain"><button class="btn afSm" data-af="pasteBtn">貼上新的圖</button><button class="btn afSm" data-af="pickFile">選擇檔案…</button></span>'
      + '<span class="grow"></span><button class="mfLnk" data-af="reshowSteps">看三個步驟</button>' : '') + '</div>';
}
function resultHtml() {
  const r = cur(), st = curStyle(), p = pg.params(), aT = baseAspect(), aspI = r.w / r.h, shT = shapeOf(aT, af(), L());
  const de = pg.deltaE(), warnDE = pg.deWarn();
  const list = checkList({ same: r.same, locked: pg.locked(), aspI, aspT: aT, deltaE: de, warnDE, w0: r.w, h0: r.h });
  const chk = (k, ic, body) => '<div class="afChk ' + k + '"><span class="ic">' + ic + '</span><div class="tx">' + body + '</div></div>';
  const out = list.map(c => {
    if (c.key === 'same') return chk('warn', '!', '<b>這張跟原圖幾乎一樣</b>——是不是貼到原本的照片了？AI 產的圖要到 AI 那邊的圖上按右鍵「複製圖片」再貼回來。');
    if (c.key === 'lock') return chk('warn', '!', '<b>比例鎖已解除</b>：圖會被拉成 ' + p.width + ' × ' + p.height + ' mm 的比例（會變形）。'
      + '<div class="afActs"><button class="btn ghost afSm" data-af="relock">鎖回原圖比例</button></div>');
    if (c.key === 'shape') {
      if (c.state === 'ok') return chk('ok', '✓', '比例：' + esc(shT.ui) + '，跟磚一樣');
      const h0 = Math.round(p.width / aT * 10) / 10;
      return chk('warn', '!', '<b>比例：AI 給的是' + aiShapeLabel(aspI) + '，磚原本是' + esc(shT.ui) + '</b><br>'
        + (S.fit === 'follow' ? '已把磚改成 ' + p.width + ' × ' + p.height + ' mm（跟圖一樣，不變形、不裁掉東西）。' : '已把圖裁成磚的比例（' + cropPct(aspI, aT) + '），磚維持 ' + p.width + ' × ' + p.height + ' mm。')
        + '<div class="afActs">' + (S.fit === 'follow' ? '<button class="btn ghost afSm" data-af="fit" data-v="crop">改成裁切，維持 ' + p.width + ' × ' + h0 + ' mm</button>'
          : '<button class="btn ghost afSm" data-af="fit" data-v="follow">改成磚跟著圖</button>')
        + '<button class="mfLnk" data-af="fix" data-k="shape">或請 AI 重產' + esc(shT.ui) + '（複製這句）</button></div>');
    }
    if (c.key === 'color') return c.state === 'warn'
      ? chk('warn', '!', '<b>顏色：AI 用了這組料印不出來的顏色</b>（模擬色差 ' + de.toFixed(1) + '）——印出來會跟這張圖差很多。'
        + '<div class="afActs"><button class="btn ghost afSm" data-af="fix" data-k="color">複製「只用這組料的顏色」的說法</button></div>')
      : chk('ok', '✓', '顏色：這組料印得出來（模擬色差 ' + de.toFixed(1) + '，' + (de < CLOSE_DE ? '很貼近' : '貼近') + '）');
    if (c.key === 'small') return c.state === 'warn' ? chk('warn', '!', '<b>圖太小（' + r.w + ' × ' + r.h + '）</b>：細節會糊。到 AI 那邊下載原圖（不要截縮圖）再貼回來。')
      : chk('ok', '✓', '圖夠大：' + r.w + ' × ' + r.h);
    return chk('eye', '?', '請看一眼圖的四個角：有 AI 工具的小標誌（浮水印），它會被印出來——換一張或換一個工具。');
  });
  const nw = warnCount(list);
  const verdict = nw ? '<div class="mfFlash warn afVerdict">有 ' + nw + ' 件事建議看一下（不擋你：照樣可以按右上〔產生並載入列印板〕）。</div>'
    : '<div class="mfFlash ok afVerdict">✓ 可以印——看中間的列印模擬，滿意就按右上〔產生並載入列印板〕。</div>';
  const pe = { text: '剪貼簿裡是文字，不是圖片。到 AI 那邊的圖上按右鍵 →「複製圖片」再貼。', empty: '剪貼簿裡沒有圖片。',
    denied: '這個畫面不讓按鈕直接讀剪貼簿——請直接按 Ctrl+V，或按〔選擇檔案…〕。', notimage: '這個檔案不是圖片（可以用 PNG、JPG、WebP）。' };
  let h = histHtml(true);
  if (S.pasteErr) h += '<div class="mfFlash warn">' + pe[S.pasteErr] + '</div>';
  if (S.hostErr) h += '<div class="mfFlash warn">' + esc(S.hostErr) + '</div>';
  h += verdict + out.join('');
  h += '<div class="afStat sub" id="afStat"></div>';
  const n = pxNow(st), note = sizeChangedNote(S.copiedAt, n);
  if (note) h += '<div class="mfFlash warn afRow">' + esc(note) + '<button class="btn afSm" data-af="recopy">複製新的提示詞</button></div>';
  if (S.recopied) h += '<div class="afOk">✓ 已複製新的提示詞（最細 ' + n.x + ' px）——貼到 ChatGPT 同一個對話送出，新的圖一樣貼回來。</div>';
  const lab = k => esc((phraseOf(af(), k) || {}).label || '');
  h += '<div class="afRetry"><div class="t">自己看了不滿意？換個說法再產一次 <span class="sub" style="font-weight:400">按一下會複製一句話（' + (L() === 'zh' ? '中文' : 'English') + '），貼到 ChatGPT 同一個對話送出，新的圖一樣貼回來</span></div><div class="afChips">'
    + (af().retry || []).map(p2 => '<button class="afChip" data-af="fix" data-k="' + esc(p2.key) + '">' + esc(p2.label) + '</button>').join('') + '</div>'
    + (S.copiedWhat ? '<div class="afOk">✓ 已複製「' + lab(S.copiedWhat) + '」的說法——貼到 ChatGPT 同一個對話送出，新的圖一樣貼回來（Ctrl+V）。</div>' : '') + '</div>';
  return h;
}
function baseChoices() { const md = pg.mode(); return lib().styles.filter(s => s.requiresAI && s.mode === md); }
function editHtml() {
  const E = S.edit, isNew = !E.id;
  const full = E.showFull ? '<pre class="afPrompt">' + esc(promptFor({ id: 'my-preview', name: E.name, tones: E.tones, desc: E.desc, mode: pg.mode(), base: E.base })) + '</pre>' : '';
  return '<div class="afEdit">'
    + '<div class="row"><label>從哪一款開始</label><select id="afEdBase">' + baseChoices().map(s => '<option value="' + esc(s.id) + '"' + (s.id === E.base ? ' selected' : '') + '>' + esc(s.name) + '（' + s.tones + ' 階）</option>').join('') + '</select>'
    + '<span class="sub">換這個會重填下面的畫風描述</span></div>'
    + '<div class="row"><label>名字</label><input type="text" id="afEdName" class="fld" maxlength="' + MINE_NAME_MAX + '" value="' + esc(E.name) + '">'
    + '<b class="afTonesLb">色階數</b><input type="number" id="afEdTones" min="2" max="' + LEVELS_MAX + '" value="' + E.tones + '"><span class="sub">剛好幾個平色（最多 ' + LEVELS_MAX + '）</span></div>'
    + '<div class="row top"><label>畫風描述</label><div class="fld"><textarea id="afEdDesc" rows="4">' + esc(E.desc) + '</textarea></div></div>'
    + '<div class="afAuto">系統會自動接在後面、不用寫：只用你選的這組料的顏色、剛好 ' + E.tones + ' 個平色、線條不會細到印不出來、主體填滿畫面、五官畫清楚。'
    + '<button class="mfLnk" data-af="edFull">' + (E.showFull ? '收起全文' : '看會複製出去的全文') + '</button></div>' + full
    + (E.err ? '<div class="mfFlash warn">' + esc(E.err) + '</div>' : '')
    + '<div class="afActs afEdActs">' + (isNew ? '' : '<button class="btn afDangerGhost afSm" data-af="edDelete">刪除這一款</button>') + '<span class="grow"></span>'
    + '<button class="btn primary" data-af="edSave">' + (isNew ? '存成我的款式' : '存檔') + '</button><button class="btn" data-af="edCancel">取消</button></div>'
    + (S.mineHost ? '' : '<div class="afFine">' + esc(S.mineNote || '（這個瀏覽器存不了，重新整理就會不見；產品裡是存在這台電腦的材料庫資料夾）') + '</div>')
    + '</div>';
}
function readEdit() {
  const E = S.edit; if (!E) return;
  const n = $('afEdName'), t = $('afEdTones'), d = $('afEdDesc');
  if (n) E.name = n.value; if (t) E.tones = clampTones(t.value, E.tones); if (d) E.desc = d.value;
}
function devRowHtml() {
  return pg.devAvail() ? '<div class="afDev"><b>開發者模式</b><button class="btn ghost afSm" data-af="devGen">用金鑰直接產一張</button><span>' + esc(pg.aiCost()) + '（內部驗提示詞用；客戶看不到這一行）</span></div>' : '';
}
let h2Cards = null;
function render() {
  if (!pg) return;
  const pad = $('afPad'), sp = $('stylePad'), h2 = $('styleH2');
  if (!pad || !sp || !h2) return;
  if (S.view === 'edit') readEdit();
  if (S.view === 'ai' && !curStyle()) S.view = 'cards';
  if (S.view === 'ai' && curStyle().mode !== pg.mode()) S.view = 'cards';     // 料數換了＝那一款不適用（同原型換機器回清單）
  doc.body.classList.toggle('afOpen', S.view !== 'cards');
  if (S.view === 'cards') {
    pad.hidden = true; sp.hidden = false;
    if (h2Cards !== null && h2.innerHTML !== h2Cards) h2.innerHTML = h2Cards;
    return;
  }
  pad.hidden = false; sp.hidden = true;
  if (S.view === 'edit') {
    h2.innerHTML = '款式 ›&nbsp;' + (S.edit.id ? '編輯我的款式' : '自己做一款') + ' <span class="afCrumb">改完按存；存在這台電腦</span>'
      + '<span class="afRt"><button class="mfLnk" data-af="edCancel">‹ 款式清單</button></span>';
    pad.innerHTML = editHtml(); return;
  }
  const st = curStyle(), mine = isMineId(st.id);
  h2.innerHTML = '款式 ›&nbsp;' + esc(st.name) + ' <span class="afTag">' + (mine ? '我的' : 'AI') + '</span><span class="afCrumb">用你自己的 AI 產圖，不用金鑰</span>'
    + '<span class="afRt">' + (mine ? '<button class="mfLnk" data-af="editMine">編輯這一款</button>' : '') + '<button class="mfLnk" data-af="toCards">‹ 款式清單</button></span>';
  pad.innerHTML = ((S.step === 4 && cur()) ? resultHtml() : stepsHtml()) + devRowHtml();
  pad.querySelectorAll('canvas[data-afth]').forEach(c => { const r = S.results[+c.dataset.afth]; if (!r) return; const x = c.getContext('2d'); x.clearRect(0, 0, 54, 36);
    const k = Math.min(54 / r.w, 36 / r.h), w = r.w * k, hh = r.h * k; x.drawImage(r.bmp, (54 - w) / 2, (36 - hh) / 2, w, hh); });
  const st2 = $('afStat'); if (st2) st2.innerHTML = statHtml();
}

/* ---- 動作 ---- */
function open(id) {
  const st = styleOf(id); if (!st) return;
  if (!pg.ensureMode(st.mode)) return;
  if (S.styleId !== id) { S.copied = false; S.copiedAt = null; S.step = 1; S.back = false; S.copyWarn = ''; S.showFull = false; S.pasteErr = ''; S.hostErr = ''; }
  S.styleId = id; S.view = 'ai';
  if (cur() && cur().style === id) S.step = 4; else if (S.step === 4) S.step = S.copied ? 3 : 1;
  pg.select(id); render();
}
function startEdit(existing) {
  if (existing) S.edit = { id: existing.id, base: existing.base, name: existing.name, tones: existing.tones, desc: existing.desc, err: '', showFull: false };
  else {
    const md = pg.mode(), subj = pg.subject();
    const b = lib().styles.find(s => s.requiresAI && s.mode === md && s.subjects.includes(subj)) || baseChoices()[0];
    if (!b) return;
    S.edit = mineDraftFrom(b, L());
  }
  S.view = 'edit'; render();
  setTimeout(() => { const n = $('afEdName'); if (n) n.focus(); }, 0);
}
function confirmBox(title, body, yesText) {
  return new Promise(res => {
    doc.querySelectorAll('.cfmBack').forEach(e => e.remove());
    const back = doc.createElement('div'); back.className = 'cfmBack';
    back.innerHTML = '<div class="cfmBox"><div class="cfmTitle"></div><div class="cfmBody"></div><div class="cfmBtns"><button class="btn afDanger" data-k="yes"></button><button class="btn" data-k="no">取消</button></div></div>';
    back.querySelector('.cfmTitle').textContent = title; back.querySelector('.cfmBody').textContent = body; back.querySelector('[data-k=yes]').textContent = yesText;
    doc.body.appendChild(back);
    const close = v => { doc.removeEventListener('keydown', onK); back.remove(); res(v); };
    const onK = e => { if (e.key === 'Escape') close(false); };
    doc.addEventListener('keydown', onK);
    back.addEventListener('mousedown', e => { if (e.target === back) close(false); });
    back.querySelector('[data-k=yes]').onclick = () => close(true); back.querySelector('[data-k=no]').onclick = () => close(false);
    back.querySelector('[data-k=no]').focus();
  });
}
async function act(a, b) {
  if (a === 'openStyle') { open(b.dataset.s); return; }
  if (a === 'newMine') { startEdit(null); return; }
  if (a === 'toCards') { S.view = 'cards'; render(); pg.renderCards(); return; }
  if (a === 'toggleFull') { S.showFull = !S.showFull; render(); return; }
  if (a === 'enlarge') { const u = usableOf(curStyle()); pg.setWidth(u.need); S.copyWarn = ''; render(); toast('寬度已放大到 ' + pg.params().width + ' mm（高度等比）'); return; }
  if (a === 'focusSize') { pg.focusSize(); toast('尺寸在中間「列印模擬」下面；先定尺寸再複製，提示詞的最細線寬會照這個尺寸寫', 3400); return; }
  if (a === 'copyPrompt') {
    const st = curStyle(), u = usableOf(st);
    if (!pg.hasImage()) { S.copyWarn = '請先載入一張照片，AI 款式是拿你的照片去改，不是憑空生一張。'; render(); return; }
    if (!pg.matReady()) { S.copyWarn = '先在右欄「選顏色」選一組校正過的料——AI 產圖會照這組料印得出的顏色去畫。'; render(); return; }
    if (!u.ok) { S.copyWarn = u.cant ? '這台放不到 ' + u.need + ' mm，這款的細節印不出來——選別的款式，或換大一點的機器。' : '先按上面〔放大到 ' + u.need + ' mm〕再複製——尺寸會決定提示詞裡的最細線寬。'; render(); return; }
    if (await copyText(promptFor(st))) { S.copied = true; S.copiedAt = pxNow(st); S.copyWarn = ''; S.step = 2; render(); }
    else toast('剪貼簿寫不進去——按「看全文」自己選取複製'); return;
  }
  if (a === 'openGPT') { openGpt(); return; }
  if (a === 'copyPhoto') { toast(await copyPhoto() ? '已複製這張照片——到 ChatGPT 的對話框按 Ctrl+V' : '這個瀏覽器不讓複製圖片——請用檔案上傳'); return; }
  if (a === 'pasteBtn') { await pasteButton(); return; }
  if (a === 'pickFile') { fileInput().click(); return; }
  if (a === 'pick') { const i = +b.dataset.i, r = S.results[i]; if (S.busy || !r) return; S.view = 'ai'; S.step = 4;
    if (i === S.cur) { render(); return; }
    S.fit = 'follow'; S.copiedWhat = ''; useResult(r, '磚跟著這張圖的比例，', null); return; }
  if (a === 'fit') { const r = cur(), was = S.fit; if (S.busy || !r) return; S.fit = b.dataset.v; useResult(r, '', () => { S.fit = was; }); return; }
  if (a === 'relock') { pg.setLocked(true); render(); return; }
  if (a === 'recopy') { const st = curStyle(); if (await copyText(promptFor(st))) { S.copiedAt = pxNow(st); S.recopied = true; render(); } else toast('剪貼簿寫不進去'); return; }
  if (a === 'fix') { const k = b.dataset.k; if (await copyText(fixFor(k))) { S.copiedWhat = k; render(); } else toast('剪貼簿寫不進去'); return; }
  if (a === 'reshowSteps') { S.step = 3; S.back = false; render(); return; }
  if (a === 'backToPhoto') { backToPhoto(); return; }
  if (a === 'devGen') { const st = curStyle(); if (st && !pg.busy()) pg.devGenerate(st, tonesFor(st)); return; }
  if (a === 'editMine') { const st = curStyle(); if (st && isMineId(st.id)) startEdit(st); return; }
  if (a === 'edFull') { readEdit(); S.edit.showFull = !S.edit.showFull; render(); return; }
  if (a === 'edCancel') { const back = S.edit && S.edit.id && mineById(S.edit.id); S.edit = null; S.view = back ? 'ai' : 'cards'; render(); pg.renderCards(); return; }
  if (a === 'edSave') {
    readEdit(); const E = S.edit;
    E.err = validateMine(E, S.mine, lib().styles.map(s => s.name));
    if (E.err) { render(); return; }
    let rec;
    if (E.id) { rec = mineById(E.id); Object.assign(rec, { name: E.name.trim(), tones: E.tones, desc: E.desc, base: E.base }); }
    else { rec = mineRecord(E, pg.mode(), L()); S.mine.push(rec); }
    const r = await saveMine();
    S.edit = null; S.styleId = rec.id; S.view = 'ai'; S.step = 1; S.copied = false; S.copiedAt = null; S.back = false; S.showFull = false;
    pg.select(rec.id); pg.renderCards(); render();
    toast(r.ok ? '已存成我的款式「' + rec.name + '」；重新整理或下次打開都還在' : (r.local ? '已存（這個瀏覽器存不了，重新整理就會不見）' : '⚠ ' + r.message), 3400);
    return;
  }
  if (a === 'edDelete') {
    readEdit(); const rec = mineById(S.edit && S.edit.id); if (!rec) return;
    if (!(await confirmBox('要刪除我的款式「' + rec.name + '」嗎？', '刪掉就回不來；已經產好的圖不受影響。', '刪除'))) return;
    S.mine = S.mine.filter(s => s.id !== rec.id);
    const r = await saveMine();
    S.edit = null; S.styleId = null; S.view = 'cards'; render(); pg.renderCards();
    toast(r.ok || r.local ? '已刪除「' + rec.name + '」' : '⚠ ' + r.message); return;
  }
}
let fileEl = null;
function fileInput() {
  if (fileEl) return fileEl;
  fileEl = doc.createElement('input'); fileEl.type = 'file'; fileEl.accept = 'image/*'; fileEl.hidden = true; fileEl.id = 'afFileIn';
  fileEl.addEventListener('change', e => { const f = e.target.files && e.target.files[0]; e.target.value = ''; if (f) acceptBlob(f, '選擇檔案'); });
  doc.body.appendChild(fileEl); return fileEl;
}

/* ---- 頁面接口 ---- */
function mount(page) {
  pg = page;
  const h2 = $('styleH2'); if (h2) h2Cards = h2.innerHTML;
  doc.addEventListener('click', e => {
    const b = e.target.closest ? e.target.closest('[data-af]') : null; if (!b) return;
    if (pg.busy() && ['openStyle', 'newMine', 'devGen', 'pick', 'fit'].includes(b.dataset.af)) return;   // 生圖／壓平中：同款式卡片的忙碌態
    e.preventDefault(); act(b.dataset.af, b);
  });
  doc.addEventListener('keydown', e => {
    if ((e.key === 'Enter' || e.key === ' ') && e.target && e.target.dataset && e.target.dataset.af && e.target.classList.contains('scard')) { e.preventDefault(); e.target.click(); }
  });
  doc.addEventListener('change', e => {
    if (S.view !== 'edit' || !S.edit) return;
    if (e.target.id === 'afEdBase') {
      readEdit(); const bs = lib().styles.find(s => s.id === e.target.value);
      if (bs) { S.edit.base = bs.id; S.edit.tones = bs.tones; S.edit.desc = templateOf(bs, L()) || ''; if (/^我的/.test(S.edit.name)) S.edit.name = '我的' + bs.name; }
      render();
    } else if (e.target.id === 'afEdTones') { readEdit(); render(); }
  });
  const pad = $('afPad');
  if (pad) {
    pad.addEventListener('dragover', e => { const z = e.target.closest && e.target.closest('[data-afdrop]'); if (z) { e.preventDefault(); z.classList.add('over'); } });
    pad.addEventListener('dragleave', e => { const z = e.target.closest && e.target.closest('[data-afdrop]'); if (z) z.classList.remove('over'); });
    pad.addEventListener('drop', e => { const z = e.target.closest && e.target.closest('[data-afdrop]'); if (!z) return; e.preventDefault(); z.classList.remove('over');
      const f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]; if (f) acceptBlob(f, '拖放'); });
  }
  root.addEventListener('blur', () => { if (S.view === 'ai' && S.copied && S.step < 4) S.away = true; });
  root.addEventListener('focus', () => { if (S.away && S.view === 'ai' && S.step < 4) { S.away = false; S.back = true; S.step = 3; render(); } });
  loadMine();
}
/* 宿主送來機型能力：介面語言（提示詞跟著走）、各機上限（換到這台就收）。舊宿主沒有這兩欄＝照舊。 */
function onCapability(cap) {
  const lang = cap && typeof cap.uiLang === 'string' ? cap.uiLang : '';
  if (!pg) { S.uiLang = lang; return; }   // 還沒 mount（宿主回得比頁面啟動快）：先記語言
  if (lang !== S.uiLang) {
    const before = S.uiLang ? langInfo(S.uiLang, af()).prompt : null;
    S.uiLang = lang;
    if (before && before !== langInfo(lang, af()).prompt && S.copied) { S.copied = false; S.copiedAt = null; if (S.step === 2) S.step = 1; S.copiedWhat = ''; toast('提示詞改成' + (L() === 'zh' ? '中文' : 'English') + '（跟著介面語言）；要重新複製'); }
  }
  capRecheck('換到這台，');
  render();
}
/* 換了一張照片：上一張的 AI 圖全部作廢（C++ 那邊換照片時一樣清掉 origin／AI 圖） */
function onPhoto(bmp) {
  S.photo = bmp || null; S.photoLum = null; S.results = []; S.cur = -1; S.levelsBefore = null; S.fit = 'follow'; S.host = null; S.stat = '';
  S.copied = false; S.copiedAt = null; S.back = false; S.recopied = false; S.copiedWhat = ''; S.pasteErr = ''; S.hostErr = '';
  if (S.step === 4 || S.step > 1) S.step = 1;
  render();
}
/* 本地款式重新壓了原照片＝來源不再是 AI 圖 */
function onLocalSource() { S.cur = -1; S.host = null; if (S.step === 4) S.step = S.copied ? 3 : 1; S.levelsBefore = null; }
/* 壓平／生圖的進度與結果（頁面 ptStyleStat 同步過來）：結果畫面只顯示「進行中」與「⚠」那兩種 */
function stat(text) { S.stat = text || ''; const el = $('afStat'); if (el) el.innerHTML = statHtml(); }
function statHtml() { return (/^⚠/.test(S.stat) || /中…/.test(S.stat)) ? S.stat : ''; }
/* Ctrl+V：AI 產圖畫面＝貼回 AI 圖；其他時候交回頁面（換新照片）。回傳 true＝收下了 */
function onPaste(e) {
  if (!pg || S.view !== 'ai') return false;
  e.preventDefault();
  const dt = e.clipboardData; if (!dt) return true;
  const f = [...dt.items].find(it => it.kind === 'file' && /^image\//.test(it.type));
  if (f) { acceptBlob(f.getAsFile(), '貼上'); return true; }
  S.pasteErr = dt.types.includes('text/plain') ? 'text' : 'empty'; render(); return true;
}
function onDropFile(f) { if (!pg || S.view !== 'ai' || !f) return false; acceptBlob(f, '拖放'); return true; }
/* 金鑰直連（開發者模式）回來的圖：C++ 已經換好來源 ⇒ 進清單、走同一套檢查，不再送一次 */
function onKeyResult(bmp, blob, st) { if (!pg) return; addResult(bmp, null, '金鑰直連', st || curStyle(), true); }
/* 款式清單最後：我的款式＋「＋ 自己做一款」（同料數；每個題材都看得到——原型同） */
function extraCardsHtml(mode, selId) {
  if (!pg) return '';
  const by = id => lib().styles.find(s => s.id === id);
  return S.mine.filter(s => s.mode === mode).map(s => { const base = by(s.base);
    return '<div class="scard' + (selId === s.id ? ' sel' : '') + '" data-af="openStyle" data-s="' + esc(s.id) + '" tabindex="0">'
      + '<div><span class="nm">' + esc(s.name) + '</span><span class="tag mine">我的</span></div>'
      + '<div class="meta">' + s.tones + ' 色階　·　' + (s.mode === 'quad' ? '四料' : '雙料') + '<br>自己做的（從「' + esc(base ? base.name : '—') + '」改）；每個題材都看得到</div>'
      + '<div class="pill">點一下開始</div></div>'; }).join('')
    + '<div class="scard add" data-af="newMine" tabindex="0"><div><span class="nm">＋ 自己做一款</span></div>'
    + '<div class="meta">從現有款式複製一份，改名字、色階數、畫風描述；存在這台電腦</div></div>';
}
function titleHtml() {
  return cur() ? '<span class="afSrcT">來源（AI 產的圖・第 ' + (S.cur + 1) + ' 張）</span><button class="mfLnk afBack" type="button" data-af="backToPhoto">回到原圖</button>' : '來源（AI 生成）';
}

return Object.assign({}, core, {
  mount, render, refresh: () => { if (S.view === 'ai') render(); }, open, onCapability, onPhoto, onLocalSource, onPaste, onDropFile, onKeyResult,
  onImported, onOrigin, onMineLoaded, onMineSaved, extraCardsHtml, titleHtml, mineById, stat,
  capFit, capInfo, capRecheck, maxWidthNow, promptFor, apiSize, view: () => S.view, state: () => S,
});
});
