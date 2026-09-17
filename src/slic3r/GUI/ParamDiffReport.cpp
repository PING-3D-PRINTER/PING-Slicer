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
#include "libslic3r/libslic3r_version.h"

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
    std::string category;   // 已翻譯的主題（取自 ConfigOptionDef::category）
    std::string label;      // 已翻譯的參數名
    std::string key;        // Orca key（語言中性，客戶與我們對話時最精確的指稱）
    std::string std_value;  // 系統母版的值
    std::string cur_value;  // 目前設定的值
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

// ── 蒐集一個 preset collection 的差異 ───────────────────────────────────────
void collect(const PresetCollection &coll,
             const std::set<std::string> &declared_by_project,
             bool project_loaded,
             std::vector<DiffRow> &rows,
             std::string &parent_name,
             bool &parent_found)
{
    const Preset *parent = coll.get_selected_preset_parent();
    parent_found = parent != nullptr;
    if (! parent_found) {
        // 繼承鏈斷了（母版被移除、或這是外部匯入的 preset）。這不是當掉的理由——
        // 報告照出，只是這一塊沒有可比的標準，在報告上要講清楚。
        parent_name.clear();
        return;
    }
    parent_name = parent->name;

    const Preset            &edited = coll.get_edited_preset();
    const DynamicPrintConfig &cur   = edited.config;
    const DynamicPrintConfig &ref   = parent->config;

    // deep_compare = true：向量型（per-extruder／per-filament）的鍵要逐格比，不能整串比。
    //
    // 🔴 但 deep_diff 對向量型回的是 **"key#index"**（Preset.cpp 的 add_correct_opts_to_diff），
    //    例如 "nozzle_temperature#0"、"z_hop#1"。直接拿它去 ConfigDef::get() 會查不到 ⇒
    //    **整批每噴頭參數會無聲消失**，而那正是最要緊的一批（溫度／回抽／Z 抬升／體積流量）。
    //    所以這裡先把 "#index" 剝掉、收斂成基底鍵；值則序列化整個向量一起呈現
    //    （"210,210,210,210"），不拆單格——per-element 沒有公開的序列化介面，
    //    自己切逗號會在含逗號的字串型參數（自訂 G-code）上壞掉。
    std::vector<std::string> keys;
    for (std::string k : coll.current_different_from_parent_options(true)) {
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
        row.category  = od->category.empty() ? into_u8(_L("Other")) : into_u8(_(od->category));
        row.label     = into_u8(_(od->full_label.empty() ? od->label : od->full_label));
        if (row.label.empty())
            row.label = key;
        row.std_value = ref.has(key) ? ref.opt_serialize(key) : std::string();
        row.cur_value = cur.has(key) ? cur.opt_serialize(key) : std::string();
        row.inert     = is_inert(key, cur);
        // 沒有專案（＝使用者在自己機器上）時，差異一律是他自己調的
        row.user_changed = ! project_loaded || declared_by_project.count(key) > 0;
        rows.push_back(std::move(row));
    }
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

// ── 報告本體 ────────────────────────────────────────────────────────────────
// name ＝目前選的 preset、parent ＝拿來比的系統母版。母版名一定要印出來：
// 差異清單只有在「跟什麼比」講清楚時才有意義，尤其客戶的 preset 可能繼承自舊名母版。
struct PresetPair { std::string name, parent; };

std::string build_html(const std::vector<DiffRow> &rows,
                       const PresetPair           &printer_preset,
                       const PresetPair           &process_preset,
                       const PresetPair           &filament_preset,
                       const std::string          &bundle_version,
                       const std::string          &project_name,
                       const std::vector<std::string> &warnings,
                       const std::string          &embedded_json)
{
    std::ostringstream o;
    o << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      << "<title>" << esc(_L("Parameter Difference Report")) << "</title>\n<style>\n"
      // PING CIS：白底為主、Charcoal Black 文字、Raised Orange 只當 accent
      << ":root{--ink:#202221;--orange:#EA4E16;--gray:#EFEFEF;--line:#DCDCDC;--muted:#6B6E6C;}\n"
      << "*{box-sizing:border-box;}\n"
      << "body{margin:0;padding:0 20px 70px;background:#fff;color:var(--ink);"
         "font-family:\"Noto Sans TC\",\"Source Han Sans TC\",\"Microsoft JhengHei\","
         "\"Hiragino Sans\",\"Meiryo\",system-ui,sans-serif;font-size:15px;line-height:1.75;}\n"
      << ".wrap{max-width:1060px;margin:0 auto;}\n"
      << "header{padding:40px 0 14px;border-bottom:3px solid var(--ink);}\n"
      << ".kicker{font-size:12px;letter-spacing:.2em;color:var(--orange);font-weight:700;margin:0 0 10px;}\n"
      << "h1{font-size:26px;line-height:1.35;margin:0 0 10px;font-weight:700;}\n"
      << ".kv{font-size:12.5px;color:var(--muted);margin:0;}\n"
      << ".kv b{color:var(--ink);}\n"
      << ".grp{font-weight:700;font-size:16px;margin:30px 0 0;padding-bottom:5px;"
         "border-bottom:1px solid var(--line);}\n"
      << "table{width:100%;border-collapse:collapse;margin:10px 0 4px;font-size:13px;}\n"
      << "th{text-align:left;background:var(--gray);font-weight:700;padding:7px 9px;"
         "border-bottom:2px solid var(--ink);white-space:nowrap;}\n"
      << "td{padding:7px 9px;border-bottom:1px solid var(--line);vertical-align:top;}\n"
      << "td.key{font-family:Consolas,\"Courier New\",monospace;font-size:11px;color:var(--muted);}\n"
      << "td.v{font-weight:700;} td.v.std{color:var(--muted);font-weight:600;}\n"
      << "tr.inert td{opacity:.45;}\n"
      << ".b{display:inline-block;font-size:10.5px;font-weight:700;padding:1px 6px;"
         "border-radius:2px;white-space:nowrap;margin-right:3px;}\n"
      << ".b.user{background:var(--orange);color:#fff;}\n"
      << ".b.drift{background:var(--ink);color:#fff;}\n"
      << ".b.inert{background:var(--gray);color:var(--muted);}\n"
      << ".note{font-size:12.5px;color:var(--muted);margin:14px 0 0;}\n"
      << ".warn{border:1px solid var(--line);border-left:3px solid var(--orange);"
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
            t += " <span style=\"color:#6B6E6C\">&rarr; " + esc(p.parent) + "</span>";
        return t;
    };
    o << "<b>" << esc(_L("Printer")) << "</b>: " << pp(printer_preset) << " &middot; "
      << "<b>" << esc(_L("Process")) << "</b>: "  << pp(process_preset) << " &middot; "
      << "<b>" << esc(_L("Filament")) << "</b>: " << pp(filament_preset) << "<br>"
      << "<b>" << esc(_L("Software")) << "</b>: " << esc(std::string(SLIC3R_VERSION)) << " &middot; "
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

    // 依主題分組（Eric 2026-09-17 裁「版面甲」）。組內維持鍵名排序，跨版本比對才穩定。
    std::vector<std::string> cats;
    for (const DiffRow &r : rows)
        if (std::find(cats.begin(), cats.end(), r.category) == cats.end())
            cats.push_back(r.category);

    for (const std::string &cat : cats) {
        size_t n = std::count_if(rows.begin(), rows.end(),
                                 [&cat](const DiffRow &r) { return r.category == cat; });
        o << "<div class=\"grp\">" << esc(cat) << " <span style=\"font-weight:400;"
          << "color:#6B6E6C;font-size:12px\">(" << n << ")</span></div>\n<table><tr>"
          << "<th>" << esc(_L("Parameter")) << "</th><th>Orca key</th>"
          << "<th>" << esc(_L("Standard value")) << "</th><th>" << esc(_L("Current value")) << "</th>"
          << "<th>" << esc(_L("Flag")) << "</th></tr>\n";
        for (const DiffRow &r : rows) {
            if (r.category != cat)
                continue;
            o << "<tr" << (r.inert ? " class=\"inert\"" : "") << ">"
              << "<td>" << esc(r.label) << "</td>"
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
    o << "<script type=\"application/json\" id=\"ping-param-diff-data\">\n"
      << "{\"schema\":1,\"generated\":\"" << json_esc(now_string()) << "\","
      << "\"app\":\"" << json_esc(SLIC3R_VERSION) << "\","
      << "\"bundle\":\"" << json_esc(bundle_version) << "\","
      << "\"printer\":\"" << json_esc(printer_preset.name) << "\","
      << "\"printer_parent\":\"" << json_esc(printer_preset.parent) << "\","
      << "\"process\":\"" << json_esc(process_preset.name) << "\","
      << "\"process_parent\":\"" << json_esc(process_preset.parent) << "\","
      << "\"filament\":\"" << json_esc(filament_preset.name) << "\","
      << "\"filament_parent\":\"" << json_esc(filament_preset.parent) << "\","
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
    // 線材只取第 1 槽（1 號料）——多料機每槽各有 preset，v1 先比主槽，其餘走售服端腳本。
    const size_t n_filaments   = pb->filament_presets.size();
    const size_t idx_process   = 0;
    const size_t idx_filament  = 1;
    const size_t idx_printer   = n_filaments + 1;

    std::vector<DiffRow>     rows;
    std::vector<std::string> warnings;
    std::string              parent_process, parent_filament, parent_printer;
    bool                     ok_process = false, ok_filament = false, ok_printer = false;

    collect(pb->printers,  project_changed_keys(declared, idx_printer),  project_loaded,
            rows, parent_printer,  ok_printer);
    collect(pb->prints,    project_changed_keys(declared, idx_process),  project_loaded,
            rows, parent_process,  ok_process);
    collect(pb->filaments, project_changed_keys(declared, idx_filament), project_loaded,
            rows, parent_filament, ok_filament);

    if (! ok_printer || ! ok_process || ! ok_filament)
        warnings.push_back(into_u8(_L(
            "One or more presets have no system profile to compare against (the inheritance chain is "
            "broken, or the preset was imported from outside). Those sections are missing from this report.")));

    // 依主題分組排序：先照 category 出現順序、組內照 key，兩份報告才比得起來
    std::stable_sort(rows.begin(), rows.end(),
                     [](const DiffRow &a, const DiffRow &b) { return a.key < b.key; });

    std::string bundle_version;
    {
        auto it = pb->vendors.find("PING");
        if (it != pb->vendors.end())
            bundle_version = it->second.config_version.to_string();
        if (bundle_version.empty())
            bundle_version = into_u8(_L("unknown"));
    }

    std::string project_name;
    if (Plater *plater = wxGetApp().plater())
        project_name = into_u8(plater->get_project_name());

    const std::string html = build_html(
        rows,
        PresetPair{pb->printers.get_edited_preset().name,  parent_printer},
        PresetPair{pb->prints.get_edited_preset().name,    parent_process},
        PresetPair{pb->filaments.get_edited_preset().name, parent_filament},
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
