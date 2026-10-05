#if AVATAR_COMPONENT
#include <toon/fast/avatar_state.hpp>
int main() {
  Toon::RetainedAvatarSnapshot state;
  Toon::AvatarStateAdapter adapter;
  adapter.Clear();
  return state.View() || adapter.IdentityInfo().active ? 1 : 0;
}
#else
#include <toon/render_world.hpp>
int main() {
  Toon::RenderWorld world;
  return world.Commit().meshes.empty() ? 0 : 1;
}
#endif
