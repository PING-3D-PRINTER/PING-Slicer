# 照片磚引擎協定（C-1・v1）

> 正本。JS 端實作＝`resources/web/phototile/engine_protocol.js`（單一份，勿在 engine.html 重寫規則）；
> C++ 端＝`PhotoTileEngineHost` 逐條鏡像。改協定＝**同時改本檔＋兩端＋單元測**，並升 `PROTOCOL_VERSION`。
>
> 血統：C-0 尖峰報告 §2.1（引擎契約）／§3.1–3.3（宿主實測與規格草案）／§5.2（行為矩陣）；
> C 案計畫 §3 C-1、§5 十一項裁決（Eric 2026-07-29 全項拍板）。

## 0. 名詞與角色

| 角色 | 是誰 | 職責 |
|---|---|---|
| 宿主 host | `PhotoTileEngineHost`（C++，隱藏視窗、永不 Show） | 生命週期、下指令、驗收 3MF、環境快照比對、進度／取消對接 UI |
| 引擎頁 page | `resources/web/phototile/engine.html`（隱形） | 純運算：生成 3MF、回報進度、可取消 |

**engine-neutral**：`request`／`result` 的欄位契約與傳輸層無關。日後若走 A 案（C++ 原生移植），
同一份 DTO 直接沿用、雙路黃金基準庫（`golden_report.json`）即為現成驗收（C-0 §6 轉 A 保留）。

## 1. 訊息共通規則

- 一律 JSON；每則帶 `v`（＝`PROTOCOL_VERSION`，目前 **1**）。版本不符＝立即誠實失敗，不做相容猜測。
- host→page 用 `cmd`；page→host 用 `type`。凡與作業相關者一律帶 `jobId`。
- **一次一個 job**。生成中收到新的 `generate`＝supersede：先 `cancel` 舊 job，page 回 `superseded`，再跑新的（不排隊；C-0 §5.2 矩陣 #8）。
- **零計時器**：page 側生成路徑不得使用 setTimeout／setInterval／rAF（隱形頁被節流到 ~1s/次）。讓步一律 MessageChannel。

## 2. 生命週期

```
runtime 檢測 → 建 env（獨立 user-data）→ 建 controller（隱藏 HWND＋IsVisible=false）
 → navigate(engine.html) → 等 ready（逾時 15s＝誠實不可用）
 → 服務 job … → ProcessFailed → 重建（上限 3 次，超限＝誠實不可用）
```

- `ready` **可能早於** `NavigationCompleted` 抵達 —— 兩種順序宿主都必須正確處理（C-0 §3.1 實測）。
- runtime 缺失／建立失敗＝**誠實不可用＋指引**，不得 fallback 回工作室頁（裁決 8）。
- 重建後首個結果必須通過同一份 SHA 驗證（重建前後同輸入應同輸出；不一致＝FAIL）。

## 3. 3MF 回傳與「成功」的定義

`result`（中繼資料）→ `begin` → `chunk × N`（96KB／塊）→ `end`。

宿主**唯有四項全過才可視為 success**，任一不符＝丟棄整份並回報協定錯誤（不得半成品上盤）：

1. 分塊**連號**（`index` 必須 0,1,2,… 連續，不接受亂序或重送）
2. **塊數**符合 `begin.chunks`／`end.chunks`
3. **總長度**符合 `begin.size`
4. 組裝後 **SHA-256** 等於 `end.sha256`

## 4. 訊息表

### host → page

| cmd | 欄位 | 說明 |
|---|---|---|
| `ping` | `seq` | 心跳；page 回 `pong`（帶 `busy`） |
| `imageBegin` | `jobId,mime,name,totalChars,chunks` | 影像注入開始（base64，192K 字元／塊＝現行 `WebViewDialog.cpp:333` 值） |
| `imageChunk` | `jobId,index,base64` | 序號必須連續，否則整批作廢 |
| `imageEnd` | `jobId,totalChars,chunks` | 字元數與塊數雙驗；過＝回 `imageAck` |
| `generate` | `jobId,request{...}` | 見 §5；影像取自本 job 的注入緩衝（或 `request.image`） |
| `cancel` | `jobId` | 合作式取消；page 回 `cancelAck{existed}` |

### page → host

| type | 欄位 | 說明 |
|---|---|---|
| `ready` | `engine,protocol,metadataSchema,hasMesh,limitsDefault,ua` | 握手 |
| `pong` | `seq,busy` | |
| `progress` | `jobId,stage,stageLabel,pct,elapsedMs` | 階段權重見 §6；合法上限案 quad K8 ≈20 秒（K 上限 48→8＝Eric 2026-08-02 裁 B，引擎 clamp 2..8）＝進度與取消是必備 |
| `imageAck` | `jobId,chars,chunks,mime` | |
| `result` | `jobId,ok,…` | 成功時含 `byteLength,sha256,chunks,palette,slots,stats,metadata,env,limits,diagnostics`；失敗時含 `error{code,message}` |
| `begin`／`chunk`／`end` | 見 §3 | |
| `superseded` | `jobId,by` | 舊 job 被新輸入取代 |
| `cancelAck` | `jobId,existed` | |
| `error` | `jobId,code,message` | 協定層錯誤（碼見 §7） |

## 5. request／result（engine-neutral DTO）

`request`：`jobId`／`mode`(dual\|quad)／`nozzle`(0.4\|0.6\|1.0)／`size{widthMm,heightMm,thickMm}`／
`klevels`／`noiseMm`／`pillar{enabled,xyMm}`／`seam{teeth,p2aBlock}`／`slots`(可省＝自動配色)／
`image`(ImageBitmap｜{mime,base64}｜{width,height,rgba})／`limits{gridMax,maxDecodedPixels}`／
`metadata{groupUuid,createdBy,embedSource}`／`env{...}`。

- 夾值範圍照工作室 UI；超界＝夾住並記入 `diagnostics.clamped`（不報錯）。`mode`／`nozzle` 非法＝報錯不猜。
- **原圖比例連動與 0.1mm 進位＝呼叫端職權**（C 案流中尺寸來自物件／面板，引擎只夾值不改比例；C-0 §2.1 註）。

### 5.1 `limits`（低規機保護・OOM gate）

| 欄位 | 預設 | 用途 |
|---|---|---|
| `gridMax` | 3200（＝工作室現值） | 長邊格數上限；宿主依可用實體記憶體降階 |
| `maxDecodedPixels` | 0＝不設限 | 解碼後像素上限；超過＝`image_too_large`（現行 C++ 只有 64MB 檔案上限、解碼後無上限） |

### 5.2 `env` 環境快照——過期即棄

宿主在 `generate` 帶入（欄位由宿主定義，建議：`printerPresetName`／`nozzle`／`plateId`／`plateRevision`／`projectRevision`），
引擎**原封回傳**於 `result.env`。**上盤前**再比一次現況：不相等＝丟棄結果＋通知使用者，不得寫回新情境（Codex #7）。
比較規則＝逐鍵嚴格相等（`envEqual`），新增欄位自動納入比較。

## 6. 進度階段與權重

`decode 0.02｜grid 0.10｜suggest 0.03｜quantize 0.45｜filter 0.25｜metric 0.03｜mesh 0.12`

- 權重來自 C-0 §4.1／§4.2 實測時間佔比，只用於顯示百分比。
- `quantize` 是唯一會長到分鐘級的段，因此在**列區塊**（64 列）邊界回報並讓步；
  其餘段在段邊界回報。**迴圈順序與算式不變** ⇒ 標籤結果與工作室逐位元一致。

## 7. 錯誤碼

- 引擎層（`engine.js`）：`bad_request`／`bad_image`／`image_too_large`／`mesh_module_missing`／`cancelled`／`internal`
- 協定層（`engine_protocol.js`）：`protocol_bad_message`／`protocol_bad_version`／`protocol_unknown_cmd`／
  `protocol_chunk_order`／`protocol_chunk_count`／`protocol_length_mismatch`／`protocol_sha_mismatch`／
  `protocol_job_mismatch`／`protocol_no_image`／`protocol_stale_env`

兩層不重疊；宿主據此分流（引擎層＝告訴使用者怎麼改，協定層＝重試或誠實不可用）。

## 8. 3MF 內的 metadata（schema v1）

只有 `request.metadata` 存在時才寫入（**opt-in**）：

- `Metadata/ping_phototile.json`：`schema`／`engine`／`groupUuid`／`mode`／`nozzle`／
  `canonical{widthMm,heightMm,thickMm}`／`params{klevels,noiseMm,pillar,seam,limits}`／`slots`／
  `palette[{index,hex,recipe}]`／`sourceImage{mime,byteLength,sha256,embedded,entry}`／`env`／`stats`
- `Metadata/ping_phototile_source.<ext>`：原圖位元組（裁決 6＝嵌原圖，save/reopen 可續調）

> 🔒 **保命索**：`metadata` 缺席時，3MF 的 zip entry 清單與內容與工作室（`index.html`）**完全相同**，
> 雙路黃金 oracle（`照片磚_C0尖峰產物/goldenrunner.html`，6/6 逐 entry SHA-256 全等）因此持續有效。
> 不得把 metadata 改成預設開——那會讓黃金基準失效。

### 8.1 循環洗料塔（WT 線 2026-09-08，additive；schema 仍 1）

- request 多一欄 `cycle`：`{enabled, laps:"1,2"|"1,1,1,2", sizeMm（塔邊長 mm，工作室固定送 25；缺席或 0＝引擎補 25。Eric 2026-09-08 定，取代原「44×口徑/0.4 等比放大」）, gapMm, brimMm}`（圈數預設 Eric 2026-09-08 第 4 階段裁；原 2,4／2,2,2,4 真切塔比模型本體重 3 倍）；**缺席＝關**（3MF／請求字串與舊版位元組全等）。宿主 `PhotoTileEngineRequest::cycle*` 只在開啟時寫入。
- 開啟時引擎：①不再產舊固定比洗料柱零件 ②`Metadata/model_settings.config` 的照片磚 `<object>` 多六個 metadata 鍵 `ping_pt_cycle=1`／`ping_pt_cycle_mode`／`ping_pt_cycle_laps`／`ping_pt_cycle_size`／`ping_pt_cycle_gap`／`ping_pt_cycle_brim`（bbs_3mf 匯入→`ModelObject::config`→`PrintObjectConfig`，另存重開都在）③`ping_phototile.json.params.cycle` 記同一組值。
- 切片端：`libslic3r/GCode/PingCycleTower.*` 讀物件設定，`ToolOrdering::ping_reorder_for_cycle_tower` **模型段照亮度分數淺→深完全排序**（Eric 2026-09-09 裁「丙」：亮度＝雙料 S／四料 (A×3+B×2+C×1+D×0)/300；`stable_sort` ⇒ 同分維持原生順序；不在 palette 的槽留原位。原規則「純 E0 先其餘保序」已停用——實測 333 層只有 24 層碰巧淺→深，65 層洗白後第一段就上純黑），`GCode::ping_cycle_tower_layer` 層首插塔（真實 offset 圈、配方命令 M605x 直寫、離塔後明寫第一段模型配方）。離線對照答案＝`照片磚_循環洗料塔/cycle_core.py`。
- 🆕 第七個物件鍵 `ping_pt_cycle_max_flow`（mm³/s，預設 11；0＝不設上限）＝塔的流量上限：塔速＝`min(外牆速度, 上限 ÷ 塔截面)`，首層照舊首層速度（Eric 2026-09-30「照建議」，ORCA 開發中清單 #39；起因＝四料 0.6 塔 120 mm/s＝22 mm³/s 切白回彈）。**工作室刻意不寫這個鍵**：預設值只在 C++ 一處、舊 3MF 重切也吃得到；要試別的值就在 3MF 的照片磚 `<object>` 加一行 metadata。

### 8.2 色彩校正（0914 Q3 甲，additive；schema 仍 1；牌 c-0914-PTI-04）

- request **不多欄**：校正表掛在 **`slots[0].calib`**＝`{ table:<回讀頁匯出的校正表 JSON 原物>, apply:<bool，預設 true> }`。
  理由：①校正只綁線材（核心規格 R6-1）⇒ 資料掛在料上語意成立 ②宿主 C++（`GUI_App.cpp` `phototile_generate`）把 `slotsJson` **原字串直通**、只驗「是陣列且可解析」⇒ 不必動 C++／不必 build。
- **缺席＝整段不跑** ⇒ 3MF、請求字串、`ping_phototile.json` 與舊版**位元組全等**（黃金閘門不受影響）。
- 有表且料色相符（`calibMatches`：表的 `料[0..1].色` 逐字等於 `slots[0..1].color`，大小寫無關）：
  - ④-1（永遠）：palette／零件名的預覽 hex 改用實測曲線內插色（`calibLookupLin`），**S 不動**。
  - ④-2（`apply!==false` 且四道護欄全過）：K 階改在**實測 L\*** 上均分、從實測曲線反解 S（`calibGenLadder`，兩位小數對齊）；分箱端點 LA／LB 一起換成實測端點。
  - 護欄：①任一格通道 ≥250 ⚠ ②相鄰階 ΔE<2 ⚠ ③實測 L\* 跨幅 < K×1 ⛔ ④非單調 ⛔（⛔＝退回理論階梯）。
- 回報：`result.diagnostics.calib`＝`{present,matches,applied,why,warnOnly,span}`；`ping_phototile.json.params.calib`＝`{applied,matches,why,span}`（皆只在表存在時出現）。
- 單一來源：`engine.js` 的 `dualLadderCalibrated(slots,K,calib)`——工作室預覽（`simulateVertical`）與 3MF 生成（`quantizeDual`）都只呼叫它（R8-5「兩把尺一起換」由結構保證）。
- 校正片：`engine.js` `buildCalibStrip({hexA,hexB,s?,widthMm,thickMm,bandMm,purgeMm,purgeGapMm,purgeWalls})`＝`照片磚_色彩校正/make_calib_3mf.py --purge` 的同構 JS 版（Z 疊直立條＋**隨階換比例洗料柱**）；回讀頁 `calibration.html` 新增「雙料 8 階直立條」版面（`LAYOUT='vstrip'`，各階 S 可填）。
  - 幾何唯一來源＝`engine.js` 的 `CALIB_STRIP_GEO`（對外出 `calibStripGeo()`）＝**v2c 定案**：片 40×6×16 mm、8 階每階 2 mm、柱 12×12 mm（2 圈牆、0% 填充、無上下殼）、柱片間距 8 mm。**頁面不得再寫一份幾何數字**——v1（80×8×48、每階 6 mm、無柱、實印 32m45s）就是因為 `index.html` 自己寫一份才與 python 分叉；Eric 2026-09-14 裁「用洗料塔」＋「不要印那麼久」後 v1 已作廢。v2c 實印 **12m03s**。
  - 柱是**獨立物件**（cfg `id=2000`）且 build item 排在片（`id=1000`）**之前** ⇒ 切片器逐層先印柱、再印片；柱段與片段的 `extruder` **逐階相同**（1..8）⇒ 同一個 M6051 配方，換比例那一層的殘料吐在柱裡，片從該階第一層就是乾淨色。
  - 🔴 **整組置中**：切片器載入 3MF 會把物件整組置中（0914 v2 實切：3MF 寫柱在 X 40~55，G-code 實際落 28~42）⇒ 產生器先把「片＋柱」聯集包圍盒置中在原點；回傳 `purgeBox`（預設即 `{x0:18,y0:-6,x1:30,y1:6}`），`ping_calib.txt` 直接報 `verify_calib_gcode.py --purge-box 18,-6,30,6`。
  - `purgeMm:0` ⇒ 退回無柱幾何（**只給離線對照用**，不是產品路徑）。
  - 釘住方式：`phototile_calib_test.js` 斷言 `3dmodel.model` 的 sha256 `43062b6c…191717` 與根 repo `照片磚_色彩校正/色彩校正_白深灰_8階_v2c洗料柱_FD300_0.4.3mf` 逐位元組相同（該檔即 0914 實印那條；model 不含 title ⇒ 只釘幾何與命名）。
- 單元測：`node tools/ping/phototile_calib_test.js`（保命索＝calib 缺席時 `quantizeDual` 與理論梯逐值相同；正向 oracle＝0822 白×藍真表 → R8-5 記錄的 100/75/61/43/37/31/16/0；反向＝白×黃跨幅擋、料色不符不套、非單調擋）。

## 9. 單元測

`node tools/ping/phototile_protocol_test.js` —— 涵蓋分塊往返、四項驗證的反向測試（亂序／少塊／長度竄改／SHA 竄改／
版本錯／jobId 不符）、影像注入分塊、環境快照過期判定。**改協定必須同時補測**。
