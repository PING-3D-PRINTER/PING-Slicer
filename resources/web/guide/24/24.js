function OnInit()
{
	//let strInput=JSON.stringify(cData);
	//HandleStudio(strInput);
	
	TranslatePage();
	
	RequestProfile();
}



function RequestProfile()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_userguide_profile";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function HandleStudio( pVal )
{
//	alert(strInput);
//	alert(JSON.stringify(strInput));
//	
//	let pVal=IsJson(strInput);
//	if(pVal==null)
//	{
//		alert("Msg Format Error is not Json");
//		return;
//	}
	
	let strCmd=pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='response_userguide_profile')
	{
		HandleModelList(pVal['response']);
	}
}

function ShowPrinterThumb(pItem, strImg)
{
	$(pItem).attr('src',strImg);
	$(pItem).attr('onerror',null);
}

// PING V3.6：同一個印表機選擇頁分 Fast／Classic，避免再增加精靈步驟。
const PingClassicModels = new Set([
	'EDU 200', 'PING 200', 'PING 270', 'PING 300+',
	'DUAL 300', 'DUAL 450', 'DUAL 600', 'DUAL 800'
]);
let PingProductLine = 'fast';
let PingSearchKeyword = '';

// PING: 機型名去掉變體字尾＝「系列／基本機型名」。分組與產品線分類共用同一條規則。
// 🔴 **不可**把「同進照片磚」加進這個清單（2026-08-13 實測後撤回）：加了之後照片磚機會併進
//    母系列（FD300…），而系列卡的產品線取「該系列第一台」的分類 ⇒ 照片磚被歸到 Fast 分頁，
//    違反 Eric 2026-07-19 裁「照片磚設為一類」。照片磚維持自成一個系列。
const PING_VARIANT_SUFFIX = /\s*(單料頭|單噴頭|同進|3in1|關門)$/;
function PingBaseModel(model) { return model.replace(PING_VARIANT_SUFFIX, ''); }

/* PING 2026-09-08（Eric「機器圖再大一點、大概紅框這麼大」）：系列卡的機器圖改用 ../img/cards/<系列>.png——
   由 profiles/PING/<系列>_cover.png 裁掉透明留白、統一 3:4 直式（270×360）產生（原 cover 是 600×600、機器只佔寬 36%，
   contain 進卡片後只剩六成大）。18 張變體 cover 本來就是全透明佔位檔（1,476 B），所以一律用「系列名」對應、不用 cover 檔名。
   onerror 退回 C++ 給的 cover 路徵，新增系列忘了產卡片圖也不會破圖。 */
function PingCardImgHtml(series, cover) {
	var card = '../img/cards/' + encodeURIComponent(series) + '.png';
	return '<img src="' + card + '" onerror="this.onerror=null;this.src=' + "'" + String(cover).replace(/\\/g, '/').replace(/'/g, '%27') + "'" + ';" />';
}

// PING 2026-09-07（Eric 裁・回報中心 #99 Q3 甲）：**照片磚機不再列進本頁**。
// 理由：「FD300／FF600／FF800 同進照片磚」不是使用者要挑的機器，是照片磚功能的內部載體
// （64 個虛擬料槽＋零回抽），由照片磚工作室在載入時自己裝、自己切。列在這裡的後果就是 #99：
// 只有 FD300 的人也會看到 FF600／FF800 兩台永遠用不到的機器。
// ⓘ 連帶效果（刻意的）：既有使用者只要再跑一次本頁存檔，那三台就會從 AppConfig 消失
//    ——OnExitFilter() 只送 ModelNozzleSelected 裡的機型，而 C++ 端是整組覆蓋
//    （GuideFrame::apply_config → app_config->set_vendors）⇒ 沒送到的就不存在。
//    這正是 #99 回報者手上那兩台的清除路徑，不必另外寫一次性清理。
// ⚠ 判準是機型名含「照片磚」，與 C++ 端 PhotoTileCapability 的 PHOTO_TILE_MARKER
//    （「同進照片磚」）同源；C++ 那支是單一來源，網頁端引用不到它 ⇒ 改判準時兩邊要一起改。
function PingIsPhotoTileModel(vendor, model) {
	return vendor == 'PING' && model.indexOf('照片磚') != -1;
}

// PING 2026-08-13（Eric 裁丙案）：本體變體（無後綴）的顯示名＝該機的**料數**。
// 值域由實查各系列本體的 default_filament_profile 槽數定（2026-08-13）：
//   FF600／FF800 ＝ 4 槽 ⇒ 四料｜FD 系與 DUAL 系 ＝ 2 槽 ⇒ 雙料｜其餘（FP300／EDU 200／
//   PING 200・270・300+／DL1016）＝ 1 槽 ⇒ 單料。
// ⚠ Eric 原話是「本體改成雙料」，但那只對 2 槽機成立；FF 系是四料機，一律寫雙料會標錯，
//   故按槽數推導。要改成一律「雙料」需 Eric 另裁。
function PingBaseLabel(series) {
	// 照片磚機是 64 個虛擬料槽（照片磚工作室用），不套「幾料」的說法
	if (series.indexOf('照片磚') != -1) return '照片磚';
	if (/^FF/.test(series))        return '四料';
	if (/^(FD|DUAL)/.test(series)) return '雙料';
	return '單料';
}

// 變體顯示名＝機型名去掉系列名後剩下的後綴；本體（沒有後綴）用料數當名字。
// 例：FD300 → 雙料｜FD300 同進 → 同進｜FF800 3in1 → 3in1｜FD300 同進照片磚 → 同進照片磚
function PingVariantLabel(model, series) {
	let s = model.slice(series.length).trim();
	if (s === '') return PingBaseLabel(series);
	// PING 2026-09-08（Eric）：「ABS 關門」不是一個列印模式，只是 FD300／FP300 關門時列印範圍縮小的限制
	// ⚠ 2026-09-11 更正：出貨線的關門可印區**不是 Ø200 圓**，是 Eric 2026-08-11 看圖裁的圓角三角
	//   （尖端朝 +Y、平邊朝門側；X ±142.5、Y -100~150）。Ø200 是 2026-07-26 的舊版，開發線至今仍是那版。
	// ⇒ 說明區的 ABS 頁籤拿掉，改在變體列旁註明用途。
	if (s === '關門') return '關門（ABS）';
	return s;
}

function ProductLineOf(vendor, model) {
	// 照片磚機獨立一類（Eric 2026-07-19 定：照片磚設為一類、含不同範圍的機器）
	if (vendor == 'PING' && model.indexOf('照片磚') != -1) return 'phototile';
	// 🔴 PING 2026-08-13 修：白名單收的是「基本機型名」，必須先去掉變體字尾再比對。
	//    舊版拿**完整機型名**去 has() ⇒「DUAL 300 同進」「DUAL 300 單料頭」比不到 ⇒ 落到 fast，
	//    而本體「DUAL 300」在 classic ⇒ 同型號家族被拆到兩個分頁（違 LAY-11，使用者在
	//    Classic 分頁找不到自己機器的同進版）。四組 DUAL 全中。
	return vendor == 'PING' && PingClassicModels.has(PingBaseModel(model)) ? 'classic' : 'fast';
}

// 2026-09-08：「全選／清空」兩顆鈕的 markup 抽成一支——PING 放在產品線分頁列左側，其他廠牌仍在標頭列。
function VendorSelectBtnsHtml(vendor) {
	return '<div class="SmallBtn_Green trans" tid="t11" onClick="SelectPrinterAll(\'' + vendor + '\')">all</div>' +
	       '<div class="SmallBtn trans" tid="t12" onClick="SelectPrinterNone(\'' + vendor + '\')">none</div>';
}

function ProductLineTabs(vendor) {
	if (vendor != 'PING') return '';
	return '<div class="ProductLineTabs">' +
		/* 2026-09-08（Eric）：全選／清空從 PING 標頭列搬到這裡——它們管的是下方卡片，該跟卡片同一列 */
		'<div class="LineBtns">' + VendorSelectBtnsHtml(vendor) + '</div>' +
		'<div class="LineTabs" role="tablist" aria-label="PING product line">' +
		'<button type="button" class="ProductLineTab" data-line="fast" onclick="SetPingProductLine(\'fast\')">Fast</button>' +
		'<button type="button" class="ProductLineTab" data-line="classic" onclick="SetPingProductLine(\'classic\')">Classic</button>' +
		/* 照片磚分頁 2026-09-07 移除（#99 Q3 甲）——照片磚機已不列進本頁，分頁會是空的。
		   ProductLineOf() 的 'phototile' 分支刻意留著：萬一日後有照片磚機漏擋進來，
		   它會被歸到一個沒有分頁的產品線而**被隱藏**（fail-safe），不會混進 Fast。 */
		'</div></div>';
}

function SetPingProductLine(line) {
	PingProductLine = line;
	ApplyPingProductLine();
}

function ApplyPingProductLine() {
	let searching = PingSearchKeyword.trim() != '';
	document.querySelectorAll(".OneVendorBlock[vendor='PING'] .PrinterBlock[data-product-line]").forEach(function(card) {
		card.style.display = (searching || card.dataset.productLine == PingProductLine) ? '' : 'none';
	});
	document.querySelectorAll(".OneVendorBlock[vendor='PING'] .ProductLineTab").forEach(function(tab) {
		let active = tab.dataset.line == PingProductLine;
		tab.classList.toggle('active', active);
		tab.setAttribute('aria-selected', active ? 'true' : 'false');
	});
}

function HandleModelList( pVal )
{
	if( !pVal.hasOwnProperty("model") )
		return;

	pModel=pVal['model'];
	PingSearchKeyword='';
	
	let nTotal=pModel.length;
	let ModelHtml={};
	for(let n=0;n<nTotal;n++)
	{
		let OneModel=pModel[n];
		
		let strVendor=OneModel['vendor'];
		
		//Add Vendor Html Node
		if($(".OneVendorBlock[vendor='"+strVendor+"']").length==0)
		{
			let sVV=strVendor;
			if( sVV=="BBL" )
				sVV="Bambu Lab";			
			if( sVV=="Custom")
				sVV="Custom Printer";
			if( sVV=="Other")
				sVV="Orca colosseum";

			let HtmlNewVendor='<div class="OneVendorBlock" Vendor="'+strVendor+'">'+
/* #184（Eric 2026-10-15 裁 Q1「拿掉」）：PING 不再有「PING ⌃」標頭列（收合搬到說明卡標題列），
   說明卡與搜尋框改放 #Content 頂端（PingModeHelpMount）。其他廠牌照舊保留黏頂標頭。 */
(strVendor=='PING' ? '' : '<div class="VendorStickyHead">'+'<div class="BlockBanner">'+'	<div class="BannerBtns">'+VendorSelectBtnsHtml(strVendor)+'	</div>'+'	<a>'+sVV+'</a>'+'</div>'+'</div>')+
'<div class="VendorStickyTabs">' + ProductLineTabs(strVendor) + '</div>' +   /* 分頁列第二段黏頂：說明區捲過去後停在 PING 底線下（保留 #94「知道自己在哪個產品線」） */
'<div class="PrinterArea">	'+
'</div>'+
'</div>';
			
			$('#VendorArea').append(HtmlNewVendor);   /* #184：卡片區另放 #VendorArea，說明卡＋搜尋框留在 #Content 頂端不被重畫 */
		}
		
		let ModelName=OneModel['model'];
		if (PingIsPhotoTileModel(strVendor, ModelName)) continue;   // #99 Q3 甲：照片磚機不列出

		//Collect Html Node Nozzel Html
		//PING: 依「家族」分組(機型名去掉模式字尾 單料頭/同進)，每家族一列(基本/單料頭/同進 三卡)
		let strSeries=PingBaseModel(ModelName);
		if( !ModelHtml.hasOwnProperty(strVendor))
			ModelHtml[strVendor]={};
		if( !ModelHtml[strVendor].hasOwnProperty(strSeries))
			ModelHtml[strVendor][strSeries]={cover:OneModel['cover'],
			                                 line:ProductLineOf(strVendor, ModelName), rows:''};

		// PING 2026-08-13（Eric 裁丙案）：一個系列一張卡，變體在圖下方**垂直勾選**。
		// 卡片形狀與舊版相同（圖＋底下一串勾選框），只是勾選框的內容從「口徑」換成「變體」
		// ⇒ 使用者不必重新認識版面（LAY-12 結構一致性），而且一系列一張卡之後卡片不必各佔一列。
		// 口徑不在此選（機型啟用＝該機型全部口徑可用），留到切片時在主畫面決定。
		ModelHtml[strVendor][strSeries].rows +=
'<div class="VarRow" data-vendor="'+strVendor+'" data-model="'+OneModel['model']+
'" data-nozzles="'+OneModel['nozzle_diameter']+'" onclick="ChooseModel(this)">'+
'<span class="PSel"></span><span class="VarName">'+PingVariantLabel(OneModel['model'], strSeries)+'</span></div>';
	}

	//Append —— PING: 一個系列一張卡。**沿用 .PrinterBlock class**，讓產品線過濾
	//（ApplyPingProductLine）與「跳過隱藏卡」邏輯不必改。
	for( let key in ModelHtml )
	{
		let pArea=$(".OneVendorBlock[vendor='"+key+"'] .PrinterArea");
		for( let series in ModelHtml[key] )
		{
			let g=ModelHtml[key][series];
			pArea.append('<div class="PrinterBlock" data-product-line="'+g.line+'" data-series="'+series+'">'+
'	<div class="PImg">'+PingCardImgHtml(series, g.cover)+'</div>'+
'	<div class="SeriesName">'+series+'</div>'+ g.rows +'</div>');
		}
	}
	
	
	//Update Checkbox —— PING 2026-08-13：改機型層級（任一口徑被記錄＝整台已選）
	for(let m=0;m<nTotal;m++)
	{
		let OneModel=pModel[m];

		if (PingIsPhotoTileModel(OneModel['vendor'], OneModel['model'])) continue;   // #99 Q3 甲：不回填、也不送回 C++
		let SelectList=OneModel['nozzle_selected'];
		if(SelectList!='')
		{
			SelectList=SelectList.split(';');
			for(let a=0;a<SelectList.length;a++)
			{
				if(SelectList[a]=='') continue;
				SetModelSelect(OneModel['vendor'], OneModel['model'], SelectList[a], true);
			}
		}
	}
	SyncAllCards();

	// let AlreadySelect=$("input:checked");
	// let nSelect=AlreadySelect.length;
	// if(nSelect==0)
	// {
	// 	$("input[nozzel='0.4'][vendor='Custom']").prop("checked", true);
	// }
	
	ApplyPingProductLine();
	if (typeof PingModeHelpMount === 'function') PingModeHelpMount();   /* #184：說明卡掛在 #Content 頂端（冪等；語言同 TranslatePage） */
	TranslatePage();
}

function CheckBoxOnclick(obj) {

	let strModel = obj.getAttribute("model");

	let strVendor = obj.getAttribute("vendor");
	let strNozzel = obj.getAttribute("nozzel");

	SetModelSelect(strVendor, strModel, strNozzel, obj.checked);

}

function SetModelSelect(vendor, model, nozzel, checked) {
	if (!ModelNozzleSelected.hasOwnProperty(vendor) && !checked) {
		return;
	}

	if (!ModelNozzleSelected.hasOwnProperty(vendor) && checked) {
		ModelNozzleSelected[vendor] = {};
	}

	let oVendor = ModelNozzleSelected[vendor];
	if (!oVendor.hasOwnProperty(model)) {
		oVendor[model] = {};
	}

	let oModel = oVendor[model];
	if (oModel.hasOwnProperty(nozzel) || checked) {
		oVendor[model][nozzel] = checked;
	}
}

function GetModelSelect(vendor, model, nozzel) {
	if (!ModelNozzleSelected.hasOwnProperty(vendor)) {
		return false;
	}

	let oVendor = ModelNozzleSelected[vendor];
	if (!oVendor.hasOwnProperty(model)) {
		return false;
	}

	let oModel = oVendor[model];
	if (!oModel.hasOwnProperty(nozzel)) {
		return false;
	}

	return oVendor[model][nozzel];
}

// ── PING 2026-08-13（Eric 裁「拿掉口徑勾選、只勾機型」）─────────────────────────
// 動線改成：整張卡片可點＝選這台機器，該機型**全部口徑**一起啟用；
// 口徑留到切片時在主畫面左上角的下拉決定（裝機當下使用者還不知道下一個檔要用哪個噴嘴）。
// ⓘ 底層儲存結構 ModelNozzleSelected[vendor][model][nozzle] **不變**——只是改由卡片一次寫入
//    整組口徑，所以 OnExitFilter() 的序列化、C++ 端的收訊與 AppConfig 格式全都不必動。
// ⚠ 必須寫入完整口徑清單（不可留空集合）：AppConfig::save() 對 variant 集合為空的 model
//    直接 `continue` 不寫出 ⇒ 空集合會讓整台機器從設定檔消失。
// row ＝ 一個 .VarRow（一台機器＝系列裡的一個變體）
function SetCardSelected(row, on) {
	let vendor  = row.getAttribute('data-vendor');
	let model   = row.getAttribute('data-model');
	let nozzles = (row.getAttribute('data-nozzles') || '').split(';');
	for (let i = 0; i < nozzles.length; i++) {
		if (nozzles[i] == '') continue;
		SetModelSelect(vendor, model, nozzles[i], on);
	}
	row.classList.toggle('Selected', on);
}

function ChooseModel(row) {
	SetCardSelected(row, !row.classList.contains('Selected'));
}

// 依 ModelNozzleSelected 回填變體列外觀：**任一口徑被記錄＝該台視為已選**
// （相容舊 conf：既有使用者只勾過 0.6 的機器，改版後仍顯示為已選。）
function SyncCardSelected(row) {
	let vendor  = row.getAttribute('data-vendor');
	let model   = row.getAttribute('data-model');
	let nozzles = (row.getAttribute('data-nozzles') || '').split(';');
	let on = false;
	for (let i = 0; i < nozzles.length; i++) {
		if (nozzles[i] != '' && GetModelSelect(vendor, model, nozzles[i])) { on = true; break; }
	}
	row.classList.toggle('Selected', on);
}

function SyncAllCards() {
	document.querySelectorAll('.VarRow[data-model]').forEach(SyncCardSelected);
}
// ────────────────────────────────────────────────────────────────────────────

function FilterModelList(keyword) {
	PingSearchKeyword = keyword;

	// PING 2026-08-13：不必再從 DOM 回收勾選狀態——ChooseModel/SetCardSelected 在點擊當下
	// 就寫進 ModelNozzleSelected，搜尋重建 DOM 不會遺失（重建後靠 SyncAllCards 回填外觀）。

	let nTotal = pModel.length;
	let ModelHtml = {};

	$('#VendorArea').empty();   /* #184：只重畫卡片區；搜尋框在 #Content 頂端，打字不會失焦 */
	for (let n = 0; n < nTotal; n++) {
		let OneModel = pModel[n];

		let strVendor = OneModel['vendor'];
		let ModelName = OneModel['model'];
		if (PingIsPhotoTileModel(strVendor, ModelName)) continue;   // #99 Q3 甲：搜尋結果也不列出
		if (ModelName.toLowerCase().indexOf(keyword.toLowerCase()) == -1)
			continue;

		//Add Vendor Html Node
		if ($(".OneVendorBlock[vendor='" + strVendor + "']").length == 0) {
			let sVV = strVendor;
			if (sVV == "BBL")
				sVV = "Bambu Lab";
			if (sVV == "Custom")
				sVV = "Custom Printer";
			if (sVV == "Other")
				sVV = "Orca colosseum";

			let HtmlNewVendor = '<div class="OneVendorBlock" Vendor="' + strVendor + '">' +
				/* #184：同 HandleModelList——PING 無標頭列、說明卡不在區塊內（搜尋只重畫卡片區） */
				(strVendor=='PING' ? '' : '<div class="VendorStickyHead">'+'<div class="BlockBanner">'+'	<div class="BannerBtns">'+VendorSelectBtnsHtml(strVendor)+'	</div>'+'	<a>'+sVV+'</a>'+'</div>'+'</div>') +
				'<div class="VendorStickyTabs">' + ProductLineTabs(strVendor) + '</div>' +
				'<div class="PrinterArea">	' +
				'</div>' +
				'</div>';

			$('#VendorArea').append(HtmlNewVendor);   /* #184：卡片區另放 #VendorArea，說明卡＋搜尋框留在 #Content 頂端不被重畫 */
		}

		//Collect Html Node Nozzel Html
		//PING: 同 HandleModelList，依家族分組(去模式字尾)
		let strSeries = PingBaseModel(ModelName);
		if (!ModelHtml.hasOwnProperty(strVendor))
			ModelHtml[strVendor] = {};
		if (!ModelHtml[strVendor].hasOwnProperty(strSeries))
			ModelHtml[strVendor][strSeries] = {cover: OneModel['cover'],
			                                   line: ProductLineOf(strVendor, ModelName), rows: ''};

		// PING 2026-08-13：同 HandleModelList——一系列一張卡、變體垂直勾選
		ModelHtml[strVendor][strSeries].rows +=
			'<div class="VarRow" data-vendor="' + strVendor + '" data-model="' + OneModel['model'] +
			'" data-nozzles="' + OneModel['nozzle_diameter'] + '" onclick="ChooseModel(this)">' +
			'<span class="PSel"></span><span class="VarName">' + PingVariantLabel(OneModel['model'], strSeries) + '</span></div>';
	}

	//Append —— PING: 一系列一張卡（沿用 .PrinterBlock class 讓產品線過濾不必改）
	for (let key in ModelHtml) {
		let obj = $(".OneVendorBlock[vendor='" + key + "'] .PrinterArea");
		obj.empty();
		for (let series in ModelHtml[key]) {
			let g = ModelHtml[key][series];
			obj.append('<div class="PrinterBlock" data-product-line="' + g.line + '" data-series="' + series + '">' +
				'	<div class="PImg">' + PingCardImgHtml(series, g.cover) + '</div>' +
				'	<div class="SeriesName">' + series + '</div>' + g.rows + '</div>');
		}
	}


	//Update Checkbox —— PING 2026-08-13：重建 DOM 後依 ModelNozzleSelected 回填卡片外觀
	SyncAllCards();

	// let AlreadySelect=$("input:checked");
	// let nSelect=AlreadySelect.length;
	// if(nSelect==0)
	// {
	// 	$("input[nozzel='0.4'][vendor='Custom']").prop("checked", true);
	// }

	ApplyPingProductLine();
	TranslatePage();
}

// PING: 改用 native querySelectorAll + 直接設值，移除 jQuery 逐元素 $(this) 包裝開銷（加速全選/全部清空）
// PING 2026-08-13：改走變體列（口徑 checkbox 已移除）；隱藏的系列卡（產品線分頁/搜尋過濾掉的）一律跳過。
function SetAllCards(sVendor, on) {
	let rows = document.querySelectorAll(".VarRow[data-vendor='" + sVendor + "']");
	for (let i = 0; i < rows.length; i++) {
		let card = rows[i].closest('.PrinterBlock');
		if (sVendor == 'PING' && card && card.style.display == 'none') continue;
		SetCardSelected(rows[i], on);
	}
}

function SelectPrinterAll(sVendor)  { SetAllCards(sVendor, true);  }
function SelectPrinterNone(sVendor) { SetAllCards(sVendor, false); }

function OnExitFilter() {

	let nTotal = 0;
	let ModelAll = {};
	for (vendor in ModelNozzleSelected) {
		for (model in ModelNozzleSelected[vendor]) {
			for (nozzel in ModelNozzleSelected[vendor][model]) {
				if (!ModelNozzleSelected[vendor][model][nozzel])
					continue;

				if (!ModelAll.hasOwnProperty(model)) {
					//alert("ADD: "+strModel);

					ModelAll[model] = {};

					ModelAll[model]["model"] = model;
					ModelAll[model]["nozzle_diameter"] = '';
					ModelAll[model]["vendor"] = vendor;
				}

				ModelAll[model]["nozzle_diameter"] += ModelAll[model]["nozzle_diameter"] == '' ? nozzel : ';' + nozzel;

				nTotal++;
			}

		}
	}

	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "save_userguide_models";
	tSend['data'] = ModelAll;

	SendWXMessage(JSON.stringify(tSend));

	return nTotal;

}

//
function OnExit()
{	
	let ModelAll={};
	
	let ModelSelect=$("input:checked");
	let nTotal=ModelSelect.length;

	if( nTotal==0 )
	{
		ShowNotice(1);
		
		return 0;
	}
	
	for(let n=0;n<nTotal;n++)
	{
	    let OneItem=ModelSelect[n];
		
		let strModel=OneItem.getAttribute("model");
		let strVendor=OneItem.getAttribute("vendor");
		let strNozzel=OneItem.getAttribute("nozzel");
			
		//alert(strModel+strVendor+strNozzel);
		
		if(!ModelAll.hasOwnProperty(strModel))
		{
			//alert("ADD: "+strModel);
			
			ModelAll[strModel]={};
		
			ModelAll[strModel]["model"]=strModel;
			ModelAll[strModel]["nozzle_diameter"]='';
			ModelAll[strModel]["vendor"]=strVendor;
		}
		
		ModelAll[strModel]["nozzle_diameter"]+=ModelAll[strModel]["nozzle_diameter"]==''?strNozzel:';'+strNozzel;
	}
		
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_userguide_models";
	tSend['data']=ModelAll;
	
	SendWXMessage( JSON.stringify(tSend) );

    return nTotal;
}


function ShowNotice( nShow )
{
	if(nShow==0)
	{
		$("#NoticeMask").hide();
		$("#NoticeBody").hide();
	}
	else
	{
		$("#NoticeMask").show();
		$("#NoticeBody").show();
	}
}

function CancelSelect()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="user_guide_cancel";
	tSend['data']={};
		
	SendWXMessage( JSON.stringify(tSend) );			
}


function ConfirmSelect()
{
	let nChoose=OnExitFilter();
	
	if(nChoose>0)
    {
		var tSend={};
		tSend['sequence_id']=Math.round(new Date() / 1000);
		tSend['command']="user_guide_finish";
		tSend['data']={};
		tSend['data']['action']="finish";
		
		SendWXMessage( JSON.stringify(tSend) );			
	}
}




