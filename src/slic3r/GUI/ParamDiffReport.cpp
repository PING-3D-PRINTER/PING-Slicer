#include "ParamDiffReport.hpp"

#include "GUI.hpp"          // into_u8 / from_u8
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"   // 建置時產生（build/src/libslic3r/），不帶目錄前綴——帶了本機找得到、CI 四平台找不到（SOP_單檔編譯檢查 §5）

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/filedlg.h>
#include <wx/string.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

// ── 一列差異 ────────────────────────────────────────────────────────────────
struct DiffRow
{
    int         group = 0;  // 分組（下面的 Group；決定組的先後）
    std::string category;   // 已翻譯的組名（七組之一，或退回 ConfigOptionDef::category）
    std::string label;      // 已翻譯的參數名
    std::string key;        // Orca key（語言中性，客戶與我們對話時最精確的指稱）
    std::string std_value;  // 系統母版的值
    std::string cur_value;  // 目前設定的值
    int         filament     = 0;      // 第幾支線材（1 起算）；0＝印表機／製程，或專案只有一支線材（不必標）
    bool        user_changed = true;   // 「自訂」＝這顆是使用者自己動的
    bool        inert        = false;  // 「未作用」＝在本專案其他設定下不會生效
};

// ── 「未作用」判定 ──────────────────────────────────────────────────────────
//
// 只做**明確可判定**的組：某個開關關著時，另一顆值設了也不會進到切片結果。
// 這六組都用 2026-09-17 那份客戶 3mf 實際驗過（34 項差異裡有 6 項屬於這類，
// 不標的話清單會有近兩成是噪音）。⚠️ 判定一律用「目前設定」那邊的值，不是母版的值——
// 開關是不是關著看的是使用者現在的狀態。
// 🔴 不要憑感覺往這裡加：加錯會把真正生效的差異灰掉，比不標更危險。
bool is_inert(const std::string &key, const DynamicPrintConfig &cur)
{
    auto str_is = [&cur](const char *k, const char *v) {
        return cur.has(k) && cur.opt_serialize(k) == v;
    };
    auto num_is_zero = [&cur](const char *k) {
        if (! cur.has(k))
            return false;
        const std::string s = cur.opt_serialize(k);
        // 涵蓋 "0"、"0%"、"0.0"；用字串比對是因為這幾顆分屬 int／float／percent 三種型別
        return s == "0" || s == "0%" || s == "0.0" || s == "0.00";
    };

    // ① Brim 類型＝不做 ⇒ Brim 相關數值不生效
    if (key == "brim_width" || key == "brim_object_gap")
        return str_is("brim_type", "no_brim");

    // ② 稀疏填充密度＝0 ⇒ 填充圖案／角度等不生效
    if (key == "sparse_infill_pattern" || key == "infill_direction" || key == "infill_combination"
        || key == "sparse_infill_line_width" || key == "sparse_infill_speed")
        return num_is_zero("sparse_infill_density");

    // ③ 沒有筏層 ⇒ 筏層參數不生效
    if (boost::starts_with(key, "raft_"))
        return num_is_zero("raft_layers");

    // ④ 冷卻降速關著 ⇒ 最小列印速度不生效（它只在降速生效時才是下限）
    if (key == "slow_down_min_speed")
        return str_is("slow_down_for_layer_cooling", "0");

    // ⑤ 懸空速度功能關著 ⇒ 四段懸空速度不生效
    if (boost::starts_with(key, "overhang_") && boost::ends_with(key, "_speed"))
        return str_is("enable_overhang_speed", "0");

    // ⑥ 沒開熨燙 ⇒ 熨燙參數不生效
    if (boost::starts_with(key, "ironing_"))
        return str_is("ironing_type", "no ironing");

    return false;
}

// ── 分組（Eric 2026-09-28 裁 Q3「照原型 v2 七組」）──────────────────────────────
//
// 組與組序＝原型 v2（原型_參數差異清單匯出_v2_20260917.html 資料列的 g: 欄）。原型只列到 27 個鍵，
// 其餘照下面的鍵名規則推；這組規則把那 27 個鍵逐一推回原型的組（c-0928-PDR-02 實測），所以不另存一張表。
// 規則先中先得，順序有意義：
//   溫度        temperature／_temp            排最前：首層溫度要跟其他層並排才好比
//   支撐        support_／tree_support_ 開頭   排在首層前：原型把 support_object_first_layer_gap 放支撐
//   首層與附著  initial_layer／first_layer／brim／skirt（原型把 raft_first_layer_density、first_layer_flow_ratio 放這裡）
//   冷卻        fan／cooling／slow_down
//   移動        z_hop／retract／travel／wipe
//   速度與流量  speed／accel／jerk／flow／volumetric
//   外觀與強度  wall／shell／infill／seam／scarf／ironing   排在速度後：原型把 small_perimeter_speed 放速度
// 一句話：鍵名講的是支撐、首層、冷卻、移動這幾樣東西，就歸那一組，不管它調的是速度還是流量；牆與填充的速度歸速度。
// 都不中 ⇒ 退回 Orca 的 category：Support／Speed／Strength 併進支撐／速度與流量／外觀與強度（不併會出現兩個
// 「支撐」），其餘用它自己的翻譯名、排在七組之後。PING 自家功能（ping_）、換料塔（tower）與沖刷（flush_）
// 不套鍵名規則，直接用自己的 category——不然同一個功能的鍵會被拆散到好幾組。
enum Group { gSupport, gShell, gFirstLayer, gTemperature, gSpeedFlow, gCooling, gTravel, gOrcaCategory };

int group_of(const std::string &key, const std::string &category)
{
    auto has = [&key](const char *s) { return key.find(s) != std::string::npos; };
    if (! boost::starts_with(key, "ping_") && ! boost::starts_with(key, "flush_") && ! has("tower")) {
        if (has("temperature") || has("_temp_") || boost::ends_with(key, "_temp"))
            return gTemperature;
        // 這兩顆的 support 是「機器支援某功能」，不是支撐
        if ((boost::starts_with(key, "support_") || boost::starts_with(key, "tree_support_"))
            && key != "support_air_filtration" && key != "support_multi_bed_types")
            return gSupport;
        if (has("initial_layer") || has("first_layer") || has("brim") || has("skirt"))
            return gFirstLayer;
        // fan 要比對成一個字：elefant_foot_compensation 裡也有 fan。slow_down_layers＝前幾層放慢，不是冷卻降速
        if (boost::starts_with(key, "fan_") || has("_fan") || has("cooling") || (has("slow_down") && key != "slow_down_layers"))
            return gCooling;
        if (boost::starts_with(key, "z_hop") || has("retract") || has("travel") || has("wipe"))
            return gTravel;
        if (has("speed") || has("accel") || has("jerk") || has("flow") || has("volumetric"))
            return gSpeedFlow;
        if (has("wall") || has("shell") || has("infill") || has("seam") || has("scarf") || has("ironing"))
            return gShell;
    }
    if (category == "Support")
        return gSupport;
    if (category == "Speed")
        return gSpeedFlow;
    if (category == "Strength")
        return gShell;
    return gOrcaCategory;
}

// 「Travel」在 .mo 已經譯成「空駛」（設定頁的用語）；報告照原型寫「移動」⇒ 用 context 另起一條，不動設定頁那條。
wxString group_name(int group)
{
    switch (group) {
    case gSupport:     return _L("Support");
    case gShell:       return _L("Shell & Strength");
    case gFirstLayer:  return _L("First Layer & Adhesion");
    case gTemperature: return _L("Temperature");
    case gSpeedFlow:   return _L("Speed & Flow");
    case gCooling:     return _L("Cooling");
    case gTravel:      return _CTX(L_CONTEXT("Travel", "ParamDiffGroup"), "ParamDiffGroup");
    default:           return wxString();
    }
}

// ── HTML 逸出 ───────────────────────────────────────────────────────────────
std::string esc(const std::string &in)
{
    std::string out;
    out.reserve(in.size() + 16);
    for (char c : in) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;
        }
    }
    return out;
}

std::string esc(const wxString &in) { return esc(into_u8(in)); }

// JSON 字串逸出。⚠️ `<` 也要轉義：內嵌資料是寫在 <script> 裡面的，
// 值裡若出現 "</script>"（自訂 G-code 很可能有任意字元）會提早關掉標籤、整份報告壞掉。
std::string json_esc(const std::string &in)
{
    std::string out;
    out.reserve(in.size() + 16);
    for (unsigned char c : in) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '<':  out += "\\u003c"; break;
        case '>':  out += "\\u003e"; break;
        case '&':  out += "\\u0026"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out;
}

// ── 從 3mf 帶進來的「客戶自己改的」鍵 ───────────────────────────────────────
//
// 開別人的專案時，PresetBundle 會把 3mf 的 different_settings_to_system 留一份下來
// （排列：[0]=process、[1..n]=filament、[n+1]=printer）。那是**他存檔當下、他那台機器的
// 系統值**算出來的，不是我們現在的標準 ⇒ 兩者相減就是「PING 後來更新、他沒動」的那一類。
//
// ⚠️ 實測命中率 26/28（2026-09-17 那份 3mf：漏報熨燙兩顆、多報一顆空值 print_host）
// ⇒ 這是近似值不是保證，報告上要照實寫。
// 沒有專案（使用者在自己機器上調參數）時這份是空的 ⇒ 全部算「自訂」，這也是對的：
// 他的系統就是我們的系統，不存在「標準較新」這回事。
std::set<std::string> project_changed_keys(const std::vector<std::string> &groups, size_t index)
{
    std::set<std::string> out;
    if (index >= groups.size())
        return out;
    std::vector<std::string> keys;
    boost::split(keys, groups[index], boost::is_any_of(";"));
    for (std::string &k : keys) {
        boost::trim(k);
        if (! k.empty())
            out.insert(k);
    }
    return out;
}

// ── 蒐集一個 preset 對它母版的差異 ─────────────────────────────────────────
// cur＝目前的設定（選中那支是編輯副本，含還沒存的修改）；parent＝拿來比的系統母版（找不到就別呼叫：
// 繼承鏈斷了不是當掉的理由，報告照出、由呼叫端在報告上講清楚那一塊沒有可比的標準）。
// opened＝開專案那一刻這一格的快照；沒開專案、或開專案之後才加的線材槽＝nullptr。
// filament＝第幾支線材（1 起算），0＝不標。
void collect(const Preset &edited,
             const Preset &parent,
             const std::set<std::string> &declared_by_project,
             const PresetBundle::ProjectPresetSnapshot *opened,
             int filament,
             std::vector<DiffRow> &rows)
{
    const DynamicPrintConfig &cur = edited.config;
    const DynamicPrintConfig &ref = parent.config;

    // B 案（c-0928-PDR-01）：專案的宣告清單只描述「開專案時那一支」預設。開專案後換過預設 ⇒ 清單講的是別支，
    // 這一組的差異全算自訂；沒換 ⇒ 開專案之後又改過的鍵（跟快照不同）也算自訂——那是使用者現在自己改的。
    const bool same_as_opened = opened != nullptr && ! opened->name.empty() && opened->name == edited.name;

    // deep_compare = true：向量型（per-extruder／per-filament）的鍵要逐格比，不能整串比。
    //
    // 🔴 但 deep_diff 對向量型回的是 **"key#index"**（Preset.cpp 的 add_correct_opts_to_diff），
    //    例如 "nozzle_temperature#0"、"z_hop#1"。直接拿它去 ConfigDef::get() 會查不到 ⇒
    //    **整批每噴頭參數會無聲消失**，而那正是最要緊的一批（溫度／回抽／Z 抬升／體積流量）。
    //    所以這裡先把 "#index" 剝掉、收斂成基底鍵；值則序列化整個向量一起呈現
    //    （"210,210,210,210"），不拆單格——per-element 沒有公開的序列化介面，
    //    自己切逗號會在含逗號的字串型參數（自訂 G-code）上壞掉。
    std::vector<std::string> keys;
    for (std::string k : PresetCollection::dirty_options(&edited, &parent, true)) {
        const size_t hash = k.find('#');
        if (hash != std::string::npos)
            k.erase(hash);
        keys.push_back(std::move(k));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    const ConfigDef *def = cur.def();
    for (const std::string &key : keys) {
        // 這些是 preset 的身分／相容性欄位，不是使用者調得到的參數，列出來只會製造噪音
        static const std::set<std::string> skip = {
            "print_settings_id", "filament_settings_id", "printer_settings_id",
            "printer_model", "printer_variant", "printer_technology",
            "compatible_printers", "compatible_printers_condition",
            "compatible_prints", "compatible_prints_condition",
            "inherits", "different_settings_to_system", "renamed_from",
            "print_host", "printhost_apikey", "printhost_cafile", "printhost_port",
            "printhost_authorization_type", "printhost_user", "printhost_password",
            "default_print_profile", "default_filament_profile",
        };
        if (skip.count(key))
            continue;

        const ConfigOptionDef *od = def ? def->get(key) : nullptr;
        if (od == nullptr)
            continue;   // 認不得的鍵（多半是舊版殘留），不猜

        DiffRow row;
        row.key       = key;
        row.group     = group_of(key, od->category);
        if (row.group != gOrcaCategory)
            row.category = into_u8(group_name(row.group));
        else   // 「Others」與空白都叫「其他」：不合併的話，英文介面會出現 Other／Others 兩組
            row.category = into_u8(od->category.empty() || od->category == "Others" ? _L("Other") : _(od->category));
        row.label     = into_u8(_(od->full_label.empty() ? od->label : od->full_label));
        if (row.label.empty())
            row.label = key;
        row.std_value = ref.has(key) ? ref.opt_serialize(key) : std::string();
        row.cur_value = cur.has(key) ? cur.opt_serialize(key) : std::string();
        row.filament  = filament;
        row.inert     = is_inert(key, cur);
        // 沒有專案（＝使用者在自己機器上）時，差異一律是他自己調的；有專案時再加上「開專案之後改過」（B 案）
        const bool changed_since_open = same_as_opened && opened->config.has(key)
                                        && opened->config.opt_serialize(key) != row.cur_value;
        row.user_changed = ! same_as_opened || declared_by_project.count(key) > 0 || changed_since_open;
        rows.push_back(std::move(row));
    }
}

// ── 第幾支線材：拿哪一份設定、跟哪一支母版比 ─────────────────────────────────
//
// 跟存 3mf 時算 different_settings_to_system 的是同一套（PresetBundle::full_fff_config 多料那段），
// 只多認一次母版改名（find_preset2，同 get_selected_preset_parent）：
//   · 目前選中的那支 ⇒ 編輯中的副本（含還沒存的修改），母版照 get_selected_preset_parent()——跟印表機／製程同一條路
//   · 其他支 ⇒ 存檔版；自己就是基底（系統／預設／沒有繼承）的跟自己比＝沒有差異；繼承來的跟它繼承的那支比
// ⚠️ 其他支的母版一律取實體（find_preset2 內部是 real）：母版可能正好是目前選中、正在編輯的那支，
//    取到編輯副本就會拿使用者還沒存的改動當標準。
struct SlotPresets { const Preset *cur = nullptr; const Preset *parent = nullptr; };

SlotPresets filament_slot(const PresetCollection &filaments, const std::string &name)
{
    if (name == filaments.get_selected_preset_name())
        return { &filaments.get_edited_preset(), filaments.get_selected_preset_parent() };
    const Preset *cur = filaments.find_preset(name, false);
    if (cur == nullptr)
        return {};
    if (cur->is_system || cur->is_default || cur->inherits().empty())
        return { cur, cur };
    return { cur, filaments.find_preset2(cur->inherits(), false) };
}

// ── 內嵌設定值全集（Eric 2026-09-17 裁 Q6 丁）────────────────────────────────
//
// 為什麼要嵌：客戶在他自己機器上產報告時，「標準」欄就是**他的**標準——他的軟體不知道
// PING 後來改過什麼，所以表格裡不會有「PING 已更新」那一類。把他的設定值全集帶回來，
// 我們就能用**自己的**標準重算一次，不必他知道自己落後、也不必再跟他要 3mf。
//
// 只嵌「他的設定值」、不嵌「他的標準」：實測（2026-09-17）只嵌有差異的鍵會漏掉 6 項，
// 而嵌完整三組 config 對還原差異沒有額外貢獻——設定值全集就是資訊完備的最小集合。
std::string embed_config_json(const DynamicPrintConfig &full)
{
    std::ostringstream ss;
    ss << "{\n";
    std::vector<std::string> keys = full.keys();
    std::sort(keys.begin(), keys.end());
    bool first = true;
    for (const std::string &k : keys) {
        if (! first)
            ss << ",\n";
        first = false;
        ss << "  \"" << json_esc(k) << "\": \"" << json_esc(full.opt_serialize(k)) << "\"";
    }
    ss << "\n}";
    return ss.str();
}

std::string now_string()
{
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
    return buf;
}

// 軟體版號＝標題列那一組（BBLTopbar::SetTitle）：「PING Slicer V3.6.x」，測試版再加「 Beta T0xx」。
// ⚠️ 不用 SLIC3R_VERSION：那是承襲 BBS 的設定檔版號（01.10.01.50），客戶與售服都認不得；
//    原型 v2（Eric 2026-09-17 定案）寫的是「軟體：PING Slicer V3.6.1」。
std::string app_version_string()
{
    std::string s = std::string("PING Slicer V") + SoftFever_VERSION;
    if (*PING_TEST_BUILD)   // 出貨版為空字串＝不附加（同標題列）
        s += std::string(" Beta ") + PING_TEST_BUILD;
    return s;
}

// ── 報告本體 ────────────────────────────────────────────────────────────────
// name ＝目前選的 preset、parent ＝拿來比的系統母版。母版名一定要印出來：
// 差異清單只有在「跟什麼比」講清楚時才有意義，尤其客戶的 preset 可能繼承自舊名母版。
struct PresetPair { std::string name, parent; };

std::string build_html(const std::vector<DiffRow> &rows,
                       const PresetPair           &printer_preset,
                       const PresetPair           &process_preset,
                       const std::vector<PresetPair> &filament_presets,   // 照槽位順序，一支一格
                       const std::string          &bundle_version,
                       const std::string          &project_name,
                       const std::vector<std::string> &warnings,
                       const std::string          &embedded_json)
{
    std::ostringstream o;
    o << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      << "<title>" << esc(_L("Parameter Difference Report")) << "</title>\n<style>\n"
      // PING CIS（值照 ping-cis 的 tokens.json）：白底為主、Charcoal Black 文字。
      // 整份不用橘：橘色只代表「你在這裡」（CIS 2026-08-24〈橘色語意收斂〉），報告沒有這個位置；
      // 頁首小標是品牌名的文字呈現＝白底一律炭黑（Eric 2026-09-28 裁 Q2）
      << ":root{--ink:#202221;--gray:#EFEFEF;--line:#E3E7E4;--muted:#5A615D;"
         "--warn:#946310;--warn-bg:#FFF6DF;}\n"
      << "*{box-sizing:border-box;}\n"
      << "body{margin:0;padding:0 20px 70px;background:#fff;color:var(--ink);"
         "font-family:\"Noto Sans TC\",\"Source Han Sans TC\",\"Microsoft JhengHei\","
         "\"Hiragino Sans\",\"Meiryo\",system-ui,sans-serif;font-size:15px;line-height:1.75;}\n"
      << ".wrap{max-width:1060px;margin:0 auto;}\n"
      << "header{padding:40px 0 14px;border-bottom:3px solid var(--ink);}\n"
      << ".kicker{font-size:12px;letter-spacing:.2em;color:var(--ink);font-weight:700;margin:0 0 10px;}\n"
      << "h1{font-size:26px;line-height:1.35;margin:0 0 10px;font-weight:700;}\n"
      << ".kv{font-size:12.5px;color:var(--muted);margin:0;}\n"
      << ".kv b{color:var(--ink);}\n"
      << ".grp{font-weight:700;font-size:16px;margin:30px 0 0;padding-bottom:5px;"
         "border-bottom:1px solid var(--line);}\n"
      // 欄寬固定、每組同一套（Eric 2026-09-28 裁 Q3）：各組的欄線上下對齊；「參數」是主識別欄，
      // 不得比輔助的「Orca key」窄（ping-ux LAY-12 同類元件長一樣、LAY-22 主識別欄不比輔助欄窄）。
      // 欄寬固定之後長字串不會再把欄撐開 ⇒ 表頭不再 nowrap、儲存格允許任意處斷行（長 key、長 G-code 值）
      << "table{width:100%;border-collapse:collapse;table-layout:fixed;margin:10px 0 4px;font-size:13px;}\n"
      << "th:nth-child(1){width:30%;} th:nth-child(2){width:24%;} th:nth-child(3),th:nth-child(4){width:16%;}"
         " th:nth-child(5){width:14%;}\n"
      << "th{text-align:left;background:var(--gray);font-weight:700;padding:7px 9px;"
         "border-bottom:2px solid var(--ink);}\n"
      << "td{padding:7px 9px;border-bottom:1px solid var(--line);vertical-align:top;overflow-wrap:anywhere;}\n"
      // 多料專案：第幾支線材標在參數名前面（Eric 2026-09-28 裁 Q1），固定在同一個視線位置；
      // 不做成徽章，免得跟右邊三種標記混在一起
      << ".fil{font-weight:700;color:var(--muted);white-space:nowrap;margin-right:6px;padding-right:6px;"
         "border-right:1px solid var(--line);}\n"
      << "td.key{font-family:Consolas,\"Courier New\",monospace;font-size:11px;color:var(--muted);}\n"
      << "td.v{font-weight:700;} td.v.std{color:var(--muted);font-weight:600;}\n"
      << "tr.inert td{opacity:.45;}\n"
      // 三種標記一樣大：每顆都帶 1px 框（沒框的那兩顆框是透明的）
      << ".b{display:inline-block;font-size:10.5px;font-weight:700;padding:0 5px;border:1px solid transparent;"
         "border-radius:2px;white-space:nowrap;margin-right:3px;}\n"
      // 「自訂」不用橘底白字（Eric 2026-09-28 裁 Q2 照 CIS：橘色只代表「你在這裡」，而且橘底白字對比只有 3.75:1）
      << ".b.user{background:#fff;color:var(--ink);border-color:var(--ink);}\n"
      << ".b.drift{background:var(--ink);color:#fff;}\n"
      << ".b.inert{background:var(--gray);color:var(--muted);}\n"
      << ".note{font-size:12.5px;color:var(--muted);margin:14px 0 0;}\n"
      // 警示訊息用警示黃，不用橘（同上 CIS 那一節）
      << ".warn{background:var(--warn-bg);color:var(--warn);border-left:3px solid var(--warn);"
         "padding:12px 16px;margin:16px 0;font-size:13.5px;}\n"
      << "footer{margin-top:44px;padding-top:14px;border-top:1px solid var(--line);"
         "font-size:12px;color:var(--muted);}\n"
      << "@media print{body{padding:0}header{padding-top:0}}\n"
      << "</style></head>\n<body><div class=\"wrap\">\n";

    o << "<header><p class=\"kicker\">PING 3D PRINTER</p>\n"
      << "<h1>" << esc(_L("Parameter Difference Report")) << "</h1>\n<p class=\"kv\">";
    if (! project_name.empty())
        o << "<b>" << esc(_L("Project")) << "</b>: " << esc(project_name) << " &middot; ";
    auto pp = [](const PresetPair &p) {
        std::string t = esc(p.name);
        if (! p.parent.empty() && p.parent != p.name)
            t += " <span style=\"color:var(--muted)\">&rarr; " + esc(p.parent) + "</span>";
        return t;
    };
    o << "<b>" << esc(_L("Printer")) << "</b>: " << pp(printer_preset) << " &middot; "
      << "<b>" << esc(_L("Process")) << "</b>: "  << pp(process_preset);
    // 一支線材照舊接在同一行；多支就一支一行（ping-ux LAY-22 多值直列），表上的「線材 N」對得回這裡
    if (filament_presets.size() == 1)
        o << " &middot; <b>" << esc(_L("Filament")) << "</b>: " << pp(filament_presets.front());
    else
        for (size_t i = 0; i < filament_presets.size(); ++i)
            o << "<br><b>" << esc(wxString::Format(_L("Filament %d"), int(i + 1))) << "</b>: " << pp(filament_presets[i]);
    o << "<br>"
      << "<b>" << esc(_L("Software")) << "</b>: " << esc(app_version_string()) << " &middot; "
      << "<b>" << esc(_L("Profile bundle")) << "</b>: " << esc(bundle_version) << " &middot; "
      << "<b>" << esc(_L("Generated")) << "</b>: " << esc(now_string())
      << "</p></header>\n";

    for (const std::string &w : warnings)
        o << "<div class=\"warn\">" << esc(w) << "</div>\n";

    if (rows.empty()) {
        o << "<p class=\"note\" style=\"font-size:15px;margin-top:26px\">"
          << esc(_L("No differences found: every setting matches the system profile."))
          << "</p>\n";
    }

    // 依主題分組（Eric 2026-09-17 裁「版面甲」）。rows 已照「組序、key」排好（見 export_param_diff_report）
    // ⇒ 照出現順序收組，就是原型的組序；組內維持鍵名排序，跨版本比對才穩定。
    std::vector<std::string> cats;
    for (const DiffRow &r : rows)
        if (std::find(cats.begin(), cats.end(), r.category) == cats.end())
            cats.push_back(r.category);

    for (const std::string &cat : cats) {
        size_t n = std::count_if(rows.begin(), rows.end(),
                                 [&cat](const DiffRow &r) { return r.category == cat; });
        o << "<div class=\"grp\">" << esc(cat) << " <span style=\"font-weight:400;"
          << "color:var(--muted);font-size:12px\">(" << n << ")</span></div>\n<table><tr>"
          << "<th>" << esc(_L("Parameter")) << "</th><th>Orca key</th>"
          << "<th>" << esc(_L("Standard value")) << "</th><th>" << esc(_L("Current value")) << "</th>"
          << "<th>" << esc(_L("Flag")) << "</th></tr>\n";
        for (const DiffRow &r : rows) {
            if (r.category != cat)
                continue;
            o << "<tr" << (r.inert ? " class=\"inert\"" : "") << "><td>";
            if (r.filament > 0)
                o << "<span class=\"fil\">" << esc(wxString::Format(_L("Filament %d"), r.filament)) << "</span>";
            o << esc(r.label) << "</td>"
              << "<td class=\"key\">" << esc(r.key) << "</td>"
              << "<td class=\"v std\">" << esc(r.std_value) << "</td>"
              << "<td class=\"v\">" << esc(r.cur_value) << "</td><td>";
            if (r.user_changed)
                o << "<span class=\"b user\">" << esc(_L("Custom")) << "</span>";
            else
                o << "<span class=\"b drift\">" << esc(_L("Updated by PING")) << "</span>";
            if (r.inert)
                o << "<span class=\"b inert\">" << esc(_L("No effect")) << "</span>";
            o << "</td></tr>\n";
        }
        o << "</table>\n";
    }

    o << "<p class=\"note\">" << esc(_L(
             "Flags: \"Custom\" means this value was changed from the system profile. "
             "\"Updated by PING\" means you did not change it — PING adjusted the standard later; "
             "it only appears when the report is generated on a machine whose profiles are newer than "
             "the ones the project was saved with. \"No effect\" means the value is inactive because of "
             "another setting in this project."))
      << "</p>\n";

    o << "<footer>" << esc(_L(
             "Generated by PING Slicer. This file also carries the current settings as data so that "
             "PING support can re-compare them against the latest PING standard."))
      << "</footer>\n</div>\n";

    // 機器可讀的那一份。放在 </div> 之後、</body> 之前，人看報告完全不受影響。
    // filament／filament_parent＝1 號槽（schema 1 原有欄位，意思不變）；filaments＝每一支，照槽位順序。
    const PresetPair first_filament = filament_presets.empty() ? PresetPair{} : filament_presets.front();
    std::string filaments_json;
    for (const PresetPair &f : filament_presets)
        filaments_json += std::string(filaments_json.empty() ? "" : ",") + "{\"name\":\"" + json_esc(f.name)
                          + "\",\"parent\":\"" + json_esc(f.parent) + "\"}";
    o << "<script type=\"application/json\" id=\"ping-param-diff-data\">\n"
      << "{\"schema\":1,\"generated\":\"" << json_esc(now_string()) << "\","
      << "\"app\":\"" << json_esc(app_version_string()) << "\","
      << "\"bundle\":\"" << json_esc(bundle_version) << "\","
      << "\"printer\":\"" << json_esc(printer_preset.name) << "\","
      << "\"printer_parent\":\"" << json_esc(printer_preset.parent) << "\","
      << "\"process\":\"" << json_esc(process_preset.name) << "\","
      << "\"process_parent\":\"" << json_esc(process_preset.parent) << "\","
      << "\"filament\":\"" << json_esc(first_filament.name) << "\","
      << "\"filament_parent\":\"" << json_esc(first_filament.parent) << "\","
      << "\"filaments\":[" << filaments_json << "],"
      << "\"config\":" << embedded_json << "}\n"
      << "</script>\n</body></html>\n";
    return o.str();
}

} // anonymous namespace

bool export_param_diff_report(wxWindow *parent)
{
    PresetBundle *pb = wxGetApp().preset_bundle;
    if (pb == nullptr)
        return false;

    const bool project_loaded = ! pb->project_different_settings_to_system.empty();
    const std::vector<std::string> &declared = pb->project_different_settings_to_system;

    // different_settings_to_system 的排列：[0]=process、[1..n]=filament、[n+1]=printer。
    //
    // ⚠️ printer 的索引要用**這份 vector 自己的長度**推，不能用目前的 filament_presets.size()：
    //    那份是載入專案當下依 num_filaments + 2 配好的，而使用者載入後還可以改噴頭數
    //    ⇒ 用現況去索引會整個錯位，把線材的鍵當成印表機的。線材那幾格同理只認開專案時的 n 支。
    const size_t idx_process   = 0;
    const size_t idx_printer   = declared.empty() ? 0 : declared.size() - 1;
    const size_t n_declared_filaments = declared.size() >= 2 ? declared.size() - 2 : 0;

    std::vector<DiffRow>     rows;
    std::vector<std::string> warnings;
    bool                     all_parents_found = true;

    // 印表機、製程：目前選中那一支對它的母版
    auto collect_selected = [&](const PresetCollection &coll, size_t idx, const PresetBundle::ProjectPresetSnapshot &opened) {
        const Preset *parent = coll.get_selected_preset_parent();
        if (parent == nullptr) {
            all_parents_found = false;
            return PresetPair{coll.get_edited_preset().name, std::string()};
        }
        collect(coll.get_edited_preset(), *parent, project_changed_keys(declared, idx), project_loaded ? &opened : nullptr, 0, rows);
        return PresetPair{coll.get_edited_preset().name, parent->name};
    };
    const PresetPair printer_pair = collect_selected(pb->printers, idx_printer, pb->project_opened_printer);
    const PresetPair process_pair = collect_selected(pb->prints,   idx_process, pb->project_opened_print);

    // 線材逐支比（Eric 2026-09-28 裁 Q1「補」；09-17 起只比第 1 支、第 2 支起的差異報告上看不到）：
    // 每一支各自跟自己的母版比、各自套自己那一格宣告清單 [1+i] 與快照。開專案之後才加的槽兩樣都沒有
    // ⇒ 那一支的差異全算自訂（同「開專案後換過預設」）。專案只有一支線材就不標第幾支。
    const size_t            n_filaments = pb->filament_presets.size();
    std::vector<PresetPair> filament_pairs;
    for (size_t i = 0; i < n_filaments; ++i) {
        const std::string &name = pb->filament_presets[i];
        const SlotPresets  slot = filament_slot(pb->filaments, name);
        filament_pairs.push_back({name, slot.parent ? slot.parent->name : std::string()});
        if (slot.cur == nullptr || slot.parent == nullptr) {
            all_parents_found = false;
            continue;
        }
        collect(*slot.cur, *slot.parent,
                i < n_declared_filaments ? project_changed_keys(declared, 1 + i) : std::set<std::string>(),
                project_loaded && i < pb->project_opened_filaments.size() ? &pb->project_opened_filaments[i] : nullptr,
                n_filaments > 1 ? int(i + 1) : 0, rows);
    }

    if (! all_parents_found)
        warnings.push_back(into_u8(_L(
            "One or more presets have no system profile to compare against (the inheritance chain is "
            "broken, or the preset was imported from outside). Those sections are missing from this report.")));

    // 先照組序（七組照原型、Orca 分類在後）、組內照 key、同一個 key 再照第幾支線材——
    // 兩份報告才比得起來，而且同一顆參數的各支線材排在一起
    std::stable_sort(rows.begin(), rows.end(), [](const DiffRow &a, const DiffRow &b) {
        if (a.group != b.group)
            return a.group < b.group;
        return a.key != b.key ? a.key < b.key : a.filament < b.filament;
    });

    // 刻意不翻譯：拿不到版本時印 "-" 就好。"unknown" 是太泛用的 msgid，
    // 佔用它會讓日後別處誤用同一條翻譯。
    std::string bundle_version = "-";
    {
        // 照 PING.json 的寫法印（01.00.01.24）：Semver::to_string() 會吃掉前導零（1.0.1.24），而售服與治理文件
        // 講的都是檔案上那個寫法。BBS 的四段版號每段本來就是兩位（semver.c：第四段存成 patch＝CC×100＋DD）
        // ⇒ 補零就逐字還原，不必再讀一次 PING.json。
        auto it = pb->vendors.find("PING");
        if (it != pb->vendors.end() && it->second.config_version.valid()) {
            const Semver &v = it->second.config_version;
            char buf[32];
            snprintf(buf, sizeof(buf), "%02d.%02d.%02d.%02d", v.maj(), v.min(), v.patch() / 100, v.patch() % 100);
            bundle_version = buf;
        }
    }

    std::string project_name;
    if (Plater *plater = wxGetApp().plater())
        project_name = into_u8(plater->get_project_name());

    const std::string html = build_html(
        rows,
        printer_pair,
        process_pair,
        filament_pairs,
        bundle_version,
        project_name,
        warnings,
        embed_config_json(pb->full_config()));

    // 檔名：參數差異_<專案名>_<日期>.html。專案名可能帶路徑不合法字元，濾掉。
    std::string stem = project_name.empty() ? std::string("no-project") : project_name;
    for (char &c : stem)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            c = '_';
    std::string date = now_string().substr(0, 10);
    date.erase(std::remove(date.begin(), date.end(), '-'), date.end());
    // 檔名不翻譯：ASCII 前綴在任何語系／檔案系統上都安全，也省掉兩個 msgid
    const wxString default_name = from_u8("param-diff_" + stem + "_" + date + ".html");

    wxFileDialog dlg(parent, _L("Export parameter difference report"),
                     from_u8(wxGetApp().app_config->get_last_output_dir("")),
                     default_name,
                     _L("Difference report") + " (*.html)|*.html",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK)
        return false;

    const std::string path = into_u8(dlg.GetPath());
    try {
        // 先寫暫存再 rename：中斷時不會留下半份看起來正常、其實截斷的報告。
        // ⚠️ 用 libslic3r 的 rename_file() 不是 boost::filesystem::rename——後者在 Windows 上
        //    目標已存在時會失敗，而這裡一定會遇到（wxFD_OVERWRITE_PROMPT ＝使用者已同意覆寫）。
        const std::string tmp = path + ".tmp";
        {
            boost::nowide::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
            if (! f.good())
                throw std::runtime_error("cannot open file for writing");
            f << html;
            f.flush();
            if (! f.good())
                throw std::runtime_error("write failed");
        }
        if (std::error_code ec = rename_file(tmp, path))
            throw std::runtime_error("rename failed: " + ec.message());
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "export_param_diff_report failed: " << e.what();
        MessageDialog(parent,
                      _L("Failed to write the report.") + "\n" + from_u8(path),
                      _L("Export parameter difference report"), wxICON_ERROR | wxOK).ShowModal();
        return false;
    }

    wxGetApp().app_config->update_last_output_dir(
        into_u8(boost::filesystem::path(path).parent_path().string()));

    BOOST_LOG_TRIVIAL(info) << "param diff report exported: " << path
                            << ", rows=" << rows.size();
    return true;
}

} // namespace GUI
} // namespace Slic3r
