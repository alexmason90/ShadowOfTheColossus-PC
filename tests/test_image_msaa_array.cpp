// SPDX-License-Identifier: GPL-2.0-or-later
// Offline translation and SPIR-V structure checks for 2D multisample image arrays.
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "common/elf_info.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/profile.h"
#include "video_core/amdgpu/resource.h"

// The recompiler asks for the game serial, as gpu/shim/bbgpu.cpp fills it.
namespace Core {
class Emulator {
public:
    static void SetSerial() {
        auto& info = Common::ElfInfo::Instance();
        info.initialized = true;
        info.game_serial = "CUSA08809";
    }
};
} // namespace Core

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "check failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__);      \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

namespace {

using namespace Shader;

constexpr u32 OpCapability = 17;
constexpr u32 OpTypeVector = 23;
constexpr u32 OpTypeImage = 25;
constexpr u32 OpImageFetch = 95;
constexpr u32 OpImageRead = 98;
constexpr u32 OpImageWrite = 99;
constexpr u32 OpImageQuerySizeLod = 103;
constexpr u32 OpImageQuerySize = 104;
constexpr u32 OpImageQuerySamples = 107;
constexpr u32 ImageOperandSample = 0x40;
constexpr u32 CapabilityStorageImageMultisample = 27;
constexpr u32 CapabilityImageMSArray = 48;

bool WriteSpirv(const std::filesystem::path& path, const std::vector<u32>& code) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(code.data()),
              static_cast<std::streamsize>(code.size() * sizeof(u32)));
    return bool(out);
}

std::vector<u32> ImageWords(AmdGpu::Image image) {
    image.base_address = 1;
    image.type = u64(AmdGpu::ImageType::Color2DMsaaArray);
    image.data_format = u64(AmdGpu::DataFormat::Format8_8_8_8);
    image.num_format = u64(AmdGpu::NumberFormat::Unorm);
    image.width = 127;
    image.height = 63;
    image.depth = 5;
    image.last_array = 5;
    image.last_level = 2; // 4 samples; MSAA images have one mip level.
    const auto words = std::bit_cast<std::array<u32, 8>>(image);
    return {words.begin(), words.end()};
}

IR::Value MakeImageHandle(IR::IREmitter& ir) {
    const auto words = ImageWords(AmdGpu::Image::Null(false));
    const IR::Value low = ir.CompositeConstruct(ir.Imm32(words[0]), ir.Imm32(words[1]),
                                                ir.Imm32(words[2]), ir.Imm32(words[3]));
    const IR::Value high = ir.CompositeConstruct(ir.Imm32(words[4]), ir.Imm32(words[5]),
                                                 ir.Imm32(words[6]), ir.Imm32(words[7]));
    return ir.ImageHandle(low, high);
}

IR::Value MakeSampler(IR::IREmitter& ir) {
    const auto words = std::bit_cast<std::array<u32, 4>>(AmdGpu::Sampler{});
    return ir.CompositeConstruct(ir.Imm32(words[0]), ir.Imm32(words[1]), ir.Imm32(words[2]),
                                  ir.Imm32(words[3]));
}

std::vector<u32> Translate(IR::Program& program, RuntimeInfo& runtime, const Profile& profile) {
    auto& info = program.info;
    const auto resources = Optimization::ResourceDiscoverPass(program, profile);
    Optimization::FlattenExtendedUserdataPass(program);
    Optimization::ResourcePatchingPass(info, resources, profile);
    program.post_order_blocks = program.blocks;
    Optimization::DeadCodeEliminationPass(program);
    program.post_order_blocks = program.blocks;
    Optimization::CollectShaderInfoPass(program, profile);
    Backend::Bindings bindings{};
    return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
}

struct SpirvFacts {
    bool type_image_ms_array_sampled{};
    bool type_image_ms_array_storage{};
    bool image_fetch_with_sample{};
    bool image_read_with_sample{};
    bool image_write_with_sample{};
    bool query_size{};
    bool query_size_lod{};
    bool query_samples{};
    bool storage_image_multisample_capability{};
    bool image_ms_array_capability{};
    bool query_size_returns_three_components{};
};

SpirvFacts InspectSpirv(std::span<const u32> code) {
    SpirvFacts facts{};
    std::vector<u32> vector_sizes(code.size(), 0);
    for (size_t cursor = 5; cursor < code.size();) {
        const u32 word_count = code[cursor] >> 16;
        const u32 opcode = code[cursor] & 0xFFFF;
        if (word_count == 0 || cursor + word_count > code.size()) {
            break;
        }
        if (opcode == OpTypeVector && word_count >= 4) {
            vector_sizes[code[cursor + 1]] = code[cursor + 3];
        }
        cursor += word_count;
    }

    for (size_t cursor = 5; cursor < code.size();) {
        const u32 word_count = code[cursor] >> 16;
        const u32 opcode = code[cursor] & 0xFFFF;
        if (word_count == 0 || cursor + word_count > code.size()) {
            break;
        }
        const u32* inst = code.data() + cursor;
        if (opcode == OpCapability && word_count >= 2) {
            facts.storage_image_multisample_capability |=
                inst[1] == CapabilityStorageImageMultisample;
            facts.image_ms_array_capability |= inst[1] == CapabilityImageMSArray;
        } else if (opcode == OpTypeImage && word_count >= 9) {
            const bool is_2d_ms_array = inst[3] == 1 && inst[5] == 1 && inst[6] == 1;
            facts.type_image_ms_array_sampled |= is_2d_ms_array && inst[7] == 1;
            facts.type_image_ms_array_storage |= is_2d_ms_array && inst[7] == 2;
        } else if (opcode == OpImageFetch && word_count >= 7) {
            facts.image_fetch_with_sample |=
                (inst[5] & ImageOperandSample) != 0 && word_count >= 7;
        } else if (opcode == OpImageRead && word_count >= 7) {
            facts.image_read_with_sample |=
                (inst[5] & ImageOperandSample) != 0 && word_count >= 7;
        } else if (opcode == OpImageWrite && word_count >= 6) {
            facts.image_write_with_sample |=
                (inst[4] & ImageOperandSample) != 0 && word_count >= 6;
        } else if (opcode == OpImageQuerySize && word_count >= 4) {
            facts.query_size = true;
            facts.query_size_returns_three_components |= vector_sizes[inst[1]] == 3;
        } else if (opcode == OpImageQuerySizeLod) {
            facts.query_size_lod = true;
        } else if (opcode == OpImageQuerySamples) {
            facts.query_samples = true;
        }
        cursor += word_count;
    }
    return facts;
}

bool PrepareProgram(IR::Program& program, IR::Block& block) {
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    program.syntax_list.push_back(
        {.data = {.block = &block}, .type = IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
    return true;
}

bool TranslateSampledArray(const std::filesystem::path& output, const Profile& profile) {
    Info info{};
    info.hw_stage = HwStage::Fragment;
    info.sw_stage = SwStage::Fragment;
    info.pgm_hash = 0x4D534141;
    RuntimeInfo runtime{};
    runtime.Initialize(info.hw_stage, info.sw_stage);
    Common::ObjectPool<IR::Inst> pool;
    IR::Block block(pool);
    IR::IREmitter ir(block);
    const IR::Value handle = MakeImageHandle(ir);
    const IR::Value sampler = MakeSampler(ir);
    const IR::Value address1 = ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(0.0f),
                                                     ir.Imm32(0.0f), ir.Imm32(0.0f));
    const IR::Value address2 = ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(0.25f),
                                                     ir.Imm32(0.5f), ir.Imm32(2.0f));
    const IR::Value address3 = ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(0.0f),
                                                     ir.Imm32(0.0f), ir.Imm32(0.0f));
    IR::TextureInstInfo texture_info{};
    texture_info.is_array.Assign(true);
    texture_info.has_offset.Assign(true);
    texture_info.has_derivatives.Assign(true);
    texture_info.pc.Assign(0x2345);
    const IR::Value texel = ir.ImageSampleRaw(handle, sampler, address1, address2, address3,
                                              ir.Imm32(0.0f), texture_info);
    const auto target = IR::Attribute::RenderTarget0;
    for (u32 component = 0; component < 4; ++component) {
        ir.SetAttribute(target, IR::F32{ir.CompositeExtract(texel, component)}, component);
        info.stores.Set(target, component);
    }
    ir.Epilogue();
    IR::Program program(info);
    CHECK(PrepareProgram(program, block));

    const auto resources = Optimization::ResourceDiscoverPass(program, profile);
    Optimization::FlattenExtendedUserdataPass(program);
    Optimization::ResourcePatchingPass(info, resources, profile);

    bool saw_ms_array_read = false;
    bool saw_query_dimension = false;
    for (const IR::Inst& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::ImageQueryDimensions) {
            saw_query_dimension = true;
        }
        if (inst.GetOpcode() != IR::Opcode::ImageRead) {
            continue;
        }
        const IR::Inst* coords = inst.Arg(1).TryInst();
        CHECK(coords && coords->GetOpcode() == IR::Opcode::CompositeConstructU32x3);
        const IR::Inst* layer = coords->Arg(2).TryInst();
        CHECK(layer && layer->GetOpcode() == IR::Opcode::ConvertU32F32);
        CHECK(layer->Arg(0).IsImmediate()); // array layer is integer-converted directly, not scaled.
        saw_ms_array_read = true;
    }
    CHECK(saw_ms_array_read);
    CHECK(saw_query_dimension);

    program.post_order_blocks = program.blocks;
    Optimization::DeadCodeEliminationPass(program);
    program.post_order_blocks = program.blocks;
    Optimization::CollectShaderInfoPass(program, profile);
    Backend::Bindings bindings{};
    const auto spirv = Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    const SpirvFacts facts = InspectSpirv(spirv);
    CHECK(facts.type_image_ms_array_sampled);
    CHECK(facts.image_fetch_with_sample);
    CHECK(facts.query_size);
    CHECK(facts.query_size_returns_three_components);
    CHECK(!facts.query_size_lod);
    CHECK(facts.query_samples);
    CHECK(WriteSpirv(output, spirv));
    return true;
}

bool TranslateStorageArray(const std::filesystem::path& output, const Profile& profile) {
    Info info{};
    info.hw_stage = HwStage::Fragment;
    info.sw_stage = SwStage::Fragment;
    info.pgm_hash = 0x4D534142;
    RuntimeInfo runtime{};
    runtime.Initialize(info.hw_stage, info.sw_stage);
    Common::ObjectPool<IR::Inst> pool;
    IR::Block block(pool);
    IR::IREmitter ir(block);
    const IR::Value handle = MakeImageHandle(ir);
    const IR::Value coords = ir.CompositeConstruct(ir.Imm32(12U), ir.Imm32(20U),
                                                   ir.Imm32(3U), ir.Imm32(2U));
    const IR::Value color = ir.CompositeConstruct(ir.Imm32(1.0f), ir.Imm32(0.5f),
                                                   ir.Imm32(0.25f), ir.Imm32(1.0f));
    IR::TextureInstInfo texture_info{};
    texture_info.is_array.Assign(true);
    texture_info.pc.Assign(0x3456);
    const IR::Value texel = ir.ImageRead(handle, coords, {}, {}, texture_info);
    const auto target = IR::Attribute::RenderTarget0;
    for (u32 component = 0; component < 4; ++component) {
        ir.SetAttribute(target, IR::F32{ir.CompositeExtract(texel, component)}, component);
        info.stores.Set(target, component);
    }
    ir.ImageWrite(handle, coords, {}, {}, color, texture_info);
    ir.Epilogue();
    IR::Program program(info);
    CHECK(PrepareProgram(program, block));
    const auto spirv = Translate(program, runtime, profile);
    const SpirvFacts facts = InspectSpirv(spirv);
    CHECK(facts.type_image_ms_array_storage);
    CHECK(facts.image_read_with_sample);
    CHECK(facts.image_write_with_sample);
    CHECK(facts.query_samples);
    CHECK(facts.storage_image_multisample_capability);
    CHECK(facts.image_ms_array_capability);
    CHECK(WriteSpirv(output, spirv));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(dir);
    Core::Emulator::SetSerial();
    Profile profile{};
    profile.supported_spirv = 0x00010600;
    profile.support_int64 = true;
    if (!TranslateSampledArray(dir / "sampled-msaa-array.spv", profile) ||
        !TranslateStorageArray(dir / "storage-msaa-array.spv", profile)) {
        return 1;
    }
    return 0;
}
