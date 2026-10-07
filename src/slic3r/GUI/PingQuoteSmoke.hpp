#ifndef slic3r_GUI_PingQuoteSmoke_hpp_
#define slic3r_GUI_PingQuoteSmoke_hpp_

// =====================================================================
// 報價包 smoke（無人值守驗證）
//
// 為什麼要有這支：報價包是**選單觸發**的功能，要驗就得有人去點。而用截圖去點 GUI
// 有一個具體的危險——Eric 自己的 PING Slicer 常常開著，兩個視窗長得一模一樣，
// 點錯就動到他的專案。照片磚線 2026-08-03 已經因為「壓測視窗與正式版無法分辨」
// 賠掉一整輪數據（見 MainFrame.cpp 該事故註解）。
//
// 所以照這個 fork 既有的做法：環境變數觸發 → 自己載模型、自己產包、自己關閉，
// 全程沒有人的手指介入，也就沒有點錯視窗的可能。
//
// 用法（PowerShell）：
//   $env:PING_QUOTE_SMOKE       = "C:\out\smoke.pingquote"   # 輸出路徑（必填）
//   $env:PING_QUOTE_SMOKE_MODEL = "a.stl;b.stl"              # 要載入的模型，分號分隔（必填）
//   $env:PING_QUOTE_SMOKE_DELAY_MS = "8000"                  # 選填，等 app 初始化完再跑
//   # 以下三個是契約 v1.4「尺寸給物件座標」的驗收用，選填，沒設就完全不動模型：
//   $env:PING_QUOTE_SMOKE_ROTATE     = "30,45,10"            # 每個物件繞 X、Y、Z 轉幾度再產包
//   $env:PING_QUOTE_SMOKE_SCALE      = "50"                  # 等比例縮放（%）
//   $env:PING_QUOTE_SMOKE_PANEL_SIZE = "1"                   # 把尺寸面板在物件座標／世界座標會顯示的值寫進 log
//                                                            # （log 行＝`PING_QUOTE_SMOKE panel idx=…`，拿來對 quote.txt 的 size_*）
//   ping-slicer.exe --datadir <獨立 data root>
//
// 跑完會把結果寫進 log 並自己關閉。**一般啟動完全不受影響**（沒設環境變數就什麼都不做）。
// =====================================================================

namespace Slic3r { namespace GUI {

class MainFrame;

// 由 MainFrame 建構完成後呼叫；沒設 PING_QUOTE_SMOKE 就直接返回。
void run_ping_quote_smoke(MainFrame *frame);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PingQuoteSmoke_hpp_
