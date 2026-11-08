// SPDX-License-Identifier: GPL-2.0-or-later
// Offline PS4 guest-format compatibility checks.
#include <utility>

#include "video_core/amdgpu/resource.h"

int main() {
    using AmdGpu::DataFormat;
    using AmdGpu::NumberConversion;
    using AmdGpu::NumberFormat;

    static_assert(AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Float));
    static_assert(AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Unorm));
    static_assert(AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Snorm));
    static_assert(AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Uscaled));
    static_assert(AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Sscaled));
    static_assert(AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Uint));
    static_assert(AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Sint));
    static_assert(!AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::SnormNz));
    static_assert(!AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::Srgb));

    static_assert(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Float));
    static_assert(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Unorm));
    static_assert(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Snorm));
    static_assert(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Uint));
    static_assert(AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Sint));
    static_assert(!AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Uscaled));
    static_assert(!AmdGpu::IsWritable10_11_11NumberFormat(NumberFormat::Sscaled));

    // The Sea Islands buffer descriptor carries data_format and num_format in distinct fields.
    // Raw recognized pairs are not rewritten into a later GFX unified-format alias.
    AmdGpu::Buffer buffer{};
    buffer.data_format = u32(DataFormat::Format10_11_11);
    for (const auto& [raw_format, remapped_format] : {
             std::pair{NumberFormat::Unorm, NumberFormat::Unorm},
             std::pair{NumberFormat::Snorm, NumberFormat::Snorm},
             std::pair{NumberFormat::Uscaled, NumberFormat::Uint},
             std::pair{NumberFormat::Sscaled, NumberFormat::Sint},
             std::pair{NumberFormat::Uint, NumberFormat::Uint},
             std::pair{NumberFormat::Sint, NumberFormat::Sint},
             std::pair{NumberFormat::Float, NumberFormat::Float}}) {
        buffer.num_format = u32(raw_format);
        if (buffer.GetDataFmt() != DataFormat::Format10_11_11 ||
            buffer.GetNumberFmt() != remapped_format ||
            !AmdGpu::IsSupported10_11_11NumberFormat(raw_format)) {
            return 1;
        }
    }

    buffer.num_format = u32(NumberFormat::SnormNz);
    if (buffer.GetNumberFmt() != NumberFormat::Sint ||
        AmdGpu::IsSupported10_11_11NumberFormat(NumberFormat::SnormNz)) {
        return 1;
    }

    // 11:11:10 swaps the outside components when the project normalizes it to 10:11:11.
    buffer.data_format = u32(DataFormat::Format11_11_10);
    buffer.num_format = u32(NumberFormat::Unorm);
    if (buffer.GetDataFmt() != DataFormat::Format10_11_11 ||
        buffer.GetNumberFmt() != NumberFormat::Unorm) {
        return 1;
    }

    // Existing scaled and SNORM_NZ conversions for established packed integer formats remain.
    static_assert(AmdGpu::MapNumberConversion(NumberFormat::Uscaled, DataFormat::Format16) ==
                  NumberConversion::UintToUscaled);
    static_assert(AmdGpu::MapNumberConversion(NumberFormat::Sscaled, DataFormat::Format16) ==
                  NumberConversion::SintToSscaled);
    static_assert(AmdGpu::MapNumberConversion(NumberFormat::SnormNz, DataFormat::Format8) ==
                  NumberConversion::Sint8ToSnormNz);
    static_assert(AmdGpu::MapNumberConversion(NumberFormat::SnormNz, DataFormat::Format16) ==
                  NumberConversion::Sint16ToSnormNz);

    return 0;
}
