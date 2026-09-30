// SPDX-License-Identifier: Apache-2.0
// Dear ImGui as the viewport's overlay. ImGui is fed the window's events and
// laid out each frame; its draw data and textures are then copied into an
// OverlayDrawList, which the present session draws. The textures follow
// ImGui's protocol for a renderer with ImGuiBackendFlags_RendererHasTextures:
// a texture it asks to be created or updated is copied whole, with a new
// revision, and one it asks to be destroyed leaves the list. The session
// waits for the frame in flight before it releases a texture, so no frame
// still samples it.
#include "overlay.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Toon::viewport {
namespace {

static_assert(sizeof(ImDrawIdx) == sizeof(std::uint16_t),
    "the overlay's indices are 16-bit, as Dear ImGui's are by default");

// How often the numbers change, so they can be read.
constexpr double kRefreshSeconds = 0.25;
// The interval plot's width in frames.
constexpr std::size_t kPlotFrames = 240;

int ImGuiButton(PointerButton button) {
  switch (button) {
  case PointerButton::Left:
    return ImGuiMouseButton_Left;
  case PointerButton::Right:
    return ImGuiMouseButton_Right;
  case PointerButton::Middle:
    return ImGuiMouseButton_Middle;
  case PointerButton::None:
    break;
  }
  return -1;
}

OverlayTexture& TextureFor(OverlayDrawList& list, std::uint64_t id) {
  for (OverlayTexture& texture : list.textures) {
    if (texture.id == id) {
      return texture;
    }
  }
  OverlayTexture& texture = list.textures.emplace_back();
  texture.id = id;
  return texture;
}

// Copies the pixels of every texture ImGui asks to be created or updated,
// and drops those it asks to be destroyed.
void SyncTextures(ImDrawData& data, OverlayDrawList& list) {
  if (data.Textures == nullptr) {
    return;
  }
  for (ImTextureData* texture : *data.Textures) {
    // 0 is ImTextureID_Invalid.
    const auto id = static_cast<std::uint64_t>(texture->UniqueID) + 1U;
    switch (texture->Status) {
    case ImTextureStatus_WantCreate:
    case ImTextureStatus_WantUpdates: {
      OverlayTexture& copy = TextureFor(list, id);
      copy.width = static_cast<std::uint32_t>(texture->Width);
      copy.height = static_cast<std::uint32_t>(texture->Height);
      const std::size_t pixels =
          static_cast<std::size_t>(copy.width) * copy.height;
      copy.pixels.resize(pixels * 4U);
      const auto* source = static_cast<const std::uint8_t*>(
          texture->GetPixels());
      if (texture->Format == ImTextureFormat_RGBA32) {
        std::memcpy(copy.pixels.data(), source, copy.pixels.size());
      } else {
        // Alpha8: white, with the texture as alpha.
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
          copy.pixels[pixel * 4U + 0U] = 255;
          copy.pixels[pixel * 4U + 1U] = 255;
          copy.pixels[pixel * 4U + 2U] = 255;
          copy.pixels[pixel * 4U + 3U] = source[pixel];
        }
      }
      ++copy.revision;
      texture->SetTexID(static_cast<ImTextureID>(id));
      texture->SetStatus(ImTextureStatus_OK);
      break;
    }
    case ImTextureStatus_WantDestroy:
      // A texture no frame has drawn with since the one in flight.
      if (texture->UnusedFrames > 0) {
        std::erase_if(list.textures, [id](const OverlayTexture& entry) {
          return entry.id == id;
        });
        texture->SetTexID(ImTextureID_Invalid);
        texture->SetStatus(ImTextureStatus_Destroyed);
      }
      break;
    case ImTextureStatus_OK:
    case ImTextureStatus_Destroyed:
      break;
    }
  }
}

// ImGui's vertices, indices and commands, one list after another, in
// framebuffer pixels.
void CopyDraws(const ImDrawData& data, OverlayDrawList& list) {
  list.vertices.clear();
  list.indices.clear();
  list.commands.clear();
  const ImVec2 origin = data.DisplayPos;
  const ImVec2 scale = data.FramebufferScale;
  for (const ImDrawList* draw_list : data.CmdLists) {
    const auto first_vertex = static_cast<std::uint32_t>(list.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(list.indices.size());
    for (const ImDrawVert& vertex : draw_list->VtxBuffer) {
      list.vertices.push_back({(vertex.pos.x - origin.x) * scale.x,
          (vertex.pos.y - origin.y) * scale.y, vertex.uv.x, vertex.uv.y,
          vertex.col});
    }
    list.indices.insert(list.indices.end(), draw_list->IdxBuffer.begin(),
        draw_list->IdxBuffer.end());
    for (const ImDrawCmd& command : draw_list->CmdBuffer) {
      // The viewport adds no callbacks of its own.
      if (command.UserCallback != nullptr || command.ElemCount == 0) {
        continue;
      }
      OverlayCommand& copy = list.commands.emplace_back();
      copy.first_index = first_index + command.IdxOffset;
      copy.index_count = command.ElemCount;
      copy.vertex_offset = first_vertex + command.VtxOffset;
      copy.texture = static_cast<std::uint64_t>(command.GetTexID());
      copy.clip[0] = (command.ClipRect.x - origin.x) * scale.x;
      copy.clip[1] = (command.ClipRect.y - origin.y) * scale.y;
      copy.clip[2] = (command.ClipRect.z - origin.x) * scale.x;
      copy.clip[3] = (command.ClipRect.w - origin.y) * scale.y;
    }
  }
}

// A table of summaries, one row per series, in milliseconds.
void TimingTable(const char* id, const std::vector<FrameTelemetry::Named>& rows,
    const std::vector<Summary>& summaries) {
  if (!ImGui::BeginTable(id, 4,
          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    return;
  }
  ImGui::TableSetupColumn("ms");
  ImGui::TableSetupColumn("mean");
  ImGui::TableSetupColumn("p95");
  ImGui::TableSetupColumn("max");
  ImGui::TableHeadersRow();
  for (std::size_t row = 0; row < rows.size() && row < summaries.size();
      ++row) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(rows[row].name);
    ImGui::TableNextColumn();
    ImGui::Text("%7.3f", summaries[row].mean);
    ImGui::TableNextColumn();
    ImGui::Text("%7.3f", summaries[row].p95);
    ImGui::TableNextColumn();
    ImGui::Text("%7.3f", summaries[row].max);
  }
  ImGui::EndTable();
}

void CountRow(const char* name, unsigned long long frame,
    unsigned long long total) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(name);
  ImGui::TableNextColumn();
  ImGui::Text("%llu", frame);
  ImGui::TableNextColumn();
  ImGui::Text("%llu", total);
}

} // namespace

UploadCounts UploadCounts::Of(const PresentStatistics& statistics) {
  return {statistics.topology_uploads, statistics.point_uploads,
      statistics.material_writes, statistics.texture_uploads,
      statistics.skin_uploads, statistics.pose_writes,
      statistics.overlay_texture_uploads};
}

UploadCounts operator-(const UploadCounts& after, const UploadCounts& before) {
  return {after.topology - before.topology, after.points - before.points,
      after.materials - before.materials, after.textures - before.textures,
      after.skins - before.skins, after.poses - before.poses,
      after.overlay_textures - before.overlay_textures};
}

struct Overlay::State {
  ImGuiContext* context = nullptr;
  float scale = 1.0F;
  // The summaries on screen, refreshed every kRefreshSeconds.
  double since_refresh = kRefreshSeconds;
  std::vector<Summary> cpu;
  std::vector<Summary> gpu;
  std::vector<float> plot;
};

Overlay::Overlay(float content_scale) : state_(std::make_unique<State>()) {
  IMGUI_CHECKVERSION();
  state_->context = ImGui::CreateContext();
  state_->scale = content_scale;
  ImGuiIO& io = ImGui::GetIO();
  // Nothing is written next to the executable.
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.BackendPlatformName = "toon-viewport";
  io.BackendRendererName = "toon-overlay";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures |
                     ImGuiBackendFlags_RendererHasVtxOffset;
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes(content_scale);
  style.FontScaleDpi = content_scale;
}

Overlay::~Overlay() {
  ImGui::SetCurrentContext(state_->context);
  // The session releases the GPU copies itself.
  for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
    texture->SetTexID(ImTextureID_Invalid);
    texture->SetStatus(ImTextureStatus_Destroyed);
  }
  ImGui::DestroyContext(state_->context);
}

void Overlay::HandleEvent(const Event& event) {
  ImGui::SetCurrentContext(state_->context);
  ImGuiIO& io = ImGui::GetIO();
  switch (event.type) {
  case EventType::PointerMove:
    io.AddMousePosEvent(static_cast<float>(event.x),
        static_cast<float>(event.y));
    break;
  case EventType::PointerDown:
  case EventType::PointerUp:
    if (const int button = ImGuiButton(event.button); button >= 0) {
      io.AddMousePosEvent(static_cast<float>(event.x),
          static_cast<float>(event.y));
      io.AddMouseButtonEvent(button, event.type == EventType::PointerDown);
    }
    break;
  case EventType::Scroll:
    io.AddMouseWheelEvent(0.0F, static_cast<float>(event.y));
    break;
  default:
    break;
  }
}

bool Overlay::WantsPointer() const {
  ImGui::SetCurrentContext(state_->context);
  return ImGui::GetIO().WantCaptureMouse;
}

void Overlay::Hide(OverlayDrawList& list) {
  list.vertices.clear();
  list.indices.clear();
  list.commands.clear();
}

OverlayControls Overlay::Build(const OverlayFrame& frame, double delta,
    OverlayDrawList& list) {
  ImGui::SetCurrentContext(state_->context);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(frame.width),
      static_cast<float>(frame.height));
  io.DeltaTime = static_cast<float>(std::max(delta, 1e-4));

  const FrameTelemetry& telemetry = *frame.telemetry;
  const PresentStatistics& statistics = *frame.statistics;
  const auto cpu_rows = telemetry.Cpu();
  const auto gpu_rows = telemetry.Gpu();
  state_->since_refresh += delta;
  if (state_->since_refresh >= kRefreshSeconds) {
    state_->since_refresh = 0.0;
    state_->cpu.clear();
    for (const auto& row : cpu_rows) {
      state_->cpu.push_back(row.series->Summarize());
    }
    state_->gpu.clear();
    for (const auto& row : gpu_rows) {
      state_->gpu.push_back(row.series->Summarize());
    }
  }
  telemetry.interval().CopyInOrder(state_->plot);
  if (state_->plot.size() > kPlotFrames) {
    state_->plot.erase(state_->plot.begin(),
        state_->plot.end() - static_cast<std::ptrdiff_t>(kPlotFrames));
  }

  OverlayControls controls;
  ImGui::NewFrame();
  const float scale = state_->scale;
  ImGui::SetNextWindowPos(ImVec2(10.0F * scale, 10.0F * scale),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.85F);
  if (ImGui::Begin("Telemetry", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize |
              ImGuiWindowFlags_NoFocusOnAppearing)) {
    if (frame.can_open_file) {
      controls.open_file = ImGui::Button("Open File...");
      ImGui::SameLine();
      ImGui::TextDisabled("Ctrl+O");
    }
    if (!frame.open_error.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360.0F * scale);
      ImGui::TextUnformatted(frame.open_error.data(),
          frame.open_error.data() + frame.open_error.size());
      ImGui::PopTextWrapPos();
    }
    ImGui::TextUnformatted(frame.scene.data(),
        frame.scene.data() + frame.scene.size());
    ImGui::TextDisabled("%s", statistics.device_name.c_str());
    ImGui::Text("%ux%u  %s  %s", frame.width, frame.height,
        frame.vsync ? "vsync" : "no vsync",
        statistics.srgb_encoded ? "sRGB" : "linear");
    ImGui::TextUnformatted("MSAA");
    for (const std::uint32_t samples : {1U, 2U, 4U, 8U}) {
      ImGui::SameLine();
      char label[8];
      std::snprintf(label, sizeof(label), "%ux", samples);
      if (ImGui::RadioButton(label, statistics.samples == samples)) {
        controls.samples = samples;
      }
    }

    const Summary& interval =
        state_->cpu.empty() ? Summary{} : state_->cpu.front();
    ImGui::Separator();
    ImGui::Text("Frame %.2f ms  %.0f fps  p99 %.2f ms", interval.mean,
        interval.mean > 0.0 ? 1000.0 / interval.mean : 0.0, interval.p99);
    const float plot_top =
        std::max(33.3F, static_cast<float>(interval.max) * 1.1F);
    ImGui::PlotLines("##interval", state_->plot.data(),
        static_cast<int>(state_->plot.size()), 0, nullptr, 0.0F, plot_top,
        ImVec2(0.0F, 48.0F * scale));
    const PresentDrawCounts& draws = statistics.draws;
    ImGui::Text("%u draw calls  %llu triangles",
        draws.unlit + draws.outline + draws.opaque + draws.transparent,
        static_cast<unsigned long long>(draws.triangles));

    if (ImGui::CollapsingHeader("CPU", ImGuiTreeNodeFlags_DefaultOpen)) {
      TimingTable("cpu", cpu_rows, state_->cpu);
    }
    if (ImGui::CollapsingHeader("GPU", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (!statistics.gpu_timing) {
        ImGui::TextDisabled("this queue writes no timestamps");
      } else {
        TimingTable("gpu", gpu_rows, state_->gpu);
      }
    }
    if (ImGui::CollapsingHeader("Draws")) {
      const SceneCounts& counts = frame.counts;
      ImGui::Text("meshes %zu  MToon %zu  skinned %zu", counts.draws,
          counts.mtoon, counts.skinned);
      ImGui::Text("materials %zu MToon, %zu preview  textures %zu",
          counts.mtoon_materials, counts.preview_materials, counts.textures);
      ImGui::Text("unlit %u  outline %u  opaque %u  transparent %u",
          draws.unlit, draws.outline, draws.opaque, draws.transparent);
      ImGui::Text("pipeline binds %u", draws.pipeline_binds);
      ImGui::TextDisabled("overlay: %u draws, %u vertices", draws.overlay,
          draws.overlay_vertices);
    }
    if (ImGui::CollapsingHeader("Uploads") &&
        ImGui::BeginTable("uploads", 3,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("kind");
      ImGui::TableSetupColumn("last frame");
      ImGui::TableSetupColumn("session");
      ImGui::TableHeadersRow();
      const UploadCounts& last = frame.frame_uploads;
      const UploadCounts& total = frame.uploads;
      CountRow("topology", last.topology, total.topology);
      CountRow("points", last.points, total.points);
      CountRow("materials", last.materials, total.materials);
      CountRow("textures", last.textures, total.textures);
      CountRow("skins", last.skins, total.skins);
      CountRow("poses", last.poses, total.poses);
      CountRow("overlay textures", last.overlay_textures,
          total.overlay_textures);
      ImGui::EndTable();
    }
    if (ImGui::CollapsingHeader("Session")) {
      ImGui::Text("frames %llu  swapchain recreates %u",
          static_cast<unsigned long long>(statistics.frames_presented),
          statistics.swapchain_recreates);
      ImGui::Text("sample changes %u  frames read back %llu",
          statistics.sample_changes,
          static_cast<unsigned long long>(statistics.readbacks));
      ImGui::Text("validation %s, %u message(s)",
          statistics.validation_available ? "on" : "off",
          statistics.validation_message_count);
    }
    ImGui::TextDisabled("O hides this; P captures without it");
  }
  ImGui::End();
  ImGui::Render();

  ImDrawData& data = *ImGui::GetDrawData();
  SyncTextures(data, list);
  CopyDraws(data, list);
  return controls;
}

} // namespace Toon::viewport
