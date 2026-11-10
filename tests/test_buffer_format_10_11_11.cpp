// SPDX-License-Identifier: GPL-2.0-or-later
// Offline PS4 legacy 10:11:11 numeric-format and SPIR-V regression test.
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spirv/unified1/spirv.hpp>
#include <spirv/unified1/GLSL.std.450.h>

#include "common/elf_info.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/reinterpret.h"
#include "video_core/amdgpu/pixel_format.h"

#define CHECK(cond)                                                                                \
    if (!(cond)) {                                                                                 \
        std::fprintf(stderr, "check failed: %s\n", #cond);                                        \
        return 1;                                                                                  \
    }

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

namespace {

constexpr std::array<u32, 3> Widths{11, 11, 10};

struct Components {
    float x;
    float y;
    float z;

    bool operator==(const Components&) const = default;
};

struct PackedComponents {
    u32 x;
    u32 y;
    u32 z;

    bool operator==(const PackedComponents&) const = default;
};

constexpr u32 Mask(u32 width) {
    return (1u << width) - 1;
}

constexpr u32 Field(u32 exponent, u32 mantissa, u32 mantissa_bits) {
    return (exponent << mantissa_bits) | mantissa;
}

constexpr u32 Pack(PackedComponents value) {
    return (value.x & Mask(Widths[0])) | ((value.y & Mask(Widths[1])) << 11) |
           ((value.z & Mask(Widths[2])) << 22);
}

constexpr PackedComponents Unpack(u32 word) {
    return {.x = word & Mask(Widths[0]), .y = (word >> 11) & Mask(Widths[1]),
            .z = (word >> 22) & Mask(Widths[2])};
}

u32 EncodeUfloat(float value, u32 mantissa_bits) {
    if (std::signbit(value) || value == 0.0f) {
        return 0;
    }
    if (std::isnan(value)) {
        return Field(31, 1, mantissa_bits);
    }
    if (std::isinf(value)) {
        return Field(31, 0, mantissa_bits);
    }

    int exponent{};
    const float significand = std::frexp(value, &exponent) * 2.0f;
    --exponent;
    int encoded_exponent = exponent + 15;
    if (encoded_exponent <= 0) {
        const auto rounded = static_cast<u32>(std::nearbyint(std::ldexp(value, 14 + mantissa_bits)));
        return rounded >= (1u << mantissa_bits) ? Field(1, 0, mantissa_bits) : rounded;
    }
    auto rounded_mantissa = static_cast<u32>(
        std::nearbyint((significand - 1.0f) * static_cast<float>(1u << mantissa_bits)));
    if (rounded_mantissa >= (1u << mantissa_bits)) {
        rounded_mantissa = 0;
        ++encoded_exponent;
    }
    return Field(static_cast<u32>(encoded_exponent), rounded_mantissa & Mask(mantissa_bits),
                 mantissa_bits);
}

float DecodeUfloat(u32 value, u32 mantissa_bits) {
    const u32 exponent = value >> mantissa_bits;
    const u32 mantissa = value & Mask(mantissa_bits);
    if (exponent == 0) {
        return std::ldexp(static_cast<float>(mantissa), -14 - static_cast<int>(mantissa_bits));
    }
    if (exponent == 31) {
        return mantissa == 0 ? INFINITY : NAN;
    }
    return std::ldexp(1.0f + static_cast<float>(mantissa) / static_cast<float>(1u << mantissa_bits),
                      static_cast<int>(exponent) - 15);
}

u32 PackUfloat(Components value) {
    return Pack(PackedComponents{EncodeUfloat(value.x, 6), EncodeUfloat(value.y, 6),
                                 EncodeUfloat(value.z, 5)});
}

Components UnpackUfloat(u32 word) {
    const auto bits = Unpack(word);
    return {.x = DecodeUfloat(bits.x, 6), .y = DecodeUfloat(bits.y, 6),
            .z = DecodeUfloat(bits.z, 5)};
}

u32 EncodeUnorm(float value, u32 width) {
    return static_cast<u32>(std::nearbyint(std::clamp(value, 0.0f, 1.0f) * Mask(width)));
}

float DecodeUnorm(u32 value, u32 width) {
    return static_cast<float>(value & Mask(width)) / static_cast<float>(Mask(width));
}

u32 EncodeSnorm(float value, u32 width) {
    const s32 max_positive = (1 << (width - 1)) - 1;
    const auto signed_value = static_cast<s32>(std::nearbyint(
        std::clamp(value, -1.0f, 1.0f) * static_cast<float>(max_positive)));
    return static_cast<u32>(signed_value) & Mask(width);
}

s32 DecodeSnorm(u32 value, u32 width) {
    const u32 sign_bit = 1u << (width - 1);
    return static_cast<s32>((value ^ sign_bit) - sign_bit);
}

float DecodeSnormFloat(u32 value, u32 width) {
    const u32 sign_bit = 1u << (width - 1);
    const s32 signed_value = static_cast<s32>((value ^ sign_bit) - sign_bit);
    const float max_positive = static_cast<float>(sign_bit - 1);
    return std::clamp(static_cast<float>(signed_value) / max_positive, -1.0f, 1.0f);
}

u32 PackUnorm(Components value) {
    return Pack(PackedComponents{EncodeUnorm(value.x, Widths[0]), EncodeUnorm(value.y, Widths[1]),
                                 EncodeUnorm(value.z, Widths[2])});
}

Components UnpackUnorm(u32 word) {
    const auto bits = Unpack(word);
    return {.x = DecodeUnorm(bits.x, Widths[0]), .y = DecodeUnorm(bits.y, Widths[1]),
            .z = DecodeUnorm(bits.z, Widths[2])};
}

u32 PackSnorm(Components value) {
    return Pack(PackedComponents{EncodeSnorm(value.x, Widths[0]), EncodeSnorm(value.y, Widths[1]),
                                 EncodeSnorm(value.z, Widths[2])});
}

Components UnpackSnorm(u32 word) {
    const auto bits = Unpack(word);
    return {.x = DecodeSnormFloat(bits.x, Widths[0]), .y = DecodeSnormFloat(bits.y, Widths[1]),
            .z = DecodeSnormFloat(bits.z, Widths[2])};
}

PackedComponents UnpackSint(u32 word) {
    const auto bits = Unpack(word);
    const auto sign_extend = [](u32 value, u32 width) {
        const u32 sign_bit = 1u << (width - 1);
        return static_cast<u32>(static_cast<s32>((value ^ sign_bit) - sign_bit));
    };
    return {.x = sign_extend(bits.x, Widths[0]), .y = sign_extend(bits.y, Widths[1]),
            .z = sign_extend(bits.z, Widths[2])};
}

float DecodeUscaled(u32 value, u32 width) {
    return static_cast<float>(value & Mask(width));
}

float DecodeSscaled(u32 value, u32 width) {
    return static_cast<float>(DecodeSnorm(value, width));
}

template <size_t num_fields>
bool HasSubsequence(const std::vector<std::pair<u32, u32>>& fields,
                    const std::array<std::pair<u32, u32>, num_fields>& expected) {
    for (size_t start = 0; start + expected.size() <= fields.size(); ++start) {
        if (std::equal(expected.begin(), expected.end(), fields.begin() + start)) {
            return true;
        }
    }
    return false;
}

bool CheckSpirvFormatOperations(const std::vector<u32>& code) {
    std::unordered_map<u32, u32> constants;
    std::unordered_map<u32, std::string> names;
    std::unordered_map<spv::Op, u32> counts;
    bool has_fclamp{};
    bool has_round_even{};
    for (size_t word = 5; word < code.size();) {
        const u32 instruction = code[word];
        const u32 word_count = instruction >> 16;
        const auto opcode = static_cast<spv::Op>(instruction & 0xffff);
        if (word_count == 0 || word + word_count > code.size()) {
            return false;
        }
        ++counts[opcode];
        if (opcode == spv::OpExtInst && word_count >= 5) {
            has_fclamp |= code[word + 4] == GLSLstd450FClamp;
            has_round_even |= code[word + 4] == GLSLstd450RoundEven;
        }
        if (opcode == spv::OpConstant && word_count == 4) {
            constants[code[word + 2]] = code[word + 3];
        } else if (opcode == spv::OpName && word_count >= 3) {
            const auto* name = reinterpret_cast<const char*>(&code[word + 2]);
            names[code[word + 1]] = name;
        }
        word += word_count;
    }

    const auto get_constant = [&](u32 id) -> std::optional<u32> {
        const auto it = constants.find(id);
        return it == constants.end() ? std::nullopt : std::optional<u32>{it->second};
    };
    const auto has_name = [&](std::string_view name) {
        return std::ranges::any_of(names, [&](const auto& item) { return item.second == name; });
    };

    std::vector<std::pair<u32, u32>> extracted;
    std::vector<std::pair<u32, u32>> inserted;
    for (size_t word = 5; word < code.size();) {
        const u32 instruction = code[word];
        const u32 word_count = instruction >> 16;
        const auto opcode = static_cast<spv::Op>(instruction & 0xffff);
        if (opcode == spv::OpBitFieldUExtract && word_count == 6) {
            const auto offset = get_constant(code[word + 4]);
            const auto width = get_constant(code[word + 5]);
            if (!offset || !width) {
                return false;
            }
            extracted.emplace_back(*offset, *width);
        } else if (opcode == spv::OpBitFieldSExtract && word_count == 6) {
            const auto offset = get_constant(code[word + 4]);
            const auto width = get_constant(code[word + 5]);
            if (!offset || !width) {
                return false;
            }
            extracted.emplace_back(*offset, *width);
        } else if (opcode == spv::OpBitFieldInsert && word_count == 7) {
            const auto offset = get_constant(code[word + 5]);
            const auto width = get_constant(code[word + 6]);
            if (!offset || !width) {
                return false;
            }
            inserted.emplace_back(*offset, *width);
        }
        word += word_count;
    }

    constexpr std::array<std::pair<u32, u32>, 3> unpack_fields{{{0, 11}, {11, 11}, {22, 10}}};
    constexpr std::array<std::pair<u32, u32>, 2> pack_fields{{{11, 11}, {22, 10}}};
    return HasSubsequence(extracted, unpack_fields) && HasSubsequence(inserted, pack_fields) &&
           counts[spv::OpConvertUToF] > 0 && counts[spv::OpConvertSToF] > 0 &&
           counts[spv::OpConvertFToU] > 0 && counts[spv::OpConvertFToS] > 0 &&
           has_fclamp && has_round_even &&
           has_name("f32_to_uf11") && has_name("f32_to_uf10") &&
           has_name("uf11_to_f32") && has_name("uf10_to_f32");
}

bool Write(const std::filesystem::path& path, const std::vector<u32>& code) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(code.data()), code.size() * sizeof(u32));
    return bool(out);
}

std::vector<u32> MakeShader() {
    using namespace Shader;
    Info info{};
    info.hw_stage = HwStage::Fragment;
    info.sw_stage = SwStage::Fragment;
    // This small test constructs IR directly and bypasses ShaderInfoCollectionPass.
    info.uses_pack_10_11_11 = true;
    info.uses_unpack_10_11_11 = true;
    info.loads.Set(IR::Attribute::Param0, 0);
    for (u32 component = 0; component < 3; ++component) {
        info.loads.Set(IR::Attribute::Param1, component);
    }
    info.fs_interpolation[0].primary = Qualifier::Flat;
    info.fs_interpolation[1].primary = Qualifier::Flat;

    RuntimeInfo runtime{};
    runtime.Initialize(info.hw_stage, info.sw_stage);
    auto& fs = runtime.hw.fs;
    fs.num_inputs = 2;
    fs.inputs[0] = {.param_index = 0, .is_default = false, .is_flat = true};
    fs.inputs[1] = {.param_index = 1, .is_default = false, .is_flat = true};
    fs.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;

    Common::ObjectPool<IR::Inst> pool;
    IR::Block block(pool);
    IR::IREmitter ir(block);

    const auto input_word = ir.BitCast<IR::U32>(ir.GetAttribute(IR::Attribute::Param0, 0));
    const auto packed_input = ir.CompositeConstruct(ir.GetAttribute(IR::Attribute::Param1, 0),
                                                    ir.GetAttribute(IR::Attribute::Param1, 1),
                                                    ir.GetAttribute(IR::Attribute::Param1, 2));
    auto observable = ir.Imm32(0.f);
    for (const auto format : {AmdGpu::NumberFormat::Float, AmdGpu::NumberFormat::Unorm,
                              AmdGpu::NumberFormat::Snorm, AmdGpu::NumberFormat::Uint,
                              AmdGpu::NumberFormat::Sint}) {
        const auto unpacked = ir.Unpack10_11_11(format, input_word);
        observable = ir.FPAdd(observable, IR::F32{ir.CompositeExtract(unpacked, 0)});
        const auto packed = ir.Pack10_11_11(format, packed_input);
        observable = ir.FPAdd(observable, ir.BitCast<IR::F32>(packed));
    }

    // Scaled formats use the legacy integer fields plus the existing typed-buffer conversion.
    const auto unsigned_raw = ir.Unpack10_11_11(AmdGpu::NumberFormat::Uint, input_word);
    const auto signed_raw = ir.Unpack10_11_11(AmdGpu::NumberFormat::Sint, input_word);
    for (u32 component = 0; component < 3; ++component) {
        observable = ir.FPAdd(
            observable,
            IR::ApplyReadNumberConversion(
                ir, IR::F32{ir.CompositeExtract(unsigned_raw, component)},
                AmdGpu::NumberConversion::UintToUscaled));
        observable = ir.FPAdd(
            observable,
            IR::ApplyReadNumberConversion(
                ir, IR::F32{ir.CompositeExtract(signed_raw, component)},
                AmdGpu::NumberConversion::SintToSscaled));
    }

    const auto output = IR::Attribute::RenderTarget0;
    ir.SetAttribute(output, observable, 0);
    info.stores.Set(output, 0);
    ir.Epilogue();

    IR::Program program(info);
    program.blocks.push_back(&block);
    program.syntax_list.push_back(
        {.data = {.block = &block}, .type = IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});

    Profile profile{};
    profile.supported_spirv = 0x00010600;
    profile.support_int64 = true;
    Backend::Bindings bindings{};
    return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
}

} // namespace

int main(int argc, char** argv) {
    using AmdGpu::NumberFormat;
    std::fesetround(FE_TONEAREST);

    // R11, G11, B10 occupy low-to-high fields. The normalized and integer paths
    // retain the component widths and order even though their numeric meaning differs.
    CHECK(Pack(PackedComponents{1, 2, 4}) == (1u | (2u << 11) | (4u << 22)));
    CHECK((Unpack(Pack(PackedComponents{0x7ff, 0x7ff, 0x3ff})) ==
           PackedComponents{0x7ff, 0x7ff, 0x3ff}));
    CHECK(PackUfloat(Components{1.0f, 2.0f, 4.0f}) == 0x882003c0u);
    CHECK(PackUfloat(Components{0.5f, 0.25f, 0.125f}) == 0x601a0380u);
    CHECK((UnpackUfloat(0) == Components{0, 0, 0}));
    CHECK((UnpackUfloat(PackUfloat(Components{std::ldexp(1.0f, -20), std::ldexp(1.0f, -20),
                                                 std::ldexp(1.0f, -19)})) ==
           Components{std::ldexp(1.0f, -20), std::ldexp(1.0f, -20), std::ldexp(1.0f, -19)}));
    CHECK(UnpackUfloat(Field(30, 63, 6)).x == 65024.0f);
    CHECK(UnpackUfloat(Field(30, 31, 5) << 22).z == 64512.0f);
    CHECK(std::isinf(DecodeUfloat(Field(31, 0, 6), 6)));
    CHECK(std::isnan(DecodeUfloat(Field(31, 1, 5), 5)));
    CHECK(PackUfloat(Components{-1.f, -INFINITY, -0.5f}) == 0);
    CHECK(PackUfloat(Components{INFINITY, INFINITY, INFINITY}) ==
          Pack(PackedComponents{Field(31, 0, 6), Field(31, 0, 6), Field(31, 0, 5)}));
    for (const u32 word : {0u, 0x80000001u, 0x555aaaaau}) {
        const Components values = UnpackUfloat(word);
        CHECK(PackUfloat(values) == word);
    }

    // UNORM clamps to [0,1], scales by 2^width-1, and rounds to nearest-even.
    CHECK(PackUnorm(Components{0.f, 0.5f, 1.f}) ==
          Pack(PackedComponents{0, 1024, 1023}));
    CHECK((UnpackUnorm(PackUnorm(Components{0.f, 0.5f, 1.f})) ==
           Components{0.f, 1024.f / 2047.f, 1.f}));
    CHECK(PackUnorm(Components{-1.f, 2.f, 0.5f}) == Pack(PackedComponents{0, 2047, 512}));
    CHECK(EncodeUnorm(0.5f / 2047.f, 11) == 0);
    CHECK(EncodeUnorm(1.5f / 2047.f, 11) == 2);

    // SNORM uses signed two's-complement fields, max-positive normalization, and clamps
    // the most-negative code to -1 on decode.
    CHECK(PackSnorm(Components{1.f, -1.f, 0.5f}) ==
          Pack(PackedComponents{0x3ff, 0x401, 0x100}));
    CHECK((UnpackSnorm(PackSnorm(Components{-1.f, 0.f, 1.f})) ==
           Components{-1.f, 0.f, 1.f}));
    CHECK(DecodeSnorm(0x400, 11) == -1024);
    CHECK(DecodeSnorm(0x200, 10) == -512);
    CHECK(DecodeSnormFloat(0x400, 11) == -1.f);
    CHECK(DecodeSnormFloat(0x200, 10) == -1.f);
    CHECK(DecodeSnormFloat(0x3ff, 11) == 1.f);
    CHECK(DecodeSnormFloat(0x1ff, 10) == 1.f);

    // UINT/SINT pack raw VGPR integer bits into the fields; unpack preserves their
    // unsigned or sign-extended integer values in the F32x3 bit-preserving IR carrier.
    CHECK(Pack(PackedComponents{0xffffffffu, 0xffffffffu, 0xffffffffu}) == 0xffffffffu);
    CHECK((Unpack(Pack(PackedComponents{0x800, 0x401, 0x200})) ==
           PackedComponents{0, 0x401, 0x200}));
    CHECK((UnpackSint(Pack(PackedComponents{0x400, 0x7ff, 0x200})) ==
           PackedComponents{0xfffffc00u, 0xffffffffu, 0xfffffe00u}));
    CHECK(DecodeUscaled(0x7ff, 11) == 2047.f);
    CHECK(DecodeSscaled(0x400, 11) == -1024.f);
    CHECK(DecodeSscaled(0x200, 10) == -512.f);
    CHECK(static_cast<float>(static_cast<s32>(UnpackSint(Pack(PackedComponents{0x400, 0, 0})).x)) ==
          -1024.f);

    // Sea Islands exposes scaled number formats as readable but not writable. The seven
    // family members are supported for reads; scaled writes and SNORM_OGL stay fatal.
    for (const auto format : {NumberFormat::Float, NumberFormat::Unorm, NumberFormat::Snorm,
                              NumberFormat::Uscaled, NumberFormat::Sscaled, NumberFormat::Uint,
                              NumberFormat::Sint}) {
        CHECK(AmdGpu::IsSupported10_11_11NumberFormat(format));
    }
    CHECK(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Float));
    CHECK(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Unorm));
    CHECK(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Snorm));
    CHECK(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Uint));
    CHECK(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Sint));
    CHECK(!AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Uscaled));
    CHECK(!AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Sscaled));
    CHECK(!AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::SnormNz));
    // A sharp with an impossible number format (one the game has not written yet) is lowered as a
    // null sharp instead of ending the game: every unsupported read, and scaled writes too.
    using AmdGpu::DataFormat;
    CHECK(AmdGpu::IsUnconvertible10_11_11(DataFormat::Format10_11_11, NumberFormat::SnormNz, false));
    CHECK(AmdGpu::IsUnconvertible10_11_11(DataFormat::Format10_11_11, NumberFormat::SnormNz, true));
    CHECK(!AmdGpu::IsUnconvertible10_11_11(DataFormat::Format10_11_11, NumberFormat::Uscaled, false));
    CHECK(AmdGpu::IsUnconvertible10_11_11(DataFormat::Format10_11_11, NumberFormat::Uscaled, true));
    for (const auto format : {NumberFormat::Float, NumberFormat::Unorm, NumberFormat::Snorm,
                              NumberFormat::Uint, NumberFormat::Sint}) {
        CHECK(!AmdGpu::IsUnconvertible10_11_11(DataFormat::Format10_11_11, format, false));
        CHECK(!AmdGpu::IsUnconvertible10_11_11(DataFormat::Format10_11_11, format, true));
    }
    // Other data formats are not this check's business.
    CHECK(!AmdGpu::IsUnconvertible10_11_11(DataFormat::Format8_8_8_8, NumberFormat::SnormNz, false));
    CHECK(AmdGpu::RemapNumberFormat(NumberFormat::Uscaled, AmdGpu::DataFormat::Format10_11_11) ==
          NumberFormat::Uint);
    CHECK(AmdGpu::RemapNumberFormat(NumberFormat::Sscaled, AmdGpu::DataFormat::Format10_11_11) ==
          NumberFormat::Sint);

    Core::Emulator::SetSerial();
    const auto code = MakeShader();
    CHECK(CheckSpirvFormatOperations(code));
    const std::filesystem::path output_dir = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(output_dir);
    CHECK(Write(output_dir / "buffer-format-10-11-11.spv", code));
    return 0;
}
