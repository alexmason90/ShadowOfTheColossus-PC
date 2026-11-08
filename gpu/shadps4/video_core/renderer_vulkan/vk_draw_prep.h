// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "common/types.h"
#include "video_core/amdgpu/resource.h"

namespace Shader {
struct Info;
}

namespace Vulkan {

class Pipeline;

enum class PreparedDrawMode : u8 {
    None,
    Eager,
    Lazy,
};

/// CPU-only descriptor snapshots prepared by Liverpool::CaptureBackOp. These are guest AMD
/// descriptors, not Vulkan resources; the BackOp owns their lifetime until its draw is consumed.
struct PreparedDrawResources {
    struct Stage {
        const Shader::Info* info{};
        size_t buffer_count{};
        // One entry per shader buffer binding. Special buffers contain a zero descriptor because
        // BindBuffers handles them without reading a guest descriptor.
        std::vector<AmdGpu::Buffer> buffers;
    };

    static constexpr u32 MaxStages = 5;
    const Pipeline* pipeline{};
    std::array<Stage, MaxStages> stages{};
    u32 num_stages{};
    u32 guest_buffer_count{};
    PreparedDrawMode mode{PreparedDrawMode::None};
    bool materialized{};
    bool valid{};
};

/// Fail closed if the snapshot was made for another pipeline, another stage order, or a changed
/// resource layout. A null return tells the renderer to use its ordinary descriptor decode path.
template <typename Stages>
inline PreparedDrawResources* SelectPreparedDraw(
    bool enabled, PreparedDrawResources* prepared, const Pipeline* pipeline,
    const Stages& expected) noexcept {
    if (!enabled || !prepared || !prepared->valid || !pipeline || prepared->pipeline != pipeline ||
        prepared->num_stages > PreparedDrawResources::MaxStages ||
        (prepared->mode != PreparedDrawMode::Eager && prepared->mode != PreparedDrawMode::Lazy)) {
        return nullptr;
    }
    size_t i = 0;
    for (const auto* stage : expected) {
        if (!stage) {
            continue;
        }
        if (i >= prepared->num_stages || prepared->stages[i].info != stage ||
            prepared->stages[i].buffer_count != stage->buffers.size()) {
            return nullptr;
        }
        const auto& snapshot = prepared->stages[i].buffers;
        if (prepared->mode == PreparedDrawMode::Eager || prepared->materialized) {
            if (snapshot.size() != stage->buffers.size()) {
                return nullptr;
            }
        } else if (!snapshot.empty()) {
            return nullptr;
        }
        ++i;
    }
    return i == prepared->num_stages ? prepared : nullptr;
}

/// Decode a lazy candidate only after the draw thread has missed FastBind. The caller supplies
/// the ordinary descriptor decoder; values are written into the BackOp-owned vectors.
template <typename Stages, typename Decode>
inline bool MaterializeLazyPreparedDraw(PreparedDrawResources& prepared, const Stages& expected,
                                        Decode&& decode) {
    if (!prepared.valid || prepared.mode != PreparedDrawMode::Lazy || prepared.materialized ||
        prepared.num_stages > PreparedDrawResources::MaxStages) {
        return false;
    }
    size_t i = 0;
    for (const auto* stage : expected) {
        if (!stage) {
            continue;
        }
        if (i >= prepared.num_stages || prepared.stages[i].info != stage ||
            prepared.stages[i].buffer_count != stage->buffers.size() ||
            !prepared.stages[i].buffers.empty()) {
            for (auto& snapshot : prepared.stages) {
                snapshot.buffers.clear();
            }
            return false;
        }
        auto& snapshot = prepared.stages[i];
        snapshot.buffers.clear();
        snapshot.buffers.reserve(stage->buffers.size());
        for (const auto& desc : stage->buffers) {
            snapshot.buffers.emplace_back(decode(*stage, desc));
        }
        ++i;
    }
    if (i != prepared.num_stages) {
        for (auto& snapshot : prepared.stages) {
            snapshot.buffers.clear();
        }
        return false;
    }
    prepared.materialized = true;
    return true;
}

} // namespace Vulkan
