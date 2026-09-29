#ifndef slic3r_GUI_ParamDiffReport_hpp_
#define slic3r_GUI_ParamDiffReport_hpp_

class wxWindow;

namespace Slic3r {
namespace GUI {

// PING：匯出「目前載入的設定 vs 系統母版」的參數差異清單（單檔 HTML）。
//
// 為什麼要這支：客戶回報印不好時，我們要先知道他跟標準差在哪。以前是把 3mf 寄回來、
// 由售服用腳本比對；這支讓客戶（或同事）自己按一下就產出一份可寄的報告。
//
// 動線＝兩下點擊、零設定頁（Eric 2026-09-17 裁：「這一頁不用給客戶選擇吧？
// 他只要覺得有問題，就匯出即可」）。所有選項寫死成預設，要別的組合走售服端腳本。
//
// 回傳 false＝使用者取消、或寫檔失敗（失敗時本函式自己出訊息框）。
bool export_param_diff_report(wxWindow *parent);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ParamDiffReport_hpp_
