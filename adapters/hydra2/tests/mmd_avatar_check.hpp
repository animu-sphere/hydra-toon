// SPDX-License-Identifier: Apache-2.0
// Included after the shared Hydra host and GPU comparison helpers.
bool RuntimeMmdAvatar(const std::string& shaders) {
  const auto stage = UsdStage::CreateInMemory();
  if (!stage->GetRootLayer()->ImportFromString(R"USD(#usda 1.0
(
 upAxis = "Y"
 metersPerUnit = 1
)
def SkelRoot "Avatar" {
 def Skeleton "Rig" (prepend apiSchemas = ["SkelBindingAPI"]) {
  uniform token[] joints = ["root", "root/dynamic"]
  uniform matrix4d[] bindTransforms = [
   ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
   ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))]
  uniform matrix4d[] restTransforms = [
   ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
   ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))]
  rel skel:animationSource = </Avatar/Anim>
 }
 def SkelAnimation "Anim" {
  uniform token[] joints = ["root", "root/dynamic"]
  float3[] translations = [(0,0,0),(0,0,0)]
  quatf[] rotations = [(1,0,0,0),(1,0,0,0)]
  half3[] scales = [(1,1,1),(1,1,1)]
  uniform token[] blendShapes = ["lift"]
  float[] blendShapeWeights = [0]
 }
 def Mesh "Body" (prepend apiSchemas = ["SkelBindingAPI"]) {
  uniform token subdivisionScheme = "none"
  point3f[] points = [(-0.6,-0.6,0), (0.6,-0.6,0), (0.6,0.6,0), (-0.6,0.6,0)]
  normal3f[] primvars:normals = [(0,0,1),(0,0,1),(0,0,1),(0,0,1)] (interpolation = "vertex")
  int[] faceVertexCounts = [3,3]
  int[] faceVertexIndices = [0,1,2,0,2,3]
  texCoord2f[] primvars:st = [(0,0),(1,0),(1,1),(0,1)] (interpolation = "vertex")
  float[] primvars:mmd:edgeScale = [1,2,3,4] (interpolation = "vertex")
  float4[] primvars:mmd:uv1 = [(0,0,0,0),(1,1,0,0),(2,2,0,0),(3,3,0,0)] (interpolation = "vertex")
  uniform token[] skel:joints = ["root/dynamic", "root"]
  int[] primvars:skel:jointIndices = [1,1,0,0] (
   elementSize = 1
   interpolation = "vertex"
  )
  float[] primvars:skel:jointWeights = [1,1,1,1] (
   elementSize = 1
   interpolation = "vertex"
  )
  matrix4d primvars:skel:geomBindTransform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))
  rel skel:skeleton = </Avatar/Rig>
  uniform token[] skel:blendShapes = ["lift"]
  rel skel:blendShapeTargets = [</Avatar/Body/lift>]
  def BlendShape "lift" {
   uniform vector3f[] offsets = [(0.12,0.05,0.12)]
   uniform int[] pointIndices = [2]
  }
 }
}
)USD")) return false;
  Host host(stage);
  const auto baseline = host.Sync();
  auto provider = std::make_unique<MmdAvatarProvider>();
  const auto api = provider->api;
  auto mesh = [](const Toon::FrameSnapshot& frame) -> const Toon::MeshSnapshot* {
    for (const auto& m : frame.meshes) if (Toon::IsSkinned(m)) return &m;
    return nullptr;
  };
  const auto* base = mesh(baseline);
  if (!Check(base && Toon::IsMorphed(*base) && base->tangents->size() == 4,
          "MMD avatar baseline must expose a GPU skin, vertex morph and rest tangents")) return false;
  const auto anim = stage->GetPrimAtPath(SdfPath("/Avatar/Anim"));
  auto st = UsdGeomPrimvarsAPI(stage->GetPrimAtPath(SdfPath(MmdAvatarProvider::mesh))).GetPrimvar(TfToken("st"));
  const std::vector<VtVec2fArray> uvs{
      {{0,0},{1,0},{1,1},{0,1}}, {{0,0},{1,0},{0,1},{1,1}},
      {{0,0},{1,0},{-.5f,1},{1.5f,1}}, {{0,0},{1,0},{1.5f,1},{-.5f,1}},
      VtVec2fArray(4,GfVec2f(0)), {{0,0},{1,0},{1,1},{0,1}}};
  Toon::AvatarStateAdapter adapter;
  Toon::RetainedAvatarSnapshot first;
  Toon::FrameSnapshot first_output;
  std::string error;
  std::vector<std::vector<std::uint8_t>> pixels[2];
  std::vector<Toon::Matrix4> first_palette;
  std::vector<usd_physics::core::Transform> body_frames;
  double first_body_y = 0;
  double previous_y = 0;
  for (std::size_t iteration = 0; iteration < 3 * uvs.size(); ++iteration) {
    const auto frame = iteration % uvs.size();
    if (iteration && !frame) provider->Reset(iteration / uvs.size() + 1);
    const auto produced = provider->Advance();
    Toon::RetainedAvatarSnapshot held;
    const bool retained = held.Reset(api, produced, error);
    if (!iteration && !Check(first.Reset(api, produced, error), error.c_str())) return false;
    api.release_snapshot(produced);
    if (!Check(retained, error.c_str())) return false;
    const auto& state = *held.View();
    if (!iteration) {
      const auto match = HdToonMatchAvatarTargets(state, host.delegate.DescribeResidentTargets(), {});
      for (const auto& mismatch : match.mismatches)
        std::cerr << mismatch.subject << ": " << mismatch.reason << '\n';
      if (!Check(match.mismatches.empty() && match.bindings.skins.size() == 1 &&
                     match.bindings.morphs.size() == 1 && match.bindings.uvs.size() == 1 &&
                     match.bindings.skins[0].joints == std::vector<std::uint32_t>{1,0} &&
                     adapter.Bind(state, baseline, 1, match.bindings, error),
              "MMD avatar automatic skin/morph/UV binding failed")) {
        std::cerr << "skins=" << match.bindings.skins.size() << " morphs=" << match.bindings.morphs.size()
                  << " uvs=" << match.bindings.uvs.size() << " " << error << '\n';
        return false;
      }
    }
    const auto body = provider->BodyTransform();
    if (!iteration) first_body_y = body.translation.y;
    if (iteration < uvs.size()) body_frames.push_back(body);
    else if (!Check(body == body_frames[frame], "reset must reproduce each completed solver tick")) return false;
    const double root = float(MmdAvatarProvider::rootX[frame]);
    if (!Check(state.frame_id == frame + 1 && state.generation == iteration / uvs.size() + 1 &&
                   state.joint_count == 2 && state.blend_shape_count == 1 && state.vertex_uv_count == 4 &&
                   state.vertex_uvs[0].vertex_index == 2 &&
                   std::abs(state.joints[0].local.translation[0] - root) < 1e-6 &&
                   std::abs(state.joints[1].local.translation[0] + root - body.translation.x) < 1e-6 &&
                   std::abs(state.joints[1].local.translation[1] - body.translation.y) < 1e-6 &&
                   state.blend_shapes[0].weight == MmdAvatarProvider::weights[frame] &&
                   body.translation.x > 0 && body.translation.y < 0 &&
                   (!frame || body.translation.y < previous_y),
            "final MMD joints/morph must combine keyed control with this tick's Jolt feedback")) return false;
    previous_y = body.translation.y;
    for (uint32_t i = 0; i < state.vertex_uv_count; ++i) {
      const auto& value = state.vertex_uvs[i];
      const auto expected = uvs[frame][value.vertex_index];
      if (!Check(std::abs(value.value[0] - expected[0]) < 1e-6 &&
                     std::abs(value.value[1] - expected[1]) < 1e-6,
              "composed avatar UVs differ from independent group-weight values")) return false;
    }
    Toon::FrameSnapshot actual;
    if (!Check(adapter.Apply(held, baseline, 1, actual, error), error.c_str())) return false;
    if (!iteration) {
      first_output = actual;
      first_palette = *mesh(actual)->joints;
    }
    Toon::FrameSnapshot duplicate;
    if (!Check(adapter.Apply(held, baseline, 1, duplicate, error) &&
                   mesh(duplicate)->joints == mesh(actual)->joints &&
                   mesh(duplicate)->morph_weights == mesh(actual)->morph_weights &&
                   mesh(duplicate)->uvs == mesh(actual)->uvs &&
                   mesh(duplicate)->tangents == mesh(actual)->tangents,
            "duplicate avatar snapshot must reuse pose/morph/UV/tangent arrays")) return false;
    // Independent oracle inputs: keyed root values and direct solver readback,
    // not the runtime's final joints or renderer-produced palette.
    anim.GetAttribute(TfToken("translations")).Set(VtVec3fArray{
        GfVec3f(float(root),0,0), GfVec3f(float(body.translation.x-root),float(body.translation.y),0)});
    anim.GetAttribute(TfToken("blendShapeWeights")).Set(VtFloatArray{MmdAvatarProvider::weights[frame]});
    st.Set(uvs[frame]);
    auto expected = host.Sync();
    const auto* a = mesh(actual); const auto* b = mesh(expected);
    if (!Check(a && b && *a->points == *b->points && *a->indices == *b->indices &&
                   *a->normals == *b->normals && *a->uvs == *b->uvs && *a->tangents == *b->tangents &&
                   *a->influences == *b->influences && *a->morph_ranges == *b->morph_ranges &&
                   *a->morph_offsets == *b->morph_offsets && *a->morph_weights == *b->morph_weights &&
                   *a->edge_scales == *b->edge_scales && *a->sub_texture_uvs == *b->sub_texture_uvs &&
                   a->joints->size() == b->joints->size(),
            "combined MMD avatar streams differ from USD/Hydra")) return false;
    for (size_t i = 0; i < a->joints->size(); ++i)
      for (size_t j = 0; j < 16; ++j)
        if (!Check(std::abs((*a->joints)[i].m[j] - (*b->joints)[i].m[j]) < 2e-6,
                "MMD final palette differs from usdSkelImaging")) return false;
    if (!shaders.empty()) {
      Toon::RenderWorld resources;
      Toon::ToonTexture colors;
      colors.width = colors.height = 2;
      colors.encoding = Toon::ToonTextureEncoding::Linear;
      colors.pixels = std::make_shared<const std::vector<std::uint8_t>>(
          std::vector<std::uint8_t>{255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255});
      const auto color_tex = resources.CreateTexture(); resources.SetTexture(color_tex, colors);
      Toon::ToonTexture normal;
      normal.width = normal.height = 1; normal.encoding = Toon::ToonTextureEncoding::Linear;
      normal.pixels = std::make_shared<const std::vector<std::uint8_t>>(
          std::vector<std::uint8_t>{191,230,204,255});
      const auto normal_tex = resources.CreateTexture(); resources.SetTexture(normal_tex, normal);
      for (int family = 0; family < 2; ++family) {
        Toon::ToonMaterial material;
        material.model = family ? Toon::ToonShadingModel::MMD : Toon::ToonShadingModel::MToon;
        material.base_texture.texture = color_tex; material.normal_texture.texture = normal_tex;
        material.mtoon.shading_toony = 0;
        const auto mat = resources.CreateMaterial(); resources.SetMaterial(mat, material);
        const auto resource = resources.Commit();
        actual.materials = expected.materials = resource.materials;
        actual.textures = expected.textures = resource.textures;
        for (auto* output : {&actual, &expected}) for (auto& m : output->meshes) {
          m.material = mat;
          m.parts = std::make_shared<const std::vector<Toon::MeshPart>>(
              std::vector<Toon::MeshPart>{{0,uint32_t(m.indices->size()),mat}});
        }
        std::vector<std::uint8_t> image;
        if (!Gpu(actual, expected, shaders, 0, &image)) return false;
        if (iteration == 1 && family == 0) {
          // Negative controls prove both deformation streams affect the draw,
          // even while UVs and the other stream remain at the evaluated value.
          for (bool omit_skin : {false, true}) {
            auto control = actual;
            for (auto& m : control.meshes) if (Toon::IsSkinned(m)) {
              if (omit_skin) m.joints = base->joints;
              else m.morph_weights = base->morph_weights;
            }
            std::vector<std::uint8_t> control_image;
            if (!Gpu(control, control, shaders, 0, &control_image) ||
                !Check(image != control_image, "omitting skin or vertex morph must change composed pixels")) return false;
          }
        }
        if (iteration < uvs.size()) pixels[family].push_back(image);
        else if (!Check(image == pixels[family][frame],
                "world reset must reproduce composed avatar pixels")) return false;
      }
    }
  }
  if (!shaders.empty()) for (const auto& family : pixels)
    if (!Check(family[0] != family[1] && family[0] != family[5],
            "combined deformation and ongoing physics must change visible pixels")) return false;
  provider.reset();
  if (!Check(first.View()->generation == 1 && first.View()->joint_count == 2 &&
                 first.View()->joints[1].local.translation[1] == first_body_y &&
                 *mesh(first_output)->joints == first_palette && mesh(first_output)->uvs == base->uvs,
          "retained composed avatar state must survive producer destruction")) return false;
  Toon::FrameSnapshot released;
  if (!Check(adapter.Release(baseline, 1, released, error) &&
                 *mesh(released)->joints == *base->joints && *mesh(released)->morph_weights == *base->morph_weights &&
                 *mesh(released)->uvs == *base->uvs && *mesh(released)->tangents == *base->tangents,
          "avatar release must restore all immutable resident bases")) return false;
  std::cout << "MMD/Jolt resident skin + vertex morph + UV: three generations, "
            << (shaders.empty() ? "CPU streams" : "1x/4x Vulkan colour/depth") << " parity passed\n";
  return true;
}
