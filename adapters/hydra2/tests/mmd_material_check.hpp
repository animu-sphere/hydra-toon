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

bool RuntimeMmdMaterials(const char* pmx, const char* vmd, const std::string& shaders, bool combined = false) {
  const auto stage = UsdStage::Open(pmx);
  if (!Check(bool(stage), "open generated material PMX")) return false;
  std::string root, session_layer;
  stage->GetRootLayer()->ExportToString(&root);
  stage->GetSessionLayer()->ExportToString(&session_layer);
  Host host(stage);
  const auto sync = [&] {
    auto frame = host.Sync();
    // UsdSkelImaging also supplies an animated skeleton diagnostic mesh. It
    // has no runtime resident binding; compare the avatar's skinned surface.
    if (combined) std::erase_if(frame.meshes, [](const auto& mesh) { return !Toon::IsSkinned(mesh); });
    return frame;
  };
  const auto baseline = sync();
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
  if (combined && !Check(baseline.meshes.size() == 1 && session.Baseline().joint_count == 2 &&
          session.Baseline().blend_shape_count == 1 && session.Baseline().vertex_uv_count == 4 &&
          match.bindings.skins.size() == 1 && match.bindings.morphs.size() == 1 &&
          match.bindings.uvs.size() == 1, "combined automatic skin/morph/UV binding required")) return false;

  // Material-only mode holds both geometries at rest. Combined mode preserves
  // each independent path's evaluated streams and original material subsets.
  auto materialFrame = [&](Toon::FrameSnapshot value) {
    if (!combined) value.meshes = baseline.meshes;
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
  auto geometry = [&](const Toon::FrameSnapshot& a, const Toon::FrameSnapshot& b, float tolerance) {
    if (!Check(a.meshes.size() == b.meshes.size(), "combined mesh count parity")) return false;
    for (std::size_t i = 0; i < a.meshes.size(); ++i) {
      const auto& x = a.meshes[i]; const auto& y = b.meshes[i];
      const auto stream = [&](const char* name, const auto& left, const auto& right) {
        if (left == right) return true;
        std::cerr << "mesh " << i << " stream " << name << " differs\n";
        return false;
      };
      if (!Check(x.id == y.id && x.transform == y.transform && x.geom_bind == y.geom_bind &&
              x.skeleton_to_mesh == y.skeleton_to_mesh && x.visible == y.visible &&
              x.material == y.material && *x.parts == *y.parts &&
              stream("points",*x.points,*y.points) && stream("indices",*x.indices,*y.indices) &&
              stream("normals",*x.normals,*y.normals) && stream("UV",*x.uvs,*y.uvs) && stream("tangents",*x.tangents,*y.tangents) &&
              stream("skin",*x.influences,*y.influences) && stream("morph ranges",*x.morph_ranges,*y.morph_ranges) &&
              stream("morph offsets",*x.morph_offsets,*y.morph_offsets) && stream("morph weights",*x.morph_weights,*y.morph_weights) &&
              stream("edge scales",*x.edge_scales,*y.edge_scales) && stream("additional UV",*x.sub_texture_uvs,*y.sub_texture_uvs) &&
              x.joints->size() == y.joints->size(), "combined geometry streams differ")) return false;
      for (std::size_t j = 0; j < x.joints->size(); ++j) for (unsigned k = 0; k < 16; ++k)
        if (!Check(std::abs((*x.joints)[j].m[k] - (*y.joints)[j].m[k]) <= tolerance,
                "combined joint palette differs")) return false;
    }
    return true;
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
    return !combined || geometry(direct,hydra,2e-6F);
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
  std::vector<Toon::FrameSnapshot> geometry_replay;
  std::vector<std::vector<std::uint8_t>> image_replay[2];
  std::vector<std::uint8_t> base_pixels[2];
  if (!shaders.empty()) {
    const auto frame = materialFrame(baseline);
    for (unsigned i = 0; i < 2; ++i) base_pixels[i] = pixels(frame,i ? 4 : 1);
  }
  bool changed = false, changed_pixels[2]{}, changed_skin = false, changed_morph = false, changed_uv = false;
  Toon::RetainedAvatarSnapshot first;
  const auto stateValues = [](const ArStateView& state) {
    std::vector<double> values;
    for (uint32_t i = 0; i < state.joint_count; ++i) {
      const auto& joint = state.joints[i].local;
      values.insert(values.end(),joint.translation,joint.translation+3);
      values.insert(values.end(),joint.rotation,joint.rotation+4);
      values.insert(values.end(),joint.scale,joint.scale+3);
    }
    for (uint32_t i = 0; i < state.blend_shape_count; ++i) values.push_back(state.blend_shapes[i].weight);
    for (uint32_t i = 0; i < state.vertex_uv_count; ++i)
      values.insert(values.end(),state.vertex_uvs[i].value,state.vertex_uvs[i].value+2);
    for (uint32_t i = 0; i < state.material_count; ++i)
      values.insert(values.end(),state.materials[i].value,state.materials[i].value+4);
    return values;
  };
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
      const auto hydra = sync();
      if (!compare(direct,hydra)) return false;
      if (combined) {
        if (!generation) geometry_replay.push_back(direct);
        else if (!geometry(direct,geometry_replay[tick-1],0)) return false;
        for (std::size_t i = 0; i < direct.meshes.size(); ++i) {
          const auto& m = direct.meshes[i]; const auto& base = baseline.meshes[i];
          changed_skin |= *m.joints != *base.joints;
          changed_morph |= *m.morph_weights != *base.morph_weights;
          changed_uv |= *m.uvs != *base.uvs;
          if (tick == 240 && !Check(*m.morph_weights == *base.morph_weights && *m.uvs == *base.uvs &&
                  *m.tangents == *base.tangents, "zero channels restore morph/UV/tangent bases")) return false;
        }
      }
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
        retained = stateValues(state);
        session.Pause(); session.Advance(10);
        if (!Check(session.GetStats().steps == tick,"pause holds material/physics tick")) return false;
        if (!compare(direct,sync())) return false;
        session.Play();
      }
      if (!shaders.empty() && (tick == 1 || tick == 60 || tick == 120 || tick == 240)) {
        const auto a = materialFrame(direct), b = materialFrame(hydra);
        if (!Gpu(a,b,shaders)) return false; // exact colour and depth, 1x/4x
        for (unsigned i = 0; i < 2; ++i) {
          const auto image = pixels(a,i ? 4 : 1);
          if (combined && !generation && tick == 60) {
            // Each independently omitted output must change the rendered result.
            for (unsigned omitted = 0; omitted < 4; ++omitted) {
              auto control = a;
              if (omitted == 3) control.materials = baseline.materials;
              else for (std::size_t m = 0; m < control.meshes.size(); ++m) {
                auto& mesh = control.meshes[m]; const auto& base = baseline.meshes[m];
                if (omitted == 0) mesh.joints = base.joints;
                if (omitted == 1) mesh.morph_weights = base.morph_weights;
                if (omitted == 2) { mesh.uvs = base.uvs; mesh.tangents = base.tangents; }
              }
              if (!Check(pixels(control,i ? 4 : 1) != image,
                      "omitting skin/morph/UV/material must change combined pixels")) return false;
            }
          }
          changed_pixels[i] |= image != base_pixels[i];
          if (!generation) image_replay[i].push_back(image);
          else if (!Check(image == image_replay[i][(tick == 1 ? 0 : tick == 60 ? 1 : tick == 120 ? 2 : 3)],
                  "exact reset material pixels")) return false;
          if (!combined && tick == 240 && !Check(image == base_pixels[i],"zero material pixels restore baseline")) return false;
        }
      }
    }
    session.Reset();
    Toon::FrameSnapshot released;
    if (!checked(adapter.Release(baseline,1,released,error)) || !compare(released,sync())) return false;
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
  if (!Check(stateValues(*first.View()) == retained,"retained pose/morph/UV/material values survive reset/close") ||
      !Check(!combined || (changed_skin && changed_morph && changed_uv),
          "combined skin, vertex morph and UV must all change") ||
      !Check(changed && (shaders.empty() || (changed_pixels[0] && changed_pixels[1])),
          "canonical morphs must change material values and both sample-count images")) return false;
  std::cout << (combined ? "MMD combined pose/morph/UV/physics/material" : "MMD canonical material")
            << " direct/USD-Hydra parity: 720 ticks, 3 generations, "
            << "pause/reset/close, immutable bases; "
            << (shaders.empty() ? "CPU values" : "exact 1x/4x colour/depth with visible morph changes") << '\n';
  return true;
}
