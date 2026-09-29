#ifndef slic3r_GCode_PingCycleTower_hpp_
#define slic3r_GCode_PingCycleTower_hpp_

// PING 照片磚「每層循環洗料塔」（WT 線，2026-09-08；計畫正本＝設計提案/照片磚循環洗料塔_20260908_01a07fef/CLAUDE_PLAN.md）
//
// 一份資料驅動三件事：塔路徑（真實 offset 圈）、寫出順序（塔段配方由深到淺→離塔→模型段由淺到深）、用料／時間統計
// （擠出走 GCode::extrude_path ⇒ 預覽與統計吃的就是這批 G1）。離線對照答案＝照片磚_循環洗料塔/cycle_core.py（fixture 12/12）。
//
// 料路 ID：E0＝第 1 路最淺（雙料 M6051 S1／四料 A100）；塔內順序**由內往外**＝E(n-1)…E0；換料只在圈界。
// ⇒ 最深的那一路在最內圈、最淺的 E0 在最外圈（Eric 2026-09-09），塔的外皮與 brim 統一是最淺色、垂直方向無色變。
// 純 E0 判定看**配方權重**（雙料 S==1；四料 A==100 其餘 0），不看預覽色。
//
// 設定載體＝PrintObjectConfig 的 ping_pt_cycle*（工作室寫進 3MF 物件層 metadata；未開＝本檔完全不介入，行為與舊版逐位相同）。

#include <map>
#include <string>
#include <vector>
#include <memory>
#include "../libslic3r.h"
#include "../Polygon.hpp"
#include "../Polyline.hpp"
#include "../Point.hpp"
#include "PingColorMix.hpp"   // PhotoPartAssignment（collect_photo_parts 的輸出型別）

namespace Slic3r {

class Print;
class PrintObject;
class Model;

namespace PingCycle {

// 一段的規格：這一段要**同時**擠出的料路 ＋ 連續幾圈。
// channels 只有 1 支＝純料；多支＝等比混合一起擠。
// 🔴 Eric 2026-09-10（牌 c-0910-WT-10）：四料改成「深色三支一起洗 2 圈 → 純白洗 4 圈」——
//    2/3/4 三支深色互相汙染不影響結果，分三段各洗一圈是白花料與時間；真正要洗乾淨的是最後那段白色，
//    所以把省下來的圈數全給白色（白 2→4 圈＝洗料量 2.2 倍，全塔總量只多 18%）。
struct StageSpec {
    std::vector<int> channels;   // 料路索引 0＝E0（第 1 路最淺／白）…3＝E3；quad 對應 M6052 的 A/B/C/D
    int              laps = 1;
};

struct Settings {
    bool                   enabled = false;
    std::string            mode;              // "dual" | "quad"
    std::vector<StageSpec> stages;            // **由內往外**：stages[0]＝最內圈那一段（最深），最後一段＝最外圈（純 E0 最淺）
                                              // 預設 dual {E1:1, E0:3}（E0 2→3＝Eric 0909 實印裁）；quad {E1+E2+E3:2, E0:4}（Eric 0910）
    float                  size_mm = 0.f;     // 0＝預設 25 mm 固定（Eric 2026-09-08；原式 44 × 口徑/0.4 已停用）
    float                  gap_mm  = 15.f;    // 塔與模型外緣距離
    float                  brim_mm = 8.f;     // 首層外擴 brim（用**最後一段**＝最淺那段的料，見 GCode.cpp）
    float                  max_flow = 11.f;   // 塔流量上限 mm³/s（ping_pt_cycle_max_flow；0＝不設上限）。見 Tower::tower_speed()
    int                    stage_count() const { return (int) stages.size(); }
    int                    total_laps() const { int n = 0; for (const StageSpec& s : stages) n += s.laps; return n; }
};

// 一個材料段：由內往外連續幾圈同一配方（loops[0]＝最內圈）
struct Stage {
    std::string channel;      // 段標籤，只進 G-code 註解："E0"｜"E1+E2+E3"…
    std::string recipe_cmd;   // "M6051 S1" | "M6052 A100 B0 C0 D0" | "M6052 A0 B34 C33 D33" …
    int         first_lap = 0, last_lap = 0;   // 1-based
};

// 一層的進塔計畫。
// visits＝本層真的要印的顏色各自洗哪幾圈，**依 layer_extruders 的列印順序**（visits[0]＝層首那趟）。
// 🔴 refill＝「補洗色」（規格 §12 R12-7，Eric 2026-09-19 定原則、09-20 裁 Q1～Q5「照建議」）：
//    本層印不到它，但**它那支料整層都洗不到** ⇒ 留著它 R12-1 的原圈位、在層首先洗一趟，總圈數不變（Q2）。
//    refill_tool < 0 ＝本層不需要補洗（兩支料本來就都會被洗到）。
struct SplitPlan {
    std::vector<std::pair<unsigned int, std::vector<int>>> visits;
    int                                                    refill_tool = -1;
    std::vector<int>                                       refill_rings;
};

// 某個層高下的整塔幾何（圈的中心線，已含塔位置、scaled 座標）
struct LayerGeometry {
    float                 layer_height = 0.f;
    float                 width        = 0.f;   // 線寬
    float                 spacing      = 0.f;   // 線道間距（Flow）
    double                mm3_per_mm   = 0.;
    std::vector<Polyline> loops;                // **由內往外**，每圈閉合（首尾同點），從接縫起
    std::vector<Polyline> brim_loops;           // 首層用：由內往外的外擴圈（接在**最後一段＝最淺**之後）
    std::string           problem;              // 非空＝幾何不合法（分裂／消失／空腔封閉）
};

// 從 Print 讀設定、palette、幾何位置；不合法回 nullptr 並在 why 說明
std::unique_ptr<class Tower> create(const Print& print, std::string& why);

// 純料配方命令（給 ToolOrdering／GCode 共用）
// 一段的配方指令：channels 單支＝純料、多支＝等比混合，餘數給**第一支**
//（例：quad {E1,E2,E3} → "M6052 A0 B34 C33 D33"＝Eric 2026-09-10 指定的比例）。
std::string recipe_for(const std::string& mode, const std::vector<int>& channels);
// 段標籤（"E0"｜"E1+E2+E3"），只進 G-code 註解
std::string stage_label(const std::vector<int>& channels);
bool        is_pure_light_recipe(const std::string& cmd);   // 雙料 S==1；四料 A==100 其餘 0
// 配方的「亮度分數」0..1（1＝最淺、0＝最深）。Eric 2026-09-09 裁「丙」：
//   雙料＝S（E0 佔比）；四料＝(A×3 + B×2 + C×1 + D×0) / 300
// ＝把塔自己就在用的「E0 最淺、E3 最深」假設一路用到混合配方上，不必另取線材色。
// 解析不出來回 -1（呼叫端據此維持原生順序，不要當成最深）。
double      light_score(const std::string& cmd);

// 讀某台 print 是否任何物件開了循環塔（ToolOrdering 用）
bool enabled_for(const Print& print);

/* 把模型的列印零件收成「照片磚名冊」，交給 PingMix::collect_photo_palette 判定。
   三處呼叫點（本檔 collect_palette／BackgroundSlicingProcess／Plater 匯入分析）原本各有一份
   逐字相同的迴圈——同一個洞要補三次，所以 2026-09-22 收成這一支（牌 c-0922-ACC-05）。

   🔴 **帶 `ping_keep_clear_of_parts` 旗標的零件（角落固定塊）不進名冊。**
   名冊採「全有全無」：每個列印零件的名稱都要解析得出混色配方，否則整盤判 Invalid（髒照片磚）。
   那條規則立在角落固定塊（T057）之前，而固定塊是使用者**刻意加上去的非磚零件**、本來就不會有
   配方 ⇒ 不排除它，照片磚只要加固定塊就切不了（Eric 2026-09-22 實撞：
   "2 of 5 printable parts have no photo-tile recipe; first: 角落固定塊"）。
   ⚠ 只認旗標、不認名字（名字會被改、會隨語言變）；T056 以前存的舊塊沒有旗標，不在射程內
     ——那批本來就要「刪掉重加」才有 T057 的讓位保護（見 T057 版本資訊）。
   ⚠ 閘門本身**不放寬**：磚零件真的掉了配方仍然必須被擋下，那是它存在的理由。 */
void collect_photo_parts(const Model& model, std::vector<PingMix::PhotoPartAssignment>& out);

// 從模型零件名收 palette（tool→配方命令），照片磚不合法回 false（與 GUI 端 collect 同一支判定）
bool collect_palette(const Model& model, std::map<int, std::string>& palette, std::string& reason);

class Tower {
public:
    const Settings&                      settings() const { return m_settings; }
    const std::vector<Stage>&            stages()   const { return m_stages; }
    const std::map<int, std::string>&    palette()  const { return m_palette; }
    // 純 E0 的虛擬槽（0-based）
    const std::vector<unsigned int>&     pure_light_tools() const { return m_pure_light_tools; }
    float                                nozzle_mm() const { return m_nozzle; }
    float                                size_mm()   const { return m_size; }
    Point                                center()    const { return m_center; }   // scaled
    // 該層高的幾何（快取）；problem 非空＝這層不能做塔
    const LayerGeometry&                 geometry_for(float layer_height, bool first_layer);
    // 塔的列印速度 mm/s：製程外牆速度（沒設＝60），但流量不超過 Settings::max_flow。
    // 🔴 Eric 2026-09-30「照建議」（開發中清單 #39）：原本塔直接跟外牆速度（照片磚 120 mm/s）⇒ 四料 0.6 塔＝22 mm³/s，
    //    切成純白那一瞬間單支料推不動、擠出機回彈；減半（11 mm³/s）就不回彈、外牆也較均勻偏白（ORCA SOP 無頭切片 §22-9）。
    //    用流量而不是 mm/s 定：塔線寬＝口徑，0.4 本來就低於 11 ⇒ 不變；1.0 壓到約 25～27 mm/s。首層照舊由 _extrude 換成首層速度。
    double                               tower_speed(double outer_wall_speed, double mm3_per_mm) const;
    // 離線報表用：塔外輪廓（scaled，已含位置）
    const Polygon&                       outline() const { return m_outline; }

    // 🔴 每色進塔（Eric 2026-09-14 Q1「做」，牌 c-0914-PTI-01；實印證據＝B 檔 band_section_L165-210_B_split.gcode）：
    // 每個顏色開印前各自進塔、只洗分給它的圈，取代「層首一趟整塔」（A 檔白塊＝換色後噴頭裡的前一色殘留）。
    // 圈位（由內往外 1-based；規格＝照片磚_核心規格.md §12 R12-1／R12-3，Eric 2026-09-14 裁「乙」、09-16 補兩裁）：
    //   **最淺色固定佔最外兩圈**（塔外皮維持最淺色＝0909 裁定），其餘圈由外往內一色一圈、由淺到深，
    //   多出來的圈併給最深色。塔圈數＝色階數＋1（K=3→4、K=4→5、K=6→7）；
    //   **K=2 是 R12-4 的特例、維持 4 圈** ⇒ 最深色拿到第 1～2 圈，與 T044 出貨版逐位相同。
    // 本層沒有的顏色把圈併給同層下一個顏色（淺→深），後面沒有就給前一個（Eric 0913 Q2）。
    // 適用範圍＝**雙料 且 總圈數 ≥ 色數＋1**；四料照 R12-3「不動」⇒ split_active()=false，照舊層首一趟整塔。
    //
    // 🔴 **R12-7 每層兩支料都要洗到**（Eric 2026-09-19 定原則、09-20 裁 Q1～Q5「照建議」）：
    //   上面那條「缺色就把圈併給鄰色」在**整層只有純深灰**時會把 4 圈全給深灰 ⇒ 白料整段時間零流量
    //   （0919 年輕女實印：第 1～214 層，白料停擺約一小時；塔外皮也變成深灰，打破 R12-1 自己的理由）。
    //   ⇒ 改成：先看本層兩支料是不是都洗得到（**任何混合配方 0<S<1 就算兩支都洗到，不設門檻**＝Q3），
    //     缺哪一支就把**那一支的純色**當「補洗色」、**沿用它 R12-1 的原圈位**（不另外加圈 ⇒ 總圈數不變＝Q2），
    //     由 GCode 在**層首先洗補洗色、再洗本層第一色**。只做雙料（Q4）。
    //   ⚠️ 因此 K=2／K=3 的純單色層會比 0916 版多一趟補洗＝**刻意的行為改變**，不是回歸。
    bool                                 split_active() const { return !m_split_roles.empty(); }
    // 本層的進塔計畫：visits 為空＝本層不適用（沒開、或本層沒有 palette 裡的顏色）⇒ 照舊層首一趟整塔。
    SplitPlan                            split_plan(const std::vector<unsigned int>& layer_extruders) const;

    // behind＝塔擺在磚的**後方（+Y）**而不是右邊（+X）。只有右邊放不下時才會是 true（Eric 2026-09-16 裁「乙」），
    // 因為照片磚必須沿 X 擺、不得轉 90°（規格 §6 R6-12：校正表是在 X 方向量的）。
    Tower(Settings s, std::map<int, std::string> palette, float nozzle, Point center, float size, bool behind);

private:
    // 圈位角色由淺到深：first＝tool、second＝分給它的圈號（1-based、由內往外；最淺色恆兩圈、最深色可能多於一圈）
    std::vector<std::pair<int, std::vector<int>>> m_split_roles;
    void build_split_roles();
    Settings                        m_settings;
    std::vector<Stage>              m_stages;
    std::map<int, std::string>      m_palette;
    std::vector<unsigned int>       m_pure_light_tools;
    float                           m_nozzle = 0.4f;
    float                           m_size   = 44.f;
    Point                           m_center;
    bool                            m_behind = false;   // 見建構子說明；影響接縫朝哪一側
    Polygon                         m_outline;      // scaled，已平移到 m_center
    std::map<int, LayerGeometry>    m_cache;        // key = round(layer_height*1000)
    void build_outline();
};

} // namespace PingCycle
} // namespace Slic3r

#endif // slic3r_GCode_PingCycleTower_hpp_
