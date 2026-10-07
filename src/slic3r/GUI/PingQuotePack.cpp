#include "PingQuotePack.hpp"

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"
#include "MainFrame.hpp"   // 對話框要拿 mainframe 當 parent：只有前向宣告會轉不到 wxWindow*
#include "MsgDialog.hpp"
#include "GLCanvas3D.hpp"
#include "3DScene.hpp"
#include "Camera.hpp"
#include "Jobs/Job.hpp"
#include "Jobs/Worker.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"   // SaveStrategy
#include "libslic3r/Zipper.hpp"
#include "libslic3r/miniz_extension.hpp"   // tdefl_write_image_to_png_file_in_memory_ex / mz_free
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r_version.h"   // 建置時產生（build/src/libslic3r/），不帶目錄前綴

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>

#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filedlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------

// 物件本身的長寬高＝「物件座標」下的尺寸（契約 v1.4 §4-3 的 size_x/y/z）：
// 含縮放、**不含擺放旋轉**，等同尺寸面板把座標切到「物件座標」時顯示的三個數字。
//
// 【為什麼不用 instance_bounding_box】那是世界座標的外框——零件為了好印轉一個角度，
//   長寬高就跟著變（2026-10-07 實例：CureFixture 的 X 從 231.76 變 271.21），
//   客戶拿報價單對不起自己那顆零件。v1.3 條文要的是那個值，v1.4 改了。
// 【算法照抄面板】Selection::get_bounding_box_in_reference_system(Instance)：
//   把實例矩陣的縮放拿掉、剩下的當座標軸，再把世界座標的頂點投影上去取外框。
//   這裡刻意用同一套數學（含 reset_scaling_factor），鏡射、非等比縮放、
//   「在世界座標縮放轉過的物件」留下的斜切，結果都與面板一致。
// 【只算零件本體】修改器、負體積、支撐遮罩不是客戶那顆零件的一部分，不計入。
static bool quote_object_coords_size(const ModelObject &mo, const ModelInstance &inst, Vec3d &size)
{
    const Transform3d        inst_matrix = inst.get_transformation().get_matrix();
    Geometry::Transformation basis(inst_matrix);
    basis.reset_scaling_factor();
    const Transform3d basis_matrix = basis.get_matrix_no_offset();
    const Vec3d       axes[3]      = {basis_matrix * Vec3d::UnitX(), basis_matrix * Vec3d::UnitY(), basis_matrix * Vec3d::UnitZ()};

    BoundingBoxf3 bb;
    for (const ModelVolume *v : mo.volumes) {
        if (v == nullptr || !v->is_model_part())
            continue;
        // 外框只由凸包頂點決定；凸包還沒算出來就退回完整網格
        const auto         &hull = v->get_convex_hull_shared_ptr();
        const TriangleMesh &mesh = (hull != nullptr && !hull->its.vertices.empty()) ? *hull : v->mesh();
        const Transform3d   m    = inst_matrix * v->get_matrix();
        for (const stl_vertex &p : mesh.its.vertices) {
            const Vec3d w = m * p.cast<double>();
            bb.merge(Vec3d(w.dot(axes[0]), w.dot(axes[1]), w.dot(axes[2])));
        }
    }
    if (!bb.defined)
        return false;
    size = bb.size();
    return true;
}

// ISO 8601 含時區，例 2026-08-10T16:30:00+08:00。
// 手動組時區是因為 strftime 的 %z 在 Windows 給的是 "+0800"（少了冒號），
// 而契約範例是 "+08:00"。
static std::string iso8601_now()
{
    std::time_t now = std::time(nullptr);
    std::tm     lt{};
#ifdef _WIN32
    ::localtime_s(&lt, &now);
#else
    ::localtime_r(&now, &lt);
#endif
    std::tm gm{};
#ifdef _WIN32
    ::gmtime_s(&gm, &now);
#else
    ::gmtime_r(&now, &gm);
#endif
    // 本地時間與 UTC 的分鐘差（用 mktime 反推，避開各平台 tm_gmtoff 不一致）
    std::time_t local_as_utc = std::mktime(&gm);
    long        offset_min   = static_cast<long>(std::difftime(now, local_as_utc) / 60.0);
    char        sign         = offset_min < 0 ? '-' : '+';
    long        abs_min      = std::labs(offset_min);

    char buf[64];
    ::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%c%02ld:%02ld",
               lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
               sign, abs_min / 60, abs_min % 60);
    return std::string(buf);
}

// ---------------------------------------------------------------------
// 逐物件切片 Job（跑在 worker thread）
// ---------------------------------------------------------------------

namespace {

// 契約：縮圖長邊 ≤ 800px。方形畫布即可——渲染路徑會 zoom_to_box 讓物件自動置中滿版。
constexpr unsigned int PING_QUOTE_THUMB_PX = 800;

struct PlanItem
{
    int obj_idx   = -1;   // model.objects 索引
    int inst_idx  = -1;   // 代表實例（單件重量取這一份）
    int instances = 1;    // 本盤上這個物件共有幾份

    // 物件層級參數覆寫（契約 4-3：有覆寫才要輸出 process_objN.json）。
    // 沒有它的話，切片數字明明套了覆寫，包裡卻只留全域製程 ⇒ 報價系統會誤認
    // 這件沿用檔頭參數，事後也重現不出來。（Codex 反審 2026-08-10 抓出）
    bool               has_override = false;
    DynamicPrintConfig override_cfg;
};

// 包內要附的一份 preset 檔。
//
// 【為什麼是三類檔而不是一個合併檔】2026-08-10 實測：把「完整生效組態」存成單一 json
// 丟回 `--load-settings`，載入器兩關都過不了——
//   ① `from=project` 直接被拒（OrcaSlicer.cpp:1883 只收 system/User/user）
//   ② 補成 `type=process` 後改報「process not compatible with printer」，
//      因為宣告成製程就沒有機器 preset 可以配對。
// 也就是說合併檔**看起來很完整、實際上還原不了**，而契約的驗收第 7 項要的正是「載得回去」。
// 所以改成輸出引擎原生的三類 preset，各自帶正確的 type：
//   machine.json ＋ process.json ＋ filament_N.json
// 還原方式＝`--load-settings machine.json;process.json --load-filaments filament_1.json;...`
struct PresetFile
{
    std::string        filename;   // 包內檔名
    std::string        type;       // machine / process / filament
    std::string        name;       // preset 名
    std::string        filament_id;// 只有 type=filament 要（載入器會讀，save_to_json 不會寫）
    DynamicPrintConfig config;     // 已攤平（去掉 inherits/setting_id）
};

class PingQuotePackJob : public Job
{
public:
    PingQuotePackJob(Plater *plater, PingQuoteOptions opts);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;

private:
    void slice_one(const PlanItem &item, PingQuoteObject &out, Ctl &ctl);
    // 下面兩支只能在主執行緒叫（前者要 GL context）——都由 finalize() 呼叫
    bool render_object_thumbnail(int obj_idx, int inst_idx, std::vector<unsigned char> &png_out);
    bool build_preset_json(const PresetFile &pf, std::string &out);
    bool build_restore_3mf(std::string &out);

    Plater                  *m_plater = nullptr;
    PingQuoteOptions         m_opts;
    Model                    m_model;              // 使用者模型的複本（mesh 走 shared_ptr，複製便宜）
    DynamicPrintConfig       m_config;             // 完整組態＋盤面設定
    std::vector<std::string> m_filament_names;     // 各槽位的線材 preset 名
    bool                     m_is_bbl = false;
    std::vector<PresetFile>  m_preset_files;
    std::vector<PlanItem>    m_plan;
    PingQuotePack            m_pack;
    std::string              m_fatal;              // 非空＝整批失敗的原因
};

PingQuotePackJob::PingQuotePackJob(Plater *plater, PingQuoteOptions opts) : m_plater(plater), m_opts(std::move(opts))
{
    // ── 這個建構子跑在主執行緒：把 worker 需要的東西全部快照下來，
    //    之後 process() 不再碰任何 GUI 狀態。
    PresetBundle  &pb    = *wxGetApp().preset_bundle;
    PartPlate     *plate = plater->get_partplate_list().get_curr_plate();

    // 組態的組法與背景切片一致（見 Plater.cpp priv::update_background_process）：
    // full_config ＋ 該盤自己的設定，多噴頭還要帶 filament map。
    if (pb.get_printer_extruder_count() > 1 && plate != nullptr) {
        std::vector<int> f_maps = plate->get_real_filament_maps(pb.project_config);
        m_config = pb.full_config(false, f_maps);
    } else {
        m_config = pb.full_config(false);
    }
    if (plate != nullptr)
        m_config.apply(*plate->config());

    m_is_bbl         = pb.is_bbl_vendor();
    m_filament_names = pb.filament_presets;
    m_model          = plater->model();

    // 檔頭
    m_pack.generator      = std::string("PING Slicer ") + SoftFever_VERSION;
    m_pack.generated_at   = iso8601_now();
    // 機型要給 printer_model（"FD300"），不是機器 preset 的名字（"FD300 0.4 nozzle"）。
    // 契約範例就是 FD300，而且口徑本來就是獨立欄位——帶著「0.4 nozzle」會原封不動
    // 印在寄給客戶的報價單上。取不到才退回 preset 名。
    m_pack.printer        = pb.printers.get_edited_preset().name;
    m_pack.process_preset = pb.prints.get_edited_preset().name;
    m_pack.process_modified = pb.prints.get_edited_preset().is_dirty;

    // 一律走 const 取值路徑：非 const 的 option() 帶著 create 參數，不小心用錯會就地生一個
    // 空選項出來，那就變成「抓不到卻有值」——正是契約最忌諱的事。
    const DynamicPrintConfig &cfg = m_config;

    if (const auto *pm = cfg.option<ConfigOptionString>("printer_model"); pm != nullptr && !pm->value.empty())
        m_pack.printer = pm->value;

    if (const auto *nd = cfg.option<ConfigOptionFloats>("nozzle_diameter"); nd != nullptr && !nd->values.empty()) {
        m_pack.nozzle     = nd->values.front();
        m_pack.has_nozzle = true;
    }
    if (const auto *lh = cfg.option<ConfigOptionFloat>("layer_height"); lh != nullptr) {
        m_pack.layer_height     = lh->value;
        m_pack.has_layer_height = true;
    }
    if (const auto *inf = cfg.option<ConfigOptionPercent>("sparse_infill_density"); inf != nullptr) {
        m_pack.infill_density = static_cast<int>(std::lround(inf->value));
        m_pack.has_infill     = true;
    }
    if (const auto *wl = cfg.option<ConfigOptionInt>("wall_loops"); wl != nullptr) {
        m_pack.wall_loops     = wl->value;
        m_pack.has_wall_loops = true;
    }
    {
        const auto *en = cfg.option<ConfigOptionBool>("enable_support");
        if (en != nullptr && !en->value) {
            m_pack.support = "none";
        } else if (en != nullptr) {
            // support_type 的值是 normal(auto)/tree(auto)/normal(manual)/tree(manual)，
            // 契約只認 normal / tree，取前綴即可。
            std::string st = m_config.opt_serialize("support_type");
            m_pack.support = (st.rfind("tree", 0) == 0) ? "tree" : "normal";
        }
    }

    // ── 收集要附進包裡的 preset 檔。取 edited preset＝使用者當下實際生效的那份
    //    （含他手動改過還沒存的值）。去掉 inherits/setting_id 讓它獨立可讀——
    //    換一台電腦沒有那條繼承鏈，留著就是存了個還原不了的東西。
    {
        auto add_preset = [this](const Preset &p, const char *type, const std::string &filename) {
            PresetFile pf;
            pf.filename = filename;
            pf.type     = type;
            pf.name        = p.name;
            pf.filament_id = p.filament_id;
            pf.config      = p.config;
            pf.config.erase("inherits");
            pf.config.erase("setting_id");
            m_preset_files.push_back(std::move(pf));
        };
        add_preset(pb.printers.get_edited_preset(), "machine", "machine.json");
        add_preset(pb.prints.get_edited_preset(),   "process", "process.json");
        int slot = 0;
        for (const std::string &fname : pb.filament_presets) {
            ++slot;
            const Preset *fp = pb.filaments.find_preset(fname);
            if (fp != nullptr)
                add_preset(*fp, "filament", "filament_" + std::to_string(slot) + ".json");
        }
    }

    // ── 計畫：這一盤上、可列印的物件，一「份」一列。
    //
    // 【不能無條件把同物件的所有份合成一列】ModelInstance 各自帶一份 transform，
    // 所以同一個物件的第二份**可以被單獨縮放或旋轉**。若直接拿第一份的重量/時間/尺寸
    // 再標 instances=N，而第二份其實放大了 1.5 倍，報價金額就直接錯——而代印線已經
    // 決定用 instances 自動填數量，錯的會一路吃到報價單上。（Codex 反審 2026-08-10 抓出）
    //
    // 判準用「變換後的外框尺寸」而不是去比 transform 的各個分量：外框不同 ⇒ 一定不同件；
    // 外框相同 ⇒ 縮放必相同。寧可多切一次（多一列），也不要把不一樣的東西併成一列。
    const DynamicPrintConfig *base_process_cfg = nullptr;
    for (const PresetFile &pf : m_preset_files)
        if (pf.filename == "process.json") { base_process_cfg = &pf.config; break; }

    for (size_t oi = 0; oi < m_model.objects.size(); ++oi) {
        const ModelObject *mo = m_model.objects[oi];
        // key（外框尺寸取到 0.0001mm）→ 在 m_plan 裡的位置
        std::vector<std::pair<std::string, size_t>> buckets;
        for (size_t ii = 0; ii < mo->instances.size(); ++ii) {
            const bool on_this_plate = (plate == nullptr) || plate->contain_instance(static_cast<int>(oi), static_cast<int>(ii));
            if (!on_this_plate || !mo->instances[ii]->is_printable())
                continue;

            const BoundingBoxf3 bb = mo->instance_bounding_box(ii);
            const Vec3d         sz = bb.defined ? bb.size() : Vec3d(0., 0., 0.);
            auto                q  = [](double v) { return std::to_string(static_cast<long long>(std::llround(v * 10000.))); };
            // 契約 v1.4 起輸出的 size_* 是物件座標的尺寸，所以它也要進 key：
            // 世界外框相同、物件尺寸不同的兩份（例：轉 90° 再把 X/Y 縮放對調）不能併成一列。
            Vec3d               osz = Vec3d::Zero();
            quote_object_coords_size(*mo, *mo->instances[ii], osz);
            const std::string   key = q(sz.x()) + "/" + q(sz.y()) + "/" + q(sz.z()) + "|" + q(osz.x()) + "/" + q(osz.y()) + "/" + q(osz.z());

            auto it = std::find_if(buckets.begin(), buckets.end(),
                                   [&key](const std::pair<std::string, size_t> &b) { return b.first == key; });
            if (it != buckets.end()) {
                ++m_plan[it->second].instances;
            } else {
                PlanItem item;
                item.obj_idx   = static_cast<int>(oi);
                item.inst_idx  = static_cast<int>(ii);
                item.instances = 1;
                // 【只算「真的覆寫到製程參數」的鍵】物件的 config 幾乎永遠不是空的——
                // Orca 會給每個載入的物件掛上 `extruder`（噴頭指派），那是模型層級的東西、
                // 不是製程參數。用 empty() 判斷會讓每一件都產一份多餘的 process_objN.json
                // （2026-08-10 實測：三件全中，內容與全域製程只差一個 name）。
                // 判準改成「這個鍵在製程 preset 裡存在嗎」——存在才是覆寫。
                if (base_process_cfg != nullptr) {
                    DynamicPrintConfig ov;
                    const DynamicPrintConfig &oc = mo->config.get();
                    for (const std::string &k : oc.keys()) {
                        if (!base_process_cfg->has(k))
                            continue;
                        if (const ConfigOption *opt = oc.option(k); opt != nullptr)
                            ov.set_key_value(k, opt->clone());
                    }
                    if (!ov.empty()) {
                        item.has_override = true;
                        item.override_cfg = std::move(ov);
                    }
                }
                buckets.emplace_back(key, m_plan.size());
                m_plan.push_back(item);
            }
        }
    }

    if (m_plan.empty())
        m_fatal = _u8L("這一盤上沒有可列印的物件。");
}

void PingQuotePackJob::slice_one(const PlanItem &item, PingQuoteObject &out, Ctl &ctl)
{
    // 模型複本：只留目標物件的那一份實例可列印，其餘全部關掉。
    // Print::apply 會把非 printable 的實例整個濾掉（PrintApply.cpp:145），
    // 所以這一刀就等於「只切這一件」，而且物件仍在原本的盤面位置，
    // 位置相關的差異（travel、skirt）與使用者看到的擺放一致。
    Model work = m_model;
    for (size_t oi = 0; oi < work.objects.size(); ++oi) {
        ModelObject *mo      = work.objects[oi];
        const bool   is_targ = (static_cast<int>(oi) == item.obj_idx);
        if (is_targ)
            mo->printable = true;
        for (size_t ii = 0; ii < mo->instances.size(); ++ii)
            mo->instances[ii]->printable = is_targ && (static_cast<int>(ii) == item.inst_idx);
    }

    Print print;
    print.is_BBL_printer() = m_is_bbl;
    // 使用者按取消時，讓正在跑的這一刀也停下來（狀態回呼在切片過程中會被反覆叫到）
    print.set_status_callback([&print, &ctl](const PrintBase::SlicingStatus &) {
        if (ctl.was_canceled())
            print.cancel();
    });

    print.apply(work, m_config);

    StringObjectException err = print.validate();
    if (!err.string.empty()) {
        out.error = err.string;
        return;
    }

    print.process();

    // export_gcode 是統計數字被算出來的地方（GCode.cpp update_print_estimated_stats），
    // 所以必須真的產一份 gcode；產完就刪，報價系統不需要它。
    boost::filesystem::path tmp =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("ping-quote-%%%%-%%%%.gcode");
    GCodeProcessorResult result;
    print.export_gcode(tmp.string(), &result, nullptr);

    const double t = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time;
    if (t > 0.) {
        out.time_s   = static_cast<int>(std::lround(t));
        out.has_time = true;
    }

    out.filament_changes = static_cast<int>(result.print_statistics.total_filament_changes);
    out.has_changes      = true;

    // ── 重量：各料 = 體積(mm³) × 密度(g/cm³) × 0.001；**總重＝各料相加**。
    //
    // 【為什麼不直接用引擎的 print_statistics().total_weight】
    // 引擎算總重時會跳過「不在 GCode writer extruder 清單裡」的料（GCode.cpp:1900 的
    // continue）。2026-08-10 實錄：不需支撐的花瓶，第二支料只擠了 prime line 的 0.18 g，
    // 引擎沒算進去，我算了 ⇒ 逐料相加比總重多 0.18，違反契約「相加必須等於 weight_g」。
    // 我試過改成沿用引擎的跳過規則，但 Print::extruders() 與 writer 那份清單並不一致，
    // 仍然對不上（那一件的逐料明細就整組被保險擋掉，反而讓它看起來像單料件）。
    //
    // 所以改成**由同一個迴圈同時算出兩者**：不變數由建構保證、永遠不可能自相矛盾，
    // 而且 0.18 g 是真的擠出去的料，計入才符合契約「所有線材的總重量」的語意。
    // 代價是總重可能與軟體畫面差極小的量（該例 0.19%），方向偏保守（不會少收）。
    const DynamicPrintConfig &cfg     = m_config;
    const auto               *density = cfg.option<ConfigOptionFloats>("filament_density");

    double sum         = 0.;
    bool   breakdown_ok = true;
    for (const auto &kv : result.print_statistics.total_volumes_per_extruder) {
        const size_t eid = kv.first;
        if (kv.second <= 0.)
            continue;                       // 沒擠出東西的槽位不列
        std::string fname = (eid < m_filament_names.size()) ? m_filament_names[eid] : std::string();
        double      d     = (density != nullptr && eid < density->values.size()) ? density->values[eid] : 0.;
        if (fname.empty() || d <= 0.) {
            // 名字或密度缺一，這一格湊不出「名稱↔重量」的對應 ⇒ 明細整組作廢，
            // 但總重仍然要算（缺的是明細，不是重量本身）
            breakdown_ok = false;
            if (d > 0.) sum += kv.second * d * 0.001;
            continue;
        }
        const double g = kv.second * d * 0.001;
        out.filaments.push_back(fname);
        out.weight_by_filament.push_back(g);
        sum += g;
    }

    if (sum > 0.) {
        out.weight_g   = sum;
        out.has_weight = true;
    }
    if (!breakdown_ok) {
        out.filaments.clear();
        out.weight_by_filament.clear();
    }

    // 與引擎自己的數字對照，差太多就留一行 log——不是為了改值，是為了日後有跡可循
    const double engine_total = print.print_statistics().total_weight;
    if (engine_total > 0. && std::fabs(sum - engine_total) > engine_total * 0.01)
        BOOST_LOG_TRIVIAL(warning) << "ping-quote: '" << out.name << "' sum=" << sum
                                   << " deviates >1% from engine total_weight=" << engine_total;

    { boost::system::error_code ec; boost::filesystem::remove(tmp, ec); }
}

// 取該物件的原始模型檔名（契約 v1.2 的 source_file，選填）。
//
// 【只回檔名、不回路徑】完整路徑會把切片人員電腦的目錄結構帶進客戶看得到的報價單，
//   對報價系統又毫無用處——契約也明寫「只要檔名、不要夾檔」。
// 【為什麼先看 volume 再看 object】從 .3mf 專案開檔時 ModelObject::input_file 是那顆
//   .3mf 的路徑，對「這件對回客戶寄來的哪一顆檔」完全沒有幫助；ModelVolume::source
//   .input_file 才記著當初匯入的網格檔名。直接開 STL 時兩者相同，取誰都對。
// 【抓不到就回空】例如在軟體內生成的物件（照片磚）根本沒有來源檔。
//   空字串＝該行整行省略，這是契約允許的（代印線 Q5 明講不強制）。
static std::string quote_source_file(const ModelObject &mo)
{
    auto basename = [](const std::string &p) -> std::string {
        if (p.empty())
            return std::string();
        return boost::filesystem::path(p).filename().string();
    };
    for (const ModelVolume *v : mo.volumes)
        if (v != nullptr && v->is_model_part() && !v->source.input_file.empty())
            return basename(v->source.input_file);
    return basename(mo.input_file);
}

void PingQuotePackJob::process(Ctl &ctl)
{
    if (!m_fatal.empty())
        return;

    const int n = static_cast<int>(m_plan.size());
    for (int k = 0; k < n; ++k) {
        if (ctl.was_canceled())
            return;

        const PlanItem   &item = m_plan[k];
        const ModelObject *mo  = m_model.objects[item.obj_idx];

        PingQuoteObject out;
        out.name        = mo->name;
        out.source_file = quote_source_file(*mo);   // 契約 v1.2 新增（選填）
        out.obj_idx   = item.obj_idx;
        out.inst_idx  = item.inst_idx;
        out.instances = item.instances;

        Vec3d sz = Vec3d::Zero();
        if (quote_object_coords_size(*mo, *mo->instances[static_cast<size_t>(item.inst_idx)], sz)) {
            out.size_x = sz.x(); out.size_y = sz.y(); out.size_z = sz.z();
            out.has_size = true;
        }

        ctl.update_status(k * 100 / std::max(n, 1),
                          (boost::format(_u8L("正在切片第 %1%/%2% 件：%3%")) % (k + 1) % n % out.name).str());

        try {
            slice_one(item, out, ctl);
        } catch (const CanceledException &) {
            return;                       // 使用者按了取消，安靜退場
        } catch (const std::exception &e) {
            // 單件失敗不該讓整批陣亡——記下原因，這一段就不輸出
            out.error = e.what();
            BOOST_LOG_TRIVIAL(error) << "ping-quote: object '" << out.name << "' failed: " << e.what();
        }

        if (!out.error.empty()) {
            ++m_pack.objects_failed;   // 一定要讓對方知道少了幾件，否則就是靜默漏報
            m_pack.warnings.push_back(out.name + "：" + out.error);
        }

        m_pack.objects.push_back(std::move(out));
    }

    ctl.update_status(100, _u8L("估價資訊已產生"));
}

// 產一張「只有這個物件」的縮圖。**必須在主執行緒跑**（要 GL context）。
//
// 契約要求「純模型：不要列印床、不要盤面網格、不要任何介面元素」——這在既有渲染路徑
// 是天然結果，不必另外處理：render_thumbnail_internal 只畫傳進去的 volume，明確不畫盤面
// （GLCanvas3D.cpp:6225「don't render plate in thumbnail」），背景也直接清成全透明
// （同檔 6126 glClearColor(0,0,0,0)）。
//
// use_plate_box=false ⇒ 不套盤面框、鏡頭 zoom_to_box 直接對著這一件 ⇒ 物件自動置中滿版。
// 參數組合照抄既有的單物件預覽（Plater::update_obj_preview_thumbnail）。
bool PingQuotePackJob::render_object_thumbnail(int obj_idx, int inst_idx, std::vector<unsigned char> &png_out)
{
    if (obj_idx < 0 || obj_idx >= static_cast<int>(m_model.objects.size()))
        return false;
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return false;
    GLCanvas3D *canvas = plater->get_view3D_canvas3D();
    if (canvas == nullptr)
        return false;

    ModelObject *mo = m_model.objects[obj_idx];
    if (inst_idx < 0 || inst_idx >= static_cast<int>(mo->instances.size()))
        return false;

    // 自己組 volume 集合。**注意**：use_plate_box=false 那條路徑不會再過濾 modifier，
    // 所以修改器與負體積必須在這裡就不要載進來，否則會被畫進客戶看的圖裡。
    GLVolumeCollection vols;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
        if (mo->volumes[vi] == nullptr || !mo->volumes[vi]->is_model_part())
            continue;
        vols.load_object_volume(mo, obj_idx, static_cast<int>(vi), inst_idx, "volume", true, false, false, false);
    }
    if (vols.volumes.empty())
        return false;

    ModelObjectPtrs model_objects;
    model_objects.emplace_back(mo);
    std::vector<ColorRGBA> colors = plater->get_extruders_colors();

    ThumbnailData    td;
    ThumbnailsParams params = { {}, false, true, true, true, 0, false };
    canvas->render_thumbnail(td, colors, PING_QUOTE_THUMB_PX, PING_QUOTE_THUMB_PX, params,
                             model_objects, vols, Camera::EType::Ortho, Camera::ViewAngleType::Iso, false, false);
    if (!td.is_valid())
        return false;

    // 最後一個參數是 flip：GL 讀回來的框是上下顛倒的，不翻會得到倒立的模型
    size_t png_size = 0;
    void  *png = tdefl_write_image_to_png_file_in_memory_ex(static_cast<const void *>(td.pixels.data()),
                                                           td.width, td.height, 4, &png_size, MZ_DEFAULT_COMPRESSION, 1);
    if (png == nullptr)
        return false;
    const unsigned char *p = static_cast<const unsigned char *>(png);
    png_out.assign(p, p + png_size);
    mz_free(png);
    return true;
}

// 把一份 preset 存成引擎原生的 json 並讀回記憶體。
//
// save_to_json 只寫 version/name/from＋所有 key-value，**不會寫 `type`**，
// 而載入器兩個鍵都要（OrcaSlicer.cpp:1883/1888：from 只收 system/User/user、
// type 只收 machine/process/filament，缺一就整個檔被拒）。
// 所以 from 傳 "User"，type 在存完之後補進 json 的最前面。
bool PingQuotePackJob::build_preset_json(const PresetFile &pf, std::string &out)
{
    boost::filesystem::path tmp =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("ping-quote-preset-%%%%-%%%%.json");
    try {
        // from 一定要是 "system"：載入器判相容性時，from=system ⇒ 系統名＝preset 自己的名字
        // （OrcaSlicer.cpp:1947），正好對得上製程的 compatible_printers；若寫 "User" 它改去讀
        // inherits，而我們為了攤平已經把 inherits 拿掉了 ⇒ 系統名變空 ⇒ 永遠報
        // 「process not compatible with printer」。2026-08-10 實測確認。
        pf.config.save_to_json(tmp.string(), pf.name, std::string("system"), std::string(SoftFever_VERSION));
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "ping-quote: save_to_json(" << pf.filename << ") failed: " << e.what();
        return false;
    }

    out.clear();
    FILE *f = boost::nowide::fopen(tmp.string().c_str(), "rb");
    if (f != nullptr) {
        char   buf[8192];
        size_t n;
        while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        ::fclose(f);
    }
    { boost::system::error_code ec; boost::filesystem::remove(tmp, ec); }
    if (out.empty())
        return false;

    // 補 type：插在第一個 `{` 之後。json 的鍵沒有順序要求，這樣最不需要拉一整套 json 相依。
    const size_t brace = out.find('{');
    if (brace == std::string::npos)
        return false;
    std::string header = "\n    \"type\": \"" + pf.type + "\",";
    // 線材還要補 filament_id：它是 preset 的中繼欄位不是設定鍵，save_to_json 不會寫，
    // 但載入器的 filament 分支會讀它（OrcaSlicer.cpp:1901）。2026-08-10 實測：少了這個欄位
    // 線材檔等於白給——CLI 會安靜地退回單一預設線材（filament_map 從 "1,1" 變成 "1"），
    // 於是換料次數歸零、沖刷量消失，重切結果比報價包少了 35%，而且**不會報任何錯**。
    if (pf.type == "filament" && !pf.filament_id.empty())
        header += "\n    \"filament_id\": \"" + pf.filament_id + "\",";
    out.insert(brace + 1, header);
    return true;
}

// 匯出 3mf 還原檔（主執行緒）。
//
// 為什麼要它：三個 preset json 還原不回來——2026-08-10 實測，載回去會切到一半失敗，
// 因為換料塔位置（wipe_tower_x/y）這類是**盤面層級**設定，不屬於任何一類 preset，
// 而 FD300 是圓形盤，塔放不好就切不出來。3mf 是引擎原生的完整快照，開起來就是當時那一盤。
//
// Silence＝不彈任何對話框（無人值守與一般路徑都需要）；SkipAuxiliary＝不夾帶附件資料夾，
// 少灌一些與報價無關的東西進去。
bool PingQuotePackJob::build_restore_3mf(std::string &out)
{
    if (m_plater == nullptr)
        return false;

    boost::filesystem::path tmp =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("ping-quote-restore-%%%%-%%%%.3mf");
    int ret = -1;
    try {
        ret = m_plater->export_3mf(tmp, SaveStrategy::Silence | SaveStrategy::SkipAuxiliary, -1, nullptr);
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "ping-quote: export_3mf threw: " << e.what();
        ret = -1;
    }

    out.clear();
    if (ret == 0) {
        FILE *f = boost::nowide::fopen(tmp.string().c_str(), "rb");
        if (f != nullptr) {
            char   buf[65536];
            size_t n;
            while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0)
                out.append(buf, n);
            ::fclose(f);
        }
    }
    { boost::system::error_code ec; boost::filesystem::remove(tmp, ec); }
    return !out.empty();
}

void PingQuotePackJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    // silent 模式（smoke）一個對話框都不能彈：modal 會把自動化的 CallAfter 佇列餓死，
    // 整輪吊死——照片磚線 2026-08-03 已經用一整輪作廢的數據換過這個教訓。
    auto report_fail = [this](const std::string &msg, long icon) {
        if (!m_opts.silent)
            MessageDialog(wxGetApp().mainframe, from_u8(msg), wxString::FromUTF8("匯出估價包"), icon | wxOK).ShowModal();
        BOOST_LOG_TRIVIAL(error) << "ping-quote: " << msg;
        if (m_opts.on_done)
            m_opts.on_done(false, msg);
    };

    if (canceled) {
        if (m_opts.on_done)
            m_opts.on_done(false, "canceled");
        return;
    }
    if (eptr) {
        report_fail("job threw an exception", wxICON_ERROR);
        return;   // eptr 保持非空 ⇒ 交給 worker 照既有規矩往上拋
    }

    if (!m_fatal.empty()) {
        report_fail(m_fatal, wxICON_WARNING);
        return;
    }

    int emitted = 0;
    for (const auto &o : m_pack.objects)
        if (o.error.empty()) ++emitted;

    if (emitted == 0) {
        std::string msg = _u8L("沒有任何物件切片成功，沒有可輸出的估價資訊。");
        for (const auto &w : m_pack.warnings) msg += "\n" + w;
        report_fail(msg, wxICON_ERROR);
        return;
    }

    // ── 縮圖：在這裡做而不是在 worker 裡，因為 GL 只能在主執行緒碰，而 finalize()
    //    本來就跑在主執行緒。N 張各幾十毫秒，不值得為它做跨執行緒的來回。
    //    出圖失敗不算致命：image 那行不輸出就是了（缺值＝整行省略）。
    std::vector<std::pair<std::string, std::vector<unsigned char>>> images;
    // 物件層級參數覆寫檔（契約 4-3）。m_pack.objects 與 m_plan 同序，所以可以用位置對應。
    std::vector<std::pair<std::string, std::string>> obj_process_blobs;
    const PresetFile *base_process = nullptr;
    for (const PresetFile &pf : m_preset_files)
        if (pf.filename == "process.json") { base_process = &pf; break; }

    int seq = 0;
    for (size_t i = 0; i < m_pack.objects.size(); ++i) {
        PingQuoteObject &o = m_pack.objects[i];
        if (!o.error.empty())
            continue;
        ++seq;

        std::vector<unsigned char> png;
        if (render_object_thumbnail(o.obj_idx, o.inst_idx, png)) {
            const std::string fname = "obj" + std::to_string(seq) + ".png";
            o.image = fname;
            images.emplace_back(fname, std::move(png));
        } else {
            m_pack.warnings.push_back(o.name + "：" + _u8L("縮圖產生失敗（估價資訊仍然完整）"));
        }

        // 這一件有物件層級覆寫 ⇒ 另存一份「製程 preset ＋ 該件覆寫」的參數檔。
        // 不給的話，切片數字明明套了覆寫，包裡卻只有全域製程，事後重現不出來。
        if (base_process != nullptr && i < m_plan.size() && m_plan[i].has_override) {
            PresetFile pf = *base_process;
            pf.filename = "process_obj" + std::to_string(seq) + ".json";
            pf.name     = base_process->name + " @" + o.name;
            pf.config.apply(m_plan[i].override_cfg);
            std::string blob;
            if (build_preset_json(pf, blob)) {
                o.process_file = pf.filename;
                obj_process_blobs.emplace_back(pf.filename, std::move(blob));
            } else {
                m_pack.warnings.push_back(o.name + "：" + _u8L("物件層級參數檔產生失敗。"));
            }
        }
    }

    // preset 檔（machine/process/filament_N）——三類齊全才還原得回去，見 PresetFile 檔頭說明
    std::vector<std::pair<std::string, std::string>> preset_blobs;
    for (const PresetFile &pf : m_preset_files) {
        std::string blob;
        if (build_preset_json(pf, blob))
            preset_blobs.emplace_back(pf.filename, std::move(blob));
        else
            m_pack.warnings.push_back(pf.filename + "：" + _u8L("參數檔產生失敗，包內不含它。"));
    }
    // 只有 process.json 真的進包了才寫這一行（缺值＝整行省略）
    for (const auto &b : preset_blobs)
        if (b.first == "process.json")
            m_pack.process_file = "process.json";

    // 3mf 還原檔：**預設含**（契約 v1.2；理由與歷史寫在 PingQuotePack.hpp 的
    // include_restore_3mf 註解裡——一句話＝沒有它，客戶／列印端拿到的包無法重現這一盤）。
    // restore_file 與 restore_size_b 一起設：契約規定有檔就必須報大小，
    // 而 size 用的是**解壓後**的位元組數（＝這顆 std::string 的長度），不是 zip 內壓縮後的。
    std::string restore_3mf;
    if (m_opts.include_restore_3mf) {
        if (build_restore_3mf(restore_3mf) && !restore_3mf.empty()) {
            m_pack.restore_file   = "restore.3mf";
            m_pack.restore_size_b = static_cast<std::uint64_t>(restore_3mf.size());
        } else {
            restore_3mf.clear();   // 產失敗就別把半顆檔塞進 zip
            m_pack.warnings.push_back(_u8L("3mf 還原檔產生失敗，包內不含它。"));
        }
    }

    // quote.txt 要在縮圖跑完之後才產——image= 那幾行是上面才填進去的
    const std::string txt = ping_quote_format_txt(m_pack);

    // 預設檔名：報價_YYYYMMDD_機型.pingquote
    char        datebuf[16];
    std::time_t now = std::time(nullptr);
    std::tm     lt{};
#ifdef _WIN32
    ::localtime_s(&lt, &now);
#else
    ::localtime_r(&now, &lt);
#endif
    ::snprintf(datebuf, sizeof(datebuf), "%04d%02d%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    const wxString default_name = from_u8(std::string("報價_") + datebuf + "_" + m_pack.printer + ".pingquote");

    std::string path = m_opts.output_path;
    if (path.empty()) {
        wxFileDialog dlg(wxGetApp().mainframe, wxString::FromUTF8("匯出估價包"),
                         from_u8(wxGetApp().app_config->get_last_output_dir("")), default_name,
                         wxString::FromUTF8("PING 估價包 (*.pingquote)|*.pingquote|ZIP (*.zip)|*.zip"),
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) {
            if (m_opts.on_done)
                m_opts.on_done(false, "user cancelled the save dialog");
            return;
        }
        path = into_u8(dlg.GetPath());
    }

    try {
        Zipper zip(path);
        zip.add_entry("quote.txt", txt.data(), txt.size());
        for (const auto &img : images)
            zip.add_entry(img.first, img.second.data(), img.second.size());
        for (const auto &b : preset_blobs)
            zip.add_entry(b.first, b.second.data(), b.second.size());
        for (const auto &b : obj_process_blobs)
            zip.add_entry(b.first, b.second.data(), b.second.size());
        if (!restore_3mf.empty())
            zip.add_entry("restore.3mf", restore_3mf.data(), restore_3mf.size());
        zip.finalize();
    } catch (const std::exception &e) {
        report_fail(std::string(_u8L("無法寫入估價包：")) + e.what(), wxICON_ERROR);
        return;
    }

    // 單物件才提供剪貼簿——剪貼簿放不了圖，多物件會讓使用者以為圖也一起帶走了。
    // silent 模式不碰剪貼簿：無人值守時搶使用者的剪貼簿是很沒禮貌的副作用。
    std::string done = (boost::format(_u8L("已輸出 %1% 個物件的估價包。")) % emitted).str();
    if (m_pack.objects_failed > 0)
        done += "\n" + (boost::format(_u8L("⚠ 有 %1% 個物件切片失敗、未列入估價包，請確認是否要補報。")) % m_pack.objects_failed).str();
    if (!m_opts.silent && emitted == 1 && wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(from_u8(txt)));
        wxTheClipboard->Close();
        done += "\n" + _u8L("估價資訊也已複製到剪貼簿，可直接貼進報價系統（圖仍在檔案裡）。");
    }
    for (const auto &w : m_pack.warnings)
        done += "\n" + w;

    BOOST_LOG_TRIVIAL(info) << "ping-quote: wrote " << path << " (" << emitted << " objects)";
    if (!m_opts.silent)
        MessageDialog(wxGetApp().mainframe,
                      from_u8(done), wxString::FromUTF8("匯出估價包"), wxICON_INFORMATION | wxOK).ShowModal();
    if (m_opts.on_done)
        m_opts.on_done(true, done);
}

} // anonymous namespace

// ---------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------

void ping_quote_generate(Plater *plater, const PingQuoteOptions &opts)
{
    if (plater == nullptr) {
        if (opts.on_done)
            opts.on_done(false, "plater is null");
        return;
    }
    Worker &w = plater->get_ui_job_worker();
    replace_job(w, std::make_unique<PingQuotePackJob>(plater, opts));
}

}} // namespace Slic3r::GUI
