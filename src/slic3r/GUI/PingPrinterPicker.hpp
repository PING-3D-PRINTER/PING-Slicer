#ifndef slic3r_GUI_PingPrinterPicker_hpp_
#define slic3r_GUI_PingPrinterPicker_hpp_

#include <string>
#include <vector>

class wxWindow;

/* PING 原生選機視窗（開發中清單 #98，2026-11-10）
 *
 * 起因：客戶 V3.6.4 第一次開啟時「設定精靈」（WebView2 內嵌網頁）整片空白、進不去主畫面。
 * Eric 2026-11-10 裁：第一次開啟改用**不靠網頁**的原生小視窗選一台（預選 FD300 雙料），
 *   之後換機也用同一個視窗；網頁版選機頁（resources/web/guide/21、24）留著不刪、只是不再開。
 * 同日四題「全部照建議」（原型＝20260604 ORCA客製/原型_原生選機視窗_20261110.html）：
 *   Q1 版面 A 案＝左欄系列清單（附機器圖、Fast／Classic 頁籤）＋右欄配置單選
 *   Q2 之後換機走丙＝主要鈕〔換成這一台〕、次要鈕〔再加一台〕；啟用超過一台才出現「我的機器」列
 *      （可移除、至少留一台）
 *   Q3 第一次開啟按 ✕＝照畫面上當時選的那一台直接進主畫面
 *   Q4 線材不問（由呼叫端照該機 default_materials 裝好）
 * 另兩題已定：口徑不在這裡選（該機全部口徑一起啟用）；配置順序固定「本體→同進→單料頭→3in1→關門」。
 *
 * 分工：這支只管「畫面＋使用者選了什麼」，不碰 AppConfig／PresetBundle；
 *       設定怎麼落地在 GUI_App::run_ping_printer_picker()。
 * 系列分組、產品線（Fast／Classic）、配置顯示名與網頁版 guide/21/21.js 同一套判準——改判準兩邊一起改。
 */

namespace Slic3r {
namespace GUI {

// 一台可選的機器（＝vendor bundle 的一個 printer model）。照片磚內部機型不放進來（呼叫端先排除）。
struct PingPickerModel
{
    std::string              id;          // 機型名（＝printer_model，如「FD300 同進」）
    std::vector<std::string> variants;    // 口徑（＝printer_variant，如 0.25／0.4／0.6），照 bundle 的順序
};

struct PingPickerResult
{
    bool                     accepted = false;   // false＝取消（之後換機才可能；第一次開啟一律 true）
    std::vector<std::string> enabled;            // 確定後要啟用的機型（照 models 的順序）
    std::string              active;             // 確定後要選用的那一台
};

// 機型名去掉配置字尾＝系列名（「FD300 同進」→「FD300」）。
std::string ping_picker_series_of(const std::string& model);

/* 開選機視窗（modal）。
   models  ＝可選機型全集；enabled＝目前已啟用的（first_run 時忽略）；active＝目前選用的機型（可空）。
   first_run＝第一次開啟：只有〔確定，開始使用〕，按 ✕／Esc 也當確定。 */
PingPickerResult show_ping_printer_picker(wxWindow*                           parent,
                                          const std::vector<PingPickerModel>& models,
                                          const std::vector<std::string>&     enabled,
                                          const std::string&                  active,
                                          bool                                first_run);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PingPrinterPicker_hpp_
