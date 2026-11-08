// SPDX-License-Identifier: GPL-2.0-or-later
// Offline checks for prepared-draw gating, state validation, fallback and value ownership.
#include <array>
#include <cassert>
#include <utility>

#include "video_core/renderer_vulkan/vk_draw_prep.h"

namespace Shader {
struct Info {
    std::vector<AmdGpu::Buffer> buffers;
};
}

namespace Vulkan {
class Pipeline {};
}

int main() {
    using Vulkan::PreparedDrawResources;

    Vulkan::Pipeline pipeline, other_pipeline;
    Shader::Info vertex, fragment, other_stage;
    PreparedDrawResources snapshot{};
    snapshot.pipeline = &pipeline;
    snapshot.num_stages = 2;
    snapshot.guest_buffer_count = 3;
    snapshot.mode = Vulkan::PreparedDrawMode::Eager;
    snapshot.materialized = true;
    snapshot.valid = true;
    snapshot.stages[0].info = &vertex;
    snapshot.stages[0].buffer_count = 2;
    snapshot.stages[0].buffers.resize(2);
    snapshot.stages[1].info = &fragment;
    snapshot.stages[1].buffer_count = 1;
    snapshot.stages[1].buffers.resize(1);
    vertex.buffers.resize(2);
    fragment.buffers.resize(1);
    const std::array<const Shader::Info*, 3> expected{&vertex, nullptr, &fragment};

    // Feature-off selection uses the ordinary decoder; feature-on requires an exact state match.
    assert(!Vulkan::SelectPreparedDraw(false, &snapshot, &pipeline, expected));
    assert(Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, expected) == &snapshot);
    assert(!Vulkan::SelectPreparedDraw(true, nullptr, &pipeline, expected));
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &other_pipeline, expected));

    auto mismatch = expected;
    mismatch[0] = &other_stage;
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, mismatch));
    mismatch = expected;
    fragment.buffers.emplace_back();
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, mismatch));
    fragment.buffers.pop_back();
    mismatch = {&fragment, nullptr, &vertex};
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, mismatch));
    snapshot.valid = false;
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, expected));
    snapshot.valid = true;

    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, nullptr, expected));
    snapshot.num_stages = 1; // More active pipeline stages than captured.
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, expected));
    snapshot.num_stages = 3; // Fewer active pipeline stages than captured.
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, expected));
    snapshot.num_stages = PreparedDrawResources::MaxStages + 1;
    assert(!Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, expected));
    snapshot.num_stages = 2;
    assert(Vulkan::SelectPreparedDraw(true, &snapshot, &pipeline, expected) == &snapshot);

    // Lazy candidates validate the same layout before materialization, then own copied values.
    vertex.buffers[0].base_address = 0x1000;
    vertex.buffers[1].base_address = 0x2000;
    fragment.buffers[0].base_address = 0x3000;
    PreparedDrawResources lazy{};
    lazy.pipeline = &pipeline;
    lazy.num_stages = 2;
    lazy.guest_buffer_count = 3;
    lazy.mode = Vulkan::PreparedDrawMode::Lazy;
    lazy.valid = true;
    lazy.stages[0].info = &vertex;
    lazy.stages[0].buffer_count = vertex.buffers.size();
    lazy.stages[1].info = &fragment;
    lazy.stages[1].buffer_count = fragment.buffers.size();
    assert(Vulkan::SelectPreparedDraw(true, &lazy, &pipeline, expected) == &lazy);
    assert(Vulkan::MaterializeLazyPreparedDraw(
        lazy, expected, [](const Shader::Info&, const AmdGpu::Buffer& desc) { return desc; }));
    assert(lazy.materialized);
    assert(lazy.stages[0].buffers[0].base_address == 0x1000);
    assert(lazy.stages[0].buffers[1].base_address == 0x2000);
    assert(lazy.stages[1].buffers[0].base_address == 0x3000);
    assert(Vulkan::SelectPreparedDraw(true, &lazy, &pipeline, expected) == &lazy);
    assert(!Vulkan::MaterializeLazyPreparedDraw(
        lazy, expected, [](const Shader::Info&, const AmdGpu::Buffer& desc) { return desc; }));

    PreparedDrawResources bad_lazy{};
    bad_lazy.pipeline = &pipeline;
    bad_lazy.num_stages = 2;
    bad_lazy.mode = Vulkan::PreparedDrawMode::Lazy;
    bad_lazy.valid = true;
    bad_lazy.stages[0].info = &vertex;
    bad_lazy.stages[0].buffer_count = vertex.buffers.size();
    bad_lazy.stages[1].info = &fragment;
    bad_lazy.stages[1].buffer_count = fragment.buffers.size() + 1;
    assert(!Vulkan::SelectPreparedDraw(true, &bad_lazy, &pipeline, expected));
    assert(!Vulkan::MaterializeLazyPreparedDraw(
        bad_lazy, expected, [](const Shader::Info&, const AmdGpu::Buffer& desc) { return desc; }));
    assert(bad_lazy.stages[0].buffers.empty());

    // Fixed-size snapshot bounds apply to active stages, not null pipeline slots.
    PreparedDrawResources full{};
    full.pipeline = &pipeline;
    full.num_stages = PreparedDrawResources::MaxStages;
    full.mode = Vulkan::PreparedDrawMode::Eager;
    full.materialized = true;
    full.valid = true;
    std::array<const Shader::Info*, PreparedDrawResources::MaxStages + 1> stages{};
    for (size_t i = 0; i < PreparedDrawResources::MaxStages; ++i) {
        full.stages[i].info = &other_stage;
        full.stages[i].buffer_count = other_stage.buffers.size();
        stages[i] = &other_stage; // Zero-buffer stages are valid.
    }
    assert(Vulkan::SelectPreparedDraw(true, &full, &pipeline, stages) == &full);
    stages.back() = &other_stage;
    assert(!Vulkan::SelectPreparedDraw(true, &full, &pipeline, stages));

    // BackOp snapshots own descriptor values rather than views into mutable guest memory.
    AmdGpu::Buffer guest{};
    guest.base_address = 0x12345000;
    guest.stride = 16;
    guest.num_records = 64;
    snapshot.stages[0].buffers[0] = guest;
    guest.base_address = 0xDEADBEEF;
    auto moved = std::move(snapshot);
    assert(moved.stages[0].buffers[0].base_address == 0x12345000);
    moved.stages[0].buffers.clear();
    moved.stages[0].buffers.push_back(guest);
    assert(moved.stages[0].buffers.size() == 1);
    assert(moved.stages[0].buffers[0].base_address == 0xDEADBEEF);
}
