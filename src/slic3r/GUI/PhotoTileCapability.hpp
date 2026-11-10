#ifndef slic3r_GUI_PhotoTileCapability_hpp_
#define slic3r_GUI_PhotoTileCapability_hpp_

// =====================================================================
// 照片磚「機器能力判定」單一來源（C-1，2026-08-01 二版・照片磚線 PT）
//
// 為什麼要收斂成一支：現況是各處自己用機型名字串判（自動選機 resolver 在
// GUI_App.cpp、後處理閘門在 BackgroundSlicingProcess.cpp、拖放在 Plater.cpp），
// 判準一旦漂移就會出現「這裡認得、那裡不認得」的鬼故事。Codex C 案審查 #15
// 明確要求收斂成 **單一 capability predicate**，供拖放／側卡／生成共用。
//
// 判準（沿用全庫既有慣例，不改 preset schema——那會動到整包 bundle，超出最小改）：
//   混色機（is_mixing）        ：printer_model 含「同進」
//   Classic 前代（is_classic） ：printer_model 以「DUAL」開頭（Marlin 韌體，照片磚不支援）
//   照片磚機（is_photo_tile）  ：含「同進照片磚」**且** 家族合法（FD/FF）**且** 口徑合法
//   模式                       ：FD→dual（M6051）／FF→quad（M6052）
//   口徑                       ：一律讀 effective nozzle_diameter（機器實際噴嘴），不是面板選的值
//
// 【2026-08-01 二版改了什麼（Codex 重要 #10）】
//   ① 原本 marker 命中就 is_photo_tile=true——**家族空的也算**。現在家族與口徑不合法一律不算，
//      並把不算的理由寫進 reject_reason（不是靜默消失，診斷才追得到）。
//   ② 口徑限定 {0.4, 0.6, 1.0}（現行五台照片磚機的全集）。日後加新口徑＝改 LEGAL_NOZZLES 一行；
//      沒改就會在 reject_reason 看到「口徑不在合法集合」，不會變成鬼故事。
//   ③ 提供吃 DynamicPrintConfig 的純函式，讓 **後處理閘門**（BackgroundSlicingProcess，
//      原本自己 find("同進")）與 GUI 共用同一份判定。
// preset 欄位化（phototile_mode）列為後續硬化項，不擋 C 案。
// =====================================================================

#include <string>

namespace Slic3r {
class Preset;
class DynamicPrintConfig;
namespace GUI {

struct PhotoTileCapability
{
    bool        is_photo_tile = false;   // 這台機是不是照片磚機（marker＋家族＋口徑全合法）
    /* 只看機型名有沒有「同進照片磚」，不管家族／口徑合不合法。
       誰該用它：**排除性**的判斷（例如混色 UI 要不要藏——Eric 2026-08-22 令照片磚機不給混色）。
       為什麼不能用 is_photo_tile：那是「這台機能不能拿來產照片磚」的能力判定，口徑不合法時是 false；
       但一台名字叫「同進照片磚」、口徑被改壞的機器，依然不該冒出混色曲線 UI ⇒ 排除要看 marker。 */
    bool        has_photo_tile_marker = false;
    bool        is_mixing     = false;   // 同進混色機（照片磚機是它的子集）
    bool        is_classic    = false;   // Classic 前代（DUAL 開頭、Marlin）＝照片磚不支援
    std::string mode;                    // "dual"｜"quad"（非同進機＝空）
    double      nozzle_mm  = 0.0;        // effective nozzle（讀 nozzle_diameter[0]）
    std::string preset_name;             // 判定所依據的 preset 名（診斷用）
    std::string printer_model;           // 判定所依據的 printer_model（診斷用）
    std::string family;                  // "FD"／"FF"（診斷用）
    std::string reject_reason;           // 有 marker 卻不算照片磚機時的原因（診斷用）
};

/* 只看機型名（printer_model）有沒有照片磚 marker。給「手上只有機型名、還沒有 preset」的地方用
   （原生選機視窗列機型清單時把照片磚內部機型排除，2026-11-10 開發中清單 #98）。
   與 PhotoTileCapability::has_photo_tile_marker 同一條判準。 */
bool printer_model_has_photo_tile_marker(const std::string& printer_model);

// 單一判定（純函式）：任一組設定。後處理閘門走這支，不要自己比字串。
PhotoTileCapability photo_tile_capability_of_config(const DynamicPrintConfig& config,
                                                    const std::string& preset_name = std::string());
// 單一判定：任一 preset
PhotoTileCapability photo_tile_capability_of(const Preset& preset);
// 單一判定：目前選中的機器（拖放／側卡／生成三處都該問這個）
PhotoTileCapability photo_tile_capability_of_selected_printer();
// 便利型（等價於 photo_tile_capability_of_selected_printer().is_photo_tile）
bool current_printer_is_photo_tile();

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PhotoTileCapability_hpp_
