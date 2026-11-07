// SPDX-License-Identifier: GPL-2.0-or-later
// The vendor AMD upscaler SDK currently loads Windows DLLs. Native FSR frame
// generation and shared camera inputs are implemented by vk_fsrfg/vk_dlssg.
#ifndef _WIN32
#include "video_core/renderer_vulkan/vk_fsr4.h"
namespace Vulkan::Fsr4 {
bool Init(const Instance&) { return false; }
bool Available() { return false; }
const char* Name() { return "FSR (unavailable on Linux)"; }
bool Dispatch(VkCommandBuffer, const Params&) { return false; }
}
#endif
