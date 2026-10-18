// SPDX-License-Identifier: Apache-2.0
#include "adapter.hpp"

#include <pxr/pxr.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

// Identity camera makes the analytic triangle cover the same pixels in both
// the actual Hydra pass and the independent offscreen readback.
class PassState final : public HdRenderPassState {
  GfMatrix4d GetWorldToViewMatrix() const override { return GfMatrix4d(1); }
  GfMatrix4d GetProjectionMatrix() const override { return GfMatrix4d(1); }
};

bool Rgba(const std::string& shaders, unsigned samples) {
  Toon::FrameStatus status;
  std::string error;
  auto oracle = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders),
      status, error, {samples});
  if (status == Toon::FrameStatus::Skip) return false;
  Require(oracle != nullptr, error.c_str());
  Require(oracle->statistics().samples == samples, "requested MSAA unavailable");
  if (!oracle->SetBackground({Toon::BackgroundMode::Transparent, {}}, error)) {
    Require(samples > 1 && error.find("alphaToOne") != std::string::npos,
        error.c_str());
    return false;
  }
  const auto stage = UsdStage::CreateInMemory();
  Require(stage->GetRootLayer()->ImportFromString(R"(#usda 1.0
def Mesh "Triangle" (
    prepend apiSchemas = ["MaterialBindingAPI"]
) {
    point3f[] points = [(-0.8, -0.7, 0), (0.8, -0.7, 0), (0, 0.8, 0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    uniform token subdivisionScheme = "none"
    bool doubleSided = true
    rel material:binding = </Material>
}
def Material "Material" {
    token outputs:surface.connect = </Material/Surface.outputs:surface>
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0, 0, 0)
        color3f inputs:emissiveColor = (0.6, 0.3, 0.15)
        float inputs:opacity = 0.25
        token outputs:surface
    }
}
)"), "RGBA fixture import failed");
  HdToonRenderDelegate delegate;
  std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
  UsdImagingCreateSceneIndicesInfo info;
  info.stage = stage;
  const auto indices = UsdImagingCreateSceneIndices(info);
  index->InsertSceneIndex(indices.finalSceneIndex, SdfPath::AbsoluteRootPath());
  const HdRprimCollection collection(HdTokens->geometry,
      HdReprSelector(HdReprTokens->smoothHull));
  const auto sync = [&] {
    indices.stageSceneIndex->ApplyPendingUpdates();
    index->EnqueueCollectionToSync(collection);
    HdTaskSharedPtrVector tasks;
    HdTaskContext context;
    index->SyncAll(&tasks, &context);
  };
  sync();
  Require(delegate.CommitScene().meshes.size() == 1 &&
      !delegate.CommitScene().materials.empty(), "fixture did not sync");
  HdToonRenderBuffer color(SdfPath("/color"));
  HdToonRenderBuffer depth(SdfPath("/depth"));
  Require(color.Allocate(GfVec3i(96, 96, 1), HdFormatUNorm8Vec4, false) &&
      depth.Allocate(GfVec3i(48, 48, 1), HdFormatFloat32, false), "AOV allocation");
  auto state = std::make_shared<PassState>();
  HdRenderPassAovBinding color_binding;
  color_binding.aovName = HdAovTokens->color;
  color_binding.renderBuffer = &color;
  // The renderer setting owns the scene clear, independently of a host's
  // AOV clear hint. A conspicuous hint catches accidentally using it.
  color_binding.clearValue = VtValue(GfVec4f(1, 0, 1, 1));
  HdRenderPassAovBinding depth_binding;
  depth_binding.aovName = HdAovTokens->depth;
  depth_binding.renderBuffer = &depth;
  state->SetAovBindings({color_binding, depth_binding});
  const auto pass = delegate.CreateRenderPass(index.get(), collection);
  delegate.SetRenderSetting(TfToken("toon:msaaSamples"), VtValue(int(samples)));
  const auto mode = TfToken("toon:backgroundMode");
  const auto clear = TfToken("toon:backgroundColor");
  const auto render = [&] {
    TfErrorMark mark;
    pass->Execute(state, {HdRenderTagTokens->geometry});
    Require(mark.IsClean() && color.IsConverged() && depth.IsConverged(),
        "Hydra RGBA frame failed");
    const auto* data = static_cast<const std::uint8_t*>(color.Map());
    Require(data != nullptr, "colour map failed");
    std::vector<std::uint8_t> result(data, data + 96 * 96 * 4);
    color.Unmap();
    return result;
  };
  const auto initial = render();
  const auto initial_revision = delegate.CommitScene().revision;
  Require(initial[3] == 255 && std::abs(int(initial[0]) - 13) <= 1,
      "default background changed");
  delegate.SetRenderSetting(mode, VtValue(std::string("transparent")));
  const auto foreground = render();
  const auto draws = Toon::ExtractDrawList(delegate.CommitScene());
  Toon::ColorProduct reference;
  Toon::DepthProduct reference_depth;
  Require(oracle->Render(draws, 96, 96, reference, reference_depth, error), error.c_str());
  bool partial = false;
  for (std::size_t y = 0; y < 96; ++y) {
    for (std::size_t x = 0; x < 96; ++x) {
      const auto target = (y * 96 + x) * 4;
      const auto source = ((95 - y) * 96 + x) * 4;
      Require(std::equal(foreground.begin() + target,
          foreground.begin() + target + 4, reference.payload.begin() + source),
          "Hydra changed premultiplied RGBA or row orientation");
      const int alpha = foreground[target + 3];
      partial |= alpha > 0 && alpha < 63;
      for (int c = 0; c < 3; ++c) {
        const float rgb = c == 0 ? 0.6F : c == 1 ? 0.3F : 0.15F;
        Require(std::abs(foreground[target + c] - rgb * alpha) <= 2,
            "colour AOV applied opacity incorrectly");
      }
    }
  }
  Require(foreground[3] == 0 && foreground[(48 * 96 + 48) * 4 + 3] == 64,
      "transparent clear or Blend alpha lost");
  Require(samples == 1 || partial, "MSAA silhouette lost partial coverage");
  const auto* depth_values = static_cast<const float*>(depth.Map());
  Require(depth_values != nullptr && depth_values[0] == 1 &&
      depth_values[24 * 48 + 24] == 1, "Blend changed depth AOV");
  depth.Unmap();
  delegate.SetRenderSetting(clear, VtValue(GfVec4f(0.8F, 0.2F, 0.4F, 0.1F)));
  Require(render() == foreground, "transparent colour changed foreground");
  for (const VtValue invalid : {VtValue(42), VtValue(std::string("unknown"))}) {
    delegate.SetRenderSetting(mode, invalid);
    Require(render() == foreground, "invalid mode changed background");
  }
  delegate.SetRenderSetting(mode, VtValue(TfToken("opaque")));
  for (const VtValue invalid : {VtValue(42), VtValue(GfVec4f(-1, 0, 0, 1)),
           VtValue(GfVec4f(0, 2, 0, 1)),
           VtValue(GfVec4f(0, 0, std::numeric_limits<float>::quiet_NaN(), 1))}) {
    delegate.SetRenderSetting(clear, invalid);
    Require(render() == foreground, "invalid colour changed background");
  }
  const GfVec4f background(0.8F, 0.2F, 0.4F, 0.1F);
  delegate.SetRenderSetting(clear, VtValue(background));
  const auto opaque = render();
  for (std::size_t pixel = 0; pixel < foreground.size(); pixel += 4) {
    Require(opaque[pixel + 3] == 255, "opaque mode used setting alpha");
    const float alpha = foreground[pixel + 3] / 255.0F;
    for (int c = 0; c < 3; ++c)
      Require(std::abs(opaque[pixel + c] - (foreground[pixel + c] +
          background[c] * 255 * (1 - alpha))) <= 2,
          "Hydra source-over differs from transparent product");
  }
  // Recreate the offscreen renderer, then return to the original sample
  // count with invalid settings: its last accepted background must survive.
  delegate.SetRenderSetting(clear, VtValue(42));
  delegate.SetRenderSetting(TfToken("toon:msaaSamples"), VtValue(samples == 1 ? 4 : 1));
  Require(render()[3] == 255, "MSAA recreation lost background");
  delegate.SetRenderSetting(TfToken("toon:msaaSamples"), VtValue(int(samples)));
  Require(render() == opaque, "MSAA recreation changed accepted background");
  delegate.SetRenderSetting(clear, VtValue(background));
  delegate.SetRenderSetting(mode, VtValue(TfToken("transparent")));
  Require(render() == foreground, "background changes altered foreground");
  Require(delegate.CommitScene().revision == initial_revision,
      "background changes edited scene state");
  const auto opacity = stage->GetAttributeAtPath(
      SdfPath("/Material/Surface.inputs:opacity"));
  for (const float value : {1.0F, 0.0F}) {
    opacity.Set(value);
    sync();
    const auto pixels = render();
    const auto alpha = pixels[(48 * 96 + 48) * 4 + 3];
    Require(alpha == (value == 1 ? 255 : 0), "Opaque/zero-opacity AOV alpha");
  }
  std::cout << "Hydra premultiplied RGBA " << samples << "x passed\n";
  return true;
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string_view(argv[1]) == "--rgba-gpu") {
    try { return Rgba(argv[2], 1) && Rgba(argv[2], 4) ? 0 : 77; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  }
  if (argc != 1) return 2;
  HdToonRenderBuffer color(SdfPath("/color"));
  if (!Check(color.Allocate(GfVec3i(4, 4, 1), HdFormatUNorm8Vec4, false),
          "color allocation failed")) {
    return 1;
  }
  const std::vector<std::uint8_t> source{
      255, 0, 0, 255, 0, 255, 0, 255,
      0, 0, 255, 255, 255, 255, 255, 255};
  if (!Check(color.WriteColor(source, 2, 2), "color write failed")) {
    return 1;
  }
  color.SetConverged(true);
  // The source's top-left red pixel lands on the buffer's top row, which is
  // Hydra's last; its bottom-left blue pixel starts the buffer.
  const auto* pixels = static_cast<const std::uint8_t*>(color.Map());
  const std::size_t top_left = 3U * 4U * 4U;
  if (!Check(pixels != nullptr && pixels[0] == 0 && pixels[2] == 255 &&
                 pixels[3] == 255 && pixels[top_left] == 255 &&
                 pixels[top_left + 2U] == 0,
          "flipped, scaled color payload is incorrect") ||
      !Check(color.IsMapped(), "color map state is incorrect") ||
      !Check(color.IsConverged(), "color did not converge")) {
    return 1;
  }
  color.Unmap();

  HdToonRenderBuffer depth(SdfPath("/depth"));
  if (!Check(depth.Allocate(GfVec3i(2, 2, 1), HdFormatFloat32, false),
          "depth allocation failed") ||
      !Check(depth.WriteDepth({0.25F}, 1, 1), "depth write failed")) {
    return 1;
  }
  const auto* depths = static_cast<const float*>(depth.Map());
  if (!Check(depths != nullptr && depths[0] == 0.25F &&
                 depths[3] == 0.25F,
          "scaled depth payload is incorrect")) {
    return 1;
  }
  depth.Unmap();

  HdToonRenderBuffer ids(SdfPath("/primId"));
  if (!Check(ids.Allocate(GfVec3i(2, 1, 1), HdFormatInt32, false),
          "id allocation failed") ||
      !Check(ids.WriteIds(-1), "id write failed")) {
    return 1;
  }
  const auto* id_values = static_cast<const std::int32_t*>(ids.Map());
  if (!Check(id_values != nullptr && id_values[0] == -1 && id_values[1] == -1,
          "id payload is incorrect")) {
    return 1;
  }
  ids.Unmap();
  return 0;
}
