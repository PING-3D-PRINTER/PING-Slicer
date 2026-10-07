#ifndef slic3r_GUI_PingQuotePack_hpp_
#define slic3r_GUI_PingQuotePack_hpp_

// =====================================================================
// PING 報價包（產生報價資訊給代印報價／銷售 CRM 系統）
//
// 什麼是它：對當前盤上的每個物件「各自單獨切一次」，取得該件的真實重量與
// 真實時間，輸出成介面契約 v1 的 quote.txt；業務把檔案丟進報價系統就自動
// 長出報價列，不必再截圖讓 AI 認數字。
//
// 契約正本＝代印線發包單「crm-slicer 報價資訊 v1」
//   D:\dev\2026claude\20260612 代印報價系統\docs\發包_切片軟體_報價資訊輸出_v1_20260810.html
// v1.2 變更請求（代印線 2026-08-17 提出，Eric 2026-08-19 裁「一起做」）：
//   D:\dev\2026claude\20260612 代印報價系統\docs\發包_切片軟體_報價包夾帶還原檔_v1.2_20260817.html
//   四項＝restore.3mf 改預設含／新增檔頭 restore_size_b／新增每物件選填 source_file／schema 維持 1。
// ⚠ 這份格式是兩邊一式的介面契約：**欄位的新增／改名／改語意／改單位都不得單方變更**，
//   要改請回代印／CRM 線提出，雙方確認後一起改版（schema 跟著進位）。
//
// 【schema 為什麼還是 1（v1.2 沒進位，這不是漏改）】
//   v1.2 只有「純新增欄位」與「預設值改變」，沒有既有欄位改語意。契約 4-x 明訂
//   接收端對不認識的欄位一律忽略 ⇒ 舊解析器讀新包不會壞，不構成破壞性變更。
//   代印線 Q6 提案維持 1、本線同意。**要動 schema 一定是兩邊講好，不准單方進位。**
//
// 【架構＝B 方案（Eric 2026-08-10 裁）】
//   切片跑在**自己的 Print 實例**上，模型用複本（ModelVolume 的 mesh 是 shared_ptr，
//   複製很便宜），**完全不碰使用者盤面上的物件與復原堆疊**。
//   不走 A 方案（直接改使用者物件的 printable 旗標＋沿用背景切片）的理由：報價包是
//   業務按了就走的功能，不該有任何機會把他盤上的東西改壞——中途取消或崩潰時要還原
//   旗標的狀態機，就是照片磚那條線已經付過學費的坑。
//
// 【設計約束・日後往上加東西時不得破壞】
//   A. 抓不到的值＝**整個欄位不輸出**，絕不用 0／空字串／null 頂替。所以每個可選欄位
//      都配一個 has_* 旗標。理由：報價系統收到 weight_g=0 會當成「這件真的 0 克」拿去
//      算錢——寧可沒有，不要有錯的。
//   B. **不算價格、不輸出成本**。費率與公式在報價系統，切片端只交物理量。
//   C. 逐物件切片的每一件都各自帶一條 prime line 與一次暖機，所以 N 件的重量／時間
//      加總會大於整盤實際值。這是已知且已回報代印線的取捨，不要偷偷「修正」它。
// =====================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

class Plater;

// 單一物件的報價資料（對應 quote.txt 的一段 [object N]）
struct PingQuoteObject
{
    std::string              name;                 // 物件名，帶進報價單的「3D 檔名」欄
    // 原始模型檔名（契約 v1.2 新增，**選填**）。用途＝列印端把「報價單上的物件」
    // 對回「客戶寄來的哪一顆檔」。**只有檔名、不含路徑**——路徑會洩漏切片人員的
    // 電腦目錄結構，而且對報價系統毫無用處。抓不到就留空＝整行省略（代印線 Q5 明講不強制）。
    std::string              source_file;
    std::vector<std::string> filaments;            // 依槽位順序的線材 preset 名
    int                      obj_idx   = -1;       // 回指 model 的物件索引（縮圖用）
    int                      inst_idx  = -1;       // 代表實例索引（縮圖要照實際擺放的姿態畫）
    int                      instances = 1;        // 本盤上同一物件的份數（見下方註）

    bool   has_size = false;
    double size_x = 0., size_y = 0., size_z = 0.;  // 物件座標下的長寬高：含縮放、不含擺放旋轉（mm，契約 v1.4）

    bool   has_weight = false;
    double weight_g = 0.;                          // 所有線材總重，含支撐與該件的換料塔
    std::vector<double> weight_by_filament;        // 與 filaments 同序，相加＝weight_g

    bool   has_time = false;
    int    time_s = 0;                             // 秒（整數）——不輸出 "49m23s" 這類文字

    bool   has_changes = false;
    int    filament_changes = 0;

    // 僅在該物件有物件層級參數覆寫時才給（契約 4-3）。指向包內 process_objN.json。
    std::string process_file;
    std::string image;                             // zip 內縮圖檔名
    std::string error;                             // 非空＝該件切片失敗，整段不輸出
};

// 一次「產生報價包」的完整結果
struct PingQuotePack
{
    std::string generator;                         // 例 "PING Slicer 3.6.0"
    std::string generated_at;                      // ISO 8601 含時區
    std::string printer;                           // 機型（列印設備的顯示值）
    std::string process_preset;                    // 製程 preset 名——一行代表一整組參數
    bool        process_modified = false;          // preset 被使用者改過（尚未入契約，見 .cpp 註）

    bool   has_nozzle = false;        double nozzle = 0.;
    bool   has_layer_height = false;  double layer_height = 0.;
    bool   has_infill = false;        int    infill_density = 0;   // 純數字不帶 %
    bool   has_wall_loops = false;    int    wall_loops = 0;

    std::string support;                           // none / normal / tree（空＝省略該行）
    std::string process_file;                      // 指向包內完整製程參數檔（空＝包裡沒有）
    // 指向包內的 3mf 還原檔（空＝包裡沒有）。
    // 【Eric 2026-08-10 裁「走丙」】preset json 還原不回來——換料塔位置這類是**盤面層級**
    // 設定，不屬於 machine/process/filament 任何一類 preset，少了它 FD300 的圓形盤放不好
    // 塔就切失敗。3mf 是引擎原生的完整快照（模型＋參數＋盤面狀態），開起來就是當時那一盤。
    // ⚠ 這一項**與發包單 4-5 節「只放參數，不要放模型」明確衝突**，是 Eric 知情後的裁定，
    //    已列入要回報代印線的清單——契約兩邊一式，不得單方改完不講。
    std::string restore_file;
    // 還原檔的**解壓後**位元組數（契約 v1.2 新增；restore_file 非空時必填）。
    // ⚠ 不是 zip 內的壓縮後大小——對方拿這個值決定上傳路徑與進度提示，並用來對帳
    //   「這包的還原檔有沒有被截斷」，比的是解出來那顆檔。給壓縮後的值＝每次都對不上，
    //   對方會直接把包擋掉（v1.2 第三節寫明會擋、不會靜默略過）。
    // 0 ＝沒有還原檔（此時 restore_file 也必須是空的，兩者一起設一起清）。
    std::uint64_t restore_size_b = 0;
    std::string mode = "per_object";               // per_object＝逐件單獨切（精確）

    // 切片失敗、因此整段沒有輸出的物件數。>0 時一定要讓對方知道，
    // 否則輸出「少一件但內部完全自洽」，報價系統無從察覺、業務就漏報那一件的錢。
    int objects_failed = 0;

    std::vector<PingQuoteObject> objects;
    std::vector<std::string>     warnings;         // 給使用者看的提醒，不進 quote.txt
};

// 依介面契約 v1 產生 quote.txt 全文。
// UTF-8 無 BOM、key=value、等號兩側不加空白；頭尾標記必附（使用者貼上時常會連前後
// 多餘文字一起貼，有標記才不會解析壞掉）。
std::string ping_quote_format_txt(const PingQuotePack &pack);

// 產生報價包的選項。預設值＝一般使用者路徑（問存檔位置、完成後彈訊息）。
struct PingQuoteOptions
{
    // 非空＝直接寫到這個路徑，不問使用者。給無人值守的 smoke 用。
    std::string output_path;
    // true＝一個對話框都不彈（含錯誤）。smoke 必須設，否則 modal 會把自動化吊死
    // ——照片磚線 2026-08-03 就是被 modal 餓死 CallAfter 佇列整輪作廢。
    bool silent = false;
    // 包內要不要附 restore.3mf。**預設開**（契約 v1.2；Eric 2026-08-19 裁）。
    //
    // 【為什麼從預設關改成預設開——前提變了，不是當初做錯】
    //   v1.1 的 Q6 由代印線裁「預設不含」，理由是報價系統只讀 quote.txt 與 PNG、
    //   12.5 MB 對它是純負擔。**那個判斷在當時是對的。** 之後多了兩個當時不存在的用途：
    //   ① 代印動線（Eric 0816 定）：報價回簽→訂單準備中→「開列印需求」→ CRM 自動建
    //      列印專案並把檔案搬過去，報價當時切好的那一盤要一路流到列印端；沒有還原檔，
    //      列印人員只拿得到 STL，得再切一次，而且參數不保證與報價時一致
    //      ＝**報的價和印的東西可能不是同一組設定**。
    //   ② 客戶自助詢價（Eric 0819 定，這次移植到全版本的動機）：客戶自己產包丟給我們
    //      估價。⚠ 客戶那端算出來的重量／時間是**他的機型與他的 preset**，不是我們的
    //      成本，直接拿去報價會報錯——唯一能讓我們算出自己數字的，就是包裡這顆還原檔
    //      （我們這邊重切一次）。**沒有它，客戶丟過來的包對估價幾乎沒有用。**
    //
    // 關掉的路徑保留（代印線 Q2 要求）：偶爾只是要個數字報價、不想傳大檔。
    // 目前只吃設定檔鍵 ping_quote_include_restore_3mf（缺鍵＝用這裡的預設值）；
    // 對話框上的勾選等 UI 落點 P4 定案一起做（Eric 0819 裁「落點之後再說」）。
    bool include_restore_3mf = true;
    // 完成（或失敗）時在主執行緒回呼。ok=false 時 message 是原因。
    std::function<void(bool ok, const std::string &message)> on_done;
};

// 入口：對當前盤逐物件切片並產生報價包。
// 背景執行（Job worker），不阻塞 UI。
void ping_quote_generate(Plater *plater, const PingQuoteOptions &opts = PingQuoteOptions());

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PingQuotePack_hpp_
