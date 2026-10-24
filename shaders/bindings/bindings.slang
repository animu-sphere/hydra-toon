// SPDX-License-Identifier: Apache-2.0
// The backend's resource declarations (WebGPU policy §5): where each
// resource the shared modules read is bound, the draw constants' binding,
// and the texture accessors. Shading functions, parameter records and
// material semantics stay in the shared modules. The build selects the
// module with a define on the slangc command line, TOON_WEBGPU for WebGPU,
// so each entry point is still one invocation (WGPU-Q2). Included first,
// through skinning.slang, by every scene module.

#if defined(TOON_WEBGPU)
#include "bindings/bindings_webgpu.slang"
#else
#include "bindings/bindings_vulkan.slang"
#endif
