#pragma once

// PING delta 機「越往上，噴頭搆得到的圈越小」（照片磚 AIP 第二班，開發中清單 #22；規格 R9-11 九題 Q3、第二輪 Q13 甲）。
// 一張機型幾何表＋「某高度的可達半徑」算式，兩處共用這一份：
//   ① 循環洗料塔擺位（PingCycleTower.cpp：右邊的塔在磚頂端搆不到就改擺後面）
//   ② 照片磚工作室的各機尺寸上限（WebViewDialog.cpp 把表送給頁面，頁面不另抄一份）。
// 幾何＝Klipper 母版 printer.cfg（01各機型最新版本；FD800 Pro 取 03整理後/V1）：
//   position_endstop 取三軸最低、arm_length、delta_radius。**推算、沒上機量**，各台校正值會差幾 mm（上機實量＝開發中清單 #28）。
// 算式＝Klipper delta.py：Z ≤ limit_z 整個盤面都搆得到；超過之後可達半徑＝R − √(arm² − (arm − 超出量)²)。

#include <string>
#include <vector>

namespace Slic3r {
namespace PingDeltaReach {

struct Geometry
{
    const char* family;            // 機型（printer_model 去掉「 同進照片磚」那一段）："FD300"、"FD300 Pro"…
    double      bed_diameter;      // 盤面直徑 mm（同機型檔 printable_area）
    double      position_endstop;  // mm
    double      arm_length;        // mm
    double      delta_radius;      // mm
};

const std::vector<Geometry>& table();

// printer_model（例「FD300 同進照片磚」「FD300 Pro」）→ 該機型的幾何；認不得＝nullptr。
// 先比長的名字（「FD300 Pro」要在「FD300」之前），名字後面只能接空白或結尾。
const Geometry* find(const std::string& printer_model);

// 整個盤面都搆得到的最高 Z（mm）
double limit_z(const Geometry& g);

// 高度 z（mm）時，從盤面中心算的可達半徑（mm）；z ≤ limit_z 時＝盤面半徑。
double reach_at(const Geometry& g, double z);

} // namespace PingDeltaReach
} // namespace Slic3r
