/* 照片磚款式庫：頁面讀的 stylelib.js 與同目錄的 款式庫_照片磚.json 必須一致（2026-09-16，牌 c-0916-PTI-03）
 *
 * 為什麼要這支：在這之前 `款式庫_照片磚.json` 的註解自稱「資料正本」，**但執行期根本沒人讀它**，
 * 而且它停在 6 個款式、比真正被讀的內嵌版少了 0908 補的三個四料款式。
 * 掛名正本 × 沒人讀 × 不同步 ＝ 下一個看它的人會照著一份過期的東西做決定。
 * 這支把「兩份必須一致」變成跑得起來的斷言，而不是註解裡的一句話。
 * 2026-10-06（牌 c-1006-AIP-01）：內嵌的 `<script id="ptStyleLib">` 搬成 stylelib.js（由 JSON 產生，
 * 重產＝verify_phototile_stylelib.py --sync）；本支改成用 vm 真的執行 stylelib.js，跟瀏覽器讀到的是同一份。
 *
 * 另驗 constants.toneRules（Eric 2026-09-16 裁「寫進去」）存在且被頁面實際接上——
 * 它有兩個消費端（頁面、照片磚管線/pipeline.py），少接一邊就會漂回卡通風。
 * 2026-10-06（AIP 刀 4，牌 c-1006-AIP-02）：頁面這端的組法搬進 aiflow.js 的 buildPrompt（客戶複製的與金鑰直連同一份），
 * ⇒ 「接上」改成：aiflow.js 讀 constants.toneRules／toneRulesZh，而且 index.html 交給它的款式庫就是宣告的那個識別字。
 */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const web = path.join(__dirname, '..', '..', 'resources', 'web', 'phototile');
const html = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
const libJs = fs.readFileSync(path.join(web, 'stylelib.js'), 'utf8');
const mirrorRaw = fs.readFileSync(path.join(web, '款式庫_照片磚.json'), 'utf8');

// 識別字從 stylelib.js 推出來（不寫死，見下方 0916 教訓）
const libDecl = libJs.match(/^var\s+([A-Za-z_$][\w$]*)\s*=/m);
if (!libDecl) { console.error('FAIL stylelib.js 沒有 var <名字>= 宣告'); process.exit(1); }
const LIB_IDENT = libDecl[1];
const sandbox = {};
try { vm.runInNewContext(libJs, sandbox); } catch (e) { console.error('FAIL stylelib.js 執行失敗：' + e.message); process.exit(1); }

let inline, mirror;
inline = sandbox[LIB_IDENT];
if (!inline || typeof inline !== 'object') { console.error('FAIL stylelib.js 執行後 ' + LIB_IDENT + ' 不是物件'); process.exit(1); }
try { mirror = JSON.parse(mirrorRaw); } catch (e) { console.error('FAIL 鏡像檔不是合法 JSON：' + e.message); process.exit(1); }

const fails = [];
if (/id="ptStyleLib"/.test(html)) fails.push('index.html 還留著舊的內嵌款式庫 <script id="ptStyleLib">（兩份會漂）');
const loadAt = html.search(/<script src="stylelib\.js(\?v=[\w.-]+)?"><\/script>/);
if (loadAt < 0) fails.push('index.html 沒有載入 stylelib.js');
else if (loadAt > html.search(/<script>\s*\r?\n/)) fails.push('stylelib.js 要在主程式（第一個內嵌 <script>）之前載入');
// 比內容不比格式（鏡像檔是給人讀的 indent 版，內嵌是壓縮版）
const norm = (o) => JSON.stringify(o, Object.keys(o).sort ? undefined : undefined);
const a = JSON.stringify(inline), b = JSON.stringify(mirror);
if (a !== b) {
  const ai = (inline.styles || []).map(s => s.id).sort();
  const bi = (mirror.styles || []).map(s => s.id).sort();
  fails.push('兩份款式庫內容不一致：內嵌 ' + ai.length + ' 款 [' + ai.join(',') + ']；鏡像 ' + bi.length + ' 款 [' + bi.join(',') + ']');
  const only = ai.filter(x => !bi.includes(x)).concat(bi.filter(x => !ai.includes(x)));
  if (only.length) fails.push('  只存在於其中一邊的款式：' + only.join(','));
}

const tr = inline.constants && inline.constants.toneRules;
if (!tr || tr.length < 200) fails.push('constants.toneRules 不存在或太短（調性鐵則是 Eric 2026-09-16 裁定要寫進款式庫的）');
if (tr && !/FORBIDDEN: cute/.test(tr)) fails.push('constants.toneRules 少了「⛔ 不可愛不卡通」那條');
// 🔴 這裡一定要對「頁面實際宣告的那個識別字」比對，不能自己假設名字。
// 2026-09-16 實錯：第一版寫死 PT_STYLE_LIB，而頁面宣告的是 STYLE_LIB ⇒ 產品端一生圖就 ReferenceError，
// 而這支守衛照樣印綠燈。**守衛檢查的字串必須從程式碼推出來，不是憑印象打的。**
const decl = html.match(new RegExp('const\\s+([A-Za-z_$][\\w$]*)\\s*=\\s*' + LIB_IDENT.replace(/\$/g, '\\$') + '\\s*;'));
if (!decl) {
  fails.push('找不到 index.html 裡接 stylelib.js（' + LIB_IDENT + '）的那個 const 宣告');
} else {
  const ident = decl[1];
  const af = fs.readFileSync(path.join(web, 'aiflow.js'), 'utf8');
  if (!/C\.toneRulesZh : C\.toneRules\)/.test(af) || !/const C = o\.lib\.constants/.test(af))
    fails.push('aiflow.js 的 buildPrompt 沒有把 toneRules（中／英）接上去');
  if (!new RegExp('lib:\\(\\)=>' + ident.replace(/\$/g, '\\$') + ',').test(html))
    fails.push('index.html 交給 aiflow.js 的款式庫不是宣告的 ' + ident + '（接錯識別字＝產品一產生提示詞就 ReferenceError）');
  const wrong = html.match(/\b([A-Za-z_$][\w$]*)\.constants\.toneRules/g) || [];
  const bad = wrong.filter(w => !w.startsWith(ident + '.'));
  if (bad.length) fails.push('有地方用了不存在的識別字取 toneRules：' + [...new Set(bad)].join(',') + '（宣告的是 ' + ident + '）');
}

// #181 甲案（Eric 2026-09-23 裁 Q1「照建議」＝車 3 一起做，牌 c-0923-ACC-23）：絹印撞色不得再叫 AI 把背景畫成全圖最暗
// ——乙8 實錄：背景最暗、頭髮也暗 ⇒ 壓平時頭髮併進背景。改成與其他三款四料（0908）同一條修法。
const sp = (inline.styles || []).find(s => s.id === 'screenprint');
if (!sp) fails.push('找不到 screenprint 款式');
else {
  if (/darkest extreme/.test(sp.promptTemplate)) fails.push('screenprint 還在叫 AI 把背景畫成全圖最暗（#181 甲案；乙8 頭髮併進背景的上游）');
  if (!/clearly different from every tone touching/.test(sp.promptTemplate)) fails.push('screenprint 沒改成與其他三款四料同一條修法（背景要與人物輪廓上的每一色都不同）');
}

if (fails.length) { fails.forEach(f => console.error('FAIL ' + f)); process.exit(1); }
console.log('OK 款式庫兩份一致（' + inline.styles.length + ' 款）、toneRules 已入庫且已接上消費端、screenprint 背景修法（#181 甲案）在');
