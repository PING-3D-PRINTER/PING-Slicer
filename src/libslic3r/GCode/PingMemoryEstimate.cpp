#include "PingMemoryEstimate.hpp"

#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace Slic3r {

// 每筆 G-code 移動在「產 G-code＋載入預覽」整趟佔用的記憶體（B）。來源＝網格填充完整跑完 6 次的累積峰值除以 G1 筆數，取最大值進位。
// 2026-10-08 再量兩次（從估算那一刻到整趟峰值）：網格大件每筆 662 B、gyroid 中件 551 B ⇒ 690 對 gyroid 偏高估，不調。
static constexpr uint64_t PING_BYTES_PER_MOVE = 690;
// 每個列印單位（loop／multi-path／path）除了擠出線段之外多帶的移動筆數：空跑、回抽、回補。
// 2026-10-08 實跑四次對 G-code 的 G1 行數（小件兩次、網格大件、gyroid 中件）：預估／實際＝0.95、0.95、1.09、1.01。
// 列印單位數與 path 數幾乎成比例，下面兩個常數分不開，合起來是「每個列印單位多 3 筆」；各件實測要 1.7～3.5 筆，
// 會靠近記憶體門檻的大件落在 1.7～1.8 ⇒ 現值對大件偏高估（方向保守），不調。
static constexpr double PING_EXTRA_MOVES_PER_ENTITY = 2.0;
// 每段 path 多帶的移動筆數：速度行（G1 F）。校準見上一段。
static constexpr double PING_EXTRA_MOVES_PER_PATH = 1.0;
// 預估需要量超過可用量的這個百分比就算不夠。
static constexpr uint64_t PING_SHORTAGE_PERCENT = 85;

namespace {

void count_path(PingExtrusionCount &count, const ExtrusionPath &path)
{
    ++count.paths;
    const size_t points = path.polyline.points.size();
    count.segments += points > 0 ? points - 1 : 0;
}

uint64_t count_g1_lines(const std::string &gcode)
{
    uint64_t count = 0;
    for (size_t begin = 0; begin < gcode.size();) {
        if (gcode.compare(begin, 2, "G1") == 0) {
            const size_t next = begin + 2;
            // G10／G11 等指令不是移動；同時容許 G1X 這種沒有空白的參數。
            if (next == gcode.size() || (gcode[next] != '.' && (gcode[next] < '0' || gcode[next] > '9')))
                ++count;
        }
        const size_t end = gcode.find('\n', begin);
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return count;
}

std::string shortage_message(const PingMemoryEstimate &estimate)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(1)
        << "Estimated memory shortage: about " << double(estimate.needed_bytes) / double(uint64_t{1} << 30)
        << " GB needed to generate the G-code and the preview, about "
        << double(estimate.available.available_bytes) / double(uint64_t{1} << 30) << " GB available.";
    return out.str();
}

} // namespace

PingExtrusionCount ping_count_extrusions(const ExtrusionEntityCollection &collection)
{
    PingExtrusionCount count;
    for (const ExtrusionEntity *entity : collection.entities) {
        if (entity == nullptr)
            continue;
        if (const auto *nested = dynamic_cast<const ExtrusionEntityCollection *>(entity)) {
            count += ping_count_extrusions(*nested);
            continue;
        }

        ++count.entities;
        if (const auto *path = dynamic_cast<const ExtrusionPath *>(entity)) {
            // Sloped 與 Oriented 都沿用基底的 polyline，不必複製或展開路徑。
            count_path(count, *path);
        } else if (const auto *sloped = dynamic_cast<const ExtrusionLoopSloped *>(entity)) {
            // get_all_paths() 會配置 vector；直接走三組路徑也避免漏算斜坡首尾。
            for (const ExtrusionPath &path : sloped->starts)
                count_path(count, path);
            for (const ExtrusionPath &path : sloped->paths)
                count_path(count, path);
            for (const ExtrusionPath &path : sloped->ends)
                count_path(count, path);
        } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
            for (const ExtrusionPath &path : loop->paths)
                count_path(count, path);
        } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(entity)) {
            for (const ExtrusionPath &path : multi->paths)
                count_path(count, path);
        }
        // 未知子類別保留一個列印單位；記憶體預檢不應因擴充型別而中斷切片。
    }
    return count;
}

uint64_t ping_estimated_moves(const PingExtrusionCount &total, uint64_t wipe_tower_moves)
{
    return total.segments + uint64_t(std::llround(total.entities * PING_EXTRA_MOVES_PER_ENTITY))
        + uint64_t(std::llround(total.paths * PING_EXTRA_MOVES_PER_PATH)) + wipe_tower_moves;
}

uint64_t ping_estimated_bytes(uint64_t estimated_moves)
{
    return estimated_moves * PING_BYTES_PER_MOVE;
}

PingAvailableMemory ping_query_available_memory()
{
    PingAvailableMemory available;
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return available;

    available.known = true;
    available.commit_available_bytes = status.ullAvailPageFile;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
        available.process_private_bytes = counters.PrivateUsage;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job{};
    if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &job, sizeof(job), nullptr)) {
        const DWORD flags = job.BasicLimitInformation.LimitFlags;
        if (flags & JOB_OBJECT_LIMIT_PROCESS_MEMORY)
            available.job_limit_bytes = job.ProcessMemoryLimit;
        if (flags & JOB_OBJECT_LIMIT_JOB_MEMORY) {
            available.job_limit_bytes = (flags & JOB_OBJECT_LIMIT_PROCESS_MEMORY)
                ? std::min(available.job_limit_bytes, uint64_t(job.JobMemoryLimit)) : uint64_t(job.JobMemoryLimit);
        }
    }

    available.available_bytes = available.commit_available_bytes;
    if (available.job_limit_bytes != 0) {
        const uint64_t remaining = available.job_limit_bytes > available.process_private_bytes
            ? available.job_limit_bytes - available.process_private_bytes : 0;
        available.available_bytes = std::min(available.available_bytes, remaining);
    }
#endif
    return available;
}

PingMemoryEstimate ping_estimate_gcode_memory(const Print &print)
{
    PingMemoryEstimate estimate;
    uint64_t max_layers = 0;
    const auto skirt_layers = [&print](uint64_t layers) -> uint64_t {
        if (!print.has_skirt())
            return 0;
        return print.has_infinite_skirt() ? layers : std::min(layers, uint64_t(print.config().skirt_height.value));
    };

    for (const PrintObject *object : print.objects()) {
        const uint64_t copies = object->instances().size();
        PingExtrusionCount objects;
        for (const Layer *layer : object->layers()) {
            for (const LayerRegion *region : layer->regions()) {
                objects += ping_count_extrusions(region->perimeters);
                // Fill.cpp 已將 thin_fills 複製到 fills，另算一次會高估。
                objects += ping_count_extrusions(region->fills);
            }
        }
        estimate.objects += objects * copies;

        PingExtrusionCount supports;
        for (const SupportLayer *layer : object->support_layers())
            supports += ping_count_extrusions(layer->support_fills);
        estimate.supports += supports * copies;

        const uint64_t layers = object->layers().size();
        max_layers = std::max(max_layers, layers);
        if (print.config().skirt_type == stPerObject) {
            // 每份都有自己的裙邊；不扣掉被 brim 抑制的層，保留偏高的估算。
            estimate.skirt_brim += ping_count_extrusions(object->object_skirt()) * skirt_layers(layers) * copies;
        }
    }
    if (print.config().skirt_type == stCombined)
        estimate.skirt_brim += ping_count_extrusions(print.m_skirt) * skirt_layers(max_layers);
    for (const auto &brim : print.m_brimMap)
        estimate.skirt_brim += ping_count_extrusions(brim.second);
    for (const auto &brim : print.m_supportBrimMap)
        estimate.skirt_brim += ping_count_extrusions(brim.second);

    const WipeTowerData &tower = print.m_wipe_tower_data;
    if (tower.priming)
        for (const WipeTower::ToolChangeResult &change : *tower.priming)
            estimate.wipe_tower_moves += count_g1_lines(change.gcode);
    for (const auto &layer : tower.tool_changes)
        for (const WipeTower::ToolChangeResult &change : layer)
            estimate.wipe_tower_moves += count_g1_lines(change.gcode);
    if (tower.final_purge)
        estimate.wipe_tower_moves += count_g1_lines(tower.final_purge->gcode);

    PingExtrusionCount total = estimate.objects;
    total += estimate.supports;
    total += estimate.skirt_brim;
    estimate.estimated_moves = ping_estimated_moves(total, estimate.wipe_tower_moves);
    estimate.needed_bytes = ping_estimated_bytes(estimate.estimated_moves);
    estimate.available = ping_query_available_memory();
    return estimate;
}

bool PingMemoryEstimate::short_of_memory() const
{
    // 商與餘數分開乘，等價於交叉相乘但不會讓 uint64_t 溢位。
    const uint64_t threshold = (available.available_bytes / 100) * PING_SHORTAGE_PERCENT
        + (available.available_bytes % 100) * PING_SHORTAGE_PERCENT / 100;
    return available.known && needed_bytes > threshold;
}

std::string PingMemoryEstimate::to_log_string() const
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "objects_entities=" << objects.entities << " objects_paths=" << objects.paths << " objects_segments=" << objects.segments
        << " supports_entities=" << supports.entities << " supports_paths=" << supports.paths << " supports_segments=" << supports.segments
        << " skirt_brim_entities=" << skirt_brim.entities << " skirt_brim_paths=" << skirt_brim.paths
        << " skirt_brim_segments=" << skirt_brim.segments << " wipe_tower_moves=" << wipe_tower_moves
        << " estimated_moves=" << estimated_moves << " needed_bytes=" << needed_bytes
        << " needed_gb=" << std::fixed << std::setprecision(2) << double(needed_bytes) / double(uint64_t{1} << 30)
        << " available_known=" << available.known << " available_bytes=" << available.available_bytes
        << " commit_available_bytes=" << available.commit_available_bytes << " job_limit_bytes=" << available.job_limit_bytes
        << " process_private_bytes=" << available.process_private_bytes << " short_of_memory=" << short_of_memory();
    return out.str();
}

PingMemoryShortageError::PingMemoryShortageError(const PingMemoryEstimate &estimate)
    : SlicingError(shortage_message(estimate)), m_estimate(estimate)
{
}

} // namespace Slic3r
