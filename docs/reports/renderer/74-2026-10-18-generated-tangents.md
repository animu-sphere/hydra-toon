# Generated MikkTSpace rest frames

- Date: 2026-10-18
- Machine: Windows x86_64, NVIDIA RTX A5000, driver 597.16
- Build: MSVC 14.51; canonical CY2026 OpenUSD 26.08 `lookdev`, combined
  `viewport-usd`; plain CMake without OpenUSD
- Scope: v0.5.0's generated tangent foundation, mirrored seams and retained
  frames under skinning and normal morphs

## Behaviour

The Hydra mesh adapter generates tangents when no authored tangent array is
available and the mesh has usable normals and `st`. Its private wrapper
calls the unmodified reference MikkTSpace implementation at commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`, vendored under
`third_party/mikktspace/`. The sources retain their license notices;
the license and origin README install with the renderer. They compile as C++
in the existing Hydra target, with no new runtime dependency or download.

Generation uses normalized rest triangles, unit normals and bottom-left
`st`. MikkTSpace's per-corner results preserve the indexed vertex layout
where possible. Different frames on a shared vertex append duplicates,
including mirrored handedness; the map expands points, normals, UVs, skin
influences, morph ranges, MMD edge scales and extra UVs together. Triangle
order, material-subset ranges and source-point smooth normals are retained.

Rest point, normal, UV and topology edits regenerate the frame. A changed
seam map or triangle assignment republishes geometry; otherwise the tangent
revision is independent. Pose and morph weights retain the same rest array
and revision and use the shared GPU deformation and fragment
orthogonalization. Authored frames take precedence. Short/invalid authored
arrays, incomplete or non-finite generation inputs and entirely collapsed
UVs retain the derivative fallback. CPU point computations, including dual
quaternion skinning, also retain derivatives: they supply no corresponding
deformed tangent output.

## Controlled evidence

The existing `toon-renderer-hydra-primvars` and
`toon-renderer-hydra-primvars-gpu` checks add:

- A two-triangle mirrored UV fixture with vertex-interpolated UVs: exactly
  two conflicting shared vertices split, from four to six. The resulting
  tangent directions/signs and all duplicated streams match analytic arrays.
- Face-order and cyclic corner-order invariance, missing UVs, invalid
  indices, non-finite normals and collapsed UV rejection.
- Authored-frame priority/removal, UV scale and rotation, seam removal,
  rest-point rotation and rest-normal edits. Unchanged seam layouts retain
  geometry revisions; changed frames do not retain stale tangents.
- A USD skinning fixture with the same generated mirror seam: duplicated
  influences and sparse morph ranges agree with their source vertices.
  Pose and dense normal-only morph changes retain all static geometry and
  the tangent array/revision. A dual-quaternion CPU fallback with valid UVs
  does not consume an undeformed generated frame.

For MToon and PreviewSurface, static mirrored images and three skin/morph
times are compared at 1x and 4x MSAA against independently CPU-baked normal
directions on explicit geometry without tangents or a normal map. Every
colour byte agrees within two levels and depth agrees exactly. These cover
MToon's opposite bitangent and PreviewSurface's signed normal constant,
including a normal tilted away from the geometric normal. Repeated draws
retain point/topology/skin/morph uploads and pipelines. Vulkan validation
message counts stay zero.

## Verification

With the explicit MSVC developer environment, built the CMake trees and ran
`ctest --test-dir <tree> --output-on-failure`:

- Combined `viewport-usd`: 61/61 passed, including Hydra, viewport,
  `testusdview`, core, GPU and installed consumers.
- Plain CMake without OpenUSD: 18/18 passed.

The combined run included the lookdev runtime's `bin` and `lib` on `PATH`.
The expanded primvar checks and installed-consumer check were rerun after
the final rest-edit assertions and license installation change. Relative
documentation paths resolve and `git diff --check` passes.

After relocating the vendored files to the repository's `third_party/`,
rebuilt the combined tree and reran both primvar checks and the installed
consumer check: 3/3 passed. Upstream source hashes remained unchanged.

## Limits

These are controlled analytic CPU/GPU checks, not an external renderer or
glTF asset comparison. That reference evidence remains in the v0.5.0
roadmap, along with the PBR comparison and reusable RGBA product/compositing.
No tangent morph deltas, other UV sets, CPU-deformed frame generation or
new normal-graph support is added. A direct core host still supplies tangent
frames explicitly; generation is the Hydra input adapter's normalization.
No restricted asset or capture is committed, and no release is published.
