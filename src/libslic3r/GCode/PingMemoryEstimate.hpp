// PING（開發中清單 #62）：切片算完、產 G-code 之前，先估「接下來還要多少記憶體」。
// 起因＝大件（約 47×53×47 cm 的實心件配 gyroid 填充）產 G-code 與載入預覽時把記憶體吃光。
// 估法＝數切片結果裡的擠出線段與路徑（只數、不複製）→ 預估 G-code 的移動筆數 → 乘每筆用量，
// 跟本行程現在還能用的記憶體比。只有圖形介面的背景切片會啟用（Print::ping_set_memory_precheck），
// 命令列切片不啟用、行為不變。
#pragma once

#include <cstdint>
#include <string>

#include "../Exception.hpp"

namespace Slic3r {

class ExtrusionEntityCollection;
class Print;

// 一批擠出路徑的計數。
struct PingExtrusionCount
{
    // 不可再分的列印單位（loop／multi-path／path）。G-code 產生器每印一個，前面會帶空跑、回抽、回補這類移動。
    uint64_t entities { 0 };
    // 最底層的 ExtrusionPath 數（一個 loop 或 multi-path 可能含好幾段）。
    uint64_t paths { 0 };
    // 線段數＝各 path 的（點數 − 1）加總。一段線段對應 G-code 裡的一筆擠出移動。
    uint64_t segments { 0 };

    PingExtrusionCount& operator+=(const PingExtrusionCount &rhs)
    {
        entities += rhs.entities;
        paths    += rhs.paths;
        segments += rhs.segments;
        return *this;
    }
    // 同一批路徑印 n 次（同一個物件擺 n 份、裙邊印 n 層）。
    PingExtrusionCount operator*(uint64_t n) const { return { entities * n, paths * n, segments * n }; }
    bool operator==(const PingExtrusionCount &rhs) const
    {
        return entities == rhs.entities && paths == rhs.paths && segments == rhs.segments;
    }
};

// 本行程現在還能再用多少記憶體。known == false（非 Windows、或查不到）時不做檢查。
struct PingAvailableMemory
{
    bool     known { false };
    // commit_available_bytes 與「行程上限還剩的」取小的。
    uint64_t available_bytes { 0 };
    // Windows 的可用承諾量（實體記憶體＋分頁檔還能再給多少）。
    uint64_t commit_available_bytes { 0 };
    // 本行程被 Job 物件設的記憶體上限；0＝沒有上限。可用承諾量不會反映它，所以要另外查。
    uint64_t job_limit_bytes { 0 };
    // 本行程目前已用的私有記憶體（PrivateUsage）。
    uint64_t process_private_bytes { 0 };
};

struct PingMemoryEstimate
{
    // 外牆＋填充，已乘物件份數。
    PingExtrusionCount  objects;
    // 支撐，已乘物件份數。
    PingExtrusionCount  supports;
    // 裙邊與 brim（裙邊已乘會印的層數）。
    PingExtrusionCount  skirt_brim;
    // 換料塔預先產好的 G-code 裡的 G1 行數。
    uint64_t            wipe_tower_moves { 0 };
    // 預估整份 G-code 的移動（G1）筆數。
    uint64_t            estimated_moves { 0 };
    // estimated_moves × 每筆用量：產 G-code 到載入預覽這一趟預估還要的記憶體。
    uint64_t            needed_bytes { 0 };
    PingAvailableMemory available;

    // 預估需要量超過可用量的 85% 就算不夠；可用量查不到（known == false）一律不算不夠。
    bool        short_of_memory() const;
    // 給 log 用的一行（英文、不含換行）。
    std::string to_log_string() const;
};

// 數一個集合裡的擠出路徑（遞迴走進巢狀集合；只讀點數，不複製任何路徑）。
PingExtrusionCount  ping_count_extrusions(const ExtrusionEntityCollection &collection);
// 由計數算預估移動筆數與需要的記憶體（純算，方便測試與校準）。
uint64_t            ping_estimated_moves(const PingExtrusionCount &total, uint64_t wipe_tower_moves);
uint64_t            ping_estimated_bytes(uint64_t estimated_moves);
// Windows：可用承諾量與 Job 上限取小的；其他平台回 known == false。
PingAvailableMemory ping_query_available_memory();
// 走訪整個 Print 的切片結果。要在各物件的切片步驟、換料塔、裙邊／brim 都做完之後呼叫。
PingMemoryEstimate  ping_estimate_gcode_memory(const Print &print);

// 預估記憶體不夠。繼承 SlicingError＝「非致命」：不會把列印板標成錯誤，使用者可以直接再切。
class PingMemoryShortageError : public SlicingError
{
public:
    explicit PingMemoryShortageError(const PingMemoryEstimate &estimate);
    const PingMemoryEstimate& estimate() const { return m_estimate; }

private:
    PingMemoryEstimate m_estimate;
};

} // namespace Slic3r
