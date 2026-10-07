#include "PingDeltaReach.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Slic3r {
namespace PingDeltaReach {

const std::vector<Geometry>& table()
{
    // 值與原型 v4（D:/_sa/aip/proto4_template.html MODELS.geo）逐一相同；長名字排前面＝find() 先比到。
    static const std::vector<Geometry> t = {
        {"FD300 Pro", 300., 341.157, 336.76, 175.5 },
        {"FD450 Pro", 450., 714.57,  520.,   266.24},
        {"FD600 Pro", 600., 685.398, 634.1,  318.31},
        {"FD800 Pro", 800., 728.66,  986.5,  534.86},
        {"FD300",     300., 337.265, 334.54, 155.8 },
        {"FF600",     600., 647.91,  634.9,  318.99},
        {"FF800",     800., 730.262, 988.,   535.3 },
    };
    return t;
}

const Geometry* find(const std::string& printer_model)
{
    for (const Geometry& g : table()) {
        const size_t n = std::strlen(g.family);
        if (printer_model.compare(0, n, g.family) == 0 && (printer_model.size() == n || printer_model[n] == ' '))
            return &g;
    }
    return nullptr;
}

double limit_z(const Geometry& g)
{
    return g.position_endstop + std::sqrt(g.arm_length * g.arm_length - g.delta_radius * g.delta_radius) - g.arm_length;
}

double reach_at(const Geometry& g, double z)
{
    const double bed_r = g.bed_diameter / 2.;
    const double lz    = limit_z(g);
    if (z <= lz)
        return bed_r;
    const double a = z - lz;
    if (a >= g.arm_length)
        return 0.;
    const double d = g.arm_length - a;
    return std::max(0., std::min(bed_r, g.delta_radius - std::sqrt(g.arm_length * g.arm_length - d * d)));
}

} // namespace PingDeltaReach
} // namespace Slic3r
