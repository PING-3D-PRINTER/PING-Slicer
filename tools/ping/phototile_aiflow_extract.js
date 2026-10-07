/* 照片磚 AIP 刀 4：從原型 v4 用程式抽出「複製提示詞」流程的文字與算式 → tools/ping/phototile_aiflow_fixture.json
 *
 * 為什麼要用程式抽、不准手打（計畫頁 §03 刀 4、§06 第 4 列）：提示詞的英文是 Eric 驗過的正本、中文是對照翻譯，
 * 手打一個字不一樣，AI 拿到的就是另一句話，而畫面不會報錯。這支直接在 node 的 vm 裡**執行原型自己的程式碼**
 * （照行號範圍取出、每段開頭用錨點字串核對、整檔用 sha256 釘住），用佔位值換出樣板、用真值換出樣本：
 *   lib      ＝要寫進款式庫（款式庫_照片磚.json）的那幾段（tools/ping/phototile_aiflow_libsync.py 寫進去）
 *   samples  ＝原型在各種輸入下真的組出來的提示詞／換個說法／形狀／比例字樣（phototile_aiflow_test.js 拿產品的組法逐字比）
 *   formulas ＝原型各機上限算式的原始碼（測試在 vm 裡直接跑它，跟產品 aiflow.js 的算式逐點比；不重寫一份）
 *
 * 用法：node tools/ping/phototile_aiflow_extract.js [原型範本路徑]     （預設 D:/_sa/aip/proto4_template.html）
 * 原型範本＝D:/_sa/aip/proto4_template.html（圖片是佔位字）；治理端那份＝根 repo「20260604 ORCA客製/原型_照片磚AI產圖複製提示詞_v4_20260926.html」
 * （同一份程式碼、嵌了圖、LF 換行）。sha256 對不上就停——原型改了要重新看過再改這支的 EXPECTED_SHA256。
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');

const PROTO = process.argv[2] || 'D:/_sa/aip/proto4_template.html';
const EXPECTED_SHA256 = 'f658e1f5c41fd695d2851e75e862f69e7c885d9751b2f682bb6d7b0e4f935e92';
const OUT = path.join(__dirname, 'phototile_aiflow_fixture.json');

/* 行號範圍（1 起算、含頭尾）＋每段第一行的錨點。原型是 CRLF，行號照檔案。 */
const RANGES = [
  ['data',   353, 421, 'const MACHINES = {'],          // MACHINES／CAP／MODELS／LAYER／TOWER／常數／UI_LANGS／SUBJECTS／STYLES／TONE_RULES／CUSTOM_RULES
  ['state',  431, 444, 'const S = {'],
  ['styles', 449, 450, 'function styleById(id)'],
  ['color',  453, 469, 'const s2l = c =>'],
  ['caps',   517, 576, 'function M(){'],              // M … enforceCaps、noz／layerH／xMin／zMin
  ['grid',   588, 593, 'const GRID_LONG_MAX'],
  ['prompt', 680, 727, 'const pLang = () =>'],         // pLang … buildPrompt、FIX、fixText
  ['ratio',  947, 948, 'function ratioTxt(a)'],
];
/* 各機上限的算式（測試在 vm 裡跑原型自己的這一段）：MODELS～常數＋M～widestAtTop */
const FORMULA_RANGES = [[362, 375, 'const MODELS = {'], [517, 553, 'function M(){']];

function die(msg) { console.error('FAIL ' + msg); process.exit(1); }

const raw = fs.readFileSync(PROTO);
const sha = crypto.createHash('sha256').update(raw).digest('hex');
if (sha !== EXPECTED_SHA256) die('原型 sha256 對不上：' + sha + '（預期 ' + EXPECTED_SHA256 + '）——原型改過了，先重新看過再改 EXPECTED_SHA256');
const lines = raw.toString('utf8').split('\n').map(s => s.replace(/\r$/, ''));
const take = (a, b, anchor) => {
  if (!lines[a - 1].startsWith(anchor)) die('第 ' + a + ' 行不是「' + anchor + '」開頭：' + lines[a - 1].slice(0, 60));
  return lines.slice(a - 1, b).join('\n');
};
const code = RANGES.map(([, a, b, anc]) => take(a, b, anc)).join('\n');
const formulaSrc = FORMULA_RANGES.map(([a, b, anc]) => take(a, b, anc)).join('\n');

const EXPOSE = '\nglobalThis.__P = { S, MACHINES, MODELS, LAYER, TOWER, UI_LANGS, SUBJECTS, STYLES, TONE_RULES, CUSTOM_RULES, DETAIL, FIX, fill, fixText, pLang, mats, CAP, MINW_FACTOR, BASE_THICK, THICK_RATIO, THICK_MAX, W_MARGIN, SIZE_MIN };\n';
function freshContext() {
  const ctx = { console };
  vm.createContext(ctx);
  vm.runInContext(code + EXPOSE, ctx, { filename: 'proto4_extract.js' });
  return ctx;
}

/* ---------------- 樣本：原型照它自己的程式組出來的真值 ---------------- */
const real = freshContext();
const P = real.__P;
const MODEL_OF = { dual: 'dual', quad: 'quad' };     // dual＝FD300、quad＝FF600（原型 MODELS 的鍵）
function setup(ctx, o) {
  const S = ctx.__P.S;
  Object.assign(S, { uiLang: o.uiLang, photoAspect: o.aspect, tileW: o.tileW, nozzle: o.nozzle, noiseMm: o.noiseMm,
    zMode: o.zMode || 'one', machine: o.mode, model: MODEL_OF[o.mode], style: o.style || null, my: o.my || [] });
}
const AI_STYLES = P.STYLES.filter(s => s.ai);
const prompts = [];
const BASE = { aspect: 1.5, tileW: 100, nozzle: '0.4', noiseMm: 1.0 };
for (const st of AI_STYLES) for (const uiLang of ['zh_TW', 'en']) prompts.push(Object.assign({ style: st.id, mode: st.mode, uiLang }, BASE));
const EXTRA = [
  { uiLang: 'zh_CN', aspect: 1 / 1.5, tileW: 150, nozzle: '0.6', noiseMm: 1.0 },
  { uiLang: 'ja', aspect: 1, tileW: 120, nozzle: '0.4', noiseMm: 2.0 },
  { uiLang: 'en', aspect: 2 / 3, tileW: 60, nozzle: '1.0', noiseMm: 0.5 },
  { uiLang: 'zh_TW', aspect: 0.9, tileW: 333, nozzle: '0.4', noiseMm: 1.0 },
  { uiLang: 'en', aspect: 1.25, tileW: 40, nozzle: '0.4', noiseMm: 1.0 },
];
for (const id of ['silhouette', 'flat_contrast', 'ridge_layers_quad']) {
  const st = AI_STYLES.find(s => s.id === id);
  for (const e of EXTRA) prompts.push(Object.assign({ style: id, mode: st.mode }, e));
}
const MINE_DESC = {
  zh: '把這張照片轉成 {tones} 色調的木刻版畫風海報，線條至少 {minPx} 像素寬。',
  en: 'Turn this photo into a {tones}-tone woodcut poster; lines at least {minPx} pixels wide.',
};
const minePrompts = [
  Object.assign({ mine: { id: 'my-t1', name: '我的木刻', tones: 3, desc: MINE_DESC.zh }, mode: 'dual', uiLang: 'zh_TW' }, BASE),
  Object.assign({ mine: { id: 'my-t2', name: 'my woodcut', tones: 4, desc: MINE_DESC.en }, mode: 'quad', uiLang: 'en' }, BASE, { aspect: 1 / 1.5, tileW: 180 }),
  Object.assign({ mine: { id: 'my-t3', name: '混語', tones: 5, desc: MINE_DESC.en }, mode: 'dual', uiLang: 'zh_CN' }, BASE, { nozzle: '0.6', tileW: 90 }),
];
for (const p of prompts) {
  setup(real, p);
  const st = P.STYLES.find(s => s.id === p.style);
  p.tones = st.tones;
  p.hexes = real.__P.mats().map(m => m.hex);
  p.out = real.buildPrompt(st);
}
for (const p of minePrompts) {
  setup(real, Object.assign({}, p, { my: [p.mine], style: p.mine.id }));
  p.tones = p.mine.tones;
  p.hexes = real.__P.mats().map(m => m.hex);
  p.out = real.buildPrompt(p.mine);
  prompts.push(p);
}
const FIX_KEYS = ['color', 'thin', 'bg', 'like', 'again', 'shape'];
const fixes = [];
for (const [mode, sid] of [['dual', 'silhouette'], ['quad', 'flat_contrast']]) for (const uiLang of ['zh_TW', 'en']) for (const aspect of [1.5, 1]) {
  const o = Object.assign({}, BASE, { mode, uiLang, aspect, style: sid });
  setup(real, o);
  const st = P.STYLES.find(s => s.id === sid);
  for (const k of FIX_KEYS) fixes.push(Object.assign({ key: k, tones: st.tones, hexes: real.__P.mats().map(m => m.hex), minPx: real.minPx(), out: real.__P.fixText(k) }, o));
}
const shapes = [];
for (const uiLang of ['zh_TW', 'en']) for (const aspect of [0.5, 0.8, 1 / 1.2, 0.84, 1, 1.19, 1.2, 1.21, 1.5, 2]) {
  setup(real, Object.assign({}, BASE, { mode: 'dual', uiLang, aspect }));
  shapes.push({ uiLang, aspect, out: real.shape() });
}
const pxs = [];
for (const mode of ['dual', 'quad']) for (const nozzle of ['0.4', '0.6', '1.0']) for (const [tileW, aspect, noiseMm] of [[100, 1.5, 1], [60, 1, 0], [250, 2 / 3, 2.5], [37, 1.25, 1], [580, 1, 1]]) {
  setup(real, { uiLang: 'en', mode, nozzle, tileW, aspect, noiseMm });
  pxs.push({ mode, nozzle, tileW, aspect, noiseMm, xMin: real.xMin(), zMin: real.zMin(), minPx: real.minPx(), minPxZ: real.minPxZ() });
}
const ratios = [0.5, 0.6667, 0.7, 0.75, 0.8, 1, 1.25, 1.3333, 1.5, 1.52, 1.6, 1.7778, 2, 2.5].map(a => ({ a, out: real.ratioTxt(a) }));
const crops = [[1.5, 1], [1, 1.5], [1.7778, 1.3333], [0.75, 1], [1.5, 1.45]].map(([aI, aT]) => ({ aI, aT, out: real.cropPct(aI, aT) }));

/* ---------------- 樣板：同一段程式換佔位值跑出來 ---------------- */
const T = freshContext();
const TP = T.__P;
TP.UI_LANGS.__TZ = { prompt: 'zh', reply: '{reply}' };
TP.UI_LANGS.__TE = { prompt: 'en', reply: '{reply}' };
TP.MACHINES.__T3 = { mats: [{ hex: '{h0}' }, { hex: '{h1}' }, { hex: '{h2}' }] };
TP.MACHINES.__T5 = { mats: [0, 1, 2, 3, 4].map(i => ({ hex: '{h' + i + '}' })) };
T.shape = () => ({ txt: '{shape}', size: '{size}', zh: '{shapeUi}', px: 1024 });
T.minPx = () => '{minPx}';
T.curStyle = () => ({ tones: '{tones}' });
const LANG_KEY = { zh: '__TZ', en: '__TE' };
const HEXES3 = { zh: '{h0}、{h1}、{h2}', en: '{h0}, {h1}, {h2}' };
const HEXES5 = { zh: '{h0}、{h1}、{h2}、{h3}、{h4}', en: '{h0}, {h1}, {h2}, {h3}, {h4}' };
function setLang(L) { TP.S.uiLang = LANG_KEY[L]; TP.S.model = 'dual'; TP.S.nozzle = '0.4'; }
function mustReplace(s, a, b, what) { if (s.split(a).length !== 2) die(what + '：找不到唯一的「' + a + '」'); return s.replace(a, b); }
/* 料色數：3 支跟 5 支各跑一次，比對兩個樣板唯一不同的那一格＝數量 */
function countTemplate(t3, t5, what) {
  let i = 0; while (i < t3.length && t3[i] === t5[i]) i++;
  if (t3[i] !== '3' || t5[i] !== '5' || t3.slice(i + 1) !== t5.slice(i + 1)) die(what + '：3 支與 5 支的差別不只數量那一格');
  return t3.slice(0, i) + '{n}' + t3.slice(i + 1);
}
const lib = { toneRulesZh: TP.TONE_RULES.zh, aiFlow: {}, promptTemplateZh: {}, enCheck: { toneRules: TP.TONE_RULES.en, promptTemplate: {} } };
const af = lib.aiFlow;
af.customRules = { en: TP.CUSTOM_RULES.en, zh: TP.CUSTOM_RULES.zh };
af.replyNames = {};
for (const k of Object.keys(P.UI_LANGS)) af.replyNames[k] = P.UI_LANGS[k].reply;   // 原型只認這 4 種；其他語言的英文名稱在 aiflow.js（REPLY_EN）
af.chatPreface = {};
af.printDetail = {};
af.hexJoin = {};
for (const L of ['zh', 'en']) {
  setLang(L);
  af.chatPreface[L] = T.chatPreface();
  const thin = TP.DETAIL[L]('{x}', 1), band = TP.DETAIL[L]('{x}', '{z}');
  if (thin.includes('{z}') || !band.includes('{z}') || thin === band) die('列印細節兩種寫法分不開（' + L + '）');
  af.printDetail[L] = { thin, band };
  af.hexJoin[L] = HEXES3[L].slice(4, HEXES3[L].indexOf('{h1}'));
  if (HEXES3[L] !== ['{h0}', '{h1}', '{h2}'].join(af.hexJoin[L])) die('料色分隔字推不出來（' + L + '）');
}
setLang('zh');
TP.S.machine = '__T3'; const pz3 = mustReplace(T.paletteLine(), HEXES3.zh, '{hexes}', '中文料色那一行');
TP.S.machine = '__T5'; const pz5 = mustReplace(T.paletteLine(), HEXES5.zh, '{hexes}', '中文料色那一行');
af.paletteLineZh = countTemplate(pz3, pz5, '中文料色那一行');
setLang('en');
TP.S.machine = '__T3'; const pe3 = mustReplace(T.paletteLine(), HEXES3.en, '{hexes}', '英文料色那一行');
TP.S.machine = '__T5'; const pe5 = mustReplace(T.paletteLine(), HEXES5.en, '{hexes}', '英文料色那一行');
const paletteLineEn = countTemplate(pe3, pe5, '英文料色那一行');   // 產品的英文那一行在 matflow.js（aiPaletteLine）；只拿來驗它
af.retry = [];
const fixT = {};
for (const k of FIX_KEYS) {
  const f = TP.FIX[k], o = { key: k, label: f.zh };
  for (const L of ['en', 'zh']) {
    setLang(L); TP.S.machine = '__T3';
    let s = L === 'zh' ? f.zt() : f.en();
    if (s.includes(HEXES3[L])) s = mustReplace(s, HEXES3[L], '{hexes}', 'FIX.' + k);
    o[L] = s;
  }
  fixT[k] = o;
}
const RETRY_KEYS = ['color', 'thin', 'bg', 'like', 'again'];   // 原型結果頁五顆 fchip 的順序（resultHTML）
af.retry = RETRY_KEYS.map(k => fixT[k]);
af.ratioPhrase = fixT.shape;
/* 形狀字樣（提示詞前言的「圖的形狀」與畫面上的「橫式 3:2」）：照原型 shape() 三種情形各跑一次 */
const SH = freshContext();
af.shapes = {};
for (const [name, aspect] of [['landscape', 1.5], ['portrait', 1 / 1.5], ['square', 1]]) {
  const o = {};
  for (const L of ['zh', 'en']) {
    Object.assign(SH.__P.S, { uiLang: L === 'zh' ? 'zh_TW' : 'en', photoAspect: aspect });
    const r = SH.shape();
    o[L] = { txt: r.txt, size: r.size };
    o.ui = r.zh; o.px = r.px;
  }
  af.shapes[name] = o;
}
for (const st of AI_STYLES) {
  lib.promptTemplateZh[st.id] = st.tpl.zh;
  lib.enCheck.promptTemplate[st.id] = st.tpl.en;
}

/* ---------------- 自我核對：樣板換回真值＝原型的真輸出（證明樣板沒有抽歪） ---------------- */
const fillT = (t, m) => Object.keys(m).reduce((s, k) => s.split('{' + k + '}').join(String(m[k])), t);
for (const p of prompts) {
  const L = real.__P.UI_LANGS[p.uiLang].prompt;
  if (!p.out.includes(fillT(lib.aiFlow.paletteLineZh, { n: p.hexes.length, hexes: p.hexes.join('、') })) && L === 'zh') die('中文料色樣板換回去對不上原型輸出（' + (p.style || p.mine.id) + '）');
  if (L === 'en' && !p.out.endsWith(fillT(paletteLineEn, { n: p.hexes.length, hexes: p.hexes.join(', ') }))) die('英文料色樣板換回去對不上原型輸出');
}
for (const f of fixes) {
  const L = real.__P.UI_LANGS[f.uiLang].prompt;
  setup(real, f);
  const want = fillT(fixT[f.key][L], { hexes: f.hexes.join(af.hexJoin[L]), tones: f.tones, minPx: f.minPx, shape: real.shape().txt, size: real.shape().size });
  if (want !== f.out) die('換個說法樣板換回去對不上原型輸出：' + f.key + '／' + f.uiLang);
}

const fixture = {
  _說明: '自動生成，不要手改。照片磚 AIP 刀 4：原型 v4 用程式抽出來的提示詞文字（lib，寫進款式庫）、原型真的組出來的樣本（samples）、各機上限算式的原始碼（formulas）。重產＝node tools/ping/phototile_aiflow_extract.js；寫進款式庫＝python tools/ping/phototile_aiflow_libsync.py。',
  source: {
    template: PROTO.replace(/\\/g, '/'),
    sha256: sha,
    bytes: raw.length,
    lines: lines.length - (lines[lines.length - 1] === '' ? 1 : 0),
    governanceCopy: '根 repo 20260604 ORCA客製/原型_照片磚AI產圖複製提示詞_v4_20260926.html（同一份程式碼，嵌了圖、LF 換行）',
    ranges: RANGES.map(([name, a, b]) => ({ name, from: a, to: b })),
  },
  lib,
  paletteLineEn,
  samples: { prompts, fixes, shapes, pxs, ratios, crops },
  formulas: { ranges: FORMULA_RANGES.map(([a, b]) => ({ from: a, to: b })), src: formulaSrc },
};
const text = JSON.stringify(fixture, null, 1).replace(/\n/g, '\r\n') + '\r\n';
const tmp = OUT + '.tmp';
fs.writeFileSync(tmp, text, 'utf8');
fs.renameSync(tmp, OUT);
console.log('原型 ' + PROTO + '  sha256=' + sha + '  ' + raw.length + ' B');
console.log('OK 抽出：' + AI_STYLES.length + ' 款中文提示詞、前言／列印細節／料色（中）／我的款式規則各兩版、換個說法 ' + af.retry.length + ' 句＋改比例 1 句；樣本 提示詞 ' + prompts.length + '、換個說法 ' + fixes.length + '、形狀 ' + shapes.length + '、像素 ' + pxs.length);
console.log('寫入 ' + OUT + '（' + Buffer.byteLength(text) + ' B）');
