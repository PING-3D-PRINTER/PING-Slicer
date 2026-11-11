// PING(c-1111-RAFT-02)：筏層頂面獨立設定 5 鍵（raft_top_density／raft_top_line_width／raft_top_layer_height／
// raft_top_speed／raft_top_perpendicular）的「重算依賴」測試。
//
// 核心判準只有一條：「改設定後在同一個 Print 物件上重新 apply＋process」的結果，
// 要等於「用同樣的最終設定全新切一次」的結果。
// 比的東西＝該物件所有 SupportLayer 的擠出路徑（support_fills 攤平後每條 ExtrusionPath 的 role、polyline 各點、
// mm3_per_mm、width、height）、各支撐層的 print_z／height，另外順便比本體各層的 print_z（層高那一鍵會重新切層）。
//
// 每個情境另附「對照」斷言：拿沒重新 process 的舊結果去跟全新切比，斷言兩者「不相等」，證明比對函式分得出差別。

#include <catch2/catch_all.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_data.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace Slic3r;

namespace {

using Items = std::initializer_list<ConfigBase::SetDeserializeItem>;

// 比對方式。true＝逐層、逐條、逐點完全相同（含順序）；false＝每層「不看順序的線段集合」相同。
// 2026-11-12 實測「全新切兩次」逐點相同（見 "00 fresh slices are deterministic"），所以用最嚴格的那一種。
const bool k_compare_exact = true;

// ---------------------------------------------------------------------------------------------------------------
// 測試件
// ---------------------------------------------------------------------------------------------------------------

enum class Part {
    // 20×20×5 的方塊：只有筏層，沒有支撐柱。
    Cube,
    // 柱子頂著板：10×10×8 的柱＋其上 40×24×3 的板。板的四邊懸空，會長支撐柱與支撐介面。
    Tee
};

TriangleMesh part_mesh(Part part)
{
    if (part == Part::Cube)
        return make_cube(20., 20., 5.);
    TriangleMesh pillar = make_cube(10., 10., 8.);
    pillar.translate(15.f, 7.f, 0.f);
    TriangleMesh plate = make_cube(40., 24., 3.);
    plate.translate(0.f, 0.f, 8.f);
    // 兩個封閉殼合成一顆網格。切層高度（0.15、0.45…7.95、8.25）不會剛好落在兩殼共面的 z=8。
    pillar.merge(plate);
    return pillar;
}

// ---------------------------------------------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------------------------------------------

// 設定基底：5 鍵全 0／不勾。
DynamicPrintConfig base_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"nozzle_diameter", "0.6"},
        {"layer_height", 0.3},
        {"initial_layer_print_height", 0.35},
        {"raft_layers", 3},
        {"enable_support", true},
        {"support_type", "normal(auto)"},
        {"support_style", "snug"},
        {"bottom_surface_pattern", "monotonicline"},
        {"solid_infill_direction", 45},
        {"bottom_shell_layers", 3},
        {"bottom_shell_thickness", 0},
        {"wall_loops", 2},
        // 下面幾個只是把跟本功能無關、會拖慢或干擾的東西關掉。
        {"wall_generator", "classic"},
        {"sparse_infill_pattern", "grid"},
        {"sparse_infill_density", "15%"},
        {"skirt_loops", 0},
        {"brim_type", "no_brim"},
        {"enable_arc_fitting", false},
        // 預設是相對擠出量，Print::validate() 要求每層歸零（跟本功能無關的既有檢查）。
        {"layer_change_gcode", "G92 E0"},
        // 5 鍵明寫成預設值，免得預設值哪天被改掉而測試不知道。
        {"raft_top_density", "0%"},
        {"raft_top_line_width", "0"},
        {"raft_top_layer_height", 0},
        {"raft_top_speed", 0},
        {"raft_top_perpendicular", false}
    });
    return config;
}

DynamicPrintConfig with(DynamicPrintConfig config, Items items)
{
    config.set_deserialize_strict(items);
    return config;
}

// 勾了「頂面與本體第一層垂直」的基底。
DynamicPrintConfig perpendicular_config()
{
    return with(base_config(), {{"raft_top_perpendicular", true}});
}

// ---------------------------------------------------------------------------------------------------------------
// Print 的包裝
// ---------------------------------------------------------------------------------------------------------------

// test_data.cpp 的 init_print() 會先呼叫 arrange_objects(model, InfiniteBed{}, …)，在這個程式庫裡一律丟
// 「Objects could not fit on the bed」（既有用到它的測試都標成隱藏）。這裡照 test_ping_memory_estimate.cpp 的做法，
// 自己把物件擺在預設 200×200 熱床裡、不經過排列；其餘步驟照 init_print()。
struct Rig
{
    Model model;
    Print print;

    // Print::m_isBBLPrinter 沒有初始值（平常由介面或命令列設定），Print::validate() 會讀它；
    // 不先設好的話 validate 的結果看記憶體裡剛好是什麼（2026-11-12 實測同一組設定時過時不過）。
    Rig() { print.is_BBL_printer() = false; }
    Rig(Part part, const DynamicPrintConfig &config)
    {
        print.is_BBL_printer() = false;
        this->add_object(part, Vec3d(100., 100., 0.));
        this->apply(config);
    }

    ModelObject* add_object(Part part, const Vec3d &offset)
    {
        ModelObject *object = model.add_object();
        object->name += "object.stl";
        object->add_volume(part_mesh(part));
        object->add_instance()->set_offset(offset);
        object->ensure_on_bed();
        print.auto_assign_extruders(object);
        return object;
    }

    PrintBase::ApplyStatus apply(const DynamicPrintConfig &config_in)
    {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.apply(config_in);
        const PrintBase::ApplyStatus status = print.apply(model, config);
        print.set_status_silent();
        return status;
    }

    PrintObject*       object(size_t idx = 0)       { return print.get_object(idx); }
    const PrintObject* object(size_t idx = 0) const { return print.get_object(idx); }
};

// ---------------------------------------------------------------------------------------------------------------
// 快照與比對
// ---------------------------------------------------------------------------------------------------------------

struct PathRec
{
    int    role { 0 };
    double mm3_per_mm { 0. };
    float  width { 0.f };
    float  height { 0.f };
    Points points;
};

struct LayerRec
{
    double               print_z { 0. };
    double               height { 0. };
    std::vector<PathRec> paths;
    // 不是 ExtrusionPath／MultiPath／Loop／Collection 的東西（預期 0；不是 0 就代表攤平漏看了一種型別）。
    size_t               unknown_entities { 0 };
};

struct Snapshot
{
    std::vector<LayerRec> support;
    // 本體各層的 print_z。
    std::vector<double>   object_z;
};

PathRec to_rec(const ExtrusionPath &path)
{
    PathRec rec;
    rec.role       = int(path.role());
    rec.mm3_per_mm = path.mm3_per_mm;
    rec.width      = path.width;
    rec.height     = path.height;
    rec.points     = path.polyline.points;
    return rec;
}

// 攤平（保留順序）：Collection 往下走，MultiPath／Loop 拆成一條一條的 ExtrusionPath。
void collect_paths(const ExtrusionEntity *entity, std::vector<PathRec> &out, size_t &unknown)
{
    if (entity == nullptr)
        return;
    if (entity->is_collection()) {
        for (const ExtrusionEntity *child : static_cast<const ExtrusionEntityCollection*>(entity)->entities)
            collect_paths(child, out, unknown);
    } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath*>(entity)) {
        for (const ExtrusionPath &path : multi->paths)
            out.push_back(to_rec(path));
    } else if (const auto *loop = dynamic_cast<const ExtrusionLoop*>(entity)) {
        for (const ExtrusionPath &path : loop->paths)
            out.push_back(to_rec(path));
    } else if (const auto *path = dynamic_cast<const ExtrusionPath*>(entity)) {
        out.push_back(to_rec(*path));
    } else
        ++ unknown;
}

Snapshot take(const PrintObject &object)
{
    Snapshot snapshot;
    for (const SupportLayer *layer : object.support_layers()) {
        LayerRec rec;
        rec.print_z = layer->print_z;
        rec.height  = layer->height;
        collect_paths(&layer->support_fills, rec.paths, rec.unknown_entities);
        snapshot.support.push_back(std::move(rec));
    }
    for (const Layer *layer : object.layers())
        snapshot.object_z.push_back(layer->print_z);
    return snapshot;
}

size_t path_count(const Snapshot &snapshot)
{
    size_t n = 0;
    for (const LayerRec &layer : snapshot.support)
        n += layer.paths.size();
    return n;
}

size_t unknown_count(const Snapshot &snapshot)
{
    size_t n = 0;
    for (const LayerRec &layer : snapshot.support)
        n += layer.unknown_entities;
    return n;
}

std::string describe(const Snapshot &snapshot)
{
    std::ostringstream ss;
    ss << snapshot.support.size() << " support layers, " << path_count(snapshot) << " paths, "
       << snapshot.object_z.size() << " object layers; support z:";
    for (const LayerRec &layer : snapshot.support)
        ss << " " << layer.print_z << "(h" << layer.height << ",n" << layer.paths.size() << ")";
    return ss.str();
}

// 兩份快照共通的部分：本體層高度、支撐層數、各支撐層的 print_z／height。回空字串＝相同。
std::string diff_layers(const Snapshot &a, const Snapshot &b)
{
    std::ostringstream ss;
    if (a.object_z != b.object_z) {
        ss << "object layer print_z differ (" << a.object_z.size() << " vs " << b.object_z.size() << " layers";
        if (! a.object_z.empty() && ! b.object_z.empty())
            ss << ", first " << a.object_z.front() << " vs " << b.object_z.front();
        ss << ")";
        return ss.str();
    }
    if (a.support.size() != b.support.size()) {
        ss << "support layer count differs: " << a.support.size() << " vs " << b.support.size();
        return ss.str();
    }
    for (size_t i = 0; i < a.support.size(); ++ i) {
        const LayerRec &la = a.support[i];
        const LayerRec &lb = b.support[i];
        if (la.print_z != lb.print_z || la.height != lb.height) {
            ss << "support layer " << i << ": print_z/height " << la.print_z << "/" << la.height << " vs " << lb.print_z << "/" << lb.height;
            return ss.str();
        }
    }
    return {};
}

// 逐層、逐條、逐點完全相同（含順序）。回空字串＝相同，否則回第一個差異。
std::string diff_exact(const Snapshot &a, const Snapshot &b)
{
    std::string layers = diff_layers(a, b);
    if (! layers.empty())
        return layers;
    std::ostringstream ss;
    for (size_t i = 0; i < a.support.size(); ++ i) {
        const LayerRec &la = a.support[i];
        const LayerRec &lb = b.support[i];
        if (la.paths.size() != lb.paths.size()) {
            ss << "support layer " << i << " (z=" << la.print_z << "): path count " << la.paths.size() << " vs " << lb.paths.size();
            return ss.str();
        }
        for (size_t j = 0; j < la.paths.size(); ++ j) {
            const PathRec &pa = la.paths[j];
            const PathRec &pb = lb.paths[j];
            const char *what = nullptr;
            if (pa.role != pb.role)
                what = "role";
            else if (pa.mm3_per_mm != pb.mm3_per_mm)
                what = "mm3_per_mm";
            else if (pa.width != pb.width)
                what = "width";
            else if (pa.height != pb.height)
                what = "height";
            else if (pa.points != pb.points)
                what = "points";
            if (what != nullptr) {
                ss << "support layer " << i << " (z=" << la.print_z << ") path " << j << ": " << what << " differs"
                   << " [role " << pa.role << "/" << pb.role << ", mm3 " << pa.mm3_per_mm << "/" << pb.mm3_per_mm
                   << ", w " << pa.width << "/" << pb.width << ", h " << pa.height << "/" << pb.height
                   << ", npts " << pa.points.size() << "/" << pb.points.size() << "]";
                if (std::string(what) == "points") {
                    // 從第一個不同的點往前 2 點起，把兩邊各印 8 點（內部整數座標，1 單位＝1e-6 mm），看差在哪。
                    size_t first_diff = 0;
                    while (first_diff < pa.points.size() && first_diff < pb.points.size() && pa.points[first_diff] == pb.points[first_diff])
                        ++ first_diff;
                    const size_t from = first_diff >= 2 ? first_diff - 2 : 0;
                    ss << "; first differing point index " << first_diff;
                    for (const PathRec *path : { &pa, &pb }) {
                        ss << "\n    " << (path == &pa ? "A:" : "B:");
                        for (size_t k = from; k < path->points.size() && k < from + 8; ++ k)
                            ss << " (" << path->points[k].x() << "," << path->points[k].y() << ")";
                    }
                }
                return ss.str();
            }
        }
    }
    return {};
}

// 每層「不看順序、不看走向的線段集合」相同。回空字串＝相同。
std::string diff_unordered(const Snapshot &a, const Snapshot &b)
{
    std::string layers = diff_layers(a, b);
    if (! layers.empty())
        return layers;
    typedef std::tuple<int, coord_t, coord_t, coord_t, coord_t, double, float, float> Seg;
    auto segments = [](const LayerRec &layer) {
        std::vector<Seg> out;
        for (const PathRec &path : layer.paths)
            for (size_t k = 1; k < path.points.size(); ++ k) {
                Point p = path.points[k - 1];
                Point q = path.points[k];
                if (std::make_pair(q.x(), q.y()) < std::make_pair(p.x(), p.y()))
                    std::swap(p, q);
                out.emplace_back(path.role, p.x(), p.y(), q.x(), q.y(), path.mm3_per_mm, path.width, path.height);
            }
        std::sort(out.begin(), out.end());
        return out;
    };
    std::ostringstream ss;
    for (size_t i = 0; i < a.support.size(); ++ i) {
        const std::vector<Seg> sa = segments(a.support[i]);
        const std::vector<Seg> sb = segments(b.support[i]);
        if (sa != sb) {
            ss << "support layer " << i << " (z=" << a.support[i].print_z << "): segment sets differ (" << sa.size() << " vs " << sb.size() << " segments)";
            return ss.str();
        }
    }
    return {};
}

// 主要判準用的比對。
std::string diff(const Snapshot &a, const Snapshot &b)
{
    return k_compare_exact ? diff_exact(a, b) : diff_unordered(a, b);
}

// ---------------------------------------------------------------------------------------------------------------
// 既有問題的隔離（2026-11-12 實測；不是本功能造成的，5 鍵全 0 也一樣，見情境 13）
// ---------------------------------------------------------------------------------------------------------------
// PrintObject::invalidate_step() 的 posSlice 那一支（src/libslic3r/PrintObject.cpp，基準 f03b1486c0 就是這樣）
// 批次讓 posSupportMaterial 失效，卻沒有把 posSimplifySupportPath 一起列進去；批次失效又不會再回到
// 「posSupportMaterial 失效 ⇒ posSimplifySupportPath 失效」那一支。結果：只要是「重新切層」觸發的重算
// （bottom_shell_layers、layer_height、raft_layers、本功能的 raft_top_layer_height…），支撐路徑會重新產生，
// 但結尾的「支撐路徑簡化」（PrintObject::simplify_extrusion_path() 的第三段）被當成已完成而跳過，
// 重新 process 的支撐路徑就比全新切多出沒被簡化掉的點（量到的是筏層頂面轉角一個 5 奈米的小突起）。
//
// 用法：apply 之前建一個 SimplifyWatch，process 之後呼叫 redo_if_skipped()：這一輪如果真的沒重跑簡化，
// 就替它把漏掉的那一步補跑，然後再跟全新切比——用來確認「除了漏簡化以外沒有別的差異」。
// 哪天產品把這個洞補了，時間戳會變、這裡就不會再補跑。
struct SimplifyWatch
{
    PrintStateBase::TimeStamp stamp;
    explicit SimplifyWatch(const PrintObject &object) : stamp(object.step_state_with_timestamp(posSimplifySupportPath).timestamp) {}
    bool redo_if_skipped(PrintObject &object) const
    {
        if (object.step_state_with_timestamp(posSimplifySupportPath).timestamp != stamp)
            return false;
        for (SupportLayer *layer : object.support_layers())
            layer->simplify_support_extrusion_path();
        return true;
    }
};

// 用同樣的設定全新切一次。
Snapshot fresh_slice(Part part, const DynamicPrintConfig &config)
{
    Rig rig(part, config);
    rig.print.process();
    return take(*rig.object());
}

// ---------------------------------------------------------------------------------------------------------------
// 方向
// ---------------------------------------------------------------------------------------------------------------

struct Direction
{
    bool   valid { false };
    // 主方向（不分正反），度，[0, 180)。
    double deg { 0. };
    // 0～1：1＝所有線段同一個方向。直線填充加上少量轉折連接線時仍接近 1。
    double strength { 0. };
    double length_mm { 0. };
};

// 兩個不分正反的方向（度）的夾角，0～90 度。
double axial_difference(double a, double b)
{
    const double d = std::fmod(std::abs(a - b), 180.);
    return d > 90. ? 180. - d : d;
}

// 某個 role 的路徑的主方向。
// 同一層的「支撐介面」角色裡不只有直線填充（樹狀支撐還有輪廓圈、樹幹底下那塊），整包取平均會被帶偏，
// 所以先用 1° 一格、以長度加權的直方圖找出最長的那一族線，再只拿落在該族 ±5° 內的線段求方向：
// 角度加倍後取向量平均（正反走向不互相抵消）。strength＝那一族線佔該角色總長度的比例。
Direction dominant_direction(const std::vector<PathRec> &paths, ExtrusionRole role)
{
    struct Segment { double deg; double len; };
    std::vector<Segment> segments;
    double total = 0.;
    for (const PathRec &path : paths) {
        if (path.role != int(role))
            continue;
        for (size_t k = 1; k < path.points.size(); ++ k) {
            const double dx  = unscale<double>(path.points[k].x() - path.points[k - 1].x());
            const double dy  = unscale<double>(path.points[k].y() - path.points[k - 1].y());
            const double len = std::hypot(dx, dy);
            if (len <= 0.)
                continue;
            double deg = std::atan2(dy, dx) * 180. / PI;
            if (deg < 0.)
                deg += 180.;
            if (deg >= 180.)
                deg -= 180.;
            segments.push_back({ deg, len });
            total += len;
        }
    }
    Direction direction;
    if (total <= 0.)
        return direction;
    double histogram[180] = { 0. };
    for (const Segment &segment : segments)
        histogram[std::min(179, int(segment.deg))] += segment.len;
    int    best_bin = 0;
    double best_len = -1.;
    for (int bin = 0; bin < 180; ++ bin) {
        const double len = histogram[(bin + 179) % 180] + histogram[bin] + histogram[(bin + 1) % 180];
        if (len > best_len) {
            best_len = len;
            best_bin = bin;
        }
    }
    const double center = best_bin + 0.5;
    double sx = 0., sy = 0., family = 0.;
    for (const Segment &segment : segments)
        if (axial_difference(segment.deg, center) <= 5.) {
            sx     += segment.len * std::cos(2. * segment.deg * PI / 180.);
            sy     += segment.len * std::sin(2. * segment.deg * PI / 180.);
            family += segment.len;
        }
    direction.valid     = true;
    direction.deg       = 0.5 * std::atan2(sy, sx) * 180. / PI;
    if (direction.deg < 0.)
        direction.deg += 180.;
    direction.strength  = family / total;
    direction.length_mm = total;
    return direction;
}

double angle_between(const Direction &a, const Direction &b)
{
    return axial_difference(a.deg, b.deg);
}

// 本體第一層「底面實心填充」實際走的方向（直接量路徑，不讀 ping_bottom_fill_direction）。
Direction body_bottom_direction(const PrintObject &object)
{
    std::vector<PathRec> paths;
    size_t unknown = 0;
    if (object.layers().empty())
        return {};
    for (const LayerRegion *region : object.layers().front()->regions())
        collect_paths(&region->fills, paths, unknown);
    return dominant_direction(paths, erBottomSurface);
}

// print_z 落在 z 的那一層支撐層裡，「支撐介面」角色的路徑的主方向。
Direction support_interface_direction_at(const Snapshot &snapshot, double z)
{
    for (const LayerRec &layer : snapshot.support)
        if (std::abs(layer.print_z - z) < EPSILON)
            return dominant_direction(layer.paths, erSupportMaterialInterface);
    return {};
}

// 筏層最上面那層（本體坐的那層）的介面線方向。
Direction raft_top_direction(const PrintObject &object, const Snapshot &snapshot)
{
    return support_interface_direction_at(snapshot, object.slicing_parameters().raft_contact_top_z);
}

// 筏層頂面往下一層（筏層 3 層時＝第 2 層）的介面線方向。
Direction raft_below_top_direction(const PrintObject &object, const Snapshot &snapshot)
{
    return support_interface_direction_at(snapshot, object.slicing_parameters().raft_interface_top_z);
}

std::string describe(const Direction &direction)
{
    std::ostringstream ss;
    if (! direction.valid)
        ss << "(none)";
    else
        ss << direction.deg << " deg (strength " << direction.strength << ", " << direction.length_mm << " mm)";
    return ss.str();
}

// 斷言：筏層最上面那層介面線的方向與本體第一層底面填充方向垂直（90°±1°）。
void check_raft_top_perpendicular(const PrintObject &object, const Snapshot &snapshot)
{
    const Direction body = body_bottom_direction(object);
    const Direction raft = raft_top_direction(object, snapshot);
    INFO("body first layer bottom fill: " << describe(body));
    INFO("raft top interface: " << describe(raft));
    REQUIRE(body.valid);
    REQUIRE(raft.valid);
    // 主方向那一族線要佔大宗，量出來的方向才有意義（本體底面是單一方向的直線；筏層頂面那層另有轉折連接線，
    // 樹狀支撐還有輪廓圈，所以門檻放在一半）。
    CHECK(body.strength > 0.8);
    CHECK(raft.strength > 0.5);
    CHECK(angle_between(body, raft) == Catch::Approx(90.).margin(1.));
}

// ---------------------------------------------------------------------------------------------------------------
// G-code（隱藏案例用）
// ---------------------------------------------------------------------------------------------------------------

// test_data.cpp 的 gcode() 用沒有資料夾的相對檔名匯出、而且傳 nullptr 給 result，兩件事都會出事
// （見 test_ping_memory_estimate.cpp 的說明）。這裡改寫到系統暫存夾的絕對路徑。
std::string export_gcode_text(Print &print)
{
    const boost::filesystem::path temp =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("ping_raft_top_%%%%-%%%%-%%%%.gcode");
    GCodeProcessorResult result;
    print.export_gcode(temp.string(), &result, nullptr);
    std::ifstream in(temp.string());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    boost::nowide::remove(temp.string().c_str());
    return text;
}

// 「支撐介面」這個特徵、而且層高度在 z_lo～z_hi 之間的擠出移動，各自的速度（mm/s）。
std::vector<double> support_interface_speeds(const std::string &gcode, double z_lo, double z_hi)
{
    std::vector<double> speeds;
    std::istringstream in(gcode);
    std::string line;
    double z = -1., feedrate = 0.;
    std::string feature;
    auto value_after = [](const std::string &text, char letter, double &value) {
        for (size_t i = 0; i < text.size(); ++ i)
            if (text[i] == letter && (i == 0 || text[i - 1] == ' ')) {
                try { value = std::stod(text.substr(i + 1)); } catch (...) { return false; }
                return true;
            }
        return false;
    };
    while (std::getline(in, line)) {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("; Z_HEIGHT:", 0) == 0) { z = std::stod(line.substr(11)); continue; }
        if (line.rfind(";Z:", 0) == 0)         { z = std::stod(line.substr(3)); continue; }
        if (line.rfind("; FEATURE:", 0) == 0)  { feature = line.substr(10); feature.erase(0, feature.find_first_not_of(' ')); continue; }
        if (line.rfind(";TYPE:", 0) == 0)      { feature = line.substr(6); continue; }
        if (line.rfind("G1 ", 0) != 0)
            continue;
        const std::string code = line.substr(0, line.find(';'));
        double f = 0., e = 0., x = 0.;
        if (value_after(code, 'F', f))
            feedrate = f;
        const bool moves    = value_after(code, 'X', x) || value_after(code, 'Y', x);
        const bool extrudes = value_after(code, 'E', e) && e > 0.;
        if (moves && extrudes && feature == "Support interface" && z > z_lo && z < z_hi)
            speeds.push_back(feedrate / 60.);
    }
    return speeds;
}

} // namespace

// ===============================================================================================================
// 00 前置：全新切兩次本來就一樣嗎（決定主要判準能不能用「逐點相同」）
// ===============================================================================================================

TEST_CASE("PingRaftTop 00: fresh slices are deterministic", "[PingRaftTop]")
{
    for (Part part : { Part::Cube, Part::Tee })
        for (bool perpendicular : { false, true }) {
            INFO("part=" << (part == Part::Cube ? "cube" : "tee") << " perpendicular=" << perpendicular);
            const DynamicPrintConfig config = perpendicular ? perpendicular_config() : base_config();
            const Snapshot first  = fresh_slice(part, config);
            const Snapshot second = fresh_slice(part, config);
            INFO(describe(first));
            // 筏層 3 層一定有 3 層支撐層；柱頂板另外有支撐柱。
            REQUIRE(first.support.size() >= 3);
            REQUIRE(path_count(first) > 0);
            CHECK(unknown_count(first) == 0);
            // 不看順序的線段集合一定要相同；逐點相同則是實測結果（紅了就把 k_compare_exact 改成 false）。
            CHECK(diff_unordered(first, second) == "");
            CHECK(diff_exact(first, second) == "");
        }
}

TEST_CASE("PingRaftTop 00b: base configuration passes Print::validate", "[PingRaftTop]")
{
    // 後面每個情境用的設定都要是「真的能切」的設定，不然比的東西沒有意義。
    for (bool perpendicular : { false, true }) {
        Rig rig(Part::Tee, perpendicular ? perpendicular_config() : base_config());
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK(error.string.empty());
    }
}

// ===============================================================================================================
// 01 全零切完 → 勾垂直 → 重新 process ＝ 全新切（勾）；而且筏層頂面與本體第一層底面垂直
// ===============================================================================================================

static void scenario_check_perpendicular_after_slicing(Part part)
{
    Rig rig(part, base_config());
    rig.print.process();
    const Snapshot before = take(*rig.object());
    REQUIRE(rig.object()->is_step_done(posSupportMaterial));
    INFO("all zero: " << describe(before));
    INFO("all zero raft top: " << describe(raft_top_direction(*rig.object(), before))
         << ", body: " << describe(body_bottom_direction(*rig.object())));

    rig.apply(perpendicular_config());
    // 勾垂直只重算支撐，本體不動。
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    CHECK(rig.object()->is_step_done(posInfill));
    REQUIRE_NOTHROW(rig.print.process());
    const Snapshot after = take(*rig.object());
    const Snapshot fresh = fresh_slice(part, perpendicular_config());
    INFO("re-processed: " << describe(after));
    INFO("fresh:        " << describe(fresh));

    // 主要判準：重新 process ＝ 全新切。
    CHECK(diff(after, fresh) == "");
    // 對照：沒重新 process 的舊結果（全零）≠ 全新切（勾）。這條紅了＝比對函式分不出差別，上面那條綠了也不算數。
    CHECK(diff_unordered(before, fresh) != "");

    // 勾了之後，筏層最上面那層介面線的方向與本體第一層底面填充方向垂直。
    check_raft_top_perpendicular(*rig.object(), after);
    // 往下一層再轉 90°（＝跟本體第一層同方向）。
    const Direction top   = raft_top_direction(*rig.object(), after);
    const Direction below = raft_below_top_direction(*rig.object(), after);
    INFO("raft top: " << describe(top) << ", one layer below: " << describe(below));
    REQUIRE(below.valid);
    CHECK(angle_between(top, below) == Catch::Approx(90.).margin(1.));

    // 再把勾取消 → 重新 process ＝ 全新切（全零）＝一開始那份。
    rig.apply(base_config());
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    CHECK(rig.object()->is_step_done(posInfill));
    REQUIRE_NOTHROW(rig.print.process());
    CHECK(diff(take(*rig.object()), before) == "");
}

TEST_CASE("PingRaftTop 01: checking perpendicular after slicing equals a fresh slice", "[PingRaftTop]")
{
    SECTION("cube (raft only)") { scenario_check_perpendicular_after_slicing(Part::Cube); }
    SECTION("pillar with plate (raft + support columns)") { scenario_check_perpendicular_after_slicing(Part::Tee); }
}

// ===============================================================================================================
// 02 勾垂直切完 → 本體方向 45→135 → 重新 process ＝ 全新切；筏層頂面方向跟著轉
//    （solid_infill_direction 讓 posPrepareInfill 失效，支撐靠 PrintObject::invalidate_step() 的連帶失效才會重算）
// ===============================================================================================================

TEST_CASE("PingRaftTop 02: changing solid infill direction rotates the raft top", "[PingRaftTop]")
{
    const DynamicPrintConfig config_45  = perpendicular_config();
    const DynamicPrintConfig config_135 = with(perpendicular_config(), {{"solid_infill_direction", 135}});

    Rig rig(Part::Tee, config_45);
    rig.print.process();
    const Snapshot  before      = take(*rig.object());
    const Direction body_before = body_bottom_direction(*rig.object());
    const Direction raft_before = raft_top_direction(*rig.object(), before);
    check_raft_top_perpendicular(*rig.object(), before);

    rig.apply(config_135);
    // 白箱：填充會重算 ⇒ 支撐也要被標成要重算（PING 新增的連帶失效）。
    CHECK_FALSE(rig.object()->is_step_done(posInfill));
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    REQUIRE_NOTHROW(rig.print.process());
    const Snapshot  after      = take(*rig.object());
    const Direction body_after = body_bottom_direction(*rig.object());
    const Direction raft_after = raft_top_direction(*rig.object(), after);
    const Snapshot  fresh      = fresh_slice(Part::Tee, config_135);
    INFO("body: " << describe(body_before) << " -> " << describe(body_after));
    INFO("raft top: " << describe(raft_before) << " -> " << describe(raft_after));

    CHECK(diff(after, fresh) == "");
    // 對照：舊結果（45）≠ 全新切（135）。
    CHECK(diff_unordered(before, fresh) != "");

    // 本體方向真的轉了 90°，筏層頂面也跟著轉 90°，而且轉完仍與本體垂直。
    REQUIRE(body_before.valid);
    REQUIRE(body_after.valid);
    REQUIRE(raft_before.valid);
    REQUIRE(raft_after.valid);
    CHECK(angle_between(body_before, body_after) == Catch::Approx(90.).margin(1.));
    CHECK(angle_between(raft_before, raft_after) == Catch::Approx(90.).margin(1.));
    check_raft_top_perpendicular(*rig.object(), after);
}

// ===============================================================================================================
// 03 勾垂直切完 → 改 wall_loops 2→4 → 重新 process ＝ 全新切（wall_loops 讓 posPerimeters 失效）
// ===============================================================================================================

TEST_CASE("PingRaftTop 03: changing wall loops keeps the raft equal to a fresh slice", "[PingRaftTop]")
{
    const DynamicPrintConfig config_2 = perpendicular_config();
    const DynamicPrintConfig config_4 = with(perpendicular_config(), {{"wall_loops", 4}});

    Rig rig(Part::Tee, config_2);
    rig.print.process();
    REQUIRE(rig.object()->is_step_done(posSupportMaterial));

    rig.apply(config_4);
    // 白箱：牆會重算 ⇒ 填充會重算 ⇒ 支撐也要被標成要重算（PING 新增的連帶失效）。
    // 這個情境裡筏層的路徑其實不會變，所以「有沒有連帶失效」只有這兩條斷言看得出來。
    CHECK_FALSE(rig.object()->is_step_done(posPerimeters));
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    REQUIRE_NOTHROW(rig.print.process());
    CHECK(rig.object()->is_step_done(posSupportMaterial));
    const Snapshot after = take(*rig.object());
    const Snapshot fresh = fresh_slice(Part::Tee, config_4);
    CHECK(diff(after, fresh) == "");
    check_raft_top_perpendicular(*rig.object(), after);
}

// ===============================================================================================================
// 04 勾垂直切完 → bottom_shell_layers 改 0（底面填充拿掉）→ 重新 process ＝ 全新切；讀不到方向，筏層照舊算法
// ===============================================================================================================

TEST_CASE("PingRaftTop 04: removing the bottom shell falls back to the legacy raft", "[PingRaftTop]")
{
    const DynamicPrintConfig config_with    = perpendicular_config();
    const DynamicPrintConfig config_without = with(perpendicular_config(), {{"bottom_shell_layers", 0}});

    Rig rig(Part::Tee, config_with);
    rig.print.process();
    const Snapshot before = take(*rig.object());
    CHECK(rig.object()->layers().front()->ping_bottom_fill_direction_valid);

    // bottom_shell_layers 會重新切層（posSlice 失效）。
    const SimplifyWatch watch(*rig.object());
    rig.apply(config_without);
    CHECK_FALSE(rig.object()->is_step_done(posSlice));
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    REQUIRE_NOTHROW(rig.print.process());
    const Snapshot fresh  = fresh_slice(Part::Tee, config_without);
    const Snapshot legacy = fresh_slice(Part::Tee, with(base_config(), {{"bottom_shell_layers", 0}}));
    INFO("body bottom after: " << describe(body_bottom_direction(*rig.object())));

    SECTION("equal to a fresh slice once the skipped support simplification is redone")
    {
        // 既有問題（見 SimplifyWatch 的說明）：重新切層後支撐路徑沒有再簡化。把漏掉的那一步補跑之後再比。
        const bool redone = watch.redo_if_skipped(*rig.object());
        INFO("support simplification was skipped by the re-slice and redone by the test: " << redone);
        const Snapshot after = take(*rig.object());
        CHECK(diff(after, fresh) == "");
        // 對照：舊結果（有底面、垂直生效）≠ 全新切（沒底面）。
        CHECK(diff_unordered(before, fresh) != "");
        // 第一層沒有底面實心填充 ⇒ 方向無效。
        CHECK_FALSE(rig.object()->layers().front()->ping_bottom_fill_direction_valid);
        CHECK_FALSE(body_bottom_direction(*rig.object()).valid);
        // 讀不到方向 ⇒ 筏層照舊算法＝跟「不勾垂直」切出來的一樣。
        CHECK(diff(after, legacy) == "");
    }

    // 🔴 已知會紅（既有問題，不是本功能造成的；產品修好之前保留為紅）：
    // 嚴格照核心判準直接比。重新切層後 posSimplifySupportPath 沒有失效，支撐路徑少簡化一次。
    SECTION("was red before the 2026-11-12 posSimplifySupportPath fix: strict comparison, re-slice skips the support path simplification")
    {
        CHECK(diff(take(*rig.object()), fresh) == "");
    }
}

// 04b（加碼）：底面圖樣換成沒有單一方向的同心圓（bottom_surface_pattern 讓 posInfill 失效，是三個入口裡的第三個）。
TEST_CASE("PingRaftTop 04b: a non-straight bottom pattern falls back to the legacy raft", "[PingRaftTop]")
{
    const DynamicPrintConfig config_line       = perpendicular_config();
    const DynamicPrintConfig config_concentric = with(perpendicular_config(), {{"bottom_surface_pattern", "concentric"}});

    Rig rig(Part::Tee, config_line);
    rig.print.process();
    const Snapshot before = take(*rig.object());

    rig.apply(config_concentric);
    // 白箱：只有 posInfill 失效（牆與填充準備不動），支撐要連帶失效。
    CHECK(rig.object()->is_step_done(posPrepareInfill));
    CHECK_FALSE(rig.object()->is_step_done(posInfill));
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    REQUIRE_NOTHROW(rig.print.process());
    const Snapshot after = take(*rig.object());
    const Snapshot fresh = fresh_slice(Part::Tee, config_concentric);

    CHECK(diff(after, fresh) == "");
    CHECK(diff_unordered(before, fresh) != "");
    CHECK_FALSE(rig.object()->layers().front()->ping_bottom_fill_direction_valid);
    const Snapshot legacy = fresh_slice(Part::Tee, with(base_config(), {{"bottom_surface_pattern", "concentric"}}));
    CHECK(diff(after, legacy) == "");

    SECTION("and back to a straight pattern")
    {
        rig.apply(config_line);
        CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
        REQUIRE_NOTHROW(rig.print.process());
        const Snapshot back = take(*rig.object());
        CHECK(diff(back, fresh_slice(Part::Tee, config_line)) == "");
        check_raft_top_perpendicular(*rig.object(), back);
    }
}

// ===============================================================================================================
// 05 支撐筆刷預覽（PrintObject::generate_support_preview()）：沒經過填充
// ===============================================================================================================

TEST_CASE("PingRaftTop 05: support preview before a full slice", "[PingRaftTop]")
{
    SECTION("perpendicular checked: the preview leaves support not done, the full slice redoes it")
    {
        Rig rig(Part::Tee, perpendicular_config());
        // 預覽：切片步驟還沒做，它會自己切層，然後直接產生支撐。
        REQUIRE_NOTHROW(rig.object()->generate_support_preview());
        CHECK(rig.object()->is_step_done(posSlice));
        CHECK_FALSE(rig.object()->is_step_done(posInfill));
        // 重點：勾了垂直、填充還沒算 ⇒ 支撐不標成完成。
        CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
        // 預覽還是有東西可畫（筏層照舊算法）。
        const Snapshot preview = take(*rig.object());
        INFO("preview: " << describe(preview));
        CHECK(preview.support.size() >= 3);
        CHECK(path_count(preview) > 0);

        // 之後完整切片：不卡住、不丟例外，支撐重算成垂直的。
        REQUIRE_NOTHROW(rig.print.process());
        CHECK(rig.object()->is_step_done(posSupportMaterial));
        const Snapshot after = take(*rig.object());
        const Snapshot fresh = fresh_slice(Part::Tee, perpendicular_config());
        CHECK(diff(after, fresh) == "");
        // 對照：預覽當下的舊結果（照舊算法）≠ 全新切（垂直）。
        CHECK(diff_unordered(preview, fresh) != "");
        check_raft_top_perpendicular(*rig.object(), after);
    }

    SECTION("perpendicular checked: previewing twice still leaves support not done")
    {
        Rig rig(Part::Tee, perpendicular_config());
        REQUIRE_NOTHROW(rig.object()->generate_support_preview());
        REQUIRE_NOTHROW(rig.object()->generate_support_preview());
        CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
        REQUIRE_NOTHROW(rig.print.process());
        CHECK(rig.object()->is_step_done(posSupportMaterial));
        CHECK(diff(take(*rig.object()), fresh_slice(Part::Tee, perpendicular_config())) == "");
    }

    SECTION("perpendicular unchecked: the preview marks support done (old behaviour)")
    {
        Rig rig(Part::Tee, base_config());
        REQUIRE_NOTHROW(rig.object()->generate_support_preview());
        CHECK(rig.object()->is_step_done(posSupportMaterial));
        const PrintStateBase::TimeStamp stamp = rig.object()->step_state_with_timestamp(posSupportMaterial).timestamp;
        REQUIRE_NOTHROW(rig.print.process());
        CHECK(rig.object()->is_step_done(posSupportMaterial));
        // 舊行為：完整切片不會再重算支撐。
        CHECK(rig.object()->step_state_with_timestamp(posSupportMaterial).timestamp == stamp);
        CHECK(diff(take(*rig.object()), fresh_slice(Part::Tee, base_config())) == "");
    }

    SECTION("perpendicular checked, raft with one layer: the preview marks support done")
    {
        // 筏層只有 1 層時垂直不作用，預覽也要照舊行為。
        const DynamicPrintConfig config = with(perpendicular_config(), {{"raft_layers", 1}});
        Rig rig(Part::Tee, config);
        REQUIRE_NOTHROW(rig.object()->generate_support_preview());
        CHECK(rig.object()->is_step_done(posSupportMaterial));
        REQUIRE_NOTHROW(rig.print.process());
        CHECK(diff(take(*rig.object()), fresh_slice(Part::Tee, config)) == "");
    }

    SECTION("perpendicular checked: preview after a full slice reads the direction and marks support done")
    {
        // 完整切完之後只讓支撐失效（raft_expansion 只影響支撐），再走預覽：填充是完成的，讀得到方向。
        const DynamicPrintConfig config_a = perpendicular_config();
        const DynamicPrintConfig config_b = with(perpendicular_config(), {{"raft_expansion", 3}});
        Rig rig(Part::Tee, config_a);
        rig.print.process();
        const Snapshot before = take(*rig.object());
        rig.apply(config_b);
        CHECK(rig.object()->is_step_done(posInfill));
        CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
        REQUIRE_NOTHROW(rig.object()->generate_support_preview());
        CHECK(rig.object()->is_step_done(posSupportMaterial));
        check_raft_top_perpendicular(*rig.object(), take(*rig.object()));
        REQUIRE_NOTHROW(rig.print.process());
        const Snapshot after = take(*rig.object());
        const Snapshot fresh = fresh_slice(Part::Tee, config_b);
        CHECK(diff(after, fresh) == "");
        CHECK(diff_unordered(before, fresh) != "");
    }
}

// ===============================================================================================================
// 06 切到一半取消再重切
// ===============================================================================================================

namespace {

struct CancelOutcome
{
    std::string message;
    bool        canceled { false };
    bool        infill_done { false };
    bool        support_done { false };
    std::string difference;
};

// 在第 index 次狀態回報時取消，再重新 process，回傳重切後跟 fresh 的差異。
// prepare＝取消前要先做的事（例如先完整切一次再改設定）。
template<typename Prepare>
CancelOutcome cancel_at_and_reslice(const DynamicPrintConfig &initial, Prepare prepare, int index, const Snapshot &fresh)
{
    CancelOutcome outcome;
    Rig rig(Part::Tee, initial);
    prepare(rig);
    std::atomic<int> count { 0 };
    std::mutex       mutex;
    // 狀態回報可能來自工作執行緒（支撐是平行產生的），所以只在這裡設旗標、記訊息，不做斷言。
    rig.print.set_status_callback([&rig, &count, &mutex, &outcome, index](const PrintBase::SlicingStatus &status) {
        if (count ++ == index) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                outcome.message = status.text;
            }
            rig.print.cancel();
        }
    });
    try {
        rig.print.process();
    } catch (const CanceledException &) {
        outcome.canceled = true;
    }
    outcome.infill_done  = rig.object()->is_step_done(posInfill);
    outcome.support_done = rig.object()->is_step_done(posSupportMaterial);
    // 重切：照背景切片的做法先 restart() 清掉取消旗標。
    rig.print.set_status_silent();
    rig.print.restart();
    rig.print.process();
    outcome.difference = diff(take(*rig.object()), fresh);
    if (outcome.difference.empty() && ! rig.object()->is_step_done(posSupportMaterial))
        outcome.difference = "posSupportMaterial is not done after the re-slice";
    return outcome;
}

// 一次完整 process 會回報幾次狀態、各是什麼訊息。
template<typename Prepare>
std::vector<std::string> record_status_messages(const DynamicPrintConfig &initial, Prepare prepare)
{
    std::vector<std::string> messages;
    std::mutex               mutex;
    Rig rig(Part::Tee, initial);
    prepare(rig);
    rig.print.set_status_callback([&messages, &mutex](const PrintBase::SlicingStatus &status) {
        std::lock_guard<std::mutex> lock(mutex);
        messages.push_back(status.text);
    });
    rig.print.process();
    return messages;
}

template<typename Prepare>
void cancel_sweep(const DynamicPrintConfig &initial, Prepare prepare, const DynamicPrintConfig &final_config)
{
    const Snapshot                 fresh    = fresh_slice(Part::Tee, final_config);
    const std::vector<std::string> messages = record_status_messages(initial, prepare);
    REQUIRE_FALSE(messages.empty());
    std::ostringstream summary;
    bool   seen_generating_support = false;
    size_t canceled_count = 0;
    for (int index = 0; index < int(messages.size()); ++ index) {
        CancelOutcome outcome;
        REQUIRE_NOTHROW(outcome = cancel_at_and_reslice(initial, prepare, index, fresh));
        summary << "\n  #" << index << " \"" << outcome.message << "\": canceled=" << outcome.canceled
                << " infill_done=" << outcome.infill_done << " support_done=" << outcome.support_done
                << " reslice_vs_fresh=" << (outcome.difference.empty() ? "same" : outcome.difference);
        INFO("cancel at status #" << index << " \"" << outcome.message << "\"");
        // 主要判準：不管取消在哪一步，重切的結果＝全新切。
        CHECK(outcome.difference == "");
        if (outcome.canceled)
            ++ canceled_count;
        if (outcome.message == "Generating support") {
            // 這個時點＝填充做完、支撐剛開始（步驟已標成「已開始」，還沒產生）。
            seen_generating_support = true;
            CHECK(outcome.canceled);
            CHECK(outcome.infill_done);
            CHECK_FALSE(outcome.support_done);
        }
    }
    // 實際取消在哪幾步、當時的步驟狀態，印出來留在測試輸出裡。
    WARN("cancel sweep (" << messages.size() << " status callbacks, " << canceled_count << " canceled):" << summary.str());
    CHECK(seen_generating_support);
    CHECK(canceled_count > 0);
}

} // namespace

TEST_CASE("PingRaftTop 06: cancel half way then slice again", "[PingRaftTop]")
{
    SECTION("first slice canceled at every status callback")
    {
        cancel_sweep(perpendicular_config(), [](Rig &) {}, perpendicular_config());
    }

    SECTION("re-slice after a direction change canceled at every status callback")
    {
        // 先完整切一次（45），改成 135，重切到一半取消，再重切。
        const DynamicPrintConfig config_135 = with(perpendicular_config(), {{"solid_infill_direction", 135}});
        cancel_sweep(perpendicular_config(),
            [&config_135](Rig &rig) {
                rig.print.process();
                rig.apply(config_135);
            },
            config_135);
    }

    SECTION("re-slice after checking perpendicular canceled at every status callback")
    {
        // 先完整切一次（全零），勾垂直（只有支撐失效），重切到一半取消，再重切。
        cancel_sweep(base_config(),
            [](Rig &rig) {
                rig.print.process();
                rig.apply(perpendicular_config());
            },
            perpendicular_config());
    }
}

// ===============================================================================================================
// 07 全零切完 → 只改 raft_top_speed 0→45 → 支撐路徑不變、支撐沒被重算；改回 0 ＝ 全新切（全零）
// ===============================================================================================================

TEST_CASE("PingRaftTop 07: raft top speed does not regenerate supports", "[PingRaftTop]")
{
    const DynamicPrintConfig config_0  = base_config();
    const DynamicPrintConfig config_45 = with(base_config(), {{"raft_top_speed", 45}});

    Rig rig(Part::Tee, config_0);
    rig.print.process();
    const Snapshot zero = take(*rig.object());
    const PrintStateBase::TimeStamp support_stamp = rig.object()->step_state_with_timestamp(posSupportMaterial).timestamp;
    const PrintStateBase::TimeStamp infill_stamp  = rig.object()->step_state_with_timestamp(posInfill).timestamp;
    const PrintStateBase::TimeStamp slice_stamp   = rig.object()->step_state_with_timestamp(posSlice).timestamp;

    rig.apply(config_45);
    // 速度只在輸出 G-code 時套用：支撐、填充、切層都不失效。
    CHECK(rig.object()->is_step_done(posSupportMaterial));
    CHECK(rig.object()->is_step_done(posInfill));
    CHECK(rig.object()->is_step_done(posSlice));
    REQUIRE_NOTHROW(rig.print.process());
    // 時間戳沒變＝這幾步沒有被重算。
    CHECK(rig.object()->step_state_with_timestamp(posSupportMaterial).timestamp == support_stamp);
    CHECK(rig.object()->step_state_with_timestamp(posInfill).timestamp == infill_stamp);
    CHECK(rig.object()->step_state_with_timestamp(posSlice).timestamp == slice_stamp);
    const Snapshot with_speed = take(*rig.object());
    // 支撐路徑與全零時相同，也＝全新切（45）。
    CHECK(diff_exact(with_speed, zero) == "");
    CHECK(diff(with_speed, fresh_slice(Part::Tee, config_45)) == "");

    rig.apply(config_0);
    CHECK(rig.object()->is_step_done(posSupportMaterial));
    REQUIRE_NOTHROW(rig.print.process());
    CHECK(rig.object()->step_state_with_timestamp(posSupportMaterial).timestamp == support_stamp);
    CHECK(diff(take(*rig.object()), fresh_slice(Part::Tee, config_0)) == "");
}

// 隱藏（[.]，預設不跑）：要匯出 G-code。test_ping_memory_estimate.cpp 記錄過「在測試程式裡呼叫 Print::export_gcode
// 會存取違規」，所以獨立成一個案例、用不同的標籤，免得當掉時把其他案例一起帶走。
// 跑法：fff_print_tests.exe "[PingRaftTopExport]"
// 🔴 2026-11-12 本機實測：一呼叫 Print::export_gcode 就 SIGSEGV（結束碼 0xC0000005），這一案的斷言一條都還沒被執行過。
//    匯出這條路在測試程式裡修好之前，「筏層頂面速度有沒有寫進 G-code」只能靠真 App 的切片驗證。
TEST_CASE("PingRaftTop 07b: raft top speed in the exported G-code", "[PingRaftTopExport][.]")
{
    // 讓速度不被別的東西蓋掉：流量上限放大、冷卻降速關掉、支撐介面速度設成跟 45 不一樣的 30。
    const Items speed_items = {
        {"filament_max_volumetric_speed", "200"},
        {"slow_down_for_layer_cooling", "0"},
        {"slow_down_layers", 0},
        {"support_interface_speed", 30},
        {"support_speed", 60}
    };
    const DynamicPrintConfig config_0  = with(base_config(), speed_items);
    const DynamicPrintConfig config_45 = with(config_0, {{"raft_top_speed", 45}});

    Rig rig(Part::Tee, config_45);
    rig.print.process();
    const SlicingParameters &params = rig.object()->slicing_parameters();
    // 筏層頂面＝貼床那層以上、到本體坐的那層為止（筏層 3 層時是第 2、3 層）。
    const double z_lo = params.raft_base_top_z + EPSILON;
    const double z_hi = params.raft_contact_top_z + EPSILON;
    INFO("raft top z range: (" << z_lo << ", " << z_hi << ")");

    const std::vector<double> speeds_45 = support_interface_speeds(export_gcode_text(rig.print), z_lo, z_hi);
    REQUIRE_FALSE(speeds_45.empty());
    for (double speed : speeds_45)
        CHECK(speed == Catch::Approx(45.).margin(0.01));

    rig.apply(config_0);
    CHECK(rig.object()->is_step_done(posSupportMaterial));
    rig.print.process();
    const std::vector<double> speeds_0 = support_interface_speeds(export_gcode_text(rig.print), z_lo, z_hi);
    REQUIRE_FALSE(speeds_0.empty());
    for (double speed : speeds_0)
        CHECK(speed == Catch::Approx(30.).margin(0.01));
}

// ===============================================================================================================
// 08 全零切完 → 改密度／線寬／層高 → 重新 process ＝ 全新切
// ===============================================================================================================

// redo_skipped_simplification＝重新切層後替產品補跑漏掉的「支撐路徑簡化」再比（見 SimplifyWatch 的說明）；
// false＝嚴格照核心判準直接比。
static void scenario_change_one_key(const char *key, const char *value, bool expect_reslice, bool redo_skipped_simplification = false)
{
    const DynamicPrintConfig config_0 = base_config();
    const DynamicPrintConfig config_1 = with(base_config(), {{key, value}});

    Rig rig(Part::Tee, config_0);
    rig.print.process();
    const Snapshot before = take(*rig.object());
    const PrintStateBase::TimeStamp slice_stamp = rig.object()->step_state_with_timestamp(posSlice).timestamp;

    const SimplifyWatch watch_1(*rig.object());
    rig.apply(config_1);
    {
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK(error.string.empty());
    }
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    // 層高會改到本體起始高度，要重新切層；密度、線寬只重算支撐。
    CHECK(rig.object()->is_step_done(posSlice) == ! expect_reslice);
    REQUIRE_NOTHROW(rig.print.process());
    CHECK((rig.object()->step_state_with_timestamp(posSlice).timestamp == slice_stamp) == ! expect_reslice);
    if (redo_skipped_simplification) {
        const bool redone = watch_1.redo_if_skipped(*rig.object());
        INFO("support simplification was skipped and redone by the test (1st change): " << redone);
        // 2026-11-12 起 posSlice 的批次失效清單補上了 posSimplifySupportPath（PrintObject::invalidate_step），
        // 重新切層後產品自己會重跑支撐路徑簡化，所以任何一種改動都不該再用到這個補償。
        // （補上之前，重新切層的那一種這裡會是 true。）
        CHECK_FALSE(redone);
    }
    const Snapshot after = take(*rig.object());
    const Snapshot fresh = fresh_slice(Part::Tee, config_1);
    INFO("before: " << describe(before));
    INFO("after:  " << describe(after));
    INFO("fresh:  " << describe(fresh));

    CHECK(diff(after, fresh) == "");
    // 對照：舊結果（全零）≠ 全新切（改後）。
    CHECK(diff_unordered(before, fresh) != "");

    // 再改回 0 ＝ 全新切（全零）。
    const SimplifyWatch watch_0(*rig.object());
    rig.apply(config_0);
    CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
    REQUIRE_NOTHROW(rig.print.process());
    if (redo_skipped_simplification)
        watch_0.redo_if_skipped(*rig.object());
    CHECK(diff(take(*rig.object()), before) == "");
}

TEST_CASE("PingRaftTop 08: density, line width and layer height", "[PingRaftTop]")
{
    SECTION("raft_top_density 0 -> 100%") { scenario_change_one_key("raft_top_density", "100%", false); }
    SECTION("raft_top_line_width 0 -> 0.8") { scenario_change_one_key("raft_top_line_width", "0.8", false); }
    // 🔴 已知會紅（既有問題，不是本功能造成的；產品修好之前保留為紅）：嚴格照核心判準直接比。
    // raft_top_layer_height 會重新切層，重新切層後 posSimplifySupportPath 沒有失效，支撐路徑少簡化一次。
    SECTION("was red before the 2026-11-12 posSimplifySupportPath fix: raft_top_layer_height 0 -> 0.35, strict comparison, re-slice skips the support path simplification")
    {
        scenario_change_one_key("raft_top_layer_height", "0.35", true, false);
    }
    SECTION("raft_top_layer_height 0 -> 0.35 (re-slices), with the skipped support simplification redone")
    {
        scenario_change_one_key("raft_top_layer_height", "0.35", true, true);
        // 筏層頂＝0.35（貼床）＋0.35＋0.35＝1.05，本體第一層再往上 0.1（筏層 Z 間距）＋0.3。
        Rig rig(Part::Tee, with(base_config(), {{"raft_top_layer_height", "0.35"}}));
        rig.print.process();
        CHECK(rig.object()->slicing_parameters().raft_contact_top_z == Catch::Approx(1.05).margin(1e-6));
        const Snapshot snapshot = take(*rig.object());
        REQUIRE(snapshot.support.size() >= 3);
        CHECK(snapshot.support[1].height == Catch::Approx(0.35).margin(1e-6));
        CHECK(snapshot.support[2].height == Catch::Approx(0.35).margin(1e-6));
    }
    SECTION("all three together, with perpendicular")
    {
        const DynamicPrintConfig config_1 = with(perpendicular_config(),
            {{"raft_top_density", "80%"}, {"raft_top_line_width", "0.7"}, {"raft_top_layer_height", "0.4"}});
        Rig rig(Part::Tee, base_config());
        rig.print.process();
        const Snapshot before = take(*rig.object());
        rig.apply(config_1);
        REQUIRE_NOTHROW(rig.print.process());
        const Snapshot after = take(*rig.object());
        const Snapshot fresh = fresh_slice(Part::Tee, config_1);
        CHECK(diff(after, fresh) == "");
        CHECK(diff_unordered(before, fresh) != "");
        check_raft_top_perpendicular(*rig.object(), after);
    }
}

// ===============================================================================================================
// 09 raft_layers=1：5 鍵全填 ＝ 5 鍵全 0
// ===============================================================================================================

TEST_CASE("PingRaftTop 09: a one layer raft ignores all five keys", "[PingRaftTop]")
{
    const DynamicPrintConfig config_zero = with(base_config(), {{"raft_layers", 1}});
    const DynamicPrintConfig config_full = with(config_zero, {
        {"raft_top_density", "100%"},
        {"raft_top_line_width", "0.6"},
        {"raft_top_layer_height", "0.35"},
        {"raft_top_speed", "45"},
        {"raft_top_perpendicular", true}
    });
    for (Part part : { Part::Cube, Part::Tee }) {
        INFO("part=" << (part == Part::Cube ? "cube" : "tee"));
        const Snapshot zero = fresh_slice(part, config_zero);
        const Snapshot full = fresh_slice(part, config_full);
        INFO("zero: " << describe(zero));
        INFO("full: " << describe(full));
        REQUIRE(zero.support.size() >= 1);
        REQUIRE(path_count(zero) > 0);
        CHECK(diff_exact(full, zero) == "");

        // 同一個 Print 上從全 0 改成全填再重新 process，也一樣。
        Rig rig(part, config_zero);
        rig.print.process();
        rig.apply(config_full);
        REQUIRE_NOTHROW(rig.print.process());
        CHECK(rig.object()->is_step_done(posSupportMaterial));
        CHECK(diff_exact(take(*rig.object()), zero) == "");
        // 5 鍵全填在筏層 1 層時不該被切片前檢查擋下來。
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK(error.string.empty());

        // 筏層 1 層 → 3 層（5 鍵維持全填）：5 鍵開始作用，重新 process ＝ 全新切。
        // raft_layers 會重新切層，所以先補跑既有問題漏掉的支撐路徑簡化再比（見 SimplifyWatch 的說明）。
        const DynamicPrintConfig config_full_3 = with(config_full, {{"raft_layers", 3}});
        const SimplifyWatch watch(*rig.object());
        rig.apply(config_full_3);
        CHECK_FALSE(rig.object()->is_step_done(posSlice));
        REQUIRE_NOTHROW(rig.print.process());
        watch.redo_if_skipped(*rig.object());
        const Snapshot three = take(*rig.object());
        const Snapshot fresh_three = fresh_slice(part, config_full_3);
        CHECK(diff(three, fresh_three) == "");
        CHECK(diff_unordered(zero, fresh_three) != "");
        check_raft_top_perpendicular(*rig.object(), three);
    }
}

// ===============================================================================================================
// 10 兩個相同物件同盤＋勾垂直
// ===============================================================================================================

namespace {

enum class TwoObjects {
    // 同一個 ModelObject 兩個 instance（⇒ 一個 PrintObject、兩個 instance）。
    Instances,
    // 複製 ModelObject（共用同一顆網格 ⇒ Print::process 會把第二個當成第一個的「共用物件」，不另外切）。
    CopiedObject,
    // 兩個各自建立的 ModelObject（網格各一份 ⇒ 兩個都各自切，支撐平行產生）。
    SeparateObjects
};

void build_two(Rig &rig, TwoObjects mode, const DynamicPrintConfig &config)
{
    ModelObject *first = rig.add_object(Part::Tee, Vec3d(50., 100., 0.));
    if (mode == TwoObjects::Instances) {
        first->add_instance(*first->instances.front())->set_offset(first->instances.front()->get_offset() + Vec3d(100., 0., 0.));
    } else if (mode == TwoObjects::CopiedObject) {
        // Model::add_object(const ModelObject&) 會替複本補上 extruder=1；原本那個是 Model::add_object() 建的、沒有這一鍵，
        // 兩邊的物件設定就不一樣、Print::process 不會把它們當成同一個件（2026-11-12 實測）。
        // 介面載入的物件本來就都有 extruder=1，這裡替原本那個也補上，才走得到「共用物件」那條路。
        first->config.set_key_value("extruder", new ConfigOptionInt(1));
        ModelObject *second = rig.model.add_object(*first);
        second->instances.front()->set_offset(first->instances.front()->get_offset() + Vec3d(100., 0., 0.));
        rig.print.auto_assign_extruders(second);
    } else
        rig.add_object(Part::Tee, Vec3d(150., 100., 0.));
    rig.apply(config);
}

void check_every_object(Rig &rig)
{
    for (size_t idx = 0; idx < rig.print.objects().size(); ++ idx) {
        const PrintObject *object = rig.object(idx);
        INFO("print object " << idx << " (shared=" << (object->get_shared_object() != nullptr) << ", instances=" << object->instances().size() << ")");
        CHECK(object->is_step_done(posSupportMaterial));
        CHECK(object->is_step_done(posInfill));
        REQUIRE_FALSE(object->support_layers().empty());
        const Snapshot snapshot = take(*object);
        INFO(describe(snapshot));
        CHECK(snapshot.support.size() >= 3);
        CHECK(path_count(snapshot) > 0);
        check_raft_top_perpendicular(*object, snapshot);
    }
}

void scenario_two_objects(TwoObjects mode, size_t expected_print_objects)
{
    const DynamicPrintConfig config_45  = perpendicular_config();
    const DynamicPrintConfig config_135 = with(perpendicular_config(), {{"solid_infill_direction", 135}});
    // 單一物件全新切的結果，拿來對照（同一個件、同樣的設定，物件座標系下應該一樣）。
    const Snapshot single_45  = fresh_slice(Part::Tee, config_45);
    const Snapshot single_135 = fresh_slice(Part::Tee, config_135);

    Rig rig;
    build_two(rig, mode, config_45);
    REQUIRE(rig.print.objects().size() == expected_print_objects);
    {
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK(error.string.empty());
    }
    REQUIRE_NOTHROW(rig.print.process());
    {
        // 實際走的是哪一條路（有沒有被 Print::process 當成「共用物件」、各有幾個 instance），印出來留在測試輸出裡。
        std::ostringstream ss;
        for (size_t idx = 0; idx < rig.print.objects().size(); ++ idx)
            ss << " [object " << idx << ": shared=" << (rig.object(idx)->get_shared_object() != nullptr)
               << " instances=" << rig.object(idx)->instances().size() << "]";
        if (rig.print.objects().size() == 2) {
            // Print::process() 判斷「兩個 PrintObject 是不是同一個件」（is_print_object_the_same）用的幾個條件，各自成不成立。
            const PrintObject *a  = rig.object(0);
            const PrintObject *b  = rig.object(1);
            const ModelObject *ma = a->model_object();
            const ModelObject *mb = b->model_object();
            ss << " same_trafo=" << ! (a->trafo().matrix() != b->trafo().matrix())
               << " same_volume_count=" << (ma->volumes.size() == mb->volumes.size())
               << " same_mesh_ptr=" << ! (ma->volumes.front()->mesh_ptr() != mb->volumes.front()->mesh_ptr())
               << " same_volume_trafo=" << (ma->volumes.front()->get_transformation() == mb->volumes.front()->get_transformation())
               << " same_volume_config=" << ! (ma->volumes.front()->config.get() != mb->volumes.front()->config.get())
               << " same_object_config=" << ! (ma->config.get() != mb->config.get())
               << " same_layer_height_profile=" << ! (ma->layer_height_profile.get() != mb->layer_height_profile.get());
        }
        WARN("two objects, mode " << int(mode) << ":" << ss.str());
    }
    check_every_object(rig);
    for (size_t idx = 0; idx < rig.print.objects().size(); ++ idx) {
        INFO("print object " << idx << " vs single object fresh slice (45)");
        CHECK(diff(take(*rig.object(idx)), single_45) == "");
    }

    // 再改本體方向重新 process：每個物件都要跟著轉。
    rig.apply(config_135);
    REQUIRE_NOTHROW(rig.print.process());
    check_every_object(rig);
    for (size_t idx = 0; idx < rig.print.objects().size(); ++ idx) {
        INFO("print object " << idx << " vs single object fresh slice (135)");
        CHECK(diff(take(*rig.object(idx)), single_135) == "");
    }
}

} // namespace

TEST_CASE("PingRaftTop 10: two identical objects on one plate", "[PingRaftTop]")
{
    SECTION("one ModelObject with two instances") { scenario_two_objects(TwoObjects::Instances, 1); }
    SECTION("a copied ModelObject sharing the mesh") { scenario_two_objects(TwoObjects::CopiedObject, 2); }
    SECTION("two separately built ModelObjects") { scenario_two_objects(TwoObjects::SeparateObjects, 2); }

    // 加碼：5 鍵是物件層級的設定（一盤擺好幾片不同設定的試片）。全域不勾，只有第二個物件自己勾垂直。
    SECTION("only the second object overrides raft_top_perpendicular")
    {
        const DynamicPrintConfig config_45  = base_config();
        const DynamicPrintConfig config_135 = with(base_config(), {{"solid_infill_direction", 135}});
        Rig rig;
        rig.add_object(Part::Tee, Vec3d(50., 100., 0.));
        rig.add_object(Part::Tee, Vec3d(150., 100., 0.))->config.set_key_value("raft_top_perpendicular", new ConfigOptionBool(true));
        rig.apply(config_45);
        REQUIRE(rig.print.objects().size() == 2);
        REQUIRE_NOTHROW(rig.print.process());
        // 第一個物件＝全零的單一物件；第二個＝勾垂直的單一物件。
        CHECK(diff(take(*rig.object(0)), fresh_slice(Part::Tee, config_45)) == "");
        CHECK(diff(take(*rig.object(1)), fresh_slice(Part::Tee, with(config_45, {{"raft_top_perpendicular", true}}))) == "");
        check_raft_top_perpendicular(*rig.object(1), take(*rig.object(1)));
        const PrintStateBase::TimeStamp stamp_0 = rig.object(0)->step_state_with_timestamp(posSupportMaterial).timestamp;
        const PrintStateBase::TimeStamp stamp_1 = rig.object(1)->step_state_with_timestamp(posSupportMaterial).timestamp;

        // 改本體方向：只有勾了垂直的那個物件要連帶重算支撐，沒勾的那個照舊不重算（舊行為沒被改到）。
        rig.apply(config_135);
        CHECK(rig.object(0)->is_step_done(posSupportMaterial));
        CHECK_FALSE(rig.object(1)->is_step_done(posSupportMaterial));
        REQUIRE_NOTHROW(rig.print.process());
        CHECK(rig.object(0)->step_state_with_timestamp(posSupportMaterial).timestamp == stamp_0);
        CHECK(rig.object(1)->step_state_with_timestamp(posSupportMaterial).timestamp != stamp_1);
        CHECK(diff(take(*rig.object(0)), fresh_slice(Part::Tee, config_135)) == "");
        CHECK(diff(take(*rig.object(1)), fresh_slice(Part::Tee, with(config_135, {{"raft_top_perpendicular", true}}))) == "");
        check_raft_top_perpendicular(*rig.object(1), take(*rig.object(1)));
    }
}

// ===============================================================================================================
// 11 Print::validate()
// ===============================================================================================================

TEST_CASE("PingRaftTop 11: validate checks the raft top line width against its layer height", "[PingRaftTop]")
{
    SECTION("support line width 0.4 with raft top layer height 0.45 is rejected")
    {
        Rig rig(Part::Tee, with(base_config(), {{"support_line_width", "0.4"}, {"raft_top_layer_height", "0.45"}}));
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK_THAT(error.string, Catch::Matchers::ContainsSubstring("Raft top line width must be larger"));
        CHECK(error.opt_key == "raft_top_line_width");
    }

    SECTION("all five keys zero with support line width 0.4 is not rejected by the new check")
    {
        Rig rig(Part::Tee, with(base_config(), {{"support_line_width", "0.4"}}));
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK(error.string.find("Raft top") == std::string::npos);
        CHECK(error.string.empty());
    }

    SECTION("a one layer raft is not checked")
    {
        Rig rig(Part::Tee, with(base_config(), {{"raft_layers", 1}, {"support_line_width", "0.4"}, {"raft_top_layer_height", "0.45"}}));
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK(error.string.find("Raft top") == std::string::npos);
    }

    SECTION("setting both the line width and the layer height passes")
    {
        Rig rig(Part::Tee, with(base_config(), {{"support_line_width", "0.4"}, {"raft_top_layer_height", "0.45"}, {"raft_top_line_width", "0.7"}}));
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK(error.string.empty());
    }

    // 2026-11-12 驗證棒補（Codex 反審 r1 P1-5）：逐物件／手改檔帶進來的密度超出 0～100 要報錯，不默默當成 100。
    SECTION("raft top density above 100 is rejected")
    {
        Rig rig(Part::Tee, with(base_config(), {{"raft_top_density", "150%"}}));
        const StringObjectException error = rig.print.validate();
        INFO("validate: " << error.string);
        CHECK_THAT(error.string, Catch::Matchers::ContainsSubstring("Raft top density must be between 0 and 100%"));
        CHECK(error.opt_key == "raft_top_density");
    }

    SECTION("raft top density 100 passes, and a one layer raft does not check the density")
    {
        Rig ok(Part::Tee, with(base_config(), {{"raft_top_density", "100%"}}));
        CHECK(ok.print.validate().string.empty());
        Rig one_layer(Part::Tee, with(base_config(), {{"raft_layers", 1}, {"raft_top_density", "150%"}}));
        CHECK(one_layer.print.validate().string.find("Raft top") == std::string::npos);
    }
}

// 2026-11-12 驗證棒補（3MF 類驗證 B8 加做項）：頂面層高明填成剛好等於自動值（0.6 噴嘴＝0.45）時，分層要跟不填完全相同。
// 不同物件之間的分層是用 == 比的（equal_layering），0.75 × 0.6 是 0.44999999999999996，修正前會讓換料塔把這兩個物件擋下來。
TEST_CASE("PingRaftTop 11b: raft top layer height equal to the automatic value keeps the same layering", "[PingRaftTop]")
{
    const std::vector<unsigned int> extruders { 1 };
    auto params_of = [&extruders](const DynamicPrintConfig &config) {
        Rig rig(Part::Tee, config);
        return SlicingParameters::create_from_config(rig.print.config(), rig.print.objects().front()->config(), 11., extruders, Vec3d(1., 1., 1.));
    };
    const SlicingParameters automatic = params_of(base_config());
    const SlicingParameters explicit_same = params_of(with(base_config(), {{"raft_top_layer_height", "0.45"}}));
    const SlicingParameters explicit_other = params_of(with(base_config(), {{"raft_top_layer_height", "0.35"}}));
    CHECK(automatic.contact_raft_layer_height == explicit_same.contact_raft_layer_height);
    CHECK(automatic.interface_raft_layer_height == explicit_same.interface_raft_layer_height);
    CHECK(equal_layering(automatic, explicit_same));
    // 對照：真的不同的值要分得出來。
    CHECK(explicit_other.contact_raft_layer_height == Catch::Approx(0.35));
    CHECK_FALSE(equal_layering(automatic, explicit_other));
}

// ===============================================================================================================
// 12（加碼）樹狀支撐的兩套產生路徑：organic 走 SupportCommon.cpp，slim／strong／hybrid 走 TreeSupport.cpp
// ===============================================================================================================

TEST_CASE("PingRaftTop 12: tree supports follow the same recalculation rule", "[PingRaftTop]")
{
    for (const char *style : { "organic", "tree_hybrid", "tree_slim" }) {
        DYNAMIC_SECTION("support_style=" << style)
        {
            const Items tree_items = {{"support_type", "tree(auto)"}, {"support_style", style}};
            const DynamicPrintConfig config_zero = with(base_config(), tree_items);
            const DynamicPrintConfig config_45   = with(config_zero, {{"raft_top_perpendicular", true}});
            const DynamicPrintConfig config_135  = with(config_45, {{"solid_infill_direction", 135}});

            // 全新切兩次本來就一樣嗎（樹狀支撐的產生路徑不同，另外量一次）。
            const Snapshot fresh_45 = fresh_slice(Part::Tee, config_45);
            INFO("fresh(45): " << describe(fresh_45));
            REQUIRE(path_count(fresh_45) > 0);
            CHECK(diff_exact(fresh_45, fresh_slice(Part::Tee, config_45)) == "");

            Rig rig(Part::Tee, config_zero);
            rig.print.process();
            const Snapshot zero = take(*rig.object());

            // 全零 → 勾垂直。
            rig.apply(config_45);
            CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
            REQUIRE_NOTHROW(rig.print.process());
            const Snapshot after_45 = take(*rig.object());
            CHECK(diff(after_45, fresh_45) == "");
            CHECK(diff_unordered(zero, fresh_45) != "");
            check_raft_top_perpendicular(*rig.object(), after_45);

            // 勾垂直 → 本體方向 45→135。
            rig.apply(config_135);
            CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
            REQUIRE_NOTHROW(rig.print.process());
            const Snapshot after_135 = take(*rig.object());
            const Snapshot fresh_135 = fresh_slice(Part::Tee, config_135);
            CHECK(diff(after_135, fresh_135) == "");
            CHECK(diff_unordered(after_45, fresh_135) != "");
            check_raft_top_perpendicular(*rig.object(), after_135);
        }
    }
}

// ===============================================================================================================
// 13（基準對照）5 鍵全 0／不勾：重新切層（posSlice 失效）之後重新 process，跟全新切一樣嗎？
//    這一案完全不經過 PING 新增的路（5 鍵全 0 時 SupportParameters 的 ping_raft_top_* 旗標都是 false），
//    用來分辨「情境 4、情境 8 層高」量到的差異是不是本功能造成的。
// ===============================================================================================================

TEST_CASE("PingRaftTop 13: baseline, re-slicing with all five keys zero", "[PingRaftTop]")
{
    for (Part part : { Part::Cube, Part::Tee }) {
        const char *part_name = part == Part::Cube ? "cube" : "tee";
        // strict＝嚴格照核心判準直接比（🔴 已知會紅：既有問題，產品修好之前保留為紅）；
        // 否則＝先替產品補跑重新切層後漏掉的「支撐路徑簡化」再比（見 SimplifyWatch 的說明）。
        for (bool strict : { false, true })
        DYNAMIC_SECTION(part_name << (strict ?
            ": was red before the 2026-11-12 posSimplifySupportPath fix: bottom_shell_layers 3 -> 0 -> 3, strict comparison, re-slice skips the support path simplification" :
            ": bottom_shell_layers 3 -> 0 -> 3 (each change re-slices), with the skipped support simplification redone"))
        {
            const DynamicPrintConfig config_3 = base_config();
            const DynamicPrintConfig config_0 = with(base_config(), {{"bottom_shell_layers", 0}});
            // 診斷用：本體第一層的切片輪廓（整數座標）與物件的外框，看重新切層前後差在哪一步。
            auto first_layer_outline = [](const PrintObject &object) {
                std::ostringstream ss;
                const BoundingBox bbox = object.bounding_box();
                ss << "bbox (" << bbox.min.x() << "," << bbox.min.y() << ")-(" << bbox.max.x() << "," << bbox.max.y() << ")"
                   << " center_offset (" << object.center_offset().x() << "," << object.center_offset().y() << ") lslices:";
                for (const ExPolygon &expoly : object.layers().front()->lslices) {
                    ss << " [";
                    for (const Point &p : expoly.contour.points)
                        ss << "(" << p.x() << "," << p.y() << ")";
                    ss << "]";
                }
                return ss.str();
            };
            Rig rig(part, config_3);
            rig.print.process();
            const Snapshot    first         = take(*rig.object());
            const std::string first_outline = first_layer_outline(*rig.object());

            const SimplifyWatch watch_0(*rig.object());
            rig.apply(config_0);
            CHECK_FALSE(rig.object()->is_step_done(posSlice));
            CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
            if (strict) {
                // 白箱（原因）：支撐會重新產生，「支撐路徑簡化」也該被標成要重做。目前沒有。
                CHECK_FALSE(rig.object()->is_step_done(posSimplifySupportPath));
            }
            rig.print.process();
            if (! strict) {
                const bool redone = watch_0.redo_if_skipped(*rig.object());
                INFO("support simplification was skipped and redone by the test (3 -> 0): " << redone);
            }
            const Snapshot second = take(*rig.object());
            CHECK(diff(second, fresh_slice(part, config_0)) == "");
            // 切片輪廓本身有沒有變（變了＝差在切層；沒變＝差在支撐那一段）。
            CHECK(first_layer_outline(*rig.object()) == first_outline);

            const SimplifyWatch watch_3(*rig.object());
            rig.apply(config_3);
            CHECK_FALSE(rig.object()->is_step_done(posSlice));
            rig.print.process();
            if (! strict)
                watch_3.redo_if_skipped(*rig.object());
            const Snapshot third = take(*rig.object());
            CHECK(diff(third, first) == "");
            CHECK(first_layer_outline(*rig.object()) == first_outline);
        }
        DYNAMIC_SECTION(part_name << ": raft_expansion 1.5 -> 3 -> 1.5 (support only, no re-slice)")
        {
            const DynamicPrintConfig config_a = base_config();
            const DynamicPrintConfig config_b = with(base_config(), {{"raft_expansion", 3}});
            Rig rig(part, config_a);
            rig.print.process();
            const Snapshot first = take(*rig.object());

            rig.apply(config_b);
            CHECK(rig.object()->is_step_done(posSlice));
            CHECK_FALSE(rig.object()->is_step_done(posSupportMaterial));
            rig.print.process();
            CHECK(diff(take(*rig.object()), fresh_slice(part, config_b)) == "");

            rig.apply(config_a);
            rig.print.process();
            CHECK(diff(take(*rig.object()), first) == "");
        }
    }
}
