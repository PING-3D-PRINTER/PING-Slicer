/* =====================================================================
   PhotoTileEngine — 照片磚 C 案引擎（C-1 產品版，2026-07-31；照片磚線 PT）
   generate(request) → Promise<result>：把工作室頁（index.html）的生成鏈抽成
   「脫離 DOM 狀態」的引擎 API。所有數學／演算法逐行搬自 index.html（行號註記
   ＝搬運出處；為黃金比對，語意必須逐位元一致），只做三類改寫：
     ① params/slots/img/lastExport 全域 → request 欄位與區域變數
     ② DOM 回寫（statline/metric/canvas 畫面）→ result.diagnostics
     ③ 錯誤 → 具代碼的 EngineError（result.error）
   依賴：PhotoTileMesh（mesh_union.js，本就是 UMD 純函式模組）。
   執行環境需求：OffscreenCanvas、createImageBitmap、CompressionStream、performance
   —— 皆為 Worker 可用 API；頁面／Worker／WebView2 隱形宿主皆可跑。

   ⚠ 零計時器紀律（C-0 §3.2 紅字發現）：隱形頁面的 setTimeout 被節流到 ~1s/次。
   本檔生成路徑**不得**出現 setTimeout／setInterval／requestAnimationFrame；
   階段讓步一律走 MessageChannel macrotask（實測免疫節流）。

   C-1 相對 C-0 spike 的四項增補（演算法零變更＝黃金比對仍須 6/6 全等）：
     ① progress 回報（階段權重＋量化熱迴圈分塊回報；合法上限案 quad K8 ≈20 秒【K 上限 8＝Eric 0802 裁 B】）
     ② limits 可降階（gridMax／maxDecodedPixels＝OOM gate 與低規機保護）
     ③ metadata／原圖嵌入＝**opt-in**（request.metadata 缺席時 3MF 位元組與工作室全等，
        黃金 oracle 因此得以持續有效——這是刻意的設計約束，勿改成預設開）
     ④ env 環境快照原封回傳（C++ 端比對過期即棄）
   ===================================================================== */
(function (root, factory) {
  const api = factory(root);
  root.PhotoTileEngine = api;
  if (typeof module === "object" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function (root) {
"use strict";

/* ================= 色彩數學（index.html:207-223 verbatim） ================= */
const GAM = 2.2;
const s2l = c => Math.pow(c/255, GAM);
const l2s = c => Math.round(255*Math.pow(Math.max(0,Math.min(1,c)), 1/GAM));
function hexRgb(h){ h=h.replace('#',''); return [parseInt(h.slice(0,2),16),parseInt(h.slice(2,4),16),parseInt(h.slice(4,6),16)]; }
function hexLin(h){ return hexRgb(h).map(s2l); }
function rgbHex(r,g,b){ return '#'+[r,g,b].map(v=>v.toString(16).padStart(2,'0')).join(''); }
function lin2lab(r,g,b){
  let x = r*0.4124 + g*0.3576 + b*0.1805;
  let y = r*0.2126 + g*0.7152 + b*0.0722;
  let z = r*0.0193 + g*0.1192 + b*0.9505;
  x/=0.95047; z/=1.08883;
  const f = t => t>0.008856 ? Math.cbrt(t) : (7.787*t + 16/116);
  const fx=f(x), fy=f(y), fz=f(z);
  return [116*fy-16, 500*(fx-fy), 200*(fy-fz)];
}
const lum709 = (r,g,b) => 0.2126*r + 0.7152*g + 0.0722*b;

/* ================= 版本（進 3MF metadata、ready 握手與 goldens 追溯） ================= */
const ENGINE_VERSION = 'C1-20261003';   /* 1003（AIP 刀 1）：濾除改左右寬＋上下高兩條門檻、拿掉小色塊那步 ⇒ 每一張照片磚的標籤都會變。
   1004（AIP 刀 1 只留甲）：只拿掉開發用切換 devVariant 與乙那條路，預設路徑（甲）逐格不變 ⇒ 版本不動。
   1001（#50）：窄段改併進顏色最近的鄰段（雙料序號差、四料調色盤色差）⇒ 標籤會變；
   日後有人回報缺角，看 3MF metadata 的 engine 欄就分得出新舊判準。0914：色彩校正（calib）住進引擎。 */

/* ================= 常數（index.html:234-241, 619-620） ================= */
const AUTO_CELL_MM = 0.05;   // 內部自動高精度格點
const GRID_MAX     = 3200;   // 長邊格數上限
const SIZE_MIN_MM  = 20, SIZE_MAX_MM = 800;   /* 2026-09-02 由 400 放寬到 800（Eric 令）。
   400 是舊的軟上限、旁邊沒寫理由；實查機台才是真限制：FD300 可印高 300／FF600 580／FF800 600，
   盤面直徑約 300／600／800。放寬只是不再由我們先擋——放不下時切片器自己會擋，而那是有聲的。
   ⚠ 磚越大越清楚是真的（雜訊濾除是固定 mm、格點上限 3200 不變），但零件數與時間也跟著長。 */
const NOZZLES      = [0.4, 0.6, 1.0];

/* ================= 錯誤分類 ================= */
const ERR = {
  BAD_REQUEST:        'bad_request',          // 參數不合法（訊息含欄位）
  BAD_IMAGE:          'bad_image',            // 影像解碼失敗
  IMAGE_TOO_LARGE:    'image_too_large',      // 解碼後像素數超過 limits.maxDecodedPixels（C-1 OOM gate）
  MESH_MODULE_MISSING:'mesh_module_missing',  // mesh_union.js 未載入
  CANCELLED:          'cancelled',            // 呼叫端取消
  INTERNAL:           'internal'              // 其餘（附原始訊息）
};
class EngineError extends Error {
  constructor(code, message){ super(message); this.code = code; this.name = 'EngineError'; }
}

/* ================= 請求正規化 =================
   夾值範圍照工作室 UI 的 min/max（index.html:140-186 的 input 屬性與 onchange 夾值），
   超界＝夾住並記進 diagnostics.clamped（不是報錯——與工作室輸入框行為一致）。 */
function normalizeRequest(req){
  if (!req || typeof req !== 'object') throw new EngineError(ERR.BAD_REQUEST, 'request 必須是物件');
  const clamped = [];
  const clamp = (name, v, lo, hi, dflt) => {
    let x = Number(v);
    if (!Number.isFinite(x)) x = dflt;
    const y = Math.max(lo, Math.min(hi, x));
    if (y !== Number(v)) clamped.push(`${name}:${v}→${y}`);
    return y;
  };
  const mode = req.mode === 'quad' ? 'quad' : req.mode === 'dual' ? 'dual' : null;
  if (!mode) throw new EngineError(ERR.BAD_REQUEST, `mode 必須是 dual|quad（收到 ${req.mode}）`);
  let nozzle = Number(req.nozzle);
  if (!NOZZLES.includes(nozzle)) throw new EngineError(ERR.BAD_REQUEST, `nozzle 必須是 0.4|0.6|1.0（收到 ${req.nozzle}）`);
  const size = req.size || {};
  const P = {
    jobId:   String(req.jobId || ('job-' + (++jobSeq))),
    mode, nozzle,
    width:  clamp('width',  size.widthMm,  SIZE_MIN_MM, SIZE_MAX_MM, 100),
    height: clamp('height', size.heightMm, SIZE_MIN_MM, SIZE_MAX_MM, 75),
    thick:  clamp('thick',  size.thickMm,  2, 30, 10),                       // index.html:1014-1016
    /* 上限 48→8＝Eric 2026-08-02 裁 B。依據：①他實測雙料 8 階已偏多、6 階足夠
       ②K 掃描實證最終調色盤有 64 色上限，K≥11 之後色數完全不變、只是白燒 quantize
       （K48 比 K12 慢 3.3 倍、零色數收益；ksweep_result_20260802.json）。
       同值另住 index.html 的輸入框 max 與 clamp——改要一起改。 */
    klevels: Math.round(clamp('klevels', req.klevels, 2, 8, 8)),
    noiseMm: clamp('noiseMm', req.noiseMm, 0, 20, 1.0),                     // index.html:1017-1025
    pillar:  req.pillar ? !!req.pillar.enabled : true,
    pillarXY: Math.round(clamp('pillarXY', req.pillar && req.pillar.xyMm, 5, 60, 20)), // 同值住 index.html 的 params.pillarXY（2026-08-22 Eric 令 25→15）
    /* WT 線 2026-09-08：每層循環洗料塔（設計提案/照片磚循環洗料塔_20260908_01a07fef）。缺席＝關（3MF 與舊輸出位元組全等）；
       開啟＝不再產舊固定比洗料柱（Eric 裁 3），3MF 物件層寫 ping_pt_cycle* 六鍵給切片器（PrintObjectConfig）。
       laps 兩種寫法（都由**內**往外，與 C++ parse_stages 同語法）：舊格式純數字串 "1,3"｜"1,1,1,2"＝每段一支純料；
       新格式 "<料路集合>:<圈數>"＝一段可多支料同時擠，四料預設 "E123:2,E0:4"（Eric 2026-09-10，牌 c-0910-WT-10）。
       壞格式或空＝回該模式預設。真正的 fail loud 在切片器端（C++），這裡只做防呆。 */
    cycle: (function(c){
      if (!c || !c.enabled) return { enabled:false };
      // 雙料 "1,3"＝E1 1 圈、E0 3 圈（E0 2→3＝Eric 2026-09-09 實印裁「出塔不夠白」；本行 0910 前是 [1,2]＝沒跟上 index.html，已對齊）
      const def  = mode === 'quad' ? 'E123:8,E0:8' : '1,3';   // 四料 2/4→8/8（Eric 2026-09-10）
      const nch  = mode === 'quad' ? 4 : 2;
      const laps = (function(s){
        const items = String(s || '').split(/[,;]+/).map(x=>x.trim()).filter(Boolean);
        if (!items.length) return def;
        if (items.every(x=>/^\d+$/.test(x)))                       // 舊格式：段數＝料路數、每段 ≥1 圈
          return (items.length === nch && items.every(x=>+x >= 1)) ? items.join(',') : def;
        const segs = items.map(x=>/^E([0-9]+):(\d+)$/i.exec(x));   // 新格式：E<路號串>:<圈數>
        if (segs.some(m=>!m)) return def;
        if (segs.some(m=>+m[2] < 1 || new Set(m[1]).size !== m[1].length || [...m[1]].some(d=>+d >= nch))) return def;
        if (segs[segs.length-1][1] !== '0') return def;            // 最外段必須純 E0（同 C++ 判準）
        return segs.map(m=>`E${m[1]}:${m[2]}`).join(',');
      })(c.laps);
      // sizeMm 預設 25（Eric 2026-09-08「固定 25」；缺席／0 都給 25，不再回退成「讓切片器自動等比放大」）
      const _size = Number(c.sizeMm) > 0 ? Number(c.sizeMm) : 25;
      return { enabled:true, laps, sizeMm: _size,
               gapMm: clamp('cycle.gapMm', c.gapMm, 0, 100, 15), brimMm: clamp('cycle.brimMm', c.brimMm, 0, 30, 8) };
    })(req.cycle),
    teeth:   !!(req.seam && req.seam.teeth),
    /* p2aBlock 預設 true（Eric 2026-08-22 裁「預設開」，同值住 index.html:333）。
       引擎的預設一律跟著工作室走——否則「seam 欄缺席＝與工作室輸出位元組全等」這條契約會破：
       沒帶 seam 的宿主會拿到沒 blocker 的磚，縫又回到正／背面。teeth 同理（工作室預設 false）。 */
    p2aBlock:(req.seam && req.seam.p2aBlock !== undefined) ? !!req.seam.p2aBlock : true,
    slots:   Array.isArray(req.slots) && req.slots.length ? req.slots.map(s=>({...s})) : null, // null＝自動建議
    image:   req.image,
    /* C-1：低規機保護。gridMax 預設＝工作室現值 3200（不填＝與工作室全等）；
       maxDecodedPixels 預設 0＝不設限（C-0 §4.3 列的硬化項，由宿主依可用記憶體帶入）。 */
    limits: {
      gridMax: Math.round(clamp('limits.gridMax', (req.limits && req.limits.gridMax) || GRID_MAX, 256, GRID_MAX, GRID_MAX)),
      maxDecodedPixels: Math.max(0, Number((req.limits && req.limits.maxDecodedPixels) || 0) || 0)
    },
    /* C-1：metadata＝opt-in（缺席＝3MF 與工作室位元組全等，黃金 oracle 續用） */
    metadata: (req.metadata && typeof req.metadata === 'object') ? req.metadata : null,
    /* C-1：環境快照原封回傳，引擎不解讀（C++ 端比對 printer/plate/project revision） */
    env: (req.env && typeof req.env === 'object') ? req.env : null,
    clamped
  };
  if (P.cycle.enabled) P.pillar = false;   // WT：循環塔取代舊固定比洗料柱（Eric 2026-09-08 裁 3）
  const need = mode === 'quad' ? 4 : 2;
  if (P.slots && P.slots.length < need)
    throw new EngineError(ERR.BAD_REQUEST, `${mode} 模式 slots 需 ${need} 支（收到 ${P.slots.length}）`);
  return P;
}

/* ================= 影像來源 → ImageBitmap =================
   三形態：ImageBitmap（呼叫端已解碼）／{mime,base64}（原生橋接形態，對應
   index.html:698-704 finishImage 的 data URL 解碼路）／{width,height,rgba}（測試注入）。 */
async function resolveImage(image, limits){
  let bmp = null, source = null, owned = false;       // owned＝引擎自己解碼的，用完要關
  try {
    if (typeof ImageBitmap !== 'undefined' && image instanceof ImageBitmap) {
      bmp = image;                                    // 工作室路徑：呼叫端已解碼、無原始位元組
    } else if (image && typeof image.base64 === 'string') {
      const mime = image.mime || 'image/png';
      const response = await fetch(`data:${mime};base64,${image.base64}`);
      const blob = await response.blob();
      bmp = await createImageBitmap(blob);
      owned = true;
      source = { mime, base64: image.base64, byteLength: blob.size, name: image.name || null };
    } else if (image && image.rgba && image.width > 0 && image.height > 0) {
      const data = new ImageData(new Uint8ClampedArray(image.rgba), image.width, image.height);
      bmp = await createImageBitmap(data);
      owned = true;
    }
  } catch (e) {
    throw new EngineError(ERR.BAD_IMAGE, '影像解碼失敗：' + (e && e.message || e));
  }
  if (!bmp)
    throw new EngineError(ERR.BAD_IMAGE, 'image 需為 ImageBitmap / {mime,base64} / {width,height,rgba}');
  /* C-1 OOM gate：解碼後像素數上限（C-0 §4.3——現行 C++ 只有 64MB 檔案上限、
     解碼後像素無上限；48M px 源圖 decoded bitmap 才是真記憶體壓力）。 */
  const cap = limits && limits.maxDecodedPixels;
  if (cap > 0 && bmp.width * bmp.height > cap) {
    // 二輪 M15：先存尺寸再 close——close 後 width/height 歸零，錯誤訊息永遠顯示 0×0
    const W = bmp.width, H = bmp.height, got = W * H;
    if (typeof bmp.close === 'function') bmp.close();
    throw new EngineError(ERR.IMAGE_TOO_LARGE,
      `影像 ${W}×${H}＝${(got/1e6).toFixed(1)}M 像素，超過上限 ${(cap/1e6).toFixed(1)}M 像素`);
  }
  return { bitmap: bmp, source, owned };
}

/* ================= 影像 → 格點（index.html:648-674，canvas 畫面與 DOM 文案拿掉） ================= */
function gridDims(widthMm, heightMm, gridMax){
  const cap = gridMax > 0 ? gridMax : GRID_MAX;      // C-1：可降階（預設＝工作室現值 3200）
  let gw = Math.max(2, Math.round(widthMm / AUTO_CELL_MM));
  let gh = Math.max(2, Math.round(heightMm / AUTO_CELL_MM));
  const long = Math.max(gw, gh);
  if (long > cap) { const s = cap/long; gw = Math.max(2, Math.round(gw*s)); gh = Math.max(2, Math.round(gh*s)); }
  return { gw, gh };
}
function buildGridData(bmp, widthMm, heightMm, gridMax){
  const { gw, gh } = gridDims(widthMm, heightMm, gridMax);
  const off = new OffscreenCanvas(gw, gh), c = off.getContext('2d');
  c.drawImage(bmp, 0, 0, gw, gh);
  const d = c.getImageData(0, 0, gw, gh);
  const n = gw*gh, lab = new Float32Array(n*3), lum = new Float32Array(n);
  for (let p=0; p<n; p++){
    const r=s2l(d.data[p*4]), g=s2l(d.data[p*4+1]), b=s2l(d.data[p*4+2]);
    const L=lin2lab(r,g,b);
    lab[p*3]=L[0]; lab[p*3+1]=L[1]; lab[p*3+2]=L[2];
    lum[p]=Math.min(1,Math.pow(lum709(r,g,b),1/GAM));
  }
  const step=Math.max(1,Math.round(Math.sqrt(n/1500))), tl=[];
  for(let y=0;y<gh;y+=step) for(let x=0;x<gw;x+=step){ const p=y*gw+x; tl.push(lab[p*3],lab[p*3+1],lab[p*3+2]); }
  return { w:gw, h:gh, data:d.data, lab, lum, thumbLab:tl };
}

/* openLabelsMinWidth 已於 2026-08-22 移進 `mesh_union.js`（兩線共用同一份實作）。
   本線原本的私有複本連同三個硬傷一起退役：偶數 kernel 少一格、openedAway 恆為 0（種子數
   在 BFS 之後才算）、全開掉時靜默退回。共用版另補「開完必須再修一次最小寬」的呼叫端契約。 */

/* ================= 濾除（index.html:347-368，DOM 統計改回傳） ================= */
/* #50 甲（Eric 2026-10-01 裁 Q1「色差」）：窄段「併進顏色最近的鄰段」的「最近」——
   四料標籤是調色盤索引（類別、不是順序：序號相鄰的兩色可能是不同色相）⇒ 用調色盤兩色的 Lab 距離；
   雙料標籤是明度色階序號 ⇒ 用序號差（不建表，mesh_union.js 的預設）。
   取平方距離：只比大小，不開根號就不會有捨入造成的假平手。 */
function labToneTable(palette){
  return palette.map(A => Float64Array.from(palette, B =>
    (A.lab[0]-B.lab[0])**2 + (A.lab[1]-B.lab[1])**2 + (A.lab[2]-B.lab[2])**2));
}
/* ================= 照片磚製程的層高（AIP 刀 1；規格 R9-11 第二輪 Q10 甲「上下最少一層」） =================
   ＝resources/profiles/PING/process 裡「同進照片磚」製程檔的 layer_height（雙料＝FD、四料＝FF）。
   引擎自帶一份、不由 C++ 帶——要帶就得改請求格式＝要編譯。tools/ping/phototile_filter_test.js 每次讀那 19 支製程檔重比，
   製程改了層高、這張表沒跟上就轉紅。 */
const PT_LAYER_MM = { dual: { 0.4: 0.2, 0.6: 0.3, 1.0: 0.5 }, quad: { 0.4: 0.25, 0.6: 0.35, 1.0: 0.45 } };
function layerHeightMm(mode, nozzle){ return PT_LAYER_MM[mode === 'quad' ? 'quad' : 'dual'][nozzle]; }
/* mm → 格數一律無條件進位（最小寬／高是硬約束，round 會放行 0.78 mm 的段；2026-08-22）。
   減 1e-9：0.3/0.05 這種剛好整除的值浮點會差一點點，別讓它多進一格。 */
const mmCells = (mm, cell) => Math.max(1, Math.ceil(mm / cell - 1e-9));

/* palette（選填）：四料（strategy 'mode'）有帶 ⇒ 併段用色差表；沒帶或雙料 ⇒ 序號差。
   🔴 #50 丁：工作室模擬圖也呼叫這一支（PhotoTileEngine.filterLabels）＝預覽與 3MF 同一條濾除鏈，
   頁面不得再自己串 smoothLabelNoise／enforceMinHorizontalWidth／enforceMinVerticalHeight／openLabelsMinWidth。

   🔴 AIP 刀 1（2026-10-03；規格 R9-11 第二輪 Q8／Q10／Q11 甲、實作計畫 Q1）：濾除改成左右、上下各一條門檻——
     左右（X）＝max(雜訊濾除欄, 2×口徑)：0.4 口徑 1.0 mm、0.6／1.0 口徑照舊 1.2／2.0（噴嘴一趟來回兩條線）。
     上下（Z）＝一層（照片磚製程的層高）。
     「外框長邊 ≤ 雜訊濾除 mm 的小色塊整塊併掉」（filterSmallComponents）拿掉，由左右寬＋上下高兩條取代（Q8 的理解，照建議成立）。
   P.mode 決定層高（'quad'＝四料，其餘雙料）；頁面呼叫時一定要帶。
   🔴 刀 1 Q1 已裁＝甲（Eric 2026-10-04「照建議」；規格 R9-11「刀 1 Q1 已裁＝甲」、對照頁 00治理文件/對照_照片磚濾除甲乙_20261003.html）：
     全面分方向——平滑、開運算、違規計數的上下方向一律用一層；開完再修＝對齊層（從熱床往上每層一條帶）。
     ⇒ 等於放寬 2026-08-15 P0 開運算在上下方向那一半（0.8 mm → 一層）：比 0.8 mm 薄的橫向細節（鬍鬚、眉眼）會多留下來，
       臉的亮部多幾條一層高的橫紋——這是挑甲時一起同意的代價。
     沒採用的乙（只換小色塊那步、上下照舊 0.8 mm）與它的開發用切換 devVariant 已拿掉（牌 c-1004-AIP-02）。 */
function filterLabels(labels, img, P, paletteSize, strategy, palette){
  const M = root.PhotoTileMesh;
  if (!M) throw new EngineError(ERR.MESH_MODULE_MISSING, '連通網格模組未載入');
  const w=img.w, h=img.h, sx=P.width/w, sz=P.height/h;
  const layerMm = layerHeightMm(P.mode, P.nozzle);
  if (!(layerMm > 0)) throw new EngineError(ERR.BAD_REQUEST, `沒有這個口徑的照片磚層高：${P.mode}／${P.nozzle}`);
  const minWidthMm = Math.max(Number(P.noiseMm) || 0, 2*P.nozzle), minHeightMm = layerMm;
  const minCells = mmCells(minWidthMm, sx), minCellsV = mmCells(minHeightMm, sz);
  const tone=(strategy==='mode' && palette && palette.length) ? labToneTable(palette) : null;
  // ① 平滑（多數決／中位數）：左右視窗＝雜訊濾除欄、上下視窗＝一層
  const smooth = M.smoothLabelNoise(labels,w,h,paletteSize,sx,sz,P.noiseMm,strategy,minHeightMm);
  // ② 左右最小寬 ③ 上下最小高（取代小色塊那步）——都是窄段併進顏色最近的鄰段（#50 判準）
  const wide = M.enforceMinHorizontalWidth(smooth.labels,w,h,minCells,tone);
  const tall = M.enforceMinVerticalHeight(wide.labels,w,h,minCellsV,tone);
  /* ④【2026-08-15・Eric 裁 P0】2-D 開運算，解掉「附著在大塊上的細長突起」；左右＝左右最小寬、上下＝一層（T070 上下是 2×口徑）。
     ⚠ 原本「必須放在 filterSmallComponents 之後」的順序規則（杜賓眼睛 2.1×1.95 mm 卡在雜訊門檻邊緣會被當孤島清掉）
        隨小色塊那步拿掉而失效：②③ 只看寬／高、不看孤不孤立，眼睛遠大於兩條門檻，不會被清。 */
  const opened = M.openLabelsMinWidth(tall.labels,w,h,minCells,minCellsV);
  /* ⑤ 🔴 開完一定要再修：BFS 回填會重新製造短段（實測 0 → 57 段、最短 1 格）。
     degenerate＝門檻對這張圖不合理（一格核心都沒有）⇒ 開運算整步作廢，從開運算前那份再修。
     再修＝對齊層（snapRowsToBands）——從熱床往上每一層一條帶，帶內取多數、整條帶修左右寬 ⇒ 左右、上下同時成立。
       ⚠ 不用「左右、上下輪流修」（計畫頁原寫法）：實測不收斂（中年男 8 階輪 8 輪還剩 540 段上下違規），理由見 mesh_union.js 該函式；
         改對齊層是挑甲時一起同意的。 */
  const base = opened.degenerate ? tall.labels : opened.labels;
  const refix = M.snapRowsToBands(base,w,h,minCellsV,minCells,tone);
  const finalLabels = refix.labels;
  // ⑥ 可印性自我檢查：左右、上下兩個方向都要 0
  const violations = M.countMinWidthViolations(finalLabels,w,h,minCells);
  const violationsV = M.countMinHeightViolations(finalLabels,w,h,minCellsV);
  let changedPixels=0;
  for(let i=0;i<labels.length;i++) if(finalLabels[i]!==labels[i]) changedPixels++;
  const stats={filter:'axis-band', changedPixels,     // 分方向（左右寬＋上下一層）＋對齊層
    smoothedPixels:smooth.changedPixels, changedAreaMm2:changedPixels*sx*sz,
    widthChangedPixels:wide.changedPixels, widthMergedRuns:wide.mergedRuns, minWidthMm,
    heightChangedPixels:tall.changedPixels, heightMergedRuns:tall.mergedRuns, minHeightMm, layerMm,
    /* #50：toneMetric＝「最近」用哪把尺；*ToneFlips＝新判準跟「只比長度」選了不同邊的次數
       （寫進 3MF metadata ⇒ 收到的檔分得出新舊判準） */
    toneMetric: tone ? 'lab' : 'index', widthToneFlips: wide.toneFlips + (refix.toneFlips || 0), heightToneFlips: tall.toneFlips,
    openedAwayPixels:opened.openedAway, openDegenerate:opened.degenerate,
    refixChangedPixels: refix.changedPixels, layerBands: refix.bands || 0,
    minWidthViolations:violations, minHeightViolations:violationsV,
    thresholdMm:Number(P.noiseMm)||0, strategy:smooth.strategy};
  return { labels: finalLabels, stats };
}

/* ================= 四料 48 格校正塊（A 案；R6-2 附款／R6-6）=================
   ＝ 照片磚_色彩校正/make_calib_quad_3mf.py 的同構 JS 版。**離線那支是參考正本**，
   兩邊同參數產出的 3D/3dmodel.model 與 Metadata/model_settings.config 逐位元組相同，
   由 tools/ping/phototile_calib_test.js 用 sha256 釘住（比照雙料釘 43062b6c…191717 的做法）。

   排法＝A 案：**8 階沿 X 排、6 對往 Z 疊** ⇒ 正面 80×60 mm、厚 10 mm、48 格＋洗料柱＝49 支零件。
   （否決的 B 案＝48 格全部沿 Z 疊 ⇒ 48 cm 高，FD300 印不下。記在 R6-6，免得日後有人重提。）
   ⚠ Orca 零件上限 64，49 支在限內。

   🔴 **幾何唯一來源在這裡，頁面不得再寫一份數字**——雙料 v1→v2c 分叉就是因為 index.html 自己寫了一份。
   🔴 **洗料柱高度＝整塊高，不可縮**：同一層要換 8 次比例，柱矮於某層 ⇒ 那層以上沒被洗過、
      量到的是殘料污染色（R6-6 實作細節 2）。
   🔴 **整組置中要含洗料柱**：只用 -width/2 會讓磚置中、柱整根凸在右邊，重心偏到床邊。
   🔴 **厚 20（原 10）**：Eric 2026-09-30「照建議」（開發中清單 #40 Q3 甲）＝四料校正片跟四料照片磚預設厚度同為 20（厚度＝換色清料空間，
      條件不同量到的色就對不上）。python 參考正本 make_calib_quad_3mf.py 同步改、參考 3MF 重產，雜湊釘在 phototile_calib_test.js。
   ================================================================= */
const CALIB_QUAD_GEO={cellMm:10, thickMm:20, levels:8, pillarMm:25, pillarGapMm:15};
function calibQuadGeo(){ return Object.assign({}, CALIB_QUAD_GEO); }
/* 產出的 3MF 內容一律 LF。本檔原始碼是 CRLF ⇒ 用 fromCharCode 講死，免得哪天被編輯器或補丁工具改成 CRLF，
   那會讓與 python 參考正本的逐位元比對整張表轉紅，而原因看起來跟幾何完全無關。 */
const CALIB_QUAD_LF=String.fromCharCode(10);
/* 引擎量化式：w=round((1-s/(k-1))*100)。🔴 這條式子就是 quantizeQuad 產候選時用的那一條，
   兩邊必須逐字相同，否則量了 70% 而引擎永遠只印 71%＝白量（R6-8）。 */
function calibQuadSteps(k){ const o=[]; for(let s=0;s<k;s++) o.push(Math.round((1-s/(k-1))*100)); return o; }
/* python 參考正本的 xml_esc 連單引號也跳脫；要談逐位元同構就得跟著它（XML 兩種寫法都合法，差別只在位元）。 */
function xmlEscQuad(v){ return xmlEsc(v).replace(/'/g,'&apos;'); }
/* 接縫規則（與 make_calib_quad_3mf.py 的 seam_rule 同一套語意）：
     ① X 等值面且在內部 → paint_seam=4 enforcer：縫藏在格與格之間的接著面
     ② X 等值面且在最外側 → 不標：最外側那格的縫退回它內側的接著面（Eric 說的「最外側色塊換邊」）
     ③ Y 等值面（正面／背面）→ paint_seam=8 blocker
   ③ 對校正塊比對照片磚更要緊——**正面就是要拍照回讀顏色的那一面**，縫疤會直接污染量到的色值。
   ⚠ 洗料柱不套本規則（0720 c-0720-PT-01 定案）⇒ width 傳 null。 */
function calibQuadBox(x0,y0,z0,x1,y1,z1,width){
  const f=calibStripNum;
  const vs=[[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],[x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1]];
  const v=vs.map(p=>`<vertex x="${f(p[0])}" y="${f(p[1])}" z="${f(p[2])}"/>`).join('');
  const eps=1e-9, tol=0.01;
  const tri=CALIB_STRIP_TRI.map(t=>{
    const a=vs[t[0]], b=vs[t[1]], c=vs[t[2]];
    let seam=0;
    if(width!==null && width!==undefined){
      if(Math.abs(a[0]-b[0])<eps && Math.abs(a[0]-c[0])<eps) seam=(a[0]<tol || a[0]>width-tol) ? 0 : 4;
      else if(Math.abs(a[1]-b[1])<eps && Math.abs(a[1]-c[1])<eps) seam=8;
    }
    return `<triangle v1="${t[0]}" v2="${t[1]}" v3="${t[2]}"${seam?` paint_seam="${seam}"`:''}/>`;
  }).join('');
  return {v, tri};
}
function buildCalibQuadParts(o){
  o=o||{};
  const LF=CALIB_QUAD_LF;
  const pos=(v,d)=>Number(v)>0?Number(v):d;
  const cell=pos(o.cellMm,CALIB_QUAD_GEO.cellMm), thick=pos(o.thickMm,CALIB_QUAD_GEO.thickMm);
  const k=Math.max(2, Math.round(pos(o.levels,CALIB_QUAD_GEO.levels)));
  /* pillarMm 明寫 0 ＝ 不加洗料柱（⚠ 量值會被殘料污染，只給離線對照用）；沒帶＝用定案值 */
  const pillar=(o.pillarMm===undefined||o.pillarMm===null) ? CALIB_QUAD_GEO.pillarMm : Math.max(0,Number(o.pillarMm)||0);
  const pillarGap=pos(o.pillarGapMm,CALIB_QUAD_GEO.pillarGapMm);
  const colors=(o.colors||[]).map(h=>String(h||'').toUpperCase());
  if(colors.length<4 || colors.slice(0,4).some(h=>!/^#[0-9A-F]{6}$/.test(h))) throw new Error('四料校正塊要四個 #RRGGBB 料色');
  const names=(o.names&&o.names.length>=4 ? o.names : ['料A','料B','料C','料D']).slice(0,4);
  const title=o.title||'照片磚色彩校正 四料 48 格';
  const cols=colors.slice(0,4).map(hexLin);
  /* 與 index.html simulateVerticalQuad 的 mixLin 同式：線性空間加權平均，再回 sRGB。 */
  const mix=w=>'#'+[0,1,2].map(ch=>l2s((w[0]*cols[0][ch]+w[1]*cols[1][ch]+w[2]*cols[2][ch]+w[3]*cols[3][ch])/100)
                                   .toString(16).padStart(2,'0')).join('').toUpperCase();
  const ws=calibQuadSteps(k);
  const width=cell*k, height=cell*CALIB_QUAD_PAIRS.length;
  const objs=[], parts=[], pal=[];
  let oid=0;
  CALIB_QUAD_PAIRS.forEach((pr,r)=>{
    const i=pr[0], j=pr[1], z0=r*cell;
    ws.forEach((wi,c)=>{
      oid++;
      const w=[0,0,0,0]; w[i]=wi; w[j]=100-wi;
      const x0=c*cell;
      const b=calibQuadBox(x0,0,z0,x0+cell,thick,z0+cell,width);
      objs.push(`<object id="${oid}" type="model"><mesh><vertices>${b.v}</vertices><triangles>${b.tri}</triangles></mesh></object>`);
      const prev=mix(w);
      /* 零件名尾端必須是 A.. B.. C.. D.. 四個 token 且前面至少還有一個（C++ parse_photo_part_name 要求 n>=5）；
         #RRGGBB 供 parse_photo_part_color 取預覽色。 */
      const nm=`校正R${r+1}C${c+1} ${prev} A${w[0]} B${w[1]} C${w[2]} D${w[3]}`;
      parts.push([`    <part id="${oid}" subtype="normal_part">`,
                  `      <metadata key="name" value="${xmlEscQuad(nm)}"/>`,
                  '      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>',
                  `      <metadata key="extruder" value="${oid}"/>`,
                  '    </part>'].join(LF));
      pal.push(`extruder ${oid}  R${r+1}C${c+1}  ${names[i]}／${names[j]} = ${wi}／${100-wi} %  預覽色 ${prev}`);
    });
  });
  const cells=oid;
  if(pillar>0){
    const poid=oid+1;
    const px0=width+pillarGap, py0=thick/2-pillar/2;
    const b=calibQuadBox(px0,py0,0,px0+pillar,py0+pillar,height,null);
    objs.push(`<object id="${poid}" type="model"><mesh><vertices>${b.v}</vertices><triangles>${b.tri}</triangles></mesh></object>`);
    const pcol=mix([25,25,25,25]);
    parts.push([`    <part id="${poid}" subtype="normal_part">`,
                `      <metadata key="name" value="${xmlEscQuad('洗料柱 '+pcol+' A25 B25 C25 D25')}"/>`,
                '      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>',
                `      <metadata key="extruder" value="${poid}"/>`,
                '      <metadata key="sparse_infill_density" value="100%"/>',
                '    </part>'].join(LF));
    pal.push(`extruder ${poid}  洗料柱 ${pillar}×${pillar}×${height} mm 實心  = M6052 A25 B25 C25 D25  預覽色 ${pcol}`);
  }
  const MID=1000, f=calibStripNum;
  const totalW=width+(pillar>0 ? (pillarGap+pillar) : 0);
  const comps=objs.map((_,i)=>`<component objectid="${i+1}" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>`).join('');
  const model=['<?xml version="1.0" encoding="UTF-8"?>',
    '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">',
    ' <metadata name="Application">PING-PhotoTile-ColorCalib-Quad</metadata>',
    ' <resources>']
    .concat(objs.map(x=>'  '+x))
    .concat([`  <object id="${MID}" type="model"><components>${comps}</components></object>`,
             ' </resources>',
             ` <build><item objectid="${MID}" transform="1 0 0 0 1 0 0 0 1 ${f(-totalW/2)} ${f(-thick/2)} 0" printable="1"/></build>`,
             '</model>']).join(LF);
  const cfg=['<?xml version="1.0" encoding="UTF-8"?>', '<config>',
             `  <object id="${MID}">`,
             `    <metadata key="name" value="${xmlEscQuad(title)}"/>`,
             parts.join(LF), '  </object>', '</config>'].join(LF);
  /* ⚠ txt 只是給人看的說明，**不在 sha256 釘住的範圍內**（同雙料：只釘 model／cfg）。 */
  const txt=[title,
    `四料 M6052（A/B/C/D ＝各料佔比 %）｜${cells} 格＝${CALIB_QUAD_PAIRS.length} 對 × ${k} 階｜每格 ${cell}×${cell} mm`,
    `正面 ${width}×${height} mm、厚 ${thick} mm｜零件 ${objs.length} 支` + (pillar>0 ? `（含洗料柱 ${pillar}×${pillar} mm）` : '（無洗料柱）'),
    colors.slice(0,4).map((h,i)=>`${'ABCD'[i]}（E${i+1}）＝${names[i]} ${h}`).join(' ／ '),
    '',
    '【量測方式】正面平放拍照、整塊入鏡；每格取中心區（避開四周 2 mm，那裡有外牆轉角的陰影）。',
    '【設計註記】洗料柱高度＝整塊高，不可縮：同一層要換多次比例，柱矮於某層 ⇒ 那層以上沒被洗過、量到的是殘料污染色。',
    '回讀：主程式「幫助 → 色彩校正」選四料 48 格，四個料色照上面填。',
    '', pal.join(LF), ''].join(LF);
  return {model, cfg, txt, cell, thick, levels:k, width, height, pillar, pillarGap,
          cells, partCount:objs.length, title, colors:colors.slice(0,4), names};
}
async function buildCalibQuad(o){
  const c=buildCalibQuadParts(o||{});
  const blob=await makeZip([
    {name:'[Content_Types].xml', data:['<?xml version="1.0" encoding="UTF-8"?>',
      '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">',
      ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>',
      ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>',
      ' <Default Extension="config" ContentType="text/xml"/>',
      ' <Default Extension="txt" ContentType="text/plain"/>',
      '</Types>'].join(CALIB_QUAD_LF)},
    {name:'_rels/.rels', data:['<?xml version="1.0" encoding="UTF-8"?>',
      '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">',
      ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>',
      '</Relationships>'].join(CALIB_QUAD_LF)},
    {name:'3D/3dmodel.model', data:c.model},
    {name:'Metadata/model_settings.config', data:c.cfg},
    {name:'Metadata/ping_calib.txt', data:c.txt},
  ]);
  return Object.assign({blob}, c);
}
/* ================= 雙料量化（index.html:474-521 計算部；畫布/metric 拿掉） =================
   C-1：hooks.tick(frac) 為選配的「階段內進度＋讓步」鉤（見 §量化進度分塊）；
   雙料的逐格迴圈只有一次距離計算（實測 ≤0.1s），整段跑完回報一次即可。 */
/* 雙料色階梯：在感知亮度 L* 上均分、反解各階混比 t（t＝料B 比例；M6051 的 S＝1-t）。
   B案，Eric 2026-07-19 定——均分 S 的階梯亮端擠、暗端整段空洞。
   🔴 **這條梯子本來有兩份**：本檔的 quantizeDual（＝實際產 3MF 的那份）與
   index.html 的 simulateVertical（＝畫面預覽的那份）。兩份同義但各自實作，
   任何一邊改了另一邊沒跟，就會變成「預覽的和印出來的不是同一把尺」——
   那個坑 simulateVertical 的註解裡已經記過一次（分箱與顯色用了不同的尺）。
   ⇒ 2026-08-22 收成本函式一份，並經 `PhotoTileEngine.dualLadder` 匯出給頁面用。
   出貨線的同名函式在 index.html（該線沒有 engine.js），值與本函式逐格相同。 */
function dualLadder(hexA, hexB, K){
  const A=hexLin(hexA), B=hexLin(hexB);
  const labF   = t  => t>0.008856 ? Math.cbrt(t) : (7.787*t + 16/116);
  const labFinv= fy => fy**3>0.008856 ? fy**3 : (fy-16/116)/7.787;
  const YA=lum709(...A), YB=lum709(...B);
  const LA=116*labF(YA)-16, LB=116*labF(YB)-16;
  const t=[];
  for(let i=0;i<K;i++){
    const Lt=K<2?LA:LA+(LB-LA)*i/(K-1);
    const Yt=labFinv((Lt+16)/116);
    t.push(Math.abs(YA-YB)<1e-9 ? 0 : Math.min(1,Math.max(0,(YA-Yt)/(YA-YB))));
  }
  return {t, A, B, LA, LB};
}

/* ================= 色彩校正（0914 Q3 甲，牌 c-0914-PTI-04）=================
   救回 3fc1a61f2f（2026-08-22，核心規格 R6-8／R8-5「④-2 校正值進生成層」）——它原本住在出貨線
   index.html，T042 整區移植（7374b2011d）以開發線頁面覆蓋時被整段刪掉，三棵樹都沒了。
   這次改住**引擎**：校正表 → 校正梯子的計算只有這一份，頁面預覽（simulateVertical）與
   3MF 生成（quantizeDual）都呼叫同一支 ⇒ R8-5 那條「兩把尺必須一起換」由結構保證。

   表怎麼進引擎：掛在 **slots[0].calib**（校正只綁線材＝R6-1，資料掛在料上語意成立）。
   宿主 C++（GUI_App.cpp phototile_generate）把 slotsJson **原字串直通**、只驗「是陣列且可解析」
   ⇒ 不必動 C++、不必 build。calib 缺席＝整段不跑 ⇒ 3MF／請求字串與舊版**位元組全等**
   （同 cycle／metadata 的 additive 契約，黃金閘門不受影響）。

   calib＝{ table:<回讀頁匯出的 JSON 原物>, apply:<bool，預設 true＝④-2；false＝只做 ④-1 顯示> }。
   四道護欄（①通道≥250 警告 ②相鄰階 ΔE<2 警告 ③L* 跨幅<K 擋 ④非單調 擋）與 0822 逐字同義。 */
/* 分派：雙料走 calibParseDual（0914 原樣），四料走 calibParseQuad（R6-2 附款：六對全有）。
   外部介面不變——index.html、dualLadderCalibrated 與測試都只呼叫這一支。
   🔴 四料的回傳物**沒有 pts、改有 pairs**（六對各自一條曲線）——下游要用 `Array.isArray(tbl.pairs)` 分辨。 */
function calibParseTable(json){
  return (json && json['料數']===4) ? calibParseQuad(json) : calibParseDual(json);
}
function calibParseDual(json){
  if(!json || json['料數']!==2) throw new Error('校正表的料數只支援 2 或 4（這張是 '+(json&&json['料數'])+' 料）');
  const rows=json['量測'];
  if(!Array.isArray(rows) || !rows.length) throw new Error('找不到「量測」資料');
  const pts=rows.map(r=>{
    const S=r['配方'] && r['配方'].S;
    const hex=r['量到色'];
    if(typeof S!=='number' || !/^#[0-9a-fA-F]{6}$/.test(hex||''))
      throw new Error('第 '+(r['格']||'?')+' 格的配方或量到色不合法');
    return {S, lin:hexLin(hex), hex:hex.toUpperCase()};
  }).sort((a,b)=>b.S-a.S);                              // S 由大到小＝料A 多 → 少
  if(pts.length<2) throw new Error('校正表至少要 2 個量測點');
  const slotsDecl=(json['料']||[]).map(x=>String(x['色']||'').toUpperCase());
  return {slots:slotsDecl, pts, kind:json['校正塊種類']||'', raw:json};
}
/* 四料 48 格校正表 → 6 對實測曲線（R6-2 附款：六對全有，不得砰成 24 格）。
   來源＝calibration.html 的四料輸出：每列 {格:'R{列}C{行}', 配方:{A,B,C,D}, 量到色}。
   🔴 **不能只靠「恰有兩個非零」認 pair**：每一對的兩端是純色錨點（100/0），
   那兩格只有**一個**非零。改用「同一列（同一個 R）的非零聯集」認，聯集恰好兩支才算一對。
   某一對整列缺席＝那一對沒量過（R6-4 允許），回傳的 pairs 就少一筆，**不丟例外**；
   但格式壞掉（配方不合法、用到第三支料、加起來不是 100）是**表本身錯了**⇒ 丟例外，同雙料。 */
const CALIB_QUAD_PAIRS=[[0,1],[0,2],[0,3],[1,2],[1,3],[2,3]];   // A/B A/C A/D B/C B/D C/D（R6-2 順序）
function calibParseQuad(json){
  if(!json || json['料數']!==4) throw new Error('這不是四料校正表（料數 '+(json&&json['料數'])+'）');
  const rows=json['量測'];
  if(!Array.isArray(rows) || !rows.length) throw new Error('找不到「量測」資料');
  const per=Math.max(1, Math.round(rows.length/CALIB_QUAD_PAIRS.length));
  const groups=new Map();
  rows.forEach((r,idx)=>{
    const f=r && r['配方'];
    const w=['A','B','C','D'].map(k=>Number(f && f[k]));
    const hex=r && r['量到色'];
    const cell=String((r && r['格'])||'');
    const at=cell||('第 '+(idx+1)+' 筆');
    if(w.some(v=>!isFinite(v)||v<0) || !/^#[0-9a-fA-F]{6}$/.test(hex||''))
      throw new Error(at+'的配方或量到色不合法');
    if(Math.round(w[0]+w[1]+w[2]+w[3])!==100) throw new Error(at+'的配方加起來不是 100');
    const m=/^R(\d+)C/.exec(cell);
    const key=m ? ('R'+m[1]) : ('G'+Math.floor(idx/per));   // 沒有格名就照產生器順序切塊
    if(!groups.has(key)) groups.set(key, []);
    groups.get(key).push({w, hex:hex.toUpperCase(), at});
  });
  const pairs=[];
  groups.forEach((g,key)=>{
    const nz=new Set();
    g.forEach(r=>r.w.forEach((v,i)=>{ if(v>0) nz.add(i); }));
    const idx=[...nz].sort((a,b)=>a-b);
    if(idx.length!==2) throw new Error(key+' 這一列不是「兩支料的混比」（用到 '+idx.length+' 支料）');
    const i=idx[0], j=idx[1];
    const pts=g.map(r=>{
      /* 不在這裡再檢一次「用到第三支料」：那個索引一定已經進了上面的 nz 聯集，
         idx.length!==2 那一道先擋下來了 ⇒ 寫了也永遠到不了，而永遠到不了的檢查測不出來。 */
      /* S 的定義與雙料一致＝**前一支端點（i）的佔比**；鍵要跟引擎實際會吐的比例逐字相同（R6-8）。 */
      return {S:Math.round(r.w[i])/100, lin:hexLin(r.hex), hex:r.hex};
    }).sort((a,b)=>b.S-a.S);
    if(pts.length<2) return;                       // 1 點內插不出曲線＝等同沒量過
    pairs.push({i, j, pts});
  });
  if(!pairs.length) throw new Error('這張四料表沒有任何一對可用的量測');
  pairs.sort((a,b)=>(a.i-b.i)||(a.j-b.j));
  const slotsDecl=(json['料']||[]).map(x=>String(x['色']||'').toUpperCase());
  return {slots:slotsDecl, pairs, kind:json['校正塊種類']||'', raw:json};
}
/* 在實測曲線上取 S 對應的顏色（線性空間內插；超出範圍夾到端點）＝④-1 顯示色 */
function calibLookupLin(tbl, S){
  const P=tbl.pts;
  if(S>=P[0].S) return P[0].lin;
  if(S<=P[P.length-1].S) return P[P.length-1].lin;
  for(let i=0;i<P.length-1;i++){
    const a=P[i], b=P[i+1];
    if(S<=a.S && S>=b.S){
      const f=(a.S-S)/(a.S-b.S||1);
      return [0,1,2].map(c=>a.lin[c]+(b.lin[c]-a.lin[c])*f);
    }
  }
  return P[P.length-1].lin;
}
/* 這張表適用於目前的料嗎？校正只綁線材（R6-1）⇒ 料色不同就是別組料的表，不可套用。 */
function calibMatches(tbl, slots){
  if(!tbl || !Array.isArray(slots) || slots.length<2) return false;
  const now=[0,1].map(i=>String(slots[i]&&slots[i].color||'').toUpperCase());
  return tbl.slots.length>=2 && tbl.slots[0]===now[0] && tbl.slots[1]===now[1];
}
/* ④-2：K 階改在**實測** L* 上均分，再從實測曲線反解每階的混比 S（⇒ 改變 M6051 的 S＝改變印出來的東西）。
   ok=false 就退回理論階梯，理由要能講給人聽（blocked／warnOnly 分開）。 */
function calibGenLadder(tbl, K){
  const P=tbl && tbl.pts;
  if(!P || P.length<2) return {ok:false, why:'沒有校正表', blocked:'沒有校正表', warnOnly:''};
  const Lof = lin => { const y=lum709(...lin); return 116*(y>0.008856?Math.cbrt(y):(7.787*y+16/116))-16; };
  const L=P.map(p=>Lof(p.lin));
  const L1=L[0], L0=L[L.length-1];                       // pts 已按 S 由大到小 ⇒ [0]＝料A 純、[末]＝料B 純
  const span=Math.abs(L1-L0);
  const warn=[], block=[];
  const clipped=P.filter(p=>Math.max(...hexRgb(p.hex))>=250).length;
  if(clipped) warn.push(clipped+' 格有通道 ≥250（疑似拍照高光溢出，建議降曝光重拍）');
  let flat=0;
  for(let i=0;i<P.length-1;i++){
    const a=lin2lab(...P[i].lin), b=lin2lab(...P[i+1].lin);
    if(Math.hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2])<2) flat++;
  }
  if(flat) warn.push(flat+' 對相鄰階色差 <2（人眼分不出）');
  if(span < K*1.0) block.push('實測 L* 跨幅只有 '+span.toFixed(1)+'，'+K+' 階平均每階不到 1 L*（人眼可辨下限）⇒ 換一支更暗的料B 才有救');
  let mono=true;
  for(let i=0;i<L.length-1;i++) if((L1>L0 ? L[i+1]-L[i] : L[i]-L[i+1]) > 0.5) mono=false;
  if(!mono) block.push('實測 L* 不是單調的，反解不出唯一混比');
  const t=[];
  for(let i=0;i<K;i++){
    const Lt = K<2 ? L1 : L1+(L0-L1)*i/(K-1);
    let S=null;
    for(let j=0;j<L.length-1;j++){
      const la=L[j], lb=L[j+1];
      if(la!==lb && (Lt-la)*(Lt-lb)<=0){ S=P[j].S+(P[j+1].S-P[j].S)*(Lt-la)/(lb-la); break; }
    }
    if(S===null) S = (Math.abs(Lt-L1)<Math.abs(Lt-L0)) ? P[0].S : P[P.length-1].S;
    /* 印出去的 S 就是兩位小數（M6051 S{round((1-t)*100)/100}）⇒ 先對齊到同一格，
       否則畫面模擬的是 0.634、機器印的是 0.63，兩把尺又對不上。 */
    S=Math.round(Math.min(1,Math.max(0,S))*100)/100;
    t.push(1-S);
  }
  return {ok:block.length===0, why:block.concat(warn).join('；'), warnOnly:warn.join('；'),
          blocked:block.join('；'), t, LA:L1, LB:L0, span};
}
/* 色調映射「壓進可印範圍」（乙案，0914；opt-in＝calib.toneMap==='stretch'）：
   校正後分箱的兩端＝料的**實測** L*（R8-5「兩把尺一起換」），於是圖裡比「最深料印出來」還暗的像素全部夾到最深一階。
   0914 實錄：AI 壓平的三色巴哥（L* 5／45／95）遇到只印得到 L* 48 的深灰料 ⇒ 灰與黑都落 S0、中間階空掉、印出來兩色。
   這不是校正錯，是料的可印範圍窄；乙案＝先把圖的明暗範圍（1%～99% 百分位）線性壓進 [LB, LA] 再分箱，
   K 階都有肉、對比按比例縮。甲案（換更黑的料）另議。缺席／'none'＝完全照舊（絕對 L* 分箱、超界夾住）。 */
function toneStretch(lab, n, LA, LB, opt){
  const lo=(opt&&Number.isFinite(opt.pLo))?opt.pLo:0.01, hi=(opt&&Number.isFinite(opt.pHi))?opt.pHi:0.99;
  const H=new Uint32Array(201);                       // L* 0..100，0.5 一格
  for(let p=0;p<n;p++){ const v=lab[p*3]; H[Math.max(0,Math.min(200,Math.round(v*2)))]++; }
  let acc=0, Lmin=null, Lmax=null;
  for(let i=0;i<=200;i++){ acc+=H[i]; if(Lmin===null && acc>=n*lo) Lmin=i/2; if(Lmax===null && acc>=n*hi){ Lmax=i/2; break; } }
  if(Lmax===null) Lmax=100; if(Lmin===null) Lmin=0;
  const dark=Math.min(LA,LB), light=Math.max(LA,LB);
  const span=Math.max(1e-6, Lmax-Lmin);
  const mapL = v => dark + (Math.max(Lmin,Math.min(Lmax,v)) - Lmin) / span * (light-dark);
  return { mapL, Lmin, Lmax, dark, light };
}
/* 統一入口：quantizeDual（3MF 生成）與頁面 simulateVertical（預覽）都只呼叫這一支。
   回傳＝dualLadder 的形狀 ＋ calib 狀態 ＋（表相符時）lookupLin（④-1 顯示色）。
   calib 為 null／undefined ⇒ 與 dualLadder 逐值相同（多一個 calib:{present:false} 欄位）。 */
function dualLadderCalibrated(slots, K, calib){
  const base=dualLadder(slots[0].color, slots[1].color, K);
  const out={t:base.t, A:base.A, B:base.B, LA:base.LA, LB:base.LB,
             calib:{present:false, matches:false, applied:false, why:'', warnOnly:'', span:null}};
  if(!calib || !calib.table) return out;
  out.calib.present=true;
  let tbl;
  try{ tbl = (calib.table.pts && calib.table.slots) ? calib.table : calibParseTable(calib.table); }
  catch(e){ out.calib.why='校正表讀不到：'+(e&&e.message?e.message:'格式不合'); return out; }
  out.calib.tableSlots=tbl.slots.slice(0,2);
  /* 四料表沒有 pts（只有 pairs），落到下面會静默變成「沒有校正表」⇒ 這裡先講清楚。 */
  if(Array.isArray(tbl.pairs)){ out.calib.why='這是四料校正表，雙料模式用不到'; return out; }
  if(!calibMatches(tbl, slots)){ out.calib.why='校正表與目前料色不符（'+tbl.slots.slice(0,2).join(' · ')+'），整段未套用'; return out; }
  out.calib.matches=true;
  out.lookupLin = S => calibLookupLin(tbl, S);          // ④-1：顯示色改實測，不動 t
  if(calib.apply===false){ out.calib.why='使用者關閉「用校正值決定色階」：色階仍用理論推算'; return out; }
  const g=calibGenLadder(tbl, K);
  out.calib.warnOnly=g.warnOnly||''; out.calib.span=g.span;
  if(!g.ok){ out.calib.why=g.blocked; return out; }
  out.t=g.t; out.LA=g.LA; out.LB=g.LB;                    // ④-2：梯子與分箱端點一起換（R8-5「兩把尺」）
  out.calib.applied=true;
  return out;
}

/* ================= 校正片（Z 疊階梯＋隨階換比例洗料柱）＝照片磚_色彩校正/make_calib_3mf.py 的同構 JS 版 =================
   每階獨佔一段 Z、整層只有一個比例；柱是**獨立物件**且 build item 排在片之前 ⇒ 切片器逐層先印柱再印片，
   換比例那一層的殘料吐在柱裡，片從該階第一層就是乾淨色。
   Eric 2026-09-14 兩裁：「建議還是使用洗料塔，這樣在裡面就不會有邊印、邊有色差跑出來」＋「校正片看能不能不要印那麼久」
   ⇒ 每階不必再靠印厚去耗殘料：v1 無柱 80×8×48／每階 6 mm 實印 **32m45s**；v2c 有柱 40×6×16／每階 2 mm 實印 **12m03s**。
   為什麼不用現成的循環洗料塔：8 個比例 > 每色進塔的 3 色上限 ⇒ C++ 退回層首整塔（每層收在白料），深色階每層開頭反而帶白。
   🔴 **整組置中不可省**：切片器載入 3MF 會把物件整組置中（0914 v2 實切踩過：3MF 寫柱在 X 40~55，G-code 實際落 28~42）
   ⇒ 這裡先把「片＋柱」的聯集包圍盒置中在原點，3MF 座標才＝G-code 座標，回傳的 purgeBox 才能直接餵
   照片磚_色彩校正/verify_calib_gcode.py --purge-box 逐層核對「柱先印、片後印、M6051 在柱之前」。
   零件名尾端＝配方（C++ parse_photo_part_name 只認尾端 S<0~1>）；片的正／背面 paint_seam=8 禁縫（正面＝拍照回讀面），
   柱不標縫（0720 定案）。python 版是離線參考正本，兩邊幾何／命名同構；改這裡要回頭對它。 */
const CALIB_STRIP_TRI=[[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,1,5],[0,5,4],[3,7,6],[3,6,2],[0,4,7],[0,7,3],[1,2,6],[1,6,5]];
const CALIB_STRIP_S_DENSE_LIGHT=[1,0.94,0.87,0.78,0.67,0.53,0.35,0];   // 白端加密（0914：深色蓋色力強，S<0.3 幾乎全深）
/* v2c 定案幾何（0914 白×深灰實印驗證：80 層／2.76 g／實印 12m03s）。這裡是唯一來源——
   頁面不要再自己寫一份數字（v1→v2c 就是因為兩邊各寫一份才分叉的）。 */
const CALIB_STRIP_GEO={widthMm:40, thickMm:6, bandMm:2, purgeMm:12, purgeGapMm:8, purgeWalls:2};
function calibStripGeo(){ return Object.assign({}, CALIB_STRIP_GEO); }
/* 8 階的取樣密在「淺料的純色端」：S＝料A 佔比，料A 較亮 ⇒ 密在 S≈1；料A 較暗 ⇒ 鏡射到 S≈0。 */
function calibStripDefaultS(hexA, hexB){
  const L = h => { const y=lum709(...hexLin(h)); return 116*(y>0.008856?Math.cbrt(y):(7.787*y+16/116))-16; };
  if(L(hexA) >= L(hexB)) return CALIB_STRIP_S_DENSE_LIGHT.slice();
  return CALIB_STRIP_S_DENSE_LIGHT.map(s=>Math.round((1-s)*100)/100).reverse();
}
function calibStripFmtS(s){ return String(Math.round(s*100)/100); }   // "1"／"0.94"／"0"＝與工作室零件名同式
function calibStripNum(v){ const t=(Math.round(v*1000)/1000).toFixed(3); return t.replace(/0+$/,'').replace(/\.$/,''); }
function calibStripBox(x0,y0,z0,x1,y1,z1,markSeam){
  const f=calibStripNum;
  const vs=[[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],[x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1]];
  const v=vs.map(p=>`<vertex x="${f(p[0])}" y="${f(p[1])}" z="${f(p[2])}"/>`).join('');
  const eps=1e-9;
  const tri=CALIB_STRIP_TRI.map(t=>{
    const a=vs[t[0]], b=vs[t[1]], c=vs[t[2]];
    const seam=(markSeam!==false && Math.abs(a[1]-b[1])<eps && Math.abs(a[1]-c[1])<eps) ? 8 : 0;   // Y 等值面＝正／背面 → 禁縫
    return `<triangle v1="${t[0]}" v2="${t[1]}" v3="${t[2]}"${seam?` paint_seam="${seam}"`:''}/>`;
  }).join('');
  return {v, tri};
}
function buildCalibStripParts(o){
  o=o||{};
  const pos=(v,d)=>Number(v)>0?Number(v):d;
  const width=pos(o.widthMm,CALIB_STRIP_GEO.widthMm), thick=pos(o.thickMm,CALIB_STRIP_GEO.thickMm), band=pos(o.bandMm,CALIB_STRIP_GEO.bandMm);
  /* purgeMm 明寫 0 ＝ 退回 v1 無柱幾何（只給離線對照用）；沒帶＝用 v2c 定案值 */
  const purge=(o.purgeMm===undefined||o.purgeMm===null) ? CALIB_STRIP_GEO.purgeMm : Math.max(0, Number(o.purgeMm)||0);
  const purgeGap=pos(o.purgeGapMm,CALIB_STRIP_GEO.purgeGapMm);
  const purgeWalls=Math.max(1, Math.round(pos(o.purgeWalls,CALIB_STRIP_GEO.purgeWalls)));
  const hexA=String(o.hexA||'').toUpperCase(), hexB=String(o.hexB||'').toUpperCase();
  if(!/^#[0-9A-F]{6}$/.test(hexA) || !/^#[0-9A-F]{6}$/.test(hexB)) throw new Error('料色要是 #RRGGBB');
  const s=Array.isArray(o.s)&&o.s.length>=2 ? o.s.map(Number) : calibStripDefaultS(hexA,hexB);
  if(s[0]!==1 || s[s.length-1]!==0) throw new Error('兩端必須是純色錨點 S=1／S=0（R6-6）');
  for(let i=0;i<s.length-1;i++) if(!(s[i]>s[i+1])) throw new Error('S 必須由下往上嚴格遞減');
  const nameA=o.nameA||'料A', nameB=o.nameB||'料B';
  const title=o.title||`照片磚色彩校正 ${nameA}+${nameB} ${s.length}階（直立條）`;
  const A=hexRgb(hexA), B=hexRgb(hexB);
  const MID=1000, PID=2000, objs=[], parts=[], pparts=[], pal=[];
  let z=0;
  s.forEach((sv,i)=>{
    const oid=i+1;
    const {v,tri}=calibStripBox(0,0,z,width,thick,z+band,true);
    objs.push(`<object id="${oid}" type="model"><mesh><vertices>${v}</vertices><triangles>${tri}</triangles></mesh></object>`);
    const prev='#'+[0,1,2].map(c=>Math.round(B[c]+(A[c]-B[c])*sv).toString(16).padStart(2,'0')).join('').toUpperCase();   // 與 PingColorMix::dual_color 同式（sRGB）
    const nm=`校正${String(i+1).padStart(2,'0')} ${prev} S${calibStripFmtS(sv)}`;
    parts.push(`    <part id="${oid}" subtype="normal_part">\n      <metadata key="name" value="${xmlEsc(nm)}"/>\n      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n      <metadata key="extruder" value="${oid}"/>\n    </part>`);
    pal.push(`extruder ${oid}  S${calibStripFmtS(sv)}  ${nameA} 佔比 ${(sv*100).toFixed(1)}% ／ ${nameB} 佔比 ${((1-sv)*100).toFixed(1)}%   Z ${z.toFixed(1)}~${(z+band).toFixed(1)} mm   預覽色 ${prev}`);
    if(purge>0){
      const poid=100+oid;                            // 柱段 id 與片段錯開；extruder 與同階片段相同 ⇒ 同一個 M6051 配方
      const p=calibStripBox(0,0,z,purge,purge,z+band,false);   // 洗料柱不標縫
      objs.push(`<object id="${poid}" type="model"><mesh><vertices>${p.v}</vertices><triangles>${p.tri}</triangles></mesh></object>`);
      const pnm=`洗料柱${String(i+1).padStart(2,'0')} ${prev} S${calibStripFmtS(sv)}`;
      pparts.push(`    <part id="${poid}" subtype="normal_part">\n      <metadata key="name" value="${xmlEsc(pnm)}"/>\n      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n      <metadata key="extruder" value="${oid}"/>\n      <metadata key="sparse_infill_density" value="0%"/>\n      <metadata key="wall_loops" value="${purgeWalls}"/>\n      <metadata key="top_shell_layers" value="0"/>\n      <metadata key="bottom_shell_layers" value="0"/>\n    </part>`);
    }
    z+=band;
  });
  const f=calibStripNum;
  const comps=s.map((_,i)=>`<component objectid="${i+1}" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>`).join('');
  let topObjs=`  <object id="${MID}" type="model"><components>${comps}</components></object>\n`;
  let cfgObjs=`  <object id="${MID}">\n    <metadata key="name" value="${xmlEsc(title)}"/>\n${parts.join('\n')}\n  </object>\n`;
  let buildItems='', purgeBox=null, sx0=-width/2;
  if(purge>0){
    const pcomps=s.map((_,i)=>`<component objectid="${100+i+1}" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>`).join('');
    topObjs=`  <object id="${PID}" type="model"><components>${pcomps}</components></object>\n`+topObjs;
    /* 柱放在片的右側（+X）、與片同底、Y 置中；再把「片＋柱」聯集置中在原點（見區塊註解的置中坑）。 */
    const cx=(purgeGap+purge)/2;                     // 聯集 X 範圍 [-width/2, width/2+gap+purge] 的中心
    const px0=width/2+purgeGap-cx, py0=-purge/2;
    sx0=-width/2-cx;
    buildItems+=`<item objectid="${PID}" transform="1 0 0 0 1 0 0 0 1 ${f(px0)} ${f(py0)} 0" printable="1"/>`;   // 柱的 build item 在前＝切片器逐層先印它
    cfgObjs=`  <object id="${PID}">\n    <metadata key="name" value="${xmlEsc(title+' 洗料柱')}"/>\n${pparts.join('\n')}\n  </object>\n`+cfgObjs;
    purgeBox={x0:px0, y0:py0, x1:px0+purge, y1:py0+purge};
    pal.push(`洗料柱 ${purge}×${purge} mm 空心方管（${purgeWalls} 圈牆、0% 填充、無上下殼），位置 X ${px0.toFixed(1)}~${(px0+purge).toFixed(1)}、Y ${py0.toFixed(1)}~${(py0+purge).toFixed(1)}（bed 中心座標＝整組已置中），與片逐階同比例、每層先印；verify --purge-box ${f(px0)},${f(py0)},${f(px0+purge)},${f(py0+purge)}`);
  }
  buildItems+=`<item objectid="${MID}" transform="1 0 0 0 1 0 0 0 1 ${f(sx0)} ${f(-thick/2)} 0" printable="1"/>`;
  const model=`<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n <metadata name="Application">PING-PhotoTile-ColorCalib</metadata>\n <resources>\n`+
    objs.map(x=>'  '+x+'\n').join('')+topObjs+` </resources>\n <build>${buildItems}</build>\n</model>`;
  const cfg=`<?xml version="1.0" encoding="UTF-8"?>\n<config>\n${cfgObjs}</config>`;
  const geoLine=purge>0
    ? `尺寸 ${width}×${thick}×${z} mm｜階數 ${s.length}｜每階高 ${band} mm｜洗料柱 ${purge}×${purge} mm（${purgeWalls} 圈牆、與片間距 ${purgeGap} mm）`
    : `尺寸 ${width}×${thick}×${z} mm｜階數 ${s.length}｜每階高 ${band} mm｜無洗料柱`;
  const noteLine=purge>0
    ? `柱是獨立物件、build item 在片之前 ⇒ 切片器逐層先印柱、再印片：換比例那一層的殘料吐在柱裡，片從該階第一層就是乾淨色，所以每階只要 ${band} mm、不必靠印厚耗殘料。整組已置中 ⇒ 3MF 座標＝G-code 座標，可直接 verify_calib_gcode.py --purge-box ${f(purgeBox.x0)},${f(purgeBox.y0)},${f(purgeBox.x1)},${f(purgeBox.y1)}。`
    : `本校正片沒有洗料柱：每階獨佔一段 Z、整層只有一個比例，換比例後數十層都是同一比例 ⇒ 量上半部必定已穩定。`;
  const txt=`${title}\n雙料 M6051（S＝${nameA} 的佔比）｜${geoLine}\n料A（E1）＝${nameA} ${hexA} ／ 料B（E2）＝${nameB} ${hexB}\n\n【量測方式】正面平放拍照、整片入鏡；每一階只取上半部（下半部可能還是上一階的殘料）；頭尾兩階是純色錨點。\n【設計註記】${noteLine}\n回讀：主程式「幫助 → 色彩校正」選「雙料 8 階直立條」，S 清單填 ${s.map(calibStripFmtS).join(',')}。\n\n${pal.join('\n')}\n`;
  return {model, cfg, txt, s, width, thick, height:z, band, purge, purgeGap, purgeWalls, purgeBox, title, hexA, hexB};
}
async function buildCalibStrip(o){
  const c=buildCalibStripParts(o||{});
  const blob=await makeZip([
    {name:'[Content_Types].xml', data:`<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n <Default Extension="config" ContentType="text/xml"/>\n <Default Extension="txt" ContentType="text/plain"/>\n</Types>`},
    {name:'_rels/.rels', data:`<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>`},
    {name:'3D/3dmodel.model', data:c.model},
    {name:'Metadata/model_settings.config', data:c.cfg},
    {name:'Metadata/ping_calib.txt', data:c.txt},
  ]);
  return Object.assign({blob}, c);
}

async function quantizeDual(img, P, slots, hooks){
  const K=P.klevels;
  /* 0914 校正：slots[0].calib 存在才走校正入口；缺席＝原 dualLadder（同一份數學，輸出逐位元不變） */
  const calib = slots[0] && slots[0].calib ? slots[0].calib : null;
  const ladder = calib ? dualLadderCalibrated(slots, K, calib) : dualLadder(slots[0].color, slots[1].color, K);
  const A=ladder.A, B=ladder.B, LA=ladder.LA, LB=ladder.LB;
  const levels=[];
  for(let i=0;i<K;i++){
    const t=ladder.t[i];
    const theoryLin=[0,1,2].map(c=>A[c]*(1-t)+B[c]*t);
    /* ④-1：表相符時顯示色（零件名預覽色／palette hex）改用實測曲線上的值；t 不動（動 t 是 ④-2，在 ladder 裡） */
    const lin = ladder.lookupLin ? ladder.lookupLin(Math.round((1-t)*100)/100) : theoryLin;
    levels.push({t, lin, rgb:lin.map(l2s), lab:lin2lab(...lin)});
  }
  const n=img.w*img.h;
  const rawLabels=new Uint8Array(n);
  const spanL=LB-LA;
  /* 乙案色調映射：只在「表已套用」且 calib.toneMap==='stretch' 時啟動；其餘一律原式（逐位元不變） */
  const ts = (calib && ladder.calib && ladder.calib.applied && calib.toneMap==='stretch') ? toneStretch(img.lab, n, LA, LB, calib) : null;
  if(ts){
    for(let p=0;p<n;p++){
      const k=Math.abs(spanL)<1e-9 ? 0 : Math.round((ts.mapL(img.lab[p*3])-LA)/spanL*(K-1));
      rawLabels[p]=Math.min(K-1,Math.max(0,k));
    }
    ladder.calib.toneMap={mode:'stretch', Lmin:ts.Lmin, Lmax:ts.Lmax, dark:ts.dark, light:ts.light};
  } else {
  for(let p=0;p<n;p++){
    const k=Math.abs(spanL)<1e-9 ? 0 : Math.round((img.lab[p*3]-LA)/spanL*(K-1));
    rawLabels[p]=Math.min(K-1,Math.max(0,k));
  }
  }
  if (hooks && hooks.tick) await hooks.tick(1);
  return calib ? { rawLabels, palette: levels, filterStrategy: 'median', calib: ladder.calib }
               : { rawLabels, palette: levels, filterStrategy: 'median' };
}

/* ================= 四料量化（index.html:376-437 計算部；畫布/清單拿掉） =================
   C-1 量化進度分塊：quad 的「格數×候選數」內積是全鏈最長段（C-0 §4.2：合法上限
   400×400・K=48 量化段 47.9–54.5s）。把逐格迴圈按「列區塊」切開，區塊之間
   await hooks.tick(frac)＝回報進度＋讓 cancel 指令被觀察到。
   ⚠ 迴圈順序、算式、寫入順序全部不變 ⇒ 標籤結果與 spike／工作室逐位元一致。 */
const QUANT_ROWS_PER_BLOCK = 64;
/* 四料校正的每對護欄（沿用雙料那四道，**逐對各判各的**——R6-4 定的失效單位就是 pair）。
   🔴 跨幅這一道照 **R6-7 附款**（Eric 2026-09-22 裁 C 案）辦，不照雙料的 `span < K` 硬擋：
      跨幅 ≥ 4 L* ⇒ **不擋**，只把「這一對做得出幾階」算出來（floor(跨幅)）交給畫面講；
      跨幅 < 4 L* ⇒ 擋（連 4 階都做不出來，已不是「階數調一調」的範圍）。
   為什麼不照抄雙料那道：雙料的 `span < K` 存在，是因為雙料要**反解一條單調梯子**；
   四料不反解，是在 Lab 上找最近色 ⇒ 跨幅小的一對只是候選彼此接近，不會算壞。
   而 R6-7 末段明寫「L* 跨幅不足 ≠ 兩支料之間沒有可用的色差」（白×黃相鄰階 ΔE76 有 10.9~25.1，差在彩度）
   ⇒ 在四料把那種對整組擋掉，會平白少掉彩度那條路（R6-11）。
   ⚠ 這一處是**規格正本（R6-7 附款）與實作計畫那句「沿用雙料四道」的差異**，已擇規格正本；
      要改回硬擋，先改 R6-7 附款。 */
const CALIB_QUAD_SPAN_MIN_L=4;
function calibQuadPairGuard(pts, K){
  const Lof = lin => { const y=lum709(...lin); return 116*(y>0.008856?Math.cbrt(y):(7.787*y+16/116))-16; };
  const L=pts.map(p=>Lof(p.lin));
  const span=Math.abs(L[0]-L[L.length-1]);
  const warn=[], block=[];
  const clipped=pts.filter(p=>Math.max(...hexRgb(p.hex))>=250).length;
  if(clipped) warn.push(clipped+' 格有通道 ≥250（疑似拍照高光溢出，建議降曝光重拍）');
  let flat=0;
  for(let i=0;i<pts.length-1;i++){
    const a=lin2lab(...pts[i].lin), b=lin2lab(...pts[i+1].lin);
    if(Math.hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2])<2) flat++;
  }
  if(flat) warn.push(flat+' 對相鄰階色差 <2（人眼分不出）');
  let mono=true;
  const desc=L[0]>L[L.length-1];
  for(let i=0;i<L.length-1;i++) if((desc ? L[i+1]-L[i] : L[i]-L[i+1]) > 0.5) mono=false;
  if(!mono) block.push('實測 L* 不是單調的（多半是拍照或取色出了問題）');
  if(span < CALIB_QUAD_SPAN_MIN_L) block.push('實測 L* 跨幅只有 '+span.toFixed(1)+'，連 4 階都做不出來');
  return {ok:block.length===0, span, maxLevels:Math.max(0,Math.floor(span)),
          warnOnly:warn.join('；'), blocked:block.join('；')};
}
/* 把 calib 攤成「哪幾對可以用、各自的查表函式是什麼」。
   🔴 **hex 只當顯示與防呆，不當鍵**（R6-14 子題 1）：料色對不上只警示、不擋——
   擋的判準在 R6-16，那是「有沒有量過」，不是「顏色像不像」。 */
function calibQuadPlan(calib, slots, K){
  const out={present:true, applied:false, why:'', kind:'', pairs:{}, skipped:[], notes:[], hexWarn:''};
  let tbl;
  try{ tbl = (calib.table && Array.isArray(calib.table.pairs)) ? calib.table : calibParseTable(calib.table); }
  catch(e){ out.why='校正表讀不到：'+(e && e.message || e); return out; }
  if(!tbl || !Array.isArray(tbl.pairs)){ out.why='這是雙料校正表，四料模式用不到'; return out; }
  out.kind=tbl.kind||'';
  const now=slots.slice(0,4).map(s=>String(s && s.color || '').toUpperCase());
  const decl=(tbl.slots||[]).slice(0,4);
  const bad=[];
  for(let i=0;i<4;i++) if(decl[i] && now[i] && decl[i]!==now[i]) bad.push('ABCD'[i]+'（表 '+decl[i]+' · 現在 '+now[i]+'）');
  if(bad.length) out.hexWarn='這些料量到的顏色跟現在裝的不一樣，是不是換批了？'+bad.join('、');
  if(calib.apply===false){ out.why='未套用（只看不用）'; return out; }
  for(const pr of tbl.pairs){
    const g=calibQuadPairGuard(pr.pts, K);
    if(!g.ok){ out.skipped.push({i:pr.i, j:pr.j, why:g.blocked}); continue; }
    if(g.warnOnly) out.notes.push('ABCD'[pr.i]+'×'+'ABCD'[pr.j]+'：'+g.warnOnly);
    out.pairs[pr.i+','+pr.j]={span:g.span, maxLevels:g.maxLevels, lookup:S=>calibLookupLin(pr, S)};
  }
  out.applied = Object.keys(out.pairs).length>0;
  /* ⚠ 一對都不能用時**退回理論候選**（＝舊行為），不是擋下生成。
     硬閘門（未校正的料選不到）是 R6-16 推論①，那要在畫面上擋（段 F）；
     引擎這一層若直接擋，等於車 1 就改變了既有專案的行為——車 1 是純加法。這裡誠實回報狀態就好。 */
  if(!out.applied) out.why='沒有任何一對通過護欄'+(out.skipped.length ? ('：'+out.skipped.map(x=>x.why).join('；')) : '');
  return out;
}
/* 車 3 段 F（牌 c-0923-ACC-23；R8-5「兩把尺一起換」）：四料的**候選色＋校正計畫**只有這一份——
   頁面預覽（index.html simulateVerticalQuad）與 3MF 生成（quantizeQuad）都呼叫它。
   車 2 以前頁面那份自己用 mixLin 算理論候選、不吃校正；四料畫面接上之後那就是「預覽一把尺、輸出一把尺」。
   Lmin／Lmax＝候選色 L* 的全域兩端＝R6-7 附款 Q4 的「四料 L* 跨幅」，也是段 E 映射的目標範圍（同一個口徑）。 */
function quadCandidates(slots, K, calib){
  const cols=slots.slice(0,4).map(s=>hexLin(s.color));
  const mixLin=w=>[0,1,2].map(ch=>(w[0]*cols[0][ch]+w[1]*cols[1][ch]+w[2]*cols[2][ch]+w[3]*cols[3][ch])/100);
  const PAIRS=CALIB_QUAD_PAIRS;          // 單一來源：產生器與量化用同一張對表，才談得上「量了套得回去」
  const plan = calib ? calibQuadPlan(calib, slots, K) : null;
  const useCal = !!(plan && plan.applied);
  const cands=[]; const seenW=new Set();
  for(const [i,j] of PAIRS){
    const cp = useCal ? plan.pairs[i+','+j] : null;
    /* 🔴 沒有這一對的校正資料（或被護欄擋下）⇒ **那一對的候選整組不進候選池，不退回理論值**。
       退回理論＝用沒人量過的數字決定成品，正是 R6-16 要擋的事；而其餘五對照常（R6-4）。 */
    if(useCal && !cp) continue;
    for(let s=0;s<K;s++){
      const w=[0,0,0,0];
      w[i]=Math.round((1-s/(K-1))*100); w[j]=100-w[i];
      const key=w.join(','); if(seenW.has(key)) continue; seenW.add(key);
      /* 用**曲線內插**取值，不是逐格查表——候選是 K 階、量測是 8 階，K≠8 時對不上；
         而 w[i]/100 與校正表的鍵是同一條量化式算出來的（R6-8）。 */
      const lin = cp ? cp.lookup(w[i]/100) : mixLin(w);
      cands.push({w, lin, lab:lin2lab(...lin)});
    }
  }
  let Lmin=Infinity, Lmax=-Infinity;
  for(const c of cands){ if(c.lab[0]<Lmin) Lmin=c.lab[0]; if(c.lab[0]>Lmax) Lmax=c.lab[0]; }
  return {cands, plan, useCal, Lmin, Lmax};
}
/* 段 E 映射的啟動條件只寫這一份（預覽與生成共用）：表已套用（至少一對可用）且 calib.toneMap==='stretch'。 */
function quadToneStretch(lab, n, qc, calib){
  return (qc && qc.useCal && calib && calib.toneMap==='stretch' && qc.cands.length)
    ? toneStretch(lab, n, qc.Lmin, qc.Lmax, calib) : null;
}
/* 單色的 L*（選料時排槽位＝擠出機 1 放最淺的；色彩數學只住引擎這一份）。 */
function hexLstar(hex){ return lin2lab(...hexLin(hex))[0]; }
async function quantizeQuad(img, P, slots, hooks){
  const K=Math.max(2,P.klevels);
  /* 🔒 保命索：slots[0].calib 缺席 ⇒ plan 是 null、下面每一行都走原路，
     標籤與 palette 與舊版**逐位元相同**（同雙料 0914 的紀律）。 */
  const calib = slots[0] && slots[0].calib ? slots[0].calib : null;
  const qc = quadCandidates(slots, K, calib);
  const cands=qc.cands, plan=qc.plan, useCal=qc.useCal;
  const n=img.w*img.h;
  let dropped=0;
  /* 段 E（車 2，牌 c-0923-ACC-21；R6-16 推論③）：「把圖的明暗壓進這組料印得出來的範圍再分階」的四料版。
     兩端＝候選色（實測內插值）的 L* 全域最小／最大——與 R6-7 附款 Q4 的「四料 L* 跨幅」同一個口徑。
     映射本身用雙料那一支 toneStretch()，**不寫第二份實作**（這條線被兩份實作咬過三次）。
     只在「表已套用（至少一對可用）且 calib.toneMap==='stretch'」時啟動；其餘一律原式 ⇒
     calib 缺席、apply:false、或沒要 stretch 時，標籤與 palette 逐位元不變（保命索）。
     只動 L*、不動 a*b*：候選之間靠彩度分開的那條路（R6-11）照舊。 */
  const ts=quadToneStretch(img.lab, n, qc, calib);
  const L0of = p => ts ? ts.mapL(img.lab[p*3]) : img.lab[p*3];
  const assign=new Uint16Array(n); const usage=new Uint32Array(cands.length);
  for(let y0=0;y0<img.h;y0+=QUANT_ROWS_PER_BLOCK){
    const pEnd=Math.min(n,(y0+QUANT_ROWS_PER_BLOCK)*img.w);
    for(let p=y0*img.w;p<pEnd;p++){
      const L0=L0of(p),A0=img.lab[p*3+1],B0=img.lab[p*3+2];
      let bi=0,bd=1e9;
      for(let i=0;i<cands.length;i++){ const c=cands[i].lab;
        const d=(L0-c[0])**2+(A0-c[1])**2+(B0-c[2])**2;
        if(d<bd){bd=d;bi=i;} }
      assign[p]=bi; usage[bi]++;
    }
    if (hooks && hooks.tick) await hooks.tick(Math.min(1, pEnd/n) * 0.9);   // 主迴圈佔本階段 90%
  }
  let used=[...cands.keys()].filter(i=>usage[i]>0).sort((a,b)=>usage[b]-usage[a]);
  const MAXP = P.pillar ? 63 : 64;   // 洗料柱佔 1 支（Orca 上限 64；index.html:411）
  if(used.length>MAXP){ dropped=used.length-MAXP; used=used.slice(0,MAXP); }
  used.sort((a,b)=>cands[b].lab[0]-cands[a].lab[0]);
  const remap=new Int16Array(cands.length).fill(-1);
  const palette=used.map((ci,pi)=>{ remap[ci]=pi; const c=cands[ci];
    return {w:c.w, lin:c.lin, lab:c.lab, rgb:c.lin.map(l2s)}; });
  const rawLabels=new Uint8Array(n);
  for(let p=0;p<n;p++){
    let pi=remap[assign[p]];
    if(pi<0){
      const L0=L0of(p),A0=img.lab[p*3+1],B0=img.lab[p*3+2];   // 被併掉的候選改找最近：用同一個映射後的 L*（兩把尺一起換）
      let bd=1e9; pi=0;
      for(let i=0;i<palette.length;i++){ const c=palette[i].lab;
        const d=(L0-c[0])**2+(A0-c[1])**2+(B0-c[2])**2;
        if(d<bd){bd=d;pi=i;} }
    }
    rawLabels[p]=pi;
  }
  if (hooks && hooks.tick) await hooks.tick(1);
  if(plan && useCal && cands.length){
    /* R6-7 附款（Eric 2026-09-22 裁 Q4）：四料的「L* 跨幅」＝**48 格候選的實測 L* 全域跨幅**，
       不是頭尾兩支料的亮度差。這裡只**算出來回報**，段 F 的畫面才拿它寫「這組料最多做得出 N 階」。 */
    plan.spanL=qc.Lmax-qc.Lmin; plan.maxLevels=Math.max(0,Math.floor(qc.Lmax-qc.Lmin));
    if(ts) plan.toneMap={mode:'stretch', Lmin:ts.Lmin, Lmax:ts.Lmax, dark:ts.dark, light:ts.light};   // 同雙料 ladder.calib.toneMap 的形狀
  }
  const out={ rawLabels, palette, dropped, candidates:cands.length, filterStrategy: 'mode' };
  return plan ? Object.assign(out, {calib:plan}) : out;
}

/* ================= 平均色差診斷（index.html:512-520 / 458-461 的度量部） ================= */
function avgDeltaE(img, labels, palette){
  const n=img.w*img.h; let sumDE=0;
  for(let p=0;p<n;p++){
    const lab=palette[labels[p]].lab;
    const dl=img.lab[p*3]-lab[0], da=img.lab[p*3+1]-lab[1], db=img.lab[p*3+2]-lab[2];
    sumDE+=Math.sqrt(dl*dl+da*da+db*db);
  }
  return sumDE/n;
}

/* ================= 自動配色（index.html:783-878 垂直分支；DOM/renderSlots 拿掉） ================= */
function suggestSlots(img, mode){
  const tl=img.thumbLab, m=tl.length/3;
  const slotCount = mode==='quad' ? 4 : 2;
  const K=Math.min(9, slotCount*3);
  let cen=[];
  const byL=[...Array(m).keys()].sort((a,b)=>tl[a*3]-tl[b*3]);
  for(let i=0;i<K;i++){ const p=byL[Math.floor((i+0.5)/K*(m-1))];
    cen.push([tl[p*3],tl[p*3+1],tl[p*3+2]]); }
  let cnt=new Array(K).fill(0);
  for(let it=0;it<14;it++){
    const sum=cen.map(()=>[0,0,0,0]);
    for(let p=0;p<m;p++){
      let bi=0,bd=1e9;
      for(let i=0;i<K;i++){ const c=cen[i];
        const d=(tl[p*3]-c[0])**2+(tl[p*3+1]-c[1])**2+(tl[p*3+2]-c[2])**2;
        if(d<bd){bd=d;bi=i;} }
      sum[bi][0]+=tl[p*3]; sum[bi][1]+=tl[p*3+1]; sum[bi][2]+=tl[p*3+2]; sum[bi][3]++;
    }
    cen=cen.map((c,i)=> sum[i][3]? [sum[i][0]/sum[i][3],sum[i][1]/sum[i][3],sum[i][2]/sum[i][3]] : c);
    cnt=sum.map(s=>s[3]);
  }
  const clusters=cen.map((c,i)=>({lab:c,n:cnt[i]})).filter(c=>c.n>m*0.005);
  function lab2hex(lab){
    const fy=(lab[0]+16)/116, fx=fy+lab[1]/500, fz=fy-lab[2]/200;
    const inv=t=> t**3>0.008856 ? t**3 : (t-16/116)/7.787;
    const x=inv(fx)*0.95047, y=inv(fy), z=inv(fz)*1.08883;
    let r= 3.2406*x-1.5372*y-0.4986*z, g=-0.9689*x+1.8758*y+0.0415*z, b= 0.0557*x-0.2040*y+1.0570*z;
    return rgbHex(l2s(r),l2s(g),l2s(b));
  }
  if(slotCount===2){
    /* 【2026-08-15 修・Eric 裁 P0】原評分＝√n×(彩度+3)，**沒有任何明度分離項**：
       第二支料只要「面積大又有彩度」就贏，於是四種題材實測全挑錯——
         人像原照 → 米白＋淡粉 #eab6a3（ΔE 28.5，整張糊成粉紅）
         臉填滿畫面的模板風 → 兩支米白（38.17，等於白板）
         風景剪影（大面積天空）→ 兩支米白（30.40）
         黑色杜賓 → 米白＋棕 #975a30（13.95，黑狗被印成棕狗）
       共同觸發條件＝**單一顏色佔大面積**。雙料的每一階都落在兩支料的連線上，
       兩支太接近＝整張磚根本沒有對比可用。
       ⚠ 遵守 Eric 2026-08-05 裁示「**分散是最佳化目標，不是閘門**」：
          這裡加的是**評分權重**（隨明度差飽和的係數），**不是硬門檻**——
          全圖真的只有淺色時仍會回傳相對最好的那一支，不會失敗；
          使用者手動指定相近色（同色系漸層）完全不受影響，
          本函式只約束「我們主動建議的初值」。 */
    const LA=lin2lab(...hexLin('#f2f0eb'))[0];   // slotA 固定米白，取其 L
    let main=null, bs=-1;
    for(const c of clusters){
      const ch=Math.hypot(c.lab[1],c.lab[2]);
      /* sep 刻意**不設飽和上限**：雙料的每一階都在兩支料的連線上，
         第二支越遠、整條可用色階越長，所以「更暗」要能持續加分。
         實證＝黑白杜賓：黑(ΔL≈80) 與棕斑(ΔL≈50) 若都夾成 1，就只剩彩度決勝，
         棕斑贏 ⇒ 黑狗被印成棕狗（ΔE 13.96，強制米白＋黑只要 8.46）。 */
      const sep=Math.abs(c.lab[0]-LA)/100;
      /* 彩度降為次要因子，與四料分支的 `1+彩度/25` 同一把尺（原本 `彩度+3`
         讓有彩度的群大贏 11 倍，才會發生「彩度壓過明度」的誤選）。
         保留「偏好有彩度」的既有取向，只是不再讓它一票否決明度。 */
      const s=Math.sqrt(c.n)*(1+ch/25)*sep;
      if(s>bs){bs=s;main=c;}
    }
    if(main && Math.hypot(main.lab[1],main.lab[2])<8)
      main=clusters.reduce((a,b)=>a.lab[0]<b.lab[0]?a:b);
    return [ {color:'#f2f0eb', td:6, start:1}, {color:lab2hex(main.lab), td:2, start:1} ];
  }
  /* 四料：白底＋三支從分群直接取的彩色。
     【2026-08-05 建議配色核心規格・Eric 裁「白＋不得複製充數」】
     舊法＝色相家族（index.html:812-831）→ 取前三大家族的 deep（最深那顆），不足三族就
       `while(top.length<3) top.push(top[top.length-1]);`
     兩個缺陷，都有實測（quad／400×400mm／格點 3200²／0.125mm 格）：
       ①**複製充數**：人物照通常只有「膚色」「消色」兩族 ⇒ 第 3、4 槽吐出逐字元相同的 hex
         （#2e231c ×2）＝四料當三料用，色數只有 19、整張灰。
       ②**永遠取 deep**＝四支全擠在暗端，中間調毫無代表。
     手動換成「白＋中間膚色＋深棕＋近黑」（互異且有中間調）⇒ 色數 36、ΔE 6.789→6.251，
     效益是調 K（8→14 只換到 0.044）的約 12 倍。
     新法＝直接在**分群層**做最遠點取樣（farthest-point）：
       種子＝權重最大的群（沿用原 w＝n×(1+彩度/25)，保留「偏好有彩度」的既有取向）；
       之後每次挑「離已選集合最遠」的群，距離用 Lab 歐氏＝與量化端指派同一把尺
       （engine.js 的最近鄰與 avgDeltaE 都是 Lab 歐氏，規格的尺與量化的尺必須同一把）。
     一招解兩病：不同的群天生不會給出同一個顏色；最遠點會自動把中間調納進來。
     ⚠ **分散是最佳化目標，不是閘門**（Eric 0805 追加澄清）——本函式不設「任兩支必須差多少」
       的硬門檻，使用者要刻意選兩支相近色（例如同色系漸層）照樣可以。本規格只約束
       「我們主動建議時給的初值」，不約束使用者。 */
  let pool=clusters.filter(c=>c.lab[0]<86);
  if(!pool.length) pool=clusters;
  const picks=[];
  if(pool.length){
    let seed=pool[0], sw=-1;
    for(const c of pool){
      const w=c.n*(1+Math.hypot(c.lab[1],c.lab[2])/25);
      if(w>sw){ sw=w; seed=c; }
    }
    picks.push(seed);
    while(picks.length<3 && picks.length<pool.length){
      let best=null, bd=-1;
      for(const c of pool){
        if(picks.indexOf(c)>=0) continue;
        let md=1e9;
        for(const p of picks){
          const d=Math.hypot(c.lab[0]-p.lab[0], c.lab[1]-p.lab[1], c.lab[2]-p.lab[2]);
          if(d<md) md=d;
        }
        if(md>bd){ bd=md; best=c; }
      }
      if(!best) break;
      picks.push(best);
    }
  }
  /* 群數不足三（極單調的圖）：**不複製**——沿用色相、把亮度推到還沒被佔用的一側，
     產生一支真的不一樣的補色。這是誠實的退化（該支確實不是從圖上萃取的），不是拿同一支充數。 */
  const labs=picks.map(c=>c.lab.slice());
  while(labs.length<3){
    const base=labs.length ? labs[labs.length-1] : [50,0,0];
    const used=labs.length ? labs.map(l=>l[0]) : [50];
    const L=(Math.max.apply(null,used)<50) ? Math.min(85, Math.max.apply(null,used)+28)
                                           : Math.max(12, Math.min.apply(null,used)-28);
    labs.push([L, base[1], base[2]]);
  }
  const slots=[{color:'#f2f0eb', td:6, start:1}];
  for(let i=0;i<3;i++) slots.push({color:lab2hex(labs[i]), td:2, start:1});
  /* 最後一道保險：不論如何都不得吐出兩支逐字元相同的 hex（不同的 Lab 也可能捨入到同一個
     hex）。真撞到就把亮度再推開，最多試 8 次（有界，避免病態圖造成無窮迴圈）。 */
  for(let i=1;i<slots.length;i++){
    let guard=0;
    while(guard++<8 && slots.slice(1,i).some(function(s){ return s.color===slots[i].color; })){
      labs[i-1][0]=Math.max(8, Math.min(92, labs[i-1][0] + (labs[i-1][0]>50 ? -9 : 9)));
      slots[i].color=lab2hex(labs[i-1]);
    }
  }
  return slots;
}

/* ================= ZIP（index.html:1049-1094 verbatim） ================= */
const CRC_TABLE=(()=>{ const t=new Uint32Array(256);
  for(let i=0;i<256;i++){ let c=i; for(let k=0;k<8;k++) c=(c&1)?(0xEDB88320^(c>>>1)):(c>>>1); t[i]=c; }
  return t; })();
function crc32(u8){ let c=0xFFFFFFFF;
  for(let i=0;i<u8.length;i++) c=CRC_TABLE[(c^u8[i])&0xFF]^(c>>>8);
  return (c^0xFFFFFFFF)>>>0; }
async function deflateRaw(u8){
  if(typeof CompressionStream==='undefined') return null;
  const cs=new CompressionStream('deflate-raw');
  return new Uint8Array(await new Response(new Blob([u8]).stream().pipeThrough(cs)).arrayBuffer());
}
/* 分段 CRC：與 crc32() 同一張表、同一條公式，只是允許跨多段累積——
   為了讓 entry 的內容可以是「字串陣列」而不必先合成單一巨串（V8 字串上限 ~512MB；
   2026-08-03 閘門③ 48Mpx×fullgrid 案實撞「Invalid string length」＝model XML 過大）。 */
function crc32Pieces(pieces){
  let c=0xFFFFFFFF;
  for(const u8 of pieces)
    for(let i=0;i<u8.length;i++) c=CRC_TABLE[(c^u8[i])&0xFF]^(c>>>8);
  return (c^0xFFFFFFFF)>>>0;
}
async function deflateRawPieces(pieces){
  if(typeof CompressionStream==='undefined') return null;
  const cs=new CompressionStream('deflate-raw');
  // Blob 接受分段來源＝全程不存在單一 JS 巨串/巨陣列的合成點
  return new Uint8Array(await new Response(new Blob(pieces).stream().pipeThrough(cs)).arrayBuffer());
}
async function makeZip(entries, tick){
  /* tick（可選）＝每個 entry 壓縮前讓步＋看一次取消旗標（C-1 #11 後半，Eric 裁 C）。
     只在 entry 邊界讓步、完全不動 deflate 本身 ⇒ 輸出位元組與原版逐位元相同。
     e.data 可為：字串｜Uint8Array｜**字串/Uint8Array 陣列**（大 XML 分段用；位元組等價）。 */
  const enc=new TextEncoder(); const chunks=[]; const central=[]; let offset=0;
  for(const e of entries){
    if(tick) await tick();
    const nameB=enc.encode(e.name);
    const raw = Array.isArray(e.data) ? e.data : [e.data];
    const pieces = raw.map(p => typeof p==='string' ? enc.encode(p) : p);
    let rlen=0; for(const p of pieces) rlen+=p.length;
    const crc=crc32Pieces(pieces);
    let comp=await deflateRawPieces(pieces), method=8;
    let stored=null;
    /* ⚠ fallback 必須把 comp 清掉（Codex 二輪 B2）：0803 首版只設 stored/method、comp 留著
       ⇒ 下面 clen 取到 deflate 長度、chunks 塞進 deflate bytes，但 header 寫 method=0
       ＝自相矛盾的壞 ZIP。原版 index.html 的 `comp=data` 重指派本來是安全的，是我改壞的。
       黃金五輪沒炸純因現有 entries 全可壓縮、這條路從未走到——高熵內嵌圖（JPEG/亂數 PNG）就會中。 */
    if(!comp || comp.length>=rlen){ stored=pieces; method=0; comp=null; }
    const clen = comp ? comp.length : rlen;
    const lh=new DataView(new ArrayBuffer(30));
    lh.setUint32(0,0x04034b50,true); lh.setUint16(4,20,true);
    lh.setUint16(8,method,true);
    lh.setUint32(14,crc,true); lh.setUint32(18,clen,true); lh.setUint32(22,rlen,true);
    lh.setUint16(26,nameB.length,true);
    chunks.push(new Uint8Array(lh.buffer),nameB);
    if(comp) chunks.push(comp); else chunks.push(...stored);
    central.push({nameB,crc,clen,rlen,method,offset});
    offset+=30+nameB.length+clen;
  }
  const cdStart=offset;
  for(const c of central){
    const ch=new DataView(new ArrayBuffer(46));
    ch.setUint32(0,0x02014b50,true); ch.setUint16(4,20,true); ch.setUint16(6,20,true);
    ch.setUint16(10,c.method,true);
    ch.setUint32(16,c.crc,true); ch.setUint32(20,c.clen,true); ch.setUint32(24,c.rlen,true);
    ch.setUint16(28,c.nameB.length,true);
    ch.setUint32(42,c.offset,true);
    chunks.push(new Uint8Array(ch.buffer),c.nameB);
    offset+=46+c.nameB.length;
  }
  const eo=new DataView(new ArrayBuffer(22));
  eo.setUint32(0,0x06054b50,true);
  eo.setUint16(8,central.length,true); eo.setUint16(10,central.length,true);
  eo.setUint32(12,offset-cdStart,true); eo.setUint32(16,cdStart,true);
  chunks.push(new Uint8Array(eo.buffer));
  return new Blob(chunks,{type:'model/3mf'});
}
function xmlEsc(s){ return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

/* ================= 3MF 組裝（index.html:1096-1257 verbatim；params→P、slots 注入） =================
   【C-1 #11 後半・Eric 2026-08-02 裁 C＝只修 mesh 段】實測取消落在本段要等 17.6 秒
   （最重合法案；cancellat_report_20260802.json）——本段原本從頭到尾零讓步點。
   修法與 quantizeQuad 同款：hooks.tick＝回報進度＋讓步＋檢查取消。讓步只加在
   「零件迴圈每 4 件」與「zip 每個 entry 之間」，**迴圈順序、算式、字串組裝完全不變**
   ⇒ 輸出位元組逐位元相同（黃金閘門複驗把關）。hooks 缺席＝行為與原版完全一致。 */
/* WT 線：循環塔開啟時寫進 3MF 物件層的六個切片器鍵（bbs_3mf 匯入時進 ModelObject::config → PrintObjectConfig）。 */
/* 🔴 §12 R12-8（Eric 2026-09-20 裁）：塔圈數＝**實際色數**＋1，不是色階數＋1；K=2 照 R12-4 維持 4 圈。
   一條式子兩個呼叫端：本引擎餵「真的切出來的零件數」、工作室餵「預覽用到的色數」——**式子只有這一份**。 */
function lapsForColours(n){ const k=Math.max(2, n|0); return k===2 ? 4 : k+1; }

/* 🔴 laps 以「真的切出來的零件數」為準，不吃 UI 送來的字串（R12-8）。
   為什麼不能信 UI：工作室是在**預覽解析度**上算色數的，預覽少算一色就會寫出「圈數 < 色數＋1」，
   而 C++ 端 build_split_roles() 遇到圈數不夠分會讓 split_active()=false ⇒ **靜默**退回「層首一趯整塔」＝白塊回來。
   四料照 R12-3「不動」，維持吃 UI 的手動兩欄。 */
function cycleObjectMeta(P, actualColours){
  const laps = (P.mode!=='quad' && actualColours>=2)
    ? `1,${lapsForColours(actualColours)-1}`        /* 雙料＝最內 1 圈深色、其餘全給最淺色（同 cycleDefaultLaps） */
    : P.cycle.laps;
  const rows = [['ping_pt_cycle','1'], ['ping_pt_cycle_mode',P.mode], ['ping_pt_cycle_laps',laps],
                ['ping_pt_cycle_size',String(P.cycle.sizeMm)], ['ping_pt_cycle_gap',String(P.cycle.gapMm)], ['ping_pt_cycle_brim',String(P.cycle.brimMm)]];
  return rows.map(([k,v])=>`    <metadata key="${k}" value="${xmlEsc(v)}"/>`).join('\n') + '\n';
}

async function build3mfFrom(P, img, labels, palette, noiseStats, extras, hooks){
  if (!root.PhotoTileMesh) throw new EngineError(ERR.MESH_MODULE_MISSING, '連通網格模組未載入');
  const mode=P.mode, noiseMm=P.noiseMm;
  const w=img.w,h=img.h;
  let L=new Uint8Array(labels);
  let teethFlips=0;
  if(P.teeth){
    const O=new Uint8Array(L);
    for(let y=1;y<h;y+=2)
      for(let x=1;x<w;x++)
        if(L[y*w+x-1]!==L[y*w+x]){ O[y*w+x]=L[y*w+x-1]; teethFlips++; }
    L=O;
  }
  const sx=P.width/w, sz=P.height/h, T=P.thick;
  const collected=root.PhotoTileMesh.collectParts(L,w,h,palette.length);
  const parts=collected.parts;
  let totalTiles=0,totalVertices=0,totalTriangles=0;
  const TRI=[[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,1,5],[0,5,4],[3,6,2],[3,7,6],[0,7,3],[0,4,7],[1,2,6],[1,6,5]];
  const f=v=>Math.round(v*1000)/1000;
  const objXml=[]; const cfgParts=[]; const palLines=[];
  const meshStats=[];
  /* V 溝已移除（Eric 2026-07-29 裁）；溝參數傳 null＝notchOn=false 原始碼路（index.html:1128-1133）
     forEach→for：迴圈體一字未動，只為了能在零件之間 await（forEach 的 callback 不能 await）。 */
  for(let pi=0;pi<parts.length;pi++){
    const part=parts[pi];
    const mesh=root.PhotoTileMesh.buildLabelMesh(L,w,h,part.k,part.runs,sx,sz,T,null);
    const V=mesh.vertices.map(v=>`<vertex x="${v[0]}" y="${v[1]}" z="${v[2]}"/>`);
    const VX=mesh.vertices;
    const OUTER_TOL=Math.max(0.01,sx/2);
    const F=mesh.triangles.map(t=>{
      const x0=VX[t[0]][0];
      if(Math.abs(x0-VX[t[1]][0])<1e-9 && Math.abs(x0-VX[t[2]][0])<1e-9){
        if(P.p2aBlock && (x0<OUTER_TOL || x0>P.width-OUTER_TOL))
          return `<triangle v1="${t[0]}" v2="${t[1]}" v3="${t[2]}"/>`;
        return `<triangle v1="${t[0]}" v2="${t[1]}" v3="${t[2]}" paint_seam="4"/>`;
      }
      const y0=VX[t[0]][1];
      const blk=(P.p2aBlock && Math.abs(y0-VX[t[1]][1])<1e-9 && Math.abs(y0-VX[t[2]][1])<1e-9) ? ' paint_seam="8"' : '';
      return `<triangle v1="${t[0]}" v2="${t[1]}" v3="${t[2]}"${blk}/>`;
    });
    totalTiles+=mesh.tiles; totalVertices+=V.length; totalTriangles+=F.length;
    meshStats.push({label:part.k,components:mesh.components,tiles:mesh.tiles,vertices:V.length,triangles:F.length});
    const oid=pi+1;
    objXml.push(`<object id="${oid}" type="model"><mesh><vertices>${V.join('')}</vertices><triangles>${F.join('')}</triangles></mesh></object>`);
    const pal=palette[part.k];
    const previewColor=rgbHex(...pal.rgb).toUpperCase();
    const name = mode==='quad'
      ? `零件色${pi+1} ${previewColor} A${pal.w[0]} B${pal.w[1]} C${pal.w[2]} D${pal.w[3]}`
      : `零件色${pi+1} ${previewColor} S${Math.round((1-pal.t)*100)/100}`;
    cfgParts.push(
      `    <part id="${oid}" subtype="normal_part">\n`+
      `      <metadata key="name" value="${xmlEsc(name)}"/>\n`+
      `      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n`+
      `      <metadata key="extruder" value="${pi+1}"/>\n`+
      `    </part>`);
    palLines.push( mode==='quad'
      ? `extruder ${pi+1} color ${previewColor} = M6052 A${pal.w[0]} B${pal.w[1]} C${pal.w[2]} D${pal.w[3]}`
      : `extruder ${pi+1} color ${previewColor} = M6051 S${Math.round((1-pal.t)*100)/100}`);
    // 每 4 件讓步一次：K48 重案 ~250 件⇒~60 個讓步點；dual K6 只有 6 件⇒開銷趨近零
    if(hooks && hooks.tick && (pi&3)===3) await hooks.tick(0.05+0.75*(pi+1)/parts.length);
  }
  let pillarOid=0;
  if(P.pillar){
    const side=P.pillarXY, gap=15;
    pillarOid=parts.length+1;
    const X0=f(P.width+gap), X1=f(P.width+gap+side);
    const Y0=f(P.thick/2-side/2), Y1=f(P.thick/2+side/2);
    const Z0=0, Z1=f(P.height);
    const pv=[[X0,Y0,Z0],[X1,Y0,Z0],[X1,Y1,Z0],[X0,Y1,Z0],[X0,Y0,Z1],[X1,Y0,Z1],[X1,Y1,Z1],[X0,Y1,Z1]];
    const V=pv.map(v=>`<vertex x="${v[0]}" y="${v[1]}" z="${v[2]}"/>`).join('');
    const F=TRI.map(t=>`<triangle v1="${t[0]}" v2="${t[1]}" v3="${t[2]}"/>`).join('');
    objXml.push(`<object id="${pillarOid}" type="model"><mesh><vertices>${V}</vertices><triangles>${F}</triangles></mesh></object>`);
    const sourceLin=P.slots.slice(0,mode==='quad'?4:2).map(s=>hexLin(s.color));
    const pillarLin=[0,1,2].map(ch=>sourceLin.reduce((sum,c)=>sum+c[ch],0)/sourceLin.length);
    const pillarColor=rgbHex(...pillarLin.map(l2s)).toUpperCase();
    const pname = mode==='quad' ? `洗料柱 ${pillarColor} A25 B25 C25 D25` : `洗料柱 ${pillarColor} S0.5`;
    cfgParts.push(
      `    <part id="${pillarOid}" subtype="normal_part">\n`+
      `      <metadata key="name" value="${xmlEsc(pname)}"/>\n`+
      `      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n`+
      `      <metadata key="extruder" value="${pillarOid}"/>\n`+
      /* 洗料柱改空心方管（Eric 2026-09-02 令：「改成 20mm 正方、不要實心填充、厚度 2mm」）。
         起因＝他實測一顆 87.77 g 的成品裡，柱子就佔 45.95 g（52%）。
         🔴 不改 mesh、只改 part 覆蓋值：牆數＝2mm÷口徑（0.4→5 圈、0.6→3、1.0→2），填充 0%、上下殼 0
            ⇒ 就是一根 20×20 的方管。改 mesh 要多 8 頂點 20 三角面，且方管內壁法線寫反就成破面，沒必要冒這個險。
         ⚠ **沖刷量降到原本的 64%**（實心 15×15＝225 mm²/層 → 空心 20×20 壁厚 2＝144 mm²/層）。
            洗料柱的用途就是把上一個混比的殘料擠掉，量變少＝可能洗不乾淨。這是 Eric 知情下的取捨；
            實印若出現串色，第一個要調回來的就是這裡。 */
      `      <metadata key="sparse_infill_density" value="0%"/>\n`+
      `      <metadata key="wall_loops" value="${Math.max(1, Math.round(2.0/(P.nozzle||0.4)))}"/>
`+
      `      <metadata key="top_shell_layers" value="0"/>
`+
      `      <metadata key="bottom_shell_layers" value="0"/>
`+
      `    </part>`);
    palLines.push( mode==='quad'
      ? `extruder ${pillarOid} color ${pillarColor} = M6052 A25 B25 C25 D25（洗料柱 ${side}×${side}mm 空心方管・壁厚 2mm）`
      : `extruder ${pillarOid} color ${pillarColor} = M6051 S0.5（洗料柱 ${side}×${side}mm 空心方管・壁厚 2mm）`);
  }
  const MID=1000;
  const nObjs=parts.length+(pillarOid?1:0);
  const comps=Array.from({length:nObjs},(_,i)=>`<component objectid="${i+1}" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>`).join('');
  /* 【2026-08-03】model XML 改「分段」而不合成單一字串：48Mpx×fullgrid 案的 XML 逼近
     V8 字串上限（~512MB），昨天 K48 險過、今天 K8 實撞「Invalid string length」＝
     這個案一直在懸崖邊滑冰。分段序列與原本的樣板字串**逐位元組等價**：
     原式＝objXml.map(o=>'  '+o).join('\n') 後接樣板換行 ⇒ 每個 objXml 項恰好
     「兩空格＋內容＋\n」一次；黃金閘門把關等價性。 */
  const modelPieces=[
`<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
 <metadata name="Application">PING-PhotoTile-Prototype</metadata>
 <resources>
`];
  for(const o of objXml) modelPieces.push('  '+o+'\n');
  modelPieces.push(
`  <object id="${MID}" type="model"><components>${comps}</components></object>
 </resources>
 <build><item objectid="${MID}" transform="1 0 0 0 1 0 0 0 1 ${f(-P.width/2)} ${f(-P.thick/2)} 0" printable="1"/></build>
</model>`);
  const cfg=
`<?xml version="1.0" encoding="UTF-8"?>
<config>
  <object id="${MID}">
    <metadata key="name" value="${xmlEsc('照片磚')}"/>
${P.cycle && P.cycle.enabled ? cycleObjectMeta(P, parts.length) : ''}${cfgParts.join('\n')}
  </object>
</config>`;
  const rels=`<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>`;
  const types=`<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
 <Default Extension="config" ContentType="text/xml"/>
 <Default Extension="txt" ContentType="text/plain"/>
</Types>`;
  const palTxt=
`PING 照片磚 垂直多零件 配比表（${mode==='quad'?'四料 M6052':'雙料 M6051'}）
尺寸 ${P.width}x${P.height}x${P.thick} mm｜零件數 ${parts.length}${pillarOid?'＋洗料柱1':''}｜連通區 ${collected.components}｜網格片 ${totalTiles}｜三角形 ${totalTriangles}
口徑 ${P.nozzle} mm（色塊左右寬 ≥ ${noiseStats?noiseStats.minWidthMm.toFixed(1):(2*P.nozzle).toFixed(1)} mm、上下高 ≥ 一層 ${noiseStats?noiseStats.minHeightMm:'?'} mm，窄條已併入顏色最近的鄰色）
接縫：端面 paint_seam 標記（交界／側邊窄面）——縫拉往零件交界；接縫位置請用「對齊」（V 溝已於 2026-07-29 移除）；P2A 正背面禁縫=${P.p2aBlock?'開':'關'}
雜訊濾除 ${noiseMm>0?`${noiseMm.toFixed(1)} mm，已平滑／合併 ${noiseStats?noiseStats.changedPixels:0} 格（不留空洞）`:'關閉（左右寬、上下高照樣守）'}
${palLines.join('\n')}`;
  const entries=[
    {name:'[Content_Types].xml', data:types},
    {name:'_rels/.rels', data:rels},
    {name:'3D/3dmodel.model', data:modelPieces},   // 分段（位元組等價，見上）
    {name:'Metadata/model_settings.config', data:cfg},
    {name:'Metadata/ping_palette.txt', data:palTxt},
  ];
  /* C-1 metadata＝opt-in（extras 缺席時本陣列與 C-0 spike／工作室完全相同
     ⇒ 3MF 位元組全等、黃金 oracle 續用）。schema 說明＝tools/ping/phototile_protocol.md §4 */
  if (extras && extras.metadataJson) {
    entries.push({name:'Metadata/ping_phototile.json', data:extras.metadataJson});
    if (extras.sourceImage && extras.sourceImage.bytes && extras.sourceImage.name)
      entries.push({name:'Metadata/'+extras.sourceImage.name, data:extras.sourceImage.bytes});
  }
  const blob=await makeZip(entries, hooks && hooks.tick ? (()=>hooks.tick(0.9)) : null);
  return {blob, parts:parts.length, components:collected.components, tiles:totalTiles,
          vertices:totalVertices, triangles:totalTriangles, pillar:!!pillarOid, extruders:nObjs,
          meshStats, teethFlips};
}

/* ================= generate(request) → Promise<result> ================= */
let jobSeq = 0;
const activeJobs = new Map();

/* 階段間讓步＝MessageChannel macrotask（非 setTimeout＝不受隱形節流影響）：
   讓宿主 postMessage 送達的 cancel 能在階段邊界被觀察到（F-07 修正）。 */
const yieldMacro = () => new Promise(r => {
  const c = new MessageChannel();
  c.port1.onmessage = () => { c.port1.close(); r(); };
  c.port2.postMessage(0);
});

function cancel(jobId){
  const job = activeJobs.get(jobId);
  if (job) { job.cancelled = true; return true; }
  return false;
}

/* ================= palette DTO＝result 與 3MF metadata 的單一來源 ================= */
function paletteDto(palette, mode){
  return palette.map((pal,i)=> mode==='quad'
    ? { index:i+1, hex:rgbHex(...pal.rgb).toUpperCase(), recipe:{ gcode:'M6052', A:pal.w[0], B:pal.w[1], C:pal.w[2], D:pal.w[3] } }
    : { index:i+1, hex:rgbHex(...pal.rgb).toUpperCase(), recipe:{ gcode:'M6051', S:Math.round((1-pal.t)*100)/100 } });
}

/* SHA-256：優先用 crypto.subtle（快），沒有就用協定模組的純 JS 版。
   ⚠ **不得回傳 null**——宿主端把「沒有 digest」當協定錯誤處理（四項驗證不可有空門）。
   file:// 之類環境是否具備 subtle 因 runtime 而異，所以這裡不賭環境。 */
async function sha256Hex(bytes){
  if (root.crypto && root.crypto.subtle) {
    try {
      const d = await root.crypto.subtle.digest('SHA-256', bytes);
      return [...new Uint8Array(d)].map(b=>b.toString(16).padStart(2,'0')).join('');
    } catch (e) { /* 落到後備 */ }
  }
  if (root.PhotoTileProtocol && root.PhotoTileProtocol.sha256HexSync)
    return root.PhotoTileProtocol.sha256HexSync(bytes);
  throw new EngineError(ERR.INTERNAL, 'SHA-256 不可用（crypto.subtle 與後備實作都缺席）');
}
function base64ToBytes(b64){
  const bin = atob(b64); const out = new Uint8Array(bin.length);
  for (let i=0;i<bin.length;i++) out[i] = bin.charCodeAt(i);
  return out;
}
const MIME_EXT = { 'image/png':'png', 'image/jpeg':'jpg', 'image/jpg':'jpg', 'image/webp':'webp', 'image/bmp':'bmp' };

/* ================= 最小 metadata schema（C 案裁決 6＝嵌原圖、save/reopen 續調） =================
   寫進 3MF 的 Metadata/ping_phototile.json；欄位契約＝tools/ping/phototile_protocol.md §4。
   只有 request.metadata 存在時才產生（opt-in＝黃金比對保命索）。 */
const METADATA_SCHEMA = 1;
async function buildExtras(P, q, filtered, palette, source, img){
  const md = P.metadata || {};
  const embedSource = md.embedSource !== false;               // 預設嵌原圖（裁決 6）
  let sourceImage = null, sourceMeta = null;
  if (source && source.base64) {
    const bytes = base64ToBytes(source.base64);
    const ext = MIME_EXT[source.mime] || 'bin';
    sourceMeta = { mime: source.mime, byteLength: bytes.length,
                   sha256: await sha256Hex(bytes), name: source.name || null,
                   embedded: embedSource, entry: embedSource ? ('Metadata/ping_phototile_source.'+ext) : null };
    if (embedSource) sourceImage = { name: 'ping_phototile_source.'+ext, bytes };
  } else {
    sourceMeta = { mime:null, byteLength:0, sha256:null, name:null, embedded:false, entry:null,
                   note:'呼叫端傳入已解碼影像（無原始位元組）＝不可嵌入' };
  }
  const metadata = {
    schema: METADATA_SCHEMA,
    engine: ENGINE_VERSION,
    groupUuid: md.groupUuid || null,                          // 物件身分（C-2 原子替換用；由 C++ 產生）
    createdBy: md.createdBy || 'PING-PhotoTile',
    mode: P.mode,
    nozzle: P.nozzle,
    canonical: { widthMm: P.width, heightMm: P.height, thickMm: P.thick },  // 引擎產出的幾何尺寸
    params: { klevels: P.klevels, noiseMm: P.noiseMm,
              pillar: { enabled: P.pillar, xyMm: P.pillarXY },
              cycle: P.cycle,                                          // WT 線：{enabled} 或 {enabled,laps,sizeMm,gapMm,brimMm}（schema 仍 1，additive）
              seam: { teeth: P.teeth, p2aBlock: P.p2aBlock },
              limits: P.limits,
              ...(q.calib ? { calib: { applied: !!q.calib.applied, matches: !!q.calib.matches, why: q.calib.why||'', span: q.calib.span } } : {}) },   // 0914 additive
    slots: P.slots,
    palette,                                                  // index → hex → M6051/M6052 配比
    sourceImage: sourceMeta,
    env: P.env,                                               // 生成當下的環境快照（供追溯，不是驗證來源）
    stats: { gridW: img ? img.w : null, gridH: img ? img.h : null,
             dropped: q.dropped || 0, candidates: q.candidates || 0, noise: filtered.stats }
  };
  return { metadata, metadataJson: JSON.stringify(metadata, null, 2), sourceImage };
}

/* 階段權重（C-0 §4.1/§4.2 實測時間佔比；只用來換算對使用者顯示的百分比） */
const STAGE_WEIGHT = { decode:0.02, grid:0.10, suggest:0.03, quantize:0.45, filter:0.25, metric:0.03, mesh:0.12 };
const STAGE_ORDER  = ['decode','grid','suggest','quantize','filter','metric','mesh'];
const STAGE_LABEL  = { decode:'解碼影像', grid:'建立格點', suggest:'自動配色', quantize:'色階量化',
                       filter:'雜訊濾除與最小寬度', metric:'色差評估', mesh:'產生網格與 3MF' };
function stageBase(stage){
  let acc = 0;
  for (const s of STAGE_ORDER) { if (s === stage) break; acc += STAGE_WEIGHT[s]; }
  return acc;
}

async function generate(request, options){
  const t0 = performance.now();
  let P;
  try { P = normalizeRequest(request); }
  catch (e) {
    return { jobId: request && request.jobId || null, ok:false,
             error:{ code: e.code || ERR.BAD_REQUEST, message: e.message } };
  }
  const job = { cancelled:false };
  activeJobs.set(P.jobId, job);
  const timings = {};
  /* 引擎自己解碼出來的 ImageBitmap 一定要關，**不管這個 job 怎麼結束**。
     原本只在成功路徑的 grid 之後關（見下方 own 註解），取消或任何階段丟例外都會漏掉——
     48M 像素的圖漏一張就是幾百 MB，連續取消幾次就爆（Codex 重要 #11）。
     用 finally 兜底：ownedBitmap 在 resolveImage 之後登記，關過就清掉不重複關。 */
  let ownedBitmap = null;
  const releaseOwnedBitmap = () => {
    if (ownedBitmap && typeof ownedBitmap.close === 'function') {
      try { ownedBitmap.close(); } catch (e) { /* 已關過／不支援：忽略 */ }
    }
    ownedBitmap = null;
  };
  const ck = stage => { if (job.cancelled) throw new EngineError(ERR.CANCELLED, '已取消（' + stage + '）'); };
  /* 進度回報：宿主給 onProgress 才回報；回報本身不得引入計時器（§零計時器紀律） */
  const onProgress = options && typeof options.onProgress === 'function' ? options.onProgress : null;
  const report = (stage, frac) => {
    if (!onProgress) return;
    const pct = Math.max(0, Math.min(1, stageBase(stage) + STAGE_WEIGHT[stage] * Math.max(0, Math.min(1, frac))));
    try { onProgress({ jobId: P.jobId, stage, stageLabel: STAGE_LABEL[stage], pct,
                       elapsedMs: performance.now() - t0 }); } catch (e) { /* 進度回報失敗不影響生成 */ }
  };
  /* 階段內的分塊鉤：回報 → 讓步（MessageChannel）→ 檢查取消 */
  const hooksFor = stage => ({ tick: async frac => { report(stage, frac); await yieldMacro(); ck(stage); } });
  try {
    if (!root.PhotoTileMesh) throw new EngineError(ERR.MESH_MODULE_MISSING, '連通網格模組未載入');
    let t = performance.now();
    report('decode', 0);
    const resolved = await resolveImage(P.image, P.limits);
    const bitmap = resolved.bitmap;
    if (resolved.owned) ownedBitmap = bitmap;      // 從這一刻起，finally 保證會關
    timings.decodeMs = performance.now() - t;  report('decode', 1); await yieldMacro(); ck('decode');

    t = performance.now();
    const img = buildGridData(bitmap, P.width, P.height, P.limits.gridMax);
    /* 格點建完，解碼後的點陣圖就沒用了。**引擎自己解碼的才關**（呼叫端傳進來的
       ImageBitmap 屬於呼叫端，關掉會害它下一案沒圖——黃金 runner 就是重複使用同一張）。
       高像素輸入時這一關就是幾百 MB 的差別（C-0 §4.3：真壓力在 decoded bitmap）。 */
    releaseOwnedBitmap();
    timings.gridMs = performance.now() - t;    report('grid', 1); await yieldMacro(); ck('grid');

    if (!P.slots) P.slots = suggestSlots(img, P.mode);          // 自動配色（工作室「開圖即建議」等價）
    report('suggest', 1); await yieldMacro(); ck('suggest');

    t = performance.now();
    const q = P.mode === 'quad' ? await quantizeQuad(img, P, P.slots, hooksFor('quantize'))
                                : await quantizeDual(img, P, P.slots, hooksFor('quantize'));
    timings.quantizeMs = performance.now() - t; report('quantize', 1); await yieldMacro(); ck('quantize');

    t = performance.now();
    const filtered = filterLabels(q.rawLabels, img, P, q.palette.length, q.filterStrategy, q.palette);
    timings.filterMs = performance.now() - t;  report('filter', 1); await yieldMacro(); ck('filter');

    const deltaE = avgDeltaE(img, filtered.labels, q.palette);
    report('metric', 1); await yieldMacro(); ck('metric');

    const palette = paletteDto(q.palette, P.mode);
    const extras = P.metadata ? await buildExtras(P, q, filtered, palette, resolved.source, img) : null;

    t = performance.now();
    report('mesh', 0);
    // hooksFor('mesh')＝零件間與 zip entry 間可取消（Eric 2026-08-02 裁 C；#11 後半）
    const built = await build3mfFrom(P, img, filtered.labels, q.palette, filtered.stats, extras, hooksFor('mesh'));
    timings.meshZipMs = performance.now() - t; report('mesh', 1); ck('mesh');

    const bytes = new Uint8Array(await built.blob.arrayBuffer());
    ck('mesh');   // 二輪 I6：arrayBuffer 的 await 是 zip 後第一個讓步點，取消要在這裡被看到
    timings.totalMs = performance.now() - t0;

    return {
      jobId: P.jobId, ok: true,
      env: P.env,                                  // 環境快照原封回傳（C++ 比對過期即棄）
      metadata: extras ? extras.metadata : null,   // 與 3MF 內 ping_phototile.json 同源
      limits: P.limits,
      fileName: '照片磚_連通網格版.3mf',
      threeMF: bytes, blob: built.blob, byteLength: bytes.length,
      mode: P.mode, nozzle: P.nozzle,
      palette,
      slots: P.slots,
      stats: { gridW: img.w, gridH: img.h, parts: built.parts, components: built.components,
               tiles: built.tiles, vertices: built.vertices, triangles: built.triangles,
               extruders: built.extruders, pillar: built.pillar,
               dropped: q.dropped || 0, candidates: q.candidates || 0 },
      diagnostics: Object.assign({ noise: filtered.stats, teethFlips: built.teethFlips, avgDeltaE: deltaE,
                     clamped: P.clamped, timings, meshStats: built.meshStats },
                     q.calib ? { calib: q.calib } : {})   // 0914：校正套了沒／為什麼沒套（缺席＝與舊版同形）
    };
  } catch (e) {
    const code = e instanceof EngineError ? e.code : ERR.INTERNAL;
    return { jobId: P.jobId, ok:false, error:{ code, message: e && e.message || String(e) },
             diagnostics: { timings } };
  } finally {
    releaseOwnedBitmap();                        // 取消／例外／早退都走這裡
    activeJobs.delete(P.jobId);
  }
}

return { generate, cancel, suggestSlots, gridDims, sha256Hex, dualLadder, ERR,
         /* §12 R12-8：塔圈數的式子只有這一份，工作室的圈數欄也吃它 */
         lapsForColours,
         /* 0914 色彩校正（單一來源；頁面預覽與 3MF 生成共用） */
         calibParseTable, calibLookupLin, calibMatches, calibGenLadder, dualLadderCalibrated, toneStretch,
         buildCalibStrip, buildCalibStripParts, calibStripDefaultS, calibStripGeo,
         /* 0922 四料 48 格（R6-2 附款／R6-6／R6-14；牌 c-0922-ACC-15） */
         calibParseDual, calibParseQuad, calibQuadGeo, calibQuadSteps,
         buildCalibQuad, buildCalibQuadParts, calibQuadPairGuard, calibQuadPlan,
         /* 0923 車 3 段 F：四料候選色與映射條件（預覽與生成共用，R8-5）＋單色 L*（選料排槽位） */
         quadCandidates, quadToneStretch, hexLstar,
         /* 1001 #50 丁：模擬圖與 3MF 同一支濾除（頁面 filterVerticalLabels 改呼叫這支） */
         filterLabels,
         /* 1003 AIP 刀 1：照片磚製程層高（上下濾除＝一層；第二班的提示詞「上下最少一層」也讀這一份） */
         layerHeightMm, layerTable: () => JSON.parse(JSON.stringify(PT_LAYER_MM)),
         version: ENGINE_VERSION,
         metadataSchema: METADATA_SCHEMA,
         limitsDefault: { gridMax: GRID_MAX, maxDecodedPixels: 0 },
         _internals: { buildGridData, quantizeDual, quantizeQuad, filterLabels, build3mfFrom, paletteDto,
                       makeZip /* 二輪 B2：讓 zip fallback 可被直接測（node 高熵案） */ } };
});
