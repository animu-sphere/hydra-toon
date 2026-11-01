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
  require(a.meshes.size() == b.meshes.size(), "resident mesh count");
  for (std::size_t i = 0; i < a.meshes.size(); ++i) {
    const auto& x = a.meshes[i]; const auto& y = b.meshes[i];
    require(same(x.joints,y.joints) && same(x.morph_weights,y.morph_weights), "resident joint/morph values");
    require(same(x.points,y.points) && same(x.normals,y.normals) && same(x.tangents,y.tangents), "geometry preserved");
    require(x.pose_revision == y.pose_revision, "pose revision parity");
  }
}
bool different(const FrameSnapshot& a, const FrameSnapshot& b) {
  for (std::size_t i = 0; i < a.meshes.size(); ++i)
    if (!same(a.meshes[i].joints,b.meshes[i].joints)) return true;
  return false;
}
bool morphsDiffer(const FrameSnapshot& a, const FrameSnapshot& b) {
  for (std::size_t i = 0; i < a.meshes.size(); ++i)
    if (!same(a.meshes[i].morph_weights,b.meshes[i].morph_weights)) return true;
  return false;
}
bool materialsDiffer(const FrameSnapshot& a, const FrameSnapshot& b) {
  for (std::size_t i = 0; i < a.materials.size(); ++i)
    if (a.materials[i].material != b.materials[i].material) return true;
  return false;
}
}
int main(int argc, char** argv) {
  try {
    require(argc == 3, "VRM avatar and clip required");
    auto scene = HydraScene::Open(argv[1]); FrameSnapshot base; scene->Update(base);
    AvatarRuntimeOptions options; options.motion = argv[2]; options.loop = false;
    auto cached = AvatarRuntime::Start(*scene,base,1,options);
    options.cache = false; auto oracle = AvatarRuntime::Start(*scene,base,1,options);
    options.secondary_motion = false; auto motion = AvatarRuntime::Start(*scene,base,1,options);
    FrameSnapshot firstRun;
    bool secondaryReachedSkin = false;
    std::string error;
    for (int generation = 0; generation < 2; ++generation) {
      cached->Control(AvatarRuntime::Action::Play); oracle->Control(AvatarRuntime::Action::Play); motion->Control(AvatarRuntime::Action::Play);
      for (int tick = 0; tick < 120; ++tick) {
        cached->Tick(1./60); oracle->Tick(1./60); motion->Tick(1./60);
        const auto beforeDelivery = cached->status().physics_steps;
        for (bool late : {false,true}) {
          auto a = base, b = base, m = base;
          require(cached->Apply(a,late,error) && oracle->Apply(b,late,error) && motion->Apply(m,late,error), error.c_str());
          equal(a,b); secondaryReachedSkin = secondaryReachedSkin || different(a,m);
          if (tick == 119 && !late) {
            if (!generation) firstRun = a;
            else equal(firstRun,a);
          }
        }
        require(cached->status().physics_steps == beforeDelivery, "late reads advanced solver");
        require(cached->status().frame == cached->status().physics_steps + 1, "seed/tick identity");
      }
      cached->Control(AvatarRuntime::Action::Pause); const auto held = cached->status().frame;
      cached->Tick(100); require(cached->status().frame == held, "pause advanced solver");
      ++base.revision; base.inputs.camera = 123;
      auto a = base, b = base;
      require(cached->Apply(a,false,error) && oracle->Apply(b,false,error), error.c_str()); equal(a,b);
      require(a.inputs.camera == 123, "camera lost during cached conversion");
      cached->Control(AvatarRuntime::Action::Reset); oracle->Control(AvatarRuntime::Action::Reset); motion->Control(AvatarRuntime::Action::Reset);
      require(cached->status().physics_steps == 0 && !cached->status().playing, "reset did not seed/pause");
    }
    require(secondaryReachedSkin, "springs did not change resident skin palettes");
    require(cached->status().refused == 0 && cached->status().mismatches.empty(), "runtime refusal/mismatch");
    cached->Control(AvatarRuntime::Action::Stop); require(cached->status().stopped, "stop");
    cached->Control(AvatarRuntime::Action::Play); cached->Tick(1./60);
    require(cached->status().physics_steps == 1, "restart");
    options.cache = true; options.secondary_motion = true; options.loop = true;
    auto looping = AvatarRuntime::Start(*scene,base,2,options); looping->Control(AvatarRuntime::Action::Play);
    for (int i = 0; i < 2000 && !looping->status().resets; ++i) looping->Tick(.1);
    require(looping->status().resets > 0 && looping->status().refused == 0, "loop reset");
    std::cout << "VRM resident skin: springs differ from motion-only; exact cached/uncached and reset replay; pause, late delivery, stop/restart and loop passed\n";
    // Expressions and gaze selected in the session reach resident morphs, materials
    // and eye palettes, with the same replay and without another evaluation.
    options.loop = false; options.secondary_motion = true;
    options.face = true; options.cache = true; auto face = AvatarRuntime::Start(*scene,base,3,options);
    options.cache = false; auto faceOracle = AvatarRuntime::Start(*scene,base,3,options);
    options.face = false; auto body = AvatarRuntime::Start(*scene,base,3,options);
    require(face->status().expressions > 0 && face->status().morphs == face->status().shapes &&
        face->status().materials == face->status().material_inputs && face->status().mismatches.empty(), "face binding");
    bool morphed = false, coloured = false, gazed = false;
    FrameSnapshot faceRun;
    for (int generation = 0; generation < 2; ++generation) {
      face->Control(AvatarRuntime::Action::Play); faceOracle->Control(AvatarRuntime::Action::Play); body->Control(AvatarRuntime::Action::Play);
      for (int tick = 0; tick < 120; ++tick) {
        face->Tick(1./60); faceOracle->Tick(1./60); body->Tick(1./60);
        auto a = base, b = base, m = base;
        require(face->Apply(a,false,error) && faceOracle->Apply(b,false,error) && body->Apply(m,false,error), error.c_str());
        equal(a,b); require(!materialsDiffer(a,b), "cached/uncached materials");
        require(!morphsDiffer(m,base), "morphs without the face");
        morphed = morphed || morphsDiffer(a,base); coloured = coloured || materialsDiffer(a,base); gazed = gazed || different(a,m);
        if (tick == 119) {
          if (!generation) faceRun = a;
          else { equal(faceRun,a); require(!materialsDiffer(faceRun,a), "face reset replay"); }
        }
      }
      face->Control(AvatarRuntime::Action::Reset); faceOracle->Control(AvatarRuntime::Action::Reset); body->Control(AvatarRuntime::Action::Reset);
    }
    require(morphed, "expressions did not change resident morph weights");
    require(gazed, "the face did not change resident skin palettes");
    require(coloured || face->status().materials == 0, "expressions did not change resident materials");
    require(face->status().refused == 0, "face refusal");
    std::cout << "VRM resident face: expressions=" << face->status().expressions << " morphs=" << face->status().morphs
              << " materials=" << face->status().materials << " coloured=" << coloured << "; exact cached/uncached and reset replay passed\n";
    face->Control(AvatarRuntime::Action::Stop);
    auto released = base; require(face->Apply(released,false,error) && !morphsDiffer(released,base) && !materialsDiffer(released,base), "stop restores the authored face");
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
