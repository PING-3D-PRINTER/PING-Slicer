// PING 照片磚「每層循環洗料塔」——幾何與每層計畫。說明見 PingCycleTower.hpp 檔頭。
#include "PingCycleTower.hpp"
#include "PingColorMix.hpp"
#include "PingDeltaReach.hpp"
#include "../Print.hpp"
#include "../Layer.hpp"
#include "../Model.hpp"
#include "../ClipperUtils.hpp"
#include "../Flow.hpp"
#include "../Exception.hpp"
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Slic3r {
namespace PingCycle {

static const std::vector<std::string>& channels_for(const std::string& mode)
{
    static const std::vector<std::string> dual = {"E1", "E0"};
    static const std::vector<std::string> quad = {"E3", "E2", "E1", "E0"};
    static const std::vector<std::string> none;
    return mode == "dual" ? dual : mode == "quad" ? quad : none;
}

std::string stage_label(const std::vector<int>& channels)
{
    std::string s;
    for (size_t i = 0; i < channels.size(); ++i) {
        if (i) s += '+';
        s += 'E';
        s += char('0' + channels[i]);
    }
    return s;
}

std::string recipe_for(const std::string& mode, const std::vector<int>& channels)
{
    if (channels.empty()) return "";
    if (mode == "dual") {
        // S＝E0（第 1 路最淺）在本段的佔比：純 E0→S1、純 E1→S0、兩支一起→S0.5
        int e0 = 0;
        for (int c : channels) { if (c < 0 || c > 1) return ""; if (c == 0) ++e0; }
        std::ostringstream os;
        os << "M6051 S" << (double(e0) / double(channels.size()));
        return os.str();
    }
    if (mode == "quad") {
        // 料路索引直接對應 M6052 的 A/B/C/D（E0→A…E3→D）。等比分配、**餘數給第一支**
        // ⇒ {E1,E2,E3} 得 "M6052 A0 B34 C33 D33"（Eric 2026-09-10 指定的比例）。
        // M6052 只吃整數百分比且和必須＝100（見 PingColorMix.hpp），所以不能寫 33.3。
        int       pct[4] = {0, 0, 0, 0};
        const int n      = (int) channels.size();
        const int base   = 100 / n;
        for (int c : channels) { if (c < 0 || c > 3) return ""; pct[c] = base; }
        pct[channels.front()] += 100 - base * n;
        std::ostringstream os;
        os << "M6052 A" << pct[0] << " B" << pct[1] << " C" << pct[2] << " D" << pct[3];
        return os.str();
    }
    return "";
}

static bool read_param(const std::string& cmd, char key, double& out)
{
    std::istringstream ss(cmd);
    std::string tok;
    while (ss >> tok) {
        if (tok.size() > 1 && tok[0] == key) {
            try { out = std::stod(tok.substr(1)); return true; } catch (...) { return false; }
        }
        if (tok[0] == ';') break;
    }
    return false;
}

bool is_pure_light_recipe(const std::string& cmd)
{
    if (boost::starts_with(cmd, "M6051")) {
        double s = -1.;
        return read_param(cmd, 'S', s) && std::fabs(s - 1.0) < 1e-6;
    }
    if (boost::starts_with(cmd, "M6052")) {
        double a = -1., b = -1., c = -1., d = -1.;
        if (!read_param(cmd, 'A', a) || !read_param(cmd, 'B', b) || !read_param(cmd, 'C', c) || !read_param(cmd, 'D', d))
            return false;
        return std::fabs(a - 100.) < 1e-6 && std::fabs(b) < 1e-6 && std::fabs(c) < 1e-6 && std::fabs(d) < 1e-6;
    }
    return false;
}

double light_score(const std::string& cmd)
{
    if (boost::starts_with(cmd, "M6051")) {
        double s = -1.;
        return read_param(cmd, 'S', s) ? s : -1.;
    }
    if (boost::starts_with(cmd, "M6052")) {
        double a = -1., b = -1., c = -1., d = -1.;
        if (!read_param(cmd, 'A', a) || !read_param(cmd, 'B', b) || !read_param(cmd, 'C', c) || !read_param(cmd, 'D', d))
            return -1.;
        // 純 A100 → 1.0、純 D100 → 0.0，與 is_pure_light_recipe 的判定相容。
        return (a * 3. + b * 2. + c * 1. + d * 0.) / 300.;
    }
    return -1.;
}

bool enabled_for(const Print& print)
{
    for (const PrintObject* obj : print.objects())
        if (obj->config().ping_pt_cycle.value)
            return true;
    return false;
}

// 說明（含「為什麼排除角落固定塊」）見 PingCycleTower.hpp 的宣告處。
void collect_photo_parts(const Model& model, std::vector<PingMix::PhotoPartAssignment>& out)
{
    out.clear();
    for (const ModelObject* obj : model.objects) {
        for (const ModelVolume* vol : obj->volumes) {
            if (!vol->is_model_part()) continue;   // 修飾／支撐體不參與
            // 角落固定塊：刻意加進來的非磚零件，不該被要求有配方（牌 c-0922-ACC-05）
            const ConfigOption* keep_clear = vol->config.option("ping_keep_clear_of_parts");
            if (keep_clear != nullptr && keep_clear->getBool()) continue;
            const ConfigOption* extruder = vol->config.option("extruder");
            if ((extruder == nullptr || extruder->getInt() == 0) && vol->get_object() != nullptr)
                extruder = vol->get_object()->config.option("extruder");
            const int explicit_tool = extruder != nullptr && extruder->getInt() > 0 ? extruder->getInt() - 1 : -1;
            out.push_back({explicit_tool, vol->name});
        }
    }
}

bool collect_palette(const Model& model, std::map<int, std::string>& palette, std::string& reason)
{
    std::vector<PingMix::PhotoPartAssignment> parts;
    collect_photo_parts(model, parts);
    PingMix::PhotoPalette parsed;
    const auto status = PingMix::collect_photo_palette(parts, parsed, reason);
    if (status != PingMix::PhotoPaletteStatus::Valid) {
        if (reason.empty()) reason = "not a photo-tile plate";
        palette.clear();
        return false;
    }
    palette = parsed.recipes;
    return true;
}

// `ping_pt_cycle_laps` 支援兩種寫法，都是**由內往外**：
//   舊格式：純數字逗號串 "1,1,1,2" ⇒ 段數必須等於料路數，依 channels_for(mode) 的順序各配一支純料
//           （quad＝E3,E2,E1,E0）。既有 3MF 走這條，語意一字不變。
//   新格式：每段寫 "<料路集合>:<圈數>"，料路集合＝E 後接一串路號 ⇒ 一段可以多支料**同時**擠出。
//           quad 預設 "E123:2,E0:4"＝最內 2 圈 E1+E2+E3 一起洗、最外 4 圈純白（Eric 2026-09-10，牌 c-0910-WT-10）。
static bool parse_stages(const std::string& s, const std::string& mode,
                         std::vector<StageSpec>& out, std::string& why)
{
    out.clear();
    const auto& chans = channels_for(mode);
    if (chans.empty()) { why = "ping_pt_cycle_mode must be dual or quad, got '" + mode + "'"; return false; }
    const int n_ch = (int) chans.size();

    std::vector<std::string> toks, items;
    boost::split(toks, s, boost::is_any_of(",; "), boost::token_compress_on);
    for (std::string t : toks) { boost::trim(t); if (!t.empty()) items.push_back(t); }
    if (items.empty()) { why = "ping_pt_cycle_laps is empty"; return false; }

    const bool legacy = std::none_of(items.begin(), items.end(),
                                     [](const std::string& t) { return t.find(':') != std::string::npos; });
    if (legacy) {
        if ((int) items.size() != n_ch) {
            why = "ping_pt_cycle_laps needs " + std::to_string(n_ch) + " entries for " + mode; return false;
        }
        for (int i = 0; i < n_ch; ++i) {
            StageSpec sp;
            sp.channels.push_back(chans[i][1] - '0');          // "E3" → 3
            try { sp.laps = std::stoi(items[i]); } catch (...) { why = "bad lap count '" + items[i] + "'"; return false; }
            out.push_back(std::move(sp));
        }
    } else {
        for (const std::string& it : items) {
            const size_t colon = it.find(':');
            if (colon == std::string::npos) { why = "stage '" + it + "' needs <channels>:<laps>"; return false; }
            std::string cs = it.substr(0, colon), ls = it.substr(colon + 1);
            boost::trim(cs); boost::trim(ls);
            if (cs.size() < 2 || (cs[0] != 'E' && cs[0] != 'e')) {
                why = "stage '" + it + "': channels must look like E0 or E123"; return false;
            }
            StageSpec sp;
            for (size_t k = 1; k < cs.size(); ++k) {
                if (cs[k] < '0' || cs[k] > '9') { why = "stage '" + it + "': bad channel id"; return false; }
                const int c = cs[k] - '0';
                if (c >= n_ch) { why = "stage '" + it + "': E" + std::to_string(c) + " does not exist in " + mode; return false; }
                if (std::find(sp.channels.begin(), sp.channels.end(), c) != sp.channels.end()) {
                    why = "stage '" + it + "': channel repeated"; return false;
                }
                sp.channels.push_back(c);
            }
            try { sp.laps = std::stoi(ls); } catch (...) { why = "bad lap count '" + ls + "'"; return false; }
            out.push_back(std::move(sp));
        }
    }
    for (const StageSpec& sp : out) {
        if (sp.laps < 1)        { why = "every lap count must be >= 1"; return false; }
        if (sp.channels.empty()) { why = "every stage needs at least one channel"; return false; }
    }
    // 最外一段必須是純 E0：塔的最後一段負責把腔內洗成最淺色，模型段才接得上（模型是淺→深排序），
    // 而首層 brim 也是接在最後一段之後長出去的（GCode.cpp）——不是純 E0 的話塔底會出現一圈深色。
    if (!(out.back().channels.size() == 1 && out.back().channels[0] == 0)) {
        why = "the outermost stage must be pure E0 (the lightest channel)"; return false;
    }
    return true;
}

std::unique_ptr<Tower> create(const Print& print, std::string& why)
{
    const PrintObject* owner = nullptr;
    for (const PrintObject* obj : print.objects())
        if (obj->config().ping_pt_cycle.value) { owner = obj; break; }
    if (owner == nullptr) { why = "no object enables ping_pt_cycle"; return nullptr; }
    if (print.config().print_sequence == PrintSequence::ByObject) { why = "print_sequence=by object is not supported by the cycle tower"; return nullptr; }

    Settings st;
    st.enabled = true;
    st.mode    = owner->config().ping_pt_cycle_mode.value;
    if (!parse_stages(owner->config().ping_pt_cycle_laps.value, st.mode, st.stages, why)) return nullptr;
    st.size_mm = (float) owner->config().ping_pt_cycle_size.value;
    st.gap_mm  = (float) owner->config().ping_pt_cycle_gap.value;
    st.brim_mm = (float) owner->config().ping_pt_cycle_brim.value;
    st.max_flow = (float) owner->config().ping_pt_cycle_max_flow.value;
    std::map<int, std::string> palette;
    if (!collect_palette(print.model(), palette, why)) return nullptr;

    const float nozzle = (float) print.config().nozzle_diameter.get_at(0);
    // 沒帶尺寸時的預設＝25 mm 固定（Eric 2026-09-08「固定 25」）。原式 44×口徑/0.4 的用意是怕大口徑讓塔內路徑斷掉，
    // 但 照片磚_循環洗料塔/size_sweep.py 實算：路徑連續的最小邊長 0.4／0.6＝8.5 mm、1.0＝12.5 mm ⇒ 25 mm 對全口徑都有兩倍餘裕，
    // 且 25 mm 可容 0.4 25 圈／0.6 18 圈／1.0 10 圈（現行 3～5 圈）。等比放大真正的代價是佔床：0.6 變 66 mm ⇒ 100 mm 磚在
    // FD300（Ø300）上，塔含 brim 的最遠角算出來是半徑 152.9 mm、超過床的 150；固定 25 mm 降到 108.3 mm。
    // 真的放不下或圈數過多時，geometry_for() 仍會把「圈斷裂／空腔封閉」報成 SlicingError，不會默默印出爛塔。
    const float size   = st.size_mm > 0.f ? st.size_mm : 25.f;

    // 塔位置：**預設**擺在所有物件包絡的 +X 側、Y 置中；與模型外緣距 gap（brim 另加）。
    // 🔴 右邊放不下時才改擺**後方（+Y）**（Eric 2026-09-16 裁「乙」）：塔在 +X 時最外緣＝磚寬/2 + gap + brim + size + brim，
    //    長磚會把它推出床外（實算：FF600 半徑 300 ⇒ 磚寬上限 468 mm，而四料絹印撞色要 480 mm）。
    //    ⛔ **不可以用「把磚轉 90°」繞過**——照片磚必須沿 X 擺，校正表是在 X 方向量的（規格 §6 R6-12，Eric 同日裁）。
    //    刻意寫成「放得下就照舊」：既有磚的 G-code 逐位不變，只有**本來就會直接報錯**的情形才走新路。
    BoundingBox bb;
    for (const PrintObject* obj : print.objects())
        for (const PrintInstance& inst : obj->instances()) {
            BoundingBox b = obj->bounding_box();
            b.translate(inst.shift);
            bb.merge(b);
        }
    if (!bb.defined) { why = "no printable instance"; return nullptr; }

    // 列印範圍檢查（塔身＋brim 都要在床內）
    BoundingBoxf bed;
    for (const Vec2d& p : print.config().printable_area.values) bed.merge(p);
    const double half = size / 2. + st.brim_mm;
    const double off  = st.gap_mm + st.brim_mm + size / 2.;
    auto fits = [&bed, half](const Vec2d& c) {
        return !bed.defined || (c.x() - half >= bed.min.x() && c.x() + half <= bed.max.x() &&
                                c.y() - half >= bed.min.y() && c.y() + half <= bed.max.y());
    };
    // 🔴 兩個候選位置都在**scaled 整數座標**直接算，不要繞 mm 再 scale 回來——
    //    那個 round-trip 會讓既有（塔在右邊）的磚差個 1 unit，就破壞了「既有 G-code 逐位不變」這個前提。
    const Point beside_c(coord_t(bb.max.x() + scale_(off)), coord_t((bb.min.y() + bb.max.y()) / 2));
    const Point behind_c(coord_t((bb.min.x() + bb.max.x()) / 2), coord_t(bb.max.y() + scale_(off)));
    auto mm = [](const Point& p) { return Vec2d(unscale<double>(p.x()), unscale<double>(p.y())); };
    // 🆕 AIP 第二班（規格 R9-11 第二輪 Q13 甲，開發中清單 #22）：右邊的塔在**磚頂端那個高度搆不到**也改擺後面。
    //    delta 越往上噴頭搆得到的圈越小（PingDeltaReach）；上面的 fits 只看盤面外框、沒看高度 ⇒
    //    FF600 磚高 580 時右邊的塔頂端搆不到，機台要印到頂才報超出範圍停機。
    //    只比塔身四角（brim 只在第一層）；座標換成本盤的盤面座標（printable_area 是盤內座標、物件是世界座標）。
    //    認不得機型（find 回 nullptr）＝不檢查；放得下又搆得到的磚一律照舊＝既有 G-code 逐位不變。
    const PingDeltaReach::Geometry* geo = PingDeltaReach::find(print.config().printer_model.value);
    double top_z = 0.;
    for (const PrintObject* obj : print.objects())
        if (!obj->layers().empty())
            top_z = std::max(top_z, obj->layers().back()->print_z);
    const double reach   = geo != nullptr ? PingDeltaReach::reach_at(*geo, top_z) : 0.;
    const Vec2d  bed_mid = bed.defined ? bed.center() : Vec2d::Zero();
    const Vec2d  plate_o(print.get_plate_origin().x(), print.get_plate_origin().y());
    auto reachable = [&](const Point& c) {
        if (geo == nullptr)
            return true;
        const Vec2d p = mm(c) - plate_o - bed_mid;
        return std::hypot(std::abs(p.x()) + size / 2., std::abs(p.y()) + size / 2.) <= reach + 1e-6;
    };
    bool behind = !fits(mm(beside_c)) && fits(mm(behind_c));
    const bool out_of_reach = !behind && !reachable(beside_c) && fits(mm(behind_c)) && reachable(behind_c);
    if (out_of_reach)
        behind = true;
    const Point center = behind ? behind_c : beside_c;
    const Vec2d c_mm   = mm(center);
    if (!fits(c_mm)) {
        std::ostringstream os;
        os << "PING photo-tile cycle tower does not fit on the bed, neither beside the tile (" << mm(beside_c).x() << "," << mm(beside_c).y()
           << ") nor behind it (" << mm(behind_c).x() << "," << mm(behind_c).y() << "); size " << size
           << " mm + brim " << st.brim_mm << " mm; bed x " << bed.min.x() << ".." << bed.max.x() << " y " << bed.min.y() << ".." << bed.max.y();
        why = os.str();
        return nullptr;
    }
    if (!reachable(center)) {
        std::ostringstream os;
        os << "PING photo-tile cycle tower is out of the nozzle's reach at the top of the tile (height " << top_z << " mm): "
           << geo->family << " reaches only " << reach << " mm from the bed centre there, neither beside the tile nor behind it; "
           << "lower the tile or make it narrower";
        why = os.str();
        return nullptr;
    }

    auto tower = std::make_unique<Tower>(st, palette, nozzle, center, size, behind);
    BOOST_LOG_TRIVIAL(info) << "PING photo-tile cycle tower: mode=" << st.mode << " laps=" << owner->config().ping_pt_cycle_laps.value
                            << " size=" << size << " max_flow=" << st.max_flow << " center=(" << c_mm.x() << "," << c_mm.y() << ")"
                            << (out_of_reach ? " [behind the tile: beside out of reach at the top]" : behind ? " [behind the tile: beside did not fit]" : "")
                            << " palette=" << palette.size()
                            << " pure-E0 tools=" << tower->pure_light_tools().size();
    return tower;
}

double Tower::tower_speed(double outer_wall_speed, double mm3_per_mm) const
{
    double speed = outer_wall_speed > 0. ? outer_wall_speed : 60.;
    if (m_settings.max_flow > 0.f && mm3_per_mm > 0.)
        speed = std::min(speed, double(m_settings.max_flow) / mm3_per_mm);
    return speed;
}

Tower::Tower(Settings s, std::map<int, std::string> palette, float nozzle, Point center, float size, bool behind)
    : m_settings(std::move(s)), m_palette(std::move(palette)), m_nozzle(nozzle), m_size(size), m_center(center), m_behind(behind)
{
    int lap = 1;
    for (const StageSpec& sp : m_settings.stages) {
        Stage st;
        st.channel    = stage_label(sp.channels);
        st.recipe_cmd = recipe_for(m_settings.mode, sp.channels);
        st.first_lap  = lap;
        st.last_lap   = lap + sp.laps - 1;
        lap           = st.last_lap + 1;
        m_stages.push_back(std::move(st));
    }
    for (const auto& kv : m_palette)
        if (is_pure_light_recipe(kv.second))
            m_pure_light_tools.push_back((unsigned int) kv.first);
    build_outline();
    build_split_roles();
}

void Tower::build_split_roles()
{
    m_split_roles.clear();
    // 四料照 §12 R12-3「不動」（Eric 2026-09-13「等雙料驗證後再說」）。
    if (m_settings.mode != "dual" || m_palette.size() < 2)
        return;
    // 圈數要夠分：最淺色固定兩圈、其餘每色至少一圈 ⇒ 總圈數 ≥ 色數 + 1（＝ §12 R12-3「塔圈數＝色階數＋1」的下限）。
    // 寫成 ≥ 而不是 ==，同時顧到兩件事：① K=2 照 R12-4 維持 4 圈（多的那圈併給最深色 ⇒ 與 T044 出貨版逐位相同）
    // ② 某個色階在這張圖上沒有像素時 palette 會少一色，圈數仍然夠分 ⇒ 不會靜默退回「層首一趟整塔」（白塊回來）。
    const int laps = m_settings.total_laps();
    if (laps < (int) m_palette.size() + 1)
        return;
    std::vector<std::pair<double, int>> scored;   // (亮度分數, tool)，與 ToolOrdering 淺→深排序同一把尺
    for (const auto& kv : m_palette) {
        const double s = light_score(kv.second);
        if (s < 0.)
            return;
        scored.emplace_back(s, kv.first);
    }
    std::sort(scored.begin(), scored.end(), [](const std::pair<double, int>& a, const std::pair<double, int>& b) { return a.first > b.first; });
    for (size_t i = 1; i < scored.size(); ++i)
        if (std::fabs(scored[i - 1].first - scored[i].first) < 1e-9)
            return;                                // 同亮度分不出誰淺誰深 ⇒ 不猜，照舊整塔
    // 由淺到深配圈：最淺色拿最外兩圈，其餘由外往內各一圈，最深色把剩下的全收（只有 K=2 的 4 圈特例會多於一圈）。
    m_split_roles.push_back({ scored.front().second, { laps - 1, laps } });
    int next_outer = laps - 2;
    for (size_t i = 1; i < scored.size(); ++i) {
        const int innermost = i + 1 == scored.size() ? 1 : next_outer;
        std::vector<int> rings;
        for (int r = innermost; r <= next_outer; ++r)
            rings.push_back(r);                    // 已是由內往外，split_plan 再排一次也不變
        next_outer = innermost - 1;
        m_split_roles.push_back({ scored[i].second, std::move(rings) });
    }
    std::ostringstream os;
    for (size_t i = 0; i < m_split_roles.size(); ++i) {
        os << (i ? " / " : "") << "T" << m_split_roles[i].first << " ring";
        for (size_t k = 0; k < m_split_roles[i].second.size(); ++k)
            os << (k ? "," : " ") << m_split_roles[i].second[k];
    }
    BOOST_LOG_TRIVIAL(info) << "PING photo-tile cycle tower split (" << m_palette.size() << " colours over " << laps
                            << " rings, light->dark): " << os.str();
}

// R12-7：雙料配方 M6051 S<v> 的 v＝**最淺那一支**的佔比 ⇒ 出得到最淺那支的條件是 v>0、最深那支是 v<1；
// 0<v<1 的混合配方一次就把兩支都洗到（Q3「不設混合色門檻」）。
static void dual_material_use(double light_s, bool& uses_light, bool& uses_dark)
{
    constexpr double kEps = 1e-9;
    uses_light = light_s > kEps;
    uses_dark  = light_s < 1. - kEps;
}

SplitPlan Tower::split_plan(const std::vector<unsigned int>& layer_extruders) const
{
    SplitPlan plan;
    if (m_split_roles.empty())
        return plan;
    auto present = [&layer_extruders](int tool) {
        return std::find(layer_extruders.begin(), layer_extruders.end(), (unsigned int) tool) != layer_extruders.end();
    };
    auto use_of = [this](int tool, bool& uses_light, bool& uses_dark) {
        uses_light = uses_dark = false;
        auto it = m_palette.find(tool);
        if (it != m_palette.end())
            dual_material_use(light_score(it->second), uses_light, uses_dark);
    };
    const int n = (int) m_split_roles.size();

    // ① R12-7 先問一句：本層這兩支料都洗得到嗎？（任何混合配方就算兩支都洗到＝Q3 不設門檻）
    bool covers_light = false, covers_dark = false, any_present = false;
    for (int i = 0; i < n; ++i) {
        if (! present(m_split_roles[i].first))
            continue;
        any_present = true;
        bool l = false, d = false;
        use_of(m_split_roles[i].first, l, d);
        covers_light |= l;
        covers_dark  |= d;
    }
    if (! any_present)
        return plan;                                           // 本層一個 palette 顏色都沒有 ⇒ 照舊整塔

    // ② 缺哪一支就挑補洗色：缺最淺那支就從**最淺**的缺色挑起、缺最深那支就從**最深**的挑起
    //    （m_split_roles 已由淺到深）。它沿用自己 R12-1 的原圈位，不另外加圈 ⇒ 總圈數不變（Q2）。
    int refill_index = -1;
    if (! covers_light || ! covers_dark) {
        const bool need_light = ! covers_light;
        for (int k = 0; k < n; ++k) {
            const int i = need_light ? k : n - 1 - k;
            if (present(m_split_roles[i].first))
                continue;
            bool l = false, d = false;
            use_of(m_split_roles[i].first, l, d);
            if (need_light ? l : d) { refill_index = i; break; }
        }
    }

    // ③ 配圈：規則與 0916 版一字不變，只多一件——**補洗色收回自己的原圈位**。
    // 其餘缺色併圈的對象依舊只看「本層真的要印的顏色」，**不把圈倒給補洗色**：
    // 補洗是「補一趟」，不是把塔的大半段改成另一支料。以 K=4、本層只有純白為例，
    // 補洗的深色只拿最內第 1 圈，中間缺的那幾圈照舊併給白色（外皮維持最淺）——
    // 不這樣寫的話深色會一口氣吃掉 laps-1 圈，與 Eric 0920「節省時間、減少材料浪費」的裁示相反。
    std::map<unsigned int, std::vector<int>> own;
    for (int i = 0; i < n; ++i) {
        int owner = (present(m_split_roles[i].first) || i == refill_index) ? m_split_roles[i].first : -1;
        for (int j = i + 1; owner < 0 && j < n; ++j)          // 缺色：先給同層下一個（較深）顏色
            if (present(m_split_roles[j].first)) owner = m_split_roles[j].first;
        for (int j = i - 1; owner < 0 && j >= 0; --j)         // 後面沒有：給前一個（較淺）顏色
            if (present(m_split_roles[j].first)) owner = m_split_roles[j].first;
        if (owner < 0)
            return SplitPlan();                                // 走不到（any_present 已成立），保險
        std::vector<int>& rings = own[(unsigned int) owner];
        rings.insert(rings.end(), m_split_roles[i].second.begin(), m_split_roles[i].second.end());
    }
    if (refill_index >= 0) {
        auto it = own.find((unsigned int) m_split_roles[refill_index].first);
        if (it != own.end()) {
            plan.refill_tool  = m_split_roles[refill_index].first;
            plan.refill_rings = std::move(it->second);
            std::sort(plan.refill_rings.begin(), plan.refill_rings.end());   // 一趟內由內往外
            own.erase(it);
        }
    }
    for (unsigned int t : layer_extruders) {
        auto it = own.find(t);
        if (it == own.end())
            continue;
        std::vector<int> rings = std::move(it->second);
        std::sort(rings.begin(), rings.end());                 // 一趟內由內往外
        plan.visits.emplace_back(t, std::move(rings));
        own.erase(it);
    }
    return plan;
}

// jtRound 的第 4 參數在 ClipperUtils 裡是 ArcTolerance（scaled 單位）；不給＝DefaultMiterLimit 3 ＝ 3e-6 mm ⇒ 每圈五千點、
// 塔段一層 5 萬行 G-code（0908 真切 422 MB 實測）。0.02 mm 已遠細於噴嘴，圈點數降一到兩個數量級。
static constexpr double kArcTolMm  = 0.02;
static constexpr double kSimplifyMm = 0.01;   // 圈路徑再走一次 Douglas-Peucker（同 Orca 預設 resolution 量級）
static inline double arc_tol() { return scale_(kArcTolMm); }

static Polygon circle_polygon(const Point& c, double r_scaled, int n = 64)
{
    Polygon p;
    for (int i = 0; i < n; ++i) {
        const double a = 2. * M_PI * i / n;
        p.points.emplace_back(c.x() + coord_t(r_scaled * std::cos(a)), c.y() + coord_t(r_scaled * std::sin(a)));
    }
    return p;
}

static Polygon largest(const Polygons& polys)
{
    Polygon best;
    double  best_area = -1.;
    for (const Polygon& p : polys) {
        const double a = std::fabs(p.area());
        if (a > best_area) { best_area = a; best = p; }
    }
    return best;
}

void Tower::build_outline()
{
    // 與離線版 tower_geometry.py 同一套：圓角方 − 四側大圓（矢高）→ 開運算圓化交接。參數隨塔尺寸等比（44 mm 為基準）。
    const double k        = m_size / 44.;
    const double a        = m_size / 2.;
    const double corner_r = 6. * k, concave_s = 5. * k, smooth = 2. * k;
    Polygon square;
    square.points = { Point(coord_t(scale_(-a)), coord_t(scale_(-a))), Point(coord_t(scale_(a)), coord_t(scale_(-a))), Point(coord_t(scale_(a)), coord_t(scale_(a))), Point(coord_t(scale_(-a)), coord_t(scale_(a))) };
    Polygons rounded = offset(offset(square, -scale_(corner_r), ClipperLib::jtRound, arc_tol()), scale_(corner_r), ClipperLib::jtRound, arc_tol());
    const double c = a - corner_r - 1. * k;
    const double R = (c * c + concave_s * concave_s) / (2. * concave_s);
    const double d = a + R - concave_s;
    Polygons circles;
    for (const Vec2d& cc : { Vec2d(0, d), Vec2d(0, -d), Vec2d(d, 0), Vec2d(-d, 0) })
        circles.push_back(circle_polygon(Point(coord_t(scale_(cc.x())), coord_t(scale_(cc.y()))), scale_(R)));
    Polygons cut = diff(rounded, circles);
    Polygon  outline = largest(offset(offset(largest(cut), -scale_(smooth), ClipperLib::jtRound, arc_tol()), scale_(smooth), ClipperLib::jtRound, arc_tol()));
    outline.make_counter_clockwise();
    outline.translate(m_center);
    m_outline = outline;
}

static Polyline ring_from_seam(Polygon poly, const Point& anchor)
{
    poly.make_counter_clockwise();
    size_t best = 0; double bd = std::numeric_limits<double>::max();
    for (size_t i = 0; i < poly.points.size(); ++i) {
        const double dd = (poly.points[i] - anchor).cast<double>().squaredNorm();
        if (dd < bd) { bd = dd; best = i; }
    }
    return poly.split_at_index((int) best);   // 從接縫頂點起繞一周回到同一點（閉合）
}

const LayerGeometry& Tower::geometry_for(float layer_height, bool first_layer)
{
    const int key = int(std::lround(layer_height * 1000.f)) * 2 + (first_layer ? 1 : 0);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    LayerGeometry g;
    g.layer_height = layer_height;
    g.width        = m_nozzle;                       // 照片磚製程線寬＝口徑
    Flow flow(g.width, layer_height, m_nozzle);
    g.spacing     = flow.spacing();
    g.mm3_per_mm  = flow.mm3_per_mm();
    // 接縫一律朝**模型那一側**：塔在右邊時朝 −X、塔在後方時朝 −Y。朝錯邊的話離塔要橫越已印的圈。
    const Point anchor = m_behind ? Point(m_center.x(), coord_t(m_center.y() - scale_(m_size)))
                                  : Point(coord_t(m_center.x() - scale_(m_size)), m_center.y());
    const int   n = m_settings.total_laps();
    for (int i = 0; i < n; ++i) {
        const double inset = g.width / 2. + i * g.spacing;
        Polygons rings = offset(m_outline, -scale_(inset), ClipperLib::jtRound, arc_tol());
        if (rings.empty()) { g.problem = "ring at inset " + std::to_string(inset) + " mm vanishes"; break; }
        if (rings.size() != 1) { g.problem = "ring at inset " + std::to_string(inset) + " mm splits into " + std::to_string(rings.size()) + " rings"; break; }
        g.loops.push_back(ring_from_seam(rings.front(), anchor));
        g.loops.back().simplify(scale_(kSimplifyMm));
    }
    // 🔴 圈序反轉＝loops[0] 變成**最內圈**（Eric 2026-09-09：「由內先畫深色、最外部為淺色，不會在垂直方向看到顏色變化」）。
    // 上面是照 inset 由小到大生的（＝由外往內）；反轉之後，第 1 段（最深）落在最內圈、最後一段（純 E0 最淺）落在最外圈。
    // 列印順序完全沒變，仍是「深→淺」——只是現在等於由內往外走 ⇒ 塔的外皮永遠是最淺的那一路，垂直方向看不到顏色變化。
    // 附帶好處：離塔時人在最外圈，不必再橫越已印的圈。
    std::reverse(g.loops.begin(), g.loops.end());
    if (g.problem.empty()) {
        Polygons cavity = offset(m_outline, -scale_(g.width / 2. + (n - 1) * g.spacing + g.width / 2.), ClipperLib::jtRound, arc_tol());
        if (cavity.size() != 1) g.problem = "central cavity is closed or split";
    }
    if (g.problem.empty() && first_layer && m_settings.brim_mm > 0.f) {
        const int nb = (int) std::ceil(m_settings.brim_mm / g.spacing);
        for (int k = 1; k <= nb; ++k) {                  // 由內往外：接在**最後一段（最淺）的最外圈**之後繼續往外長
            Polygons rings = offset(m_outline, scale_(g.width / 2. + (k - 1) * g.spacing), ClipperLib::jtRound, arc_tol());
            if (rings.size() != 1) { g.problem = "brim ring " + std::to_string(k) + " is not a single ring"; break; }
            g.brim_loops.push_back(ring_from_seam(rings.front(), anchor));
            g.brim_loops.back().simplify(scale_(kSimplifyMm));
        }
    }
    if (!g.problem.empty())
        BOOST_LOG_TRIVIAL(error) << "PING photo-tile cycle tower geometry invalid at layer height " << layer_height << ": " << g.problem;
    return m_cache.emplace(key, std::move(g)).first->second;
}

} // namespace PingCycle
} // namespace Slic3r
