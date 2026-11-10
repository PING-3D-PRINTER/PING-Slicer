// 照片磚機器能力判定——單一來源實作。規格見 PhotoTileCapability.hpp 檔頭。

#include "PhotoTileCapability.hpp"

#include "GUI_App.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cmath>

namespace Slic3r { namespace GUI {

namespace {
// 全庫既有慣例。改判準＝改這幾行，不要散在各處。
const char* MIXING_MARKER     = "同進";
const char* PHOTO_TILE_MARKER = "同進照片磚";
const char* CLASSIC_PREFIX    = "DUAL";

/* 合法口徑集合＝現行五台照片磚機的全集（FD300 0.4/0.6；FF800 0.4/0.6/1.0）。
   為什麼要限：Codex #10 指出原本只要機型名帶 marker 就算數，連家族空、口徑亂七八糟的
   preset 都會被判成照片磚機——生成參數是 f(口徑)，認錯口徑等於默默產錯磚。
   ⚠ 日後新增照片磚機口徑＝在這裡加一個值；沒加會在 reject_reason 顯性寫出來（不靜默消失）。 */
const double LEGAL_NOZZLES[] = { 0.4, 0.6, 1.0 };

bool nozzle_is_legal(double mm)
{
    for (double v : LEGAL_NOZZLES)
        if (std::fabs(mm - v) < 1e-6) return true;
    return false;
}
} // namespace

bool printer_model_has_photo_tile_marker(const std::string& printer_model)
{
    return printer_model.find(PHOTO_TILE_MARKER) != std::string::npos;
}

PhotoTileCapability photo_tile_capability_of_config(const DynamicPrintConfig& config,
                                                   const std::string& preset_name)
{
    PhotoTileCapability cap;
    cap.preset_name = preset_name;

    const ConfigOptionString* pm = config.option<ConfigOptionString>("printer_model");
    if (pm == nullptr) return cap;
    cap.printer_model = pm->value;

    cap.is_mixing  = pm->value.find(MIXING_MARKER) != std::string::npos;
    cap.has_photo_tile_marker = printer_model_has_photo_tile_marker(pm->value);
    cap.is_classic = pm->value.rfind(CLASSIC_PREFIX, 0) == 0;
    // 家族：FD＝雙料同進（M6051）／FF＝四料同進（M6052）。與自動選機同準。
    if (pm->value.rfind("FD", 0) == 0)      { cap.family = "FD"; cap.mode = "dual"; }
    else if (pm->value.rfind("FF", 0) == 0) { cap.family = "FF"; cap.mode = "quad"; }

    // effective nozzle：以機器實際噴嘴為準（C-0 §1：生成不再讀面板選的口徑）。
    if (const auto* nd = config.option<ConfigOptionFloats>("nozzle_diameter"))
        if (!nd->values.empty())
            cap.nozzle_mm = nd->values.front();
    // 後備：某些 preset 只有 printer_variant（字串口徑）時仍給得出值
    if (cap.nozzle_mm <= 0.0)
        if (const auto* pv = config.option<ConfigOptionString>("printer_variant"))
            try { cap.nozzle_mm = std::stod(pv->value); } catch (...) {}

    if (!cap.has_photo_tile_marker)
        return cap;                                    // 連 marker 都沒有＝不是照片磚機，不必給理由

    // 有 marker 之後，家族與口徑都必須合法才算數（Codex #10）
    if (cap.family.empty()) {
        cap.reject_reason = "機型名帶「同進照片磚」但家族不是 FD／FF（無法決定 M6051／M6052）";
        return cap;
    }
    if (cap.is_classic) {
        cap.reject_reason = "Classic 前代（Marlin 韌體）不支援照片磚";
        return cap;
    }
    if (!nozzle_is_legal(cap.nozzle_mm)) {
        cap.reject_reason = "口徑 " + std::to_string(cap.nozzle_mm) + " 不在合法集合 {0.4, 0.6, 1.0}";
        return cap;
    }
    cap.is_photo_tile = true;
    return cap;
}

PhotoTileCapability photo_tile_capability_of(const Preset& preset)
{
    return photo_tile_capability_of_config(preset.config, preset.name);
}

PhotoTileCapability photo_tile_capability_of_selected_printer()
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return PhotoTileCapability();
    return photo_tile_capability_of(bundle->printers.get_edited_preset());
}

bool current_printer_is_photo_tile()
{
    return photo_tile_capability_of_selected_printer().is_photo_tile;
}

}} // namespace Slic3r::GUI
