// SPDX-License-Identifier: Apache-2.0
#include <toon/fast/avatar_state.hpp>
#include <locale>
#include <sstream>

namespace Toon::detail {
// Diagnostic transport only. The format and value semantics belong to
// usd-avatar-runtime/docs/architecture/OUTPUT_PATHS.md#4-parity-evidence.
std::string AvatarStateJson(const ArStateView& s) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(17);
  const auto text = [&](const char* value) {
    out << '"';
    for (const unsigned char c : std::string(value ? value : "")) {
      if (c == '"' || c == '\\')
        out << '\\' << static_cast<char>(c);
      else if (c < 32)
        out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
      else
        out << static_cast<char>(c);
    }
    out << '"';
  };
  const auto numbers = [&](const double* values, unsigned count) {
    out << '[';
    for (unsigned i = 0; i < count; ++i)
      out << (i ? "," : "") << values[i];
    out << ']';
  };
  out << "{\"instance\":" << s.instance << ",\"layout\":";
  text(s.layout_id);
  out << ",\"layout_version\":" << s.layout_version << ",\"frame\":" << s.frame_id
      << ",\"generation\":" << s.generation << ",\"input_revision\":" << s.input_revision
      << ",\"seconds\":" << s.evaluation_seconds
      << ",\"capabilities\":[";
  for (std::uint32_t i = 0; i < s.capability_count; ++i) {
    out << (i ? "," : "") << "{\"id\":";
    text(s.capabilities[i].id);
    out << ",\"version\":" << s.capabilities[i].version << '}';
  }
  out << "],\"joints\":[";
  for (std::uint32_t i = 0; i < s.joint_count; ++i) {
    const auto& joint = s.joints[i];
    out << (i ? "," : "") << "{\"skeleton\":";
    text(joint.skeleton_id);
    out << ",\"joint\":";
    text(joint.joint_id);
    out << ",\"parent\":" << joint.parent_index << ",\"translation\":";
    numbers(joint.local.translation, 3);
    out << ",\"rotation\":";
    numbers(joint.local.rotation, 4);
    out << ",\"scale\":";
    numbers(joint.local.scale, 3);
    out << '}';
  }
  out << "],\"morphs\":[";
  for (std::uint32_t i = 0; i < s.blend_shape_count; ++i) {
    const auto& morph = s.blend_shapes[i];
    out << (i ? "," : "") << "{\"mesh\":";
    text(morph.mesh_id);
    out << ",\"target\":";
    text(morph.target_id);
    out << ",\"weight\":" << morph.weight << '}';
  }
#if AR_ABI_VERSION >= 5
  out << "],\"uvs\":[";
  for (std::uint32_t i = 0; i < s.vertex_uv_count; ++i) {
    const auto& uv = s.vertex_uvs[i];
    out << (i ? "," : "") << "{\"mesh\":";
    text(uv.mesh_id);
    out << ",\"vertex\":" << uv.vertex_index << ",\"value\":";
    numbers(uv.value, 2);
    out << '}';
  }
#endif
  out << "],\"materials\":[";
  for (std::uint32_t i = 0; i < s.material_count; ++i) {
    const auto& material = s.materials[i];
    out << (i ? "," : "") << "{\"material\":";
    text(material.material_id);
    out << ",\"input\":";
    text(material.input_id);
    out << ",\"type\":" << material.value_type << ",\"overridden\":" << material.overridden << ",\"value\":";
    numbers(material.value, 4);
    out << '}';
  }
  out << "],\"visibility\":[";
  for (std::uint32_t i = 0; i < s.visibility_count; ++i) {
    out << (i ? "," : "") << "{\"target\":";
    text(s.visibility[i].target_id);
    out << ",\"visible\":" << s.visibility[i].visible << '}';
  }
  out << "],\"samples\":[";
  for (std::uint32_t i = 0; i < s.sample_count; ++i) {
    const auto& sample = s.samples[i];
    out << (i ? "," : "") << "{\"source\":";
    text(sample.source_id);
    out << ",\"actor\":";
    text(sample.actor_id);
    out << ",\"channel\":";
    text(sample.channel_id);
    out << ",\"evaluator\":";
    text(sample.evaluator_id);
    out << ",\"kind\":" << sample.kind << ",\"validity\":" << sample.validity
        << ",\"resolution\":" << sample.resolution << ",\"source_seconds\":" << sample.source_seconds
        << ",\"clock_scale\":" << sample.clock_scale << ",\"clock_offset\":" << sample.clock_offset
        << ",\"runtime_seconds\":" << sample.runtime_seconds << '}';
  }
  out << "]}";
  return out.str();
}
} // namespace Toon::detail
