#include "avatar_runtime.hpp"
#include "hydra_scene.hpp"
#include <iostream>
#include <stdexcept>

using namespace Toon;
using namespace Toon::viewport;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> bool same(const T& a, const T& b) { return bool(a) == bool(b) && (!a || *a == *b); }
void equal(const FrameSnapshot& a, const FrameSnapshot& b) {
  require(a.materials.size() == b.materials.size(), "material count parity");
  for (std::size_t i = 0; i < a.materials.size(); ++i)
    require(a.materials[i].material == b.materials[i].material &&
        a.materials[i].parameters_revision == b.materials[i].parameters_revision, "complete material/revision parity");
  require(a.time_seconds == b.time_seconds && a.meshes.size() == b.meshes.size(), "frame identity");
  for (std::size_t i = 0; i < a.meshes.size(); ++i) {
    const auto& x = a.meshes[i]; const auto& y = b.meshes[i];
    require(same(x.joints,y.joints) && same(x.morph_weights,y.morph_weights) && same(x.uvs,y.uvs), "joint/morph/UV parity");
    require(same(x.points,y.points) && same(x.normals,y.normals) && same(x.tangents,y.tangents), "geometry/tangent parity");
    require(x.points_revision == y.points_revision && x.uvs_revision == y.uvs_revision &&
        x.pose_revision == y.pose_revision && x.morph_weights_revision == y.morph_weights_revision, "upload revision parity");
  }
}
}
int main(int argc, char** argv) {
  try {
    require(argc == 3 || argc == 4, "generated PMX/VMD required");
    const bool materials = argc == 4 && std::string(argv[3]) == "--materials";
    auto scene = HydraScene::Open(argv[1]); FrameSnapshot base; scene->Update(base);
    AvatarRuntimeOptions options; options.motion = argv[2];
    auto cached = AvatarRuntime::Start(*scene,base,1,options);
    options.cache = false;
    auto oracle = AvatarRuntime::Start(*scene,base,1,options);
    std::vector<std::vector<ToonMaterial>> materialReplay;
    bool changedMaterial = false;
    std::string error;
    for (int generation = 0; generation < 2; ++generation) {
      cached->Control(AvatarRuntime::Action::Play); oracle->Control(AvatarRuntime::Action::Play);
      for (int tick = 0; tick < (materials ? 240 : 120); ++tick) {
        cached->Tick(.0083334); oracle->Tick(.0083334);
        for (bool late : {false,true}) {
          auto a = base, b = base;
          require(cached->Apply(a,late,error) && oracle->Apply(b,late,error), error.c_str()); equal(a,b);
          if (materials && !late) {
            require(cached->status().material_inputs == 24 && cached->status().materials == 24,"complete resident material binding");
            std::vector<ToonMaterial> current;
            for (std::size_t i = 0; i < a.materials.size(); ++i) {
              current.push_back(a.materials[i].material);
              changedMaterial |= a.materials[i].material != base.materials[i].material;
            }
            if (!generation) materialReplay.push_back(current);
            else require(current == materialReplay[tick],"exact material reset replay");
            if (tick == 239)
              for (std::size_t i = 0; i < a.materials.size(); ++i)
                require(a.materials[i].material == base.materials[i].material,"material zero restoration");
          }
        }
      }
      cached->Control(AvatarRuntime::Action::Pause); oracle->Control(AvatarRuntime::Action::Pause);
      const auto held = cached->status().frame;
      cached->Tick(10); oracle->Tick(10); require(cached->status().frame == held, "pause");
      // Any renderer baseline commit forces a fresh conversion; camera input survives reuse.
      ++base.revision; base.inputs.camera = 123;
      auto a = base, b = base;
      require(cached->Apply(a,false,error) && oracle->Apply(b,false,error), error.c_str()); equal(a,b);
      require(a.inputs.camera == 123, "camera input");
      cached->Control(AvatarRuntime::Action::Reset); oracle->Control(AvatarRuntime::Action::Reset);
    }
    require(cached->Report().find("refused=0") != std::string::npos, "no refusal");
    require(!materials || changedMaterial,"material morph did not change resident values");
    cached->Control(AvatarRuntime::Action::Stop); oracle->Control(AvatarRuntime::Action::Stop);
    require(cached->status().bodies == 0 && cached->status().stopped, "stop teardown");
    cached->Control(AvatarRuntime::Action::Play); cached->Tick(.0083334);
    require(cached->status().frame == 1 && cached->status().bodies > 0, "restart");
    std::cout << "MMD resident cache: exact uncached joint/morph/UV/tangent/revision parity across reset and baseline changes\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
