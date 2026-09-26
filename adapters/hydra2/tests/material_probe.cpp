// SPDX-License-Identifier: Apache-2.0
//
// MAT-Q1 probe: what of a Material's canonical semantics reaches a classic
// HdMaterial, the kind of Sprim hdToon's delegate creates. The stage runs
// through UsdImaging's scene index chain and scene index emulation, as in
// usdview, into a delegate that only records what each Sync receives. It
// links nothing of a format repository: a `vrm` container appears only when
// the session registers vrmSchema and vrmImaging through PXR_PLUGINPATH_NAME.
#include <pxr/pxr.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const SdfPath kMaterial("/Looks/Toon");

struct SyncRecord {
  HdDirtyBits bits = 0;
  size_t network_nodes = 0;
  size_t network_vrm_parameters = 0;
  bool get_returns_value = false;
  bool has_vrm = false;
  std::string vrm_groups;
  std::string shade_color;
  std::string shading_shift;
};

std::vector<SyncRecord> g_syncs;

std::string Describe(const HdDataSourceBaseHandle& ds) {
  const auto sampled = HdSampledDataSource::Cast(ds);
  if (!sampled) {
    return "absent";
  }
  std::ostringstream out;
  out << sampled->GetValue(0.0F);
  return out.str();
}

class ProbeMaterial final : public HdMaterial {
 public:
  explicit ProbeMaterial(const SdfPath& id) : HdMaterial(id) {}

  void Sync(HdSceneDelegate* scene, HdRenderParam*,
      HdDirtyBits* dirty) override {
    SyncRecord record;
    record.bits = *dirty;

    // The classic material path: the network UsdImaging built from what
    // the terminals connect to.
    const VtValue resource = scene->GetMaterialResource(GetId());
    if (resource.IsHolding<HdMaterialNetworkMap>()) {
      for (const auto& [terminal, network] :
          resource.UncheckedGet<HdMaterialNetworkMap>().map) {
        (void)terminal;
        for (const HdMaterialNode& node : network.nodes) {
          ++record.network_nodes;
          for (const auto& [name, value] : node.parameters) {
            (void)value;
            if (TfStringStartsWith(name.GetString(), "vrm:") ||
                TfStringStartsWith(name.GetString(), "inputs:vrm:")) {
              ++record.network_vrm_parameters;
            }
          }
        }
      }
    }
    record.get_returns_value =
        !scene->Get(GetId(), TfToken("inputs:vrm:mtoon:shadeColorFactor"))
             .IsEmpty();

    // The scene-index path: the prim's own data sources, read from the
    // render index's terminal scene index.
    const HdSceneIndexPrim prim = scene->GetRenderIndex()
        .GetTerminalSceneIndex()->GetPrim(GetId());
    static const TfToken vrm("vrm");
    if (const auto container =
            HdContainerDataSource::Cast(prim.dataSource
                ? prim.dataSource->Get(vrm) : nullptr)) {
      record.has_vrm = true;
      for (const TfToken& group : container->GetNames()) {
        record.vrm_groups += (record.vrm_groups.empty() ? "" : ",") +
            group.GetString();
      }
      record.shade_color = Describe(HdContainerDataSource::Get(prim.dataSource,
          HdDataSourceLocator(vrm, TfToken("mtoon"),
              TfToken("shadeColorFactor"))));
      record.shading_shift = Describe(HdContainerDataSource::Get(
          prim.dataSource, HdDataSourceLocator(vrm, TfToken("mtoon"),
              TfToken("shadingShiftFactor"))));
    }

    g_syncs.push_back(record);
    *dirty = Clean;
  }

  HdDirtyBits GetInitialDirtyBitsMask() const override { return AllDirty; }
};

// Records which locators of the material prim the scene index dirtied,
// before emulation translates them to dirty bits.
class DirtyObserver final : public HdSceneIndexObserver {
 public:
  std::string locators;

  void PrimsAdded(const HdSceneIndexBase&, const AddedPrimEntries&) override {}
  void PrimsRemoved(const HdSceneIndexBase&,
      const RemovedPrimEntries&) override {}
  void PrimsRenamed(const HdSceneIndexBase&,
      const RenamedPrimEntries&) override {}
  void PrimsDirtied(const HdSceneIndexBase&,
      const DirtiedPrimEntries& entries) override {
    for (const auto& entry : entries) {
      if (entry.primPath != kMaterial) {
        continue;
      }
      for (const HdDataSourceLocator& locator : entry.dirtyLocators) {
        locators += (locators.empty() ? "" : " ") + locator.GetString();
      }
    }
  }
};

DirtyObserver g_observer;
// What the delegate had seen of the observer when SyncAll called Update(),
// before any Sprim Sync.
std::string g_seen_at_update;

class ProbeDelegate final : public HdRenderDelegate {
 public:
  const TfTokenVector& GetSupportedRprimTypes() const override {
    static const TfTokenVector types;
    return types;
  }
  const TfTokenVector& GetSupportedSprimTypes() const override {
    static const TfTokenVector types{HdPrimTypeTokens->material};
    return types;
  }
  const TfTokenVector& GetSupportedBprimTypes() const override {
    static const TfTokenVector types;
    return types;
  }
  HdResourceRegistrySharedPtr GetResourceRegistry() const override {
    return registry_;
  }
  HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex*,
      const HdRprimCollection&) override {
    return nullptr;
  }
  HdInstancer* CreateInstancer(HdSceneDelegate*, const SdfPath&) override {
    return nullptr;
  }
  void DestroyInstancer(HdInstancer*) override {}
  HdRprim* CreateRprim(const TfToken&, const SdfPath&) override {
    return nullptr;
  }
  void DestroyRprim(HdRprim*) override {}
  HdSprim* CreateSprim(const TfToken& type, const SdfPath& id) override {
    return type == HdPrimTypeTokens->material ? new ProbeMaterial(id)
                                              : nullptr;
  }
  HdSprim* CreateFallbackSprim(const TfToken& type) override {
    return type == HdPrimTypeTokens->material
        ? new ProbeMaterial(SdfPath::EmptyPath())
        : nullptr;
  }
  void DestroySprim(HdSprim* sprim) override { delete sprim; }
  HdBprim* CreateBprim(const TfToken&, const SdfPath&) override {
    return nullptr;
  }
  HdBprim* CreateFallbackBprim(const TfToken&) override { return nullptr; }
  void DestroyBprim(HdBprim*) override {}
  void CommitResources(HdChangeTracker*) override {}

  // Hydra's hook for a delegate to observe the terminal scene index itself.
  // Emulation calls it too, so a classic delegate receives it.
  void SetTerminalSceneIndex(const HdSceneIndexBaseRefPtr& terminal) override {
    terminal_ = terminal;
    terminal_->AddObserver(HdSceneIndexObserverPtr(&g_observer));
  }
  void Update() override {
    g_seen_at_update = g_observer.locators;
    if (!g_seen_at_update.empty()) {
      g_seen_at_update += " shadingShiftFactor=" +
          Describe(HdContainerDataSource::Get(
              terminal_->GetPrim(kMaterial).dataSource,
              HdDataSourceLocator(TfToken("vrm"), TfToken("mtoon"),
                  TfToken("shadingShiftFactor"))));
    }
  }

  ~ProbeDelegate() override {
    if (terminal_) {
      terminal_->RemoveObserver(HdSceneIndexObserverPtr(&g_observer));
    }
  }

 private:
  HdSceneIndexBaseRefPtr terminal_;
  HdResourceRegistrySharedPtr registry_ =
      std::make_shared<HdResourceRegistry>();
};

void Report(const char* phase, size_t first) {
  std::cout << "phase=" << phase << " dirtied=[" << g_observer.locators
            << "] delegate_update_saw=[" << g_seen_at_update
            << "] syncs=" << g_syncs.size() - first << '\n';
  for (size_t i = first; i < g_syncs.size(); ++i) {
    const SyncRecord& r = g_syncs[i];
    char bits[16];
    std::snprintf(bits, sizeof bits, "0x%x", static_cast<unsigned>(r.bits));
    std::cout << "  sync bits=" << bits
              << " network_nodes=" << r.network_nodes
              << " network_vrm_parameters=" << r.network_vrm_parameters
              << " Get(inputs:vrm:...)=" << (r.get_returns_value ? "value"
                                                                  : "empty")
              << " vrm=" << (r.has_vrm ? "[" + r.vrm_groups + "]" : "absent")
              << " shadeColorFactor=" << r.shade_color
              << " shadingShiftFactor=" << r.shading_shift << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: toon-hydra2-material-probe <material-probe.usda>\n";
    return 2;
  }
  const UsdStageRefPtr stage = UsdStage::Open(argv[1]);
  if (!stage || !stage->GetPrimAtPath(kMaterial)) {
    std::cerr << "cannot open " << argv[1] << " with " << kMaterial << '\n';
    return 1;
  }

  UsdImagingCreateSceneIndicesInfo info;
  info.stage = stage;
  const UsdImagingSceneIndices indices = UsdImagingCreateSceneIndices(info);

  ProbeDelegate delegate;
  std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
  index->InsertSceneIndex(indices.finalSceneIndex,
      SdfPath::AbsoluteRootPath());

  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  auto sync = [&](const char* phase) {
    const size_t first = g_syncs.size();
    index->SyncAll(&tasks, &context);
    Report(phase, first);
    g_observer.locators.clear();
    g_seen_at_update.clear();
  };

  indices.stageSceneIndex->SetTime(UsdTimeCode(1.0));
  sync("populate");

  // An authored edit of a canonical value no network reads.
  stage->GetPrimAtPath(kMaterial)
      .GetAttribute(TfToken("inputs:vrm:mtoon:shadeColorFactor"))
      .Set(GfVec3f(0.1F, 0.2F, 0.3F));
  indices.stageSceneIndex->ApplyPendingUpdates();
  sync("authored-edit");

  // A time move across a time-sampled canonical value no network reads.
  indices.stageSceneIndex->SetTime(UsdTimeCode(2.0));
  sync("time-move");

  index.reset();
  return 0;
}
