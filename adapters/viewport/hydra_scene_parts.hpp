// SPDX-License-Identifier: Apache-2.0
// What a host built with OpenUSD beside hydra_scene.cpp reaches through a
// HydraScene: its stage and render delegate. Only OpenUSD translation units
// include this; hydra_scene.hpp declares the type opaque.
#pragma once

#include "adapter.hpp"

#include <pxr/usd/usd/stage.h>

namespace Toon::viewport {

struct HydraSceneParts {
  PXR_NS::UsdStageRefPtr stage;
  PXR_NS::HdToonRenderDelegate* delegate = nullptr;
};

} // namespace Toon::viewport
