// SPDX-License-Identifier: Apache-2.0
// Included after the shared Hydra host and GPU comparison helpers.
// The native session publishes USD itself; this check authors no material oracle.
Toon::ToonMaterial CanonicalMaterial(Toon::ToonMaterial value) {
  // Sampled coefficients have no canonical USD representation. Compare them
  // in the existing direct/GPU tests, and leave their realization OFF here.
  value.mmd.texture_tint = {};
  value.mmd.sphere_tint = {};
  value.mmd.toon_tint = {};
  return value;
}

bool RuntimeMmdMaterials(const char* pmx, const char* vmd, const std::string& shaders) {
  const auto stage = UsdStage::Open(pmx);
  if (!Check(bool(stage), "open generated material PMX")) return false;
  std::string root, session_layer;
  stage->GetRootLayer()->ExportToString(&root);
  stage->GetSessionLayer()->ExportToString(&session_layer);
  Host host(stage);
  const auto baseline = host.Sync();
  avatarMmdSession::Session session(stage, pmx, vmd, false, false, true);
  const auto api = session.Api();
  const auto match = HdToonMatchAvatarTargets(session.Baseline(),
      host.delegate.DescribeResidentTargets(), HdToonCanonicalMaterialInputs());
  for (const auto& mismatch : match.mismatches)
    std::cerr << mismatch.subject << ": " << mismatch.reason << '\n';
  Toon::AvatarStateAdapter adapter;
  std::string error;
  const auto checked = [&](bool ok) { return Check(ok,error.c_str()); };
  if (!Check(session.Baseline().material_count == 24 && match.mismatches.empty() &&
          match.bindings.materials.size() == 24 &&
          adapter.Bind(session.Baseline(), baseline, 1, match.bindings, error),
          "two complete automatically matched material layouts required")) return false;

  // Hold geometry at the imported baseline in BOTH products: this is a material
  // comparison, independent of pose/UV float rounding and moving physics bodies.
  // Preserve each path's material slots and the baseline's two material subsets.
  auto materialFrame = [&](Toon::FrameSnapshot value) {
    value.meshes = baseline.meshes;
    Toon::Float3 low{1e9F,1e9F,1e9F}, high{-1e9F,-1e9F,-1e9F};
    for (const auto& mesh : value.meshes) for (const auto& p : *mesh.points) {
      low.x = std::min(low.x,p.x); low.y = std::min(low.y,p.y);
      high.x = std::max(high.x,p.x); high.y = std::max(high.y,p.y);
    }
    value.view.projection.m[0] = 1.6F / (high.x-low.x);
    value.view.projection.m[5] = 1.6F / (high.y-low.y);
    value.view.projection.m[12] = -(high.x+low.x)*.5F*value.view.projection.m[0];
    value.view.projection.m[13] = -(high.y+low.y)*.5F*value.view.projection.m[5];
    return value;
  };
  auto compare = [&](const Toon::FrameSnapshot& direct, const Toon::FrameSnapshot& hydra) {
    if (!Check(direct.materials.size() == hydra.materials.size(), "material slot count parity")) return false;
    for (std::size_t i = 0; i < direct.materials.size(); ++i) {
      const auto a = CanonicalMaterial(direct.materials[i].material);
      const auto& b = hydra.materials[i].material;
      if (a != b) {
        std::cerr << "material slot " << i << " ids " << direct.materials[i].id << '/' << hydra.materials[i].id
                  << " base " << a.base_color.x << ',' << a.base_color.y << ',' << a.base_color.z
                  << '/' << b.base_color.x << ',' << b.base_color.y << ',' << b.base_color.z
                  << " alpha " << a.alpha << '/' << b.alpha
                  << " specular " << a.mmd.specular_power << '/' << b.mmd.specular_power
                  << " edge " << a.outline_width << '/' << b.outline_width
                  << " modes " << int(a.alpha_mode) << '/' << int(b.alpha_mode) << '\n';
      }
      if (!Check(direct.materials[i].id == hydra.materials[i].id &&
              a == b,
              "canonical direct/USD-Hydra material mismatch")) return false;
    }
    return true;
  };
  // Separate image capture proves the equality check is sensitive to material
  // changes, including transparent draws that intentionally write no depth.
  auto pixels = [&](const Toon::FrameSnapshot& frame, unsigned samples) {
    Toon::FrameStatus status;
    auto renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error, {samples});
    if (!renderer) throw std::runtime_error(error);
    Toon::ColorProduct color;
    Toon::DepthProduct depth;
    auto draws = Toon::ExtractDrawList(frame);
    draws.lighting.key_direction = {0.5F,0.2F,1.0F};
    if (!renderer->Render(draws,128,128,color,depth,error) || renderer->statistics().validation_message_count)
      throw std::runtime_error("material GPU capture/validation failed: " + error);
    return color.payload;
  };
  std::vector<std::vector<Toon::ToonMaterial>> replay;
  std::vector<std::vector<std::uint8_t>> image_replay[2];
  std::vector<std::uint8_t> base_pixels[2];
  if (!shaders.empty()) {
    const auto frame = materialFrame(baseline);
    for (unsigned i = 0; i < 2; ++i) base_pixels[i] = pixels(frame,i ? 4 : 1);
  }
  bool changed = false, changed_pixels[2]{};
  Toon::RetainedAvatarSnapshot first;
  std::vector<double> retained;
  for (unsigned generation = 0; generation < 3; ++generation) {
    session.Play();
    for (unsigned tick = 1; tick <= 240; ++tick) {
      // The host clock stores integral nanoseconds; round above the fixed tick.
      session.Advance(.0083334);
      Toon::RetainedAvatarSnapshot held;
      if (!checked(held.Reset(api,session.Snapshot(),error))) return false;
      const auto& state = *held.View();
      if (!Check(session.GetStats().steps == tick && state.frame_id == tick,
              "exactly one runtime physics tick per sample")) return false;
      Toon::FrameSnapshot direct;
      if (!checked(adapter.Apply(held,baseline,1,direct,error))) return false;
      const auto hydra = host.Sync();
      if (!compare(direct,hydra)) return false;
      std::vector<Toon::ToonMaterial> current;
      for (std::size_t i = 0; i < direct.materials.size(); ++i) {
        current.push_back(direct.materials[i].material);
        changed |= CanonicalMaterial(current.back()) != baseline.materials[i].material;
        if (tick == 240 && !Check(current.back() == baseline.materials[i].material,
                "zero channels must restore canonical inputs and tint identities")) return false;
      }
      if (!generation) replay.push_back(current);
      else if (!Check(current == replay[tick-1],"exact material reset replay")) return false;
      if (!generation && tick == 60) {
        if (!checked(first.Reset(api,session.Snapshot(),error))) return false;
        for (uint32_t i = 0; i < state.material_count; ++i)
          retained.insert(retained.end(),state.materials[i].value,state.materials[i].value+4);
        session.Pause(); session.Advance(10);
        if (!Check(session.GetStats().steps == tick,"pause holds material/physics tick")) return false;
        if (!compare(direct,host.Sync())) return false;
        session.Play();
      }
      if (!shaders.empty() && (tick == 1 || tick == 60 || tick == 120 || tick == 240)) {
        const auto a = materialFrame(direct), b = materialFrame(hydra);
        if (!Gpu(a,b,shaders)) return false; // exact colour and depth, 1x/4x
        for (unsigned i = 0; i < 2; ++i) {
          const auto image = pixels(a,i ? 4 : 1);
          changed_pixels[i] |= image != base_pixels[i];
          if (!generation) image_replay[i].push_back(image);
          else if (!Check(image == image_replay[i][(tick == 1 ? 0 : tick == 60 ? 1 : tick == 120 ? 2 : 3)],
                  "exact reset material pixels")) return false;
          if (tick == 240 && !Check(image == base_pixels[i],"zero material pixels restore baseline")) return false;
        }
      }
    }
    session.Reset();
    Toon::FrameSnapshot released;
    if (!checked(adapter.Release(baseline,1,released,error)) || !compare(released,host.Sync())) return false;
    // Reset advances runtime generation; rebind the new immutable layout.
    if (!checked(adapter.Bind(session.Baseline(),baseline,1,match.bindings,error))) return false;
    std::string after;
    stage->GetSessionLayer()->ExportToString(&after);
    if (!Check(after == session_layer,"reset removes USD material output")) return false;
  }
  session.Close();
  std::string after;
  stage->GetRootLayer()->ExportToString(&after);
  if (!Check(after == root,"source material layer unchanged")) return false;
  stage->GetSessionLayer()->ExportToString(&after);
  if (!Check(after == session_layer,"close restores session layer")) return false;
  std::vector<double> retained_after;
  for (uint32_t i = 0; i < first.View()->material_count; ++i)
    retained_after.insert(retained_after.end(),first.View()->materials[i].value,first.View()->materials[i].value+4);
  if (!Check(retained_after == retained,"retained materials survive reset/close") ||
      !Check(changed && (shaders.empty() || (changed_pixels[0] && changed_pixels[1])),
          "canonical morphs must change material values and both sample-count images")) return false;
  std::cout << "MMD canonical material direct/USD-Hydra parity: 720 ticks, 3 generations, "
            << "pause/reset/close, immutable bases; "
            << (shaders.empty() ? "CPU values" : "exact 1x/4x colour/depth with visible morph changes") << '\n';
  return true;
}
