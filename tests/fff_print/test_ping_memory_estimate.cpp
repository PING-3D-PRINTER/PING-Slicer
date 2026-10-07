#include <catch2/catch_all.hpp>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/PingMemoryEstimate.hpp"

#include "test_data.hpp"

#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>

using namespace Slic3r;

namespace {

ExtrusionPath make_path(std::initializer_list<Point> points)
{
    ExtrusionPath path(erPerimeter, 0.08, 0.4f, 0.2f);
    path.polyline.points = Points(points);
    return path;
}

DynamicPrintConfig memory_test_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    // 固定會改變移動筆數的選項，讓倍率與 G1 比值只驗估算器。
    config.set_deserialize_strict({
        {"layer_height", 0.2},
        {"wall_generator", "classic"},
        {"sparse_infill_pattern", "grid"},
        {"sparse_infill_density", "15%"},
        {"skirt_loops", 0},
        {"brim_type", "no_brim"},
        {"enable_support", false},
        {"enable_arc_fitting", false}
    });
    return config;
}

} // namespace

TEST_CASE("Extrusion counts include nested collections and path variants", "[PingMemoryEstimate]")
{
    ExtrusionEntityCollection collection;
    collection.append(make_path({{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}));
    collection.append(ExtrusionLoop(ExtrusionPaths{
        make_path({{0, 0}, {10, 0}, {10, 10}}),
        make_path({{10, 10}, {0, 10}, {0, 0}})
    }));
    collection.append(ExtrusionMultiPath(ExtrusionPaths{
        make_path({{0, 0}, {10, 0}}),
        make_path({{10, 0}, {10, 10}, {20, 10}, {20, 20}})
    }));
    ExtrusionEntityCollection nested;
    nested.append(make_path({{0, 0}, {10, 0}, {20, 0}}));
    collection.append(nested);
    collection.append(make_path({{0, 0}}));
    collection.entities.push_back(nullptr);
    collection.append(ExtrusionEntityCollection{});

    // entities＝1＋1＋1＋1＋1＝5；paths＝1＋2＋2＋1＋1＝7；segments＝4＋(2＋2)＋(1＋3)＋2＋0＝14。
    const PingExtrusionCount count = ping_count_extrusions(collection);
    REQUIRE(count.entities == 5);
    REQUIRE(count.paths == 7);
    REQUIRE(count.segments == 14);

    SECTION("An empty path is still one entity and one path")
    {
        collection.append(make_path({}));
        const PingExtrusionCount with_empty = ping_count_extrusions(collection);
        REQUIRE(with_empty.entities == 6);
        REQUIRE(with_empty.paths == 8);
        REQUIRE(with_empty.segments == 14);
    }

    SECTION("Sloped and oriented paths preserve their concrete types")
    {
        ExtrusionEntityCollection variants;
        const ExtrusionPathSloped::Slope begin{0.5, 0.5};
        const ExtrusionPathSloped::Slope end{1.0, 1.0};
        // Sloped 沒覆寫 clone，直接交所有權才能真的測到衍生型別。
        variants.entities.push_back(new ExtrusionPathSloped(make_path({{0, 0}, {10, 0}, {20, 0}}), begin, end));
        auto *oriented = new ExtrusionPathOriented(erPerimeter, 0.08, 0.4f, 0.2f);
        variants.entities.push_back(oriented);
        oriented->polyline.points = {{0, 0}, {10, 0}, {20, 0}, {30, 0}};

        // 先建無斜坡的空 loop，再給定已知點數，避免把斜坡細分演算法混入計數測試。
        ExtrusionPaths original;
        auto *loop = new ExtrusionLoopSloped(original, 0.0, 0.0, 1.0, 1.0);
        variants.entities.push_back(loop);
        loop->starts.emplace_back(make_path({{0, 0}, {10, 0}, {20, 0}}), begin, end);
        loop->paths.push_back(make_path({{20, 0}, {30, 0}}));
        loop->ends.emplace_back(make_path({{30, 0}, {20, 0}, {10, 0}, {0, 0}}), begin, end);

        // entities＝3；paths＝1＋1＋(1＋1＋1)＝5；segments＝2＋3＋(2＋1＋3)＝11。
        const PingExtrusionCount variant_count = ping_count_extrusions(variants);
        REQUIRE(variant_count.entities == 3);
        REQUIRE(variant_count.paths == 5);
        REQUIRE(variant_count.segments == 11);
    }
}

TEST_CASE("Extrusion counts support addition and copy multiplication", "[PingMemoryEstimate]")
{
    PingExtrusionCount count{2, 3, 5};
    const PingExtrusionCount extra{7, 11, 13};
    REQUIRE(&(count += extra) == &count);
    REQUIRE(count.entities == 9);
    REQUIRE(count.paths == 14);
    REQUIRE(count.segments == 18);

    const PingExtrusionCount tripled = count * 3;
    REQUIRE(tripled.entities == 27);
    REQUIRE(tripled.paths == 42);
    REQUIRE(tripled.segments == 54);
    REQUIRE(count == PingExtrusionCount{9, 14, 18});
    REQUIRE(count * 0 == PingExtrusionCount{});
    REQUIRE(count * 1 == count);
}

TEST_CASE("Estimated moves preserve segments and add path overhead", "[PingMemoryEstimate]")
{
    REQUIRE(ping_estimated_moves({}, 0) == 0);
    REQUIRE(ping_estimated_moves({}, 37) == 37);
    const PingExtrusionCount total{3, 7, 11};
    const uint64_t baseline = ping_estimated_moves(total, 19);
    REQUIRE(ping_estimated_moves({3, 7, 28}, 19) - baseline == 17);
    REQUIRE(ping_estimated_moves({4, 7, 11}, 19) >= baseline);
    REQUIRE(ping_estimated_moves({3, 8, 11}, 19) >= baseline);
    REQUIRE(ping_estimated_moves(total, 42) - baseline == 23);
    REQUIRE(ping_estimated_moves({4, 7, 11}, 19) - baseline == ping_estimated_moves({1, 0, 0}, 0));
    REQUIRE(ping_estimated_moves({3, 8, 11}, 19) - baseline == ping_estimated_moves({0, 1, 0}, 0));
    REQUIRE(ping_estimated_bytes(0) == 0);
    REQUIRE(ping_estimated_bytes(1) > 0);
    REQUIRE(ping_estimated_bytes(23) == ping_estimated_bytes(1) * 23);
}

TEST_CASE("Memory shortage uses a strict known memory threshold", "[PingMemoryEstimate]")
{
    PingMemoryEstimate estimate;
    estimate.needed_bytes = std::numeric_limits<uint64_t>::max();
    REQUIRE_FALSE(estimate.short_of_memory());
    estimate.available.available_bytes = 100;
    REQUIRE_FALSE(estimate.short_of_memory());
    estimate.available.known = true;
    estimate.needed_bytes = 85;
    REQUIRE_FALSE(estimate.short_of_memory());
    estimate.needed_bytes = 86;
    REQUIRE(estimate.short_of_memory());
    estimate.available.available_bytes = 0;
    estimate.needed_bytes = 0;
    REQUIRE_FALSE(estimate.short_of_memory());
    estimate.needed_bytes = 1;
    REQUIRE(estimate.short_of_memory());
    // 交叉相乘若溢位，極大可用量會被誤判成不足。
    estimate.available.available_bytes = std::numeric_limits<uint64_t>::max();
    REQUIRE_FALSE(estimate.short_of_memory());
    estimate.needed_bytes = std::numeric_limits<uint64_t>::max();
    REQUIRE(estimate.short_of_memory());
}

TEST_CASE("Print object counts scale with instances of the same model object", "[PingMemoryEstimate]")
{
    Model model;
    Print print;
    const DynamicPrintConfig config = memory_test_config();
    Slic3r::Test::init_print({Slic3r::Test::TestMesh::cube_20x20x20}, print, model, config);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(print.objects().size() == 1);
    REQUIRE(print.objects().front()->instances().size() == 1);
    print.process();
    const PingMemoryEstimate single = ping_estimate_gcode_memory(print);
    REQUIRE(single.objects.segments > 0);

    ModelObject *object = model.objects.front();
    const ModelInstance *first = object->instances.front();
    const Vec3d offset = first->get_offset();
    // 複製相同變換且只移 XY；PrintApply 會合併為同一個 PrintObject，40 mm 間距不重疊。
    object->add_instance(*first)->set_offset(offset + Vec3d(40.0, 0.0, 0.0));
    object->add_instance(*first)->set_offset(offset + Vec3d(80.0, 0.0, 0.0));
    print.apply(model, config);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(print.objects().size() == 1);
    REQUIRE(print.objects().front()->instances().size() == 3);
    print.process();
    REQUIRE(print.objects().size() == 1);
    REQUIRE(print.objects().front()->instances().size() == 3);
    const PingMemoryEstimate triple = ping_estimate_gcode_memory(print);
    REQUIRE(triple.objects.entities == single.objects.entities * 3);
    REQUIRE(triple.objects.paths == single.objects.paths * 3);
    REQUIRE(triple.objects.segments == single.objects.segments * 3);
}

TEST_CASE("Print estimates distinguish object paths from supports", "[PingMemoryEstimate]")
{
    Print print;
    DynamicPrintConfig config = memory_test_config();
    SECTION("An unsupported cube has no support paths")
    {
        Slic3r::Test::init_and_process_print({Slic3r::Test::TestMesh::cube_20x20x20}, print, config);
        const PingMemoryEstimate estimate = ping_estimate_gcode_memory(print);
        REQUIRE(estimate.objects.segments > 0);
        REQUIRE(estimate.supports.entities == 0);
        REQUIRE(estimate.supports.paths == 0);
        REQUIRE(estimate.supports.segments == 0);
    }
    SECTION("Automatic supports for an overhang contribute segments")
    {
        config.set_deserialize_strict({
            {"enable_support", true},
            {"support_type", "normal(auto)"},
            {"support_threshold_angle", 45}
        });
        Slic3r::Test::init_and_process_print({Slic3r::Test::TestMesh::overhang}, print, config);
        REQUIRE(ping_estimate_gcode_memory(print).supports.segments > 0);
    }
}

TEST_CASE("Estimated moves stay within a broad range of generated G1 lines", "[PingMemoryEstimate]")
{
    Print print;
    Slic3r::Test::init_and_process_print({Slic3r::Test::TestMesh::cube_20x20x20}, print, memory_test_config());
    const PingMemoryEstimate estimate = ping_estimate_gcode_memory(print);
    std::istringstream gcode(Slic3r::Test::gcode(print));
    uint64_t actual_moves = 0;
    std::string line;
    while (std::getline(gcode, line)) {
        // 測試以獨立的逐行讀取驗算，G10／G11 不算 G1 移動。
        if (line.compare(0, 2, "G1") == 0 && (line.size() == 2 || line[2] == ' ' || line[2] == '\t' || line[2] == '\r'))
            ++actual_moves;
    }
    INFO("estimated_moves=" << estimate.estimated_moves << " actual_g1_moves=" << actual_moves);
    REQUIRE(actual_moves > 0);
    const double ratio = double(estimate.estimated_moves) / double(actual_moves);
    REQUIRE(ratio >= 0.5);
    REQUIRE(ratio <= 2.0);
    REQUIRE(estimate.needed_bytes == ping_estimated_bytes(estimate.estimated_moves));
}

TEST_CASE("Available memory follows the platform contract", "[PingMemoryEstimate]")
{
    const PingAvailableMemory available = ping_query_available_memory();
#ifdef _WIN32
    REQUIRE(available.known);
    REQUIRE(available.available_bytes > 0);
    REQUIRE(available.available_bytes <= available.commit_available_bytes);
#else
    REQUIRE_FALSE(available.known);
#endif
}

TEST_CASE("Memory shortage errors preserve estimates and are slicing errors", "[PingMemoryEstimate]")
{
    PingMemoryEstimate estimate;
    estimate.objects = {2, 3, 5};
    estimate.supports = {7, 11, 13};
    estimate.skirt_brim = {17, 19, 23};
    estimate.wipe_tower_moves = 29;
    estimate.estimated_moves = 31;
    estimate.needed_bytes = 37;
    estimate.available = {true, 41, 43, 47, 53};

    REQUIRE_THROWS_AS([&estimate] { throw PingMemoryShortageError(estimate); }(), SlicingError);
    const PingMemoryShortageError error(estimate);
    const PingMemoryEstimate &saved = error.estimate();
    REQUIRE(saved.objects == estimate.objects);
    REQUIRE(saved.supports == estimate.supports);
    REQUIRE(saved.skirt_brim == estimate.skirt_brim);
    REQUIRE(saved.wipe_tower_moves == estimate.wipe_tower_moves);
    REQUIRE(saved.estimated_moves == estimate.estimated_moves);
    REQUIRE(saved.needed_bytes == estimate.needed_bytes);
    REQUIRE(saved.available.known == estimate.available.known);
    REQUIRE(saved.available.available_bytes == estimate.available.available_bytes);
    REQUIRE(saved.available.commit_available_bytes == estimate.available.commit_available_bytes);
    REQUIRE(saved.available.job_limit_bytes == estimate.available.job_limit_bytes);
    REQUIRE(saved.available.process_private_bytes == estimate.available.process_private_bytes);
    REQUIRE_FALSE(std::string(error.what()).empty());
    estimate.needed_bytes = 0;
    REQUIRE(saved.needed_bytes == 37);
}

TEST_CASE("Memory estimate logs contain numeric fields on one line", "[PingMemoryEstimate]")
{
    PingMemoryEstimate estimate;
    estimate.objects = {1, 2, 3};
    estimate.supports = {4, 5, 6};
    estimate.skirt_brim = {7, 8, 9};
    estimate.wipe_tower_moves = 10;
    estimate.estimated_moves = 11;
    estimate.needed_bytes = uint64_t{3} << 29;
    estimate.available = {true, uint64_t{2} << 30, uint64_t{3} << 30, uint64_t{4} << 30, uint64_t{5} << 30};
    const std::string log = estimate.to_log_string();
    INFO(log);
    REQUIRE(log.find_first_of("\r\n") == std::string::npos);
    const std::string padded = " " + log + " ";
    for (const char *field : {
        "objects_entities=1", "objects_paths=2", "objects_segments=3",
        "supports_entities=4", "supports_paths=5", "supports_segments=6",
        "skirt_brim_entities=7", "skirt_brim_paths=8", "skirt_brim_segments=9",
        "wipe_tower_moves=10", "estimated_moves=11", "needed_bytes=1610612736", "needed_gb=1.50",
        "available_known=1", "available_bytes=2147483648", "commit_available_bytes=3221225472",
        "job_limit_bytes=4294967296", "process_private_bytes=5368709120", "short_of_memory=0"
    }) {
        INFO(field);
        REQUIRE(padded.find(" " + std::string(field) + " ") != std::string::npos);
    }
}
