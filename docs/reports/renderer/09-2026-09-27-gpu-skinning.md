# GPU skinning: the avatar is skinned in the vertex stage, and a pose writes joint buffers alone

- Date: 2026-09-27
- Machine: Windows 11, MSVC 14.51, NVIDIA RTX A5000
- Tooling: `ost 0.23.13`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`) and `vrmImaging` 0.9.0 (`sha256:3043a239…`) of
  [report 06](06-2026-09-27-vrm-formation.md), unchanged
- Occasion: the Renderer Phase 1 slice after
  [report 08](08-2026-09-27-basic-textures.md), replacing the CPU ext
  computations that skinned every mesh since
  [report 01](01-2026-09-26-phase0-mesh-camera.md)

## TL;DR

**A mesh UsdSkel skins linearly is skinned in the vertex stage of both scene
pipelines. Its rest points and influences are uploaded when its binding
changes; a pose change writes that mesh's joint buffer and nothing else
(design policy §11). On the avatar, all 20 draws are skinned on the GPU, and
turning an arm joint writes 20 joint buffers with no point, topology, skin,
material or texture upload, with or without `vrmImaging`.** Dual quaternion
skinning still runs usdSkelImaging's CPU kernel, and a blend shape weight
change uploads the points, until Renderer Phase 3's morph targets.

## 1. What changed

- **Core.** A mesh can carry a `ToonSkin` — influences per point, or one
  constant set, and the geometry bind transform — with its own revision, and
  a `ToonSkinPose` — each joint's skinning transform and the skeleton-to-mesh
  transform — with another. Setting the skin or pose a mesh already has
  changes nothing. `IsSkinned` says whether a draw skins a mesh: its
  influences cover every point its topology reaches and its pose has every
  joint they name. A skinned mesh's `points` and `normals` are its rest
  pose's.
- **Backend.** Both scene pipelines gain a second descriptor set, read by the
  vertex stage: a mesh's influences and its joint buffer, whose entry 0 is
  the skeleton-to-mesh transform and entry 1 + j is joint j's skinning
  transform composed with the geometry bind transform. Each skinned mesh has
  its own set, from pools of 64; an unskinned mesh binds one shared set it
  never reads. The influence count and the skinning flags ride in the
  existing push constants, so neither pipeline gained a variant, and the
  vertex index is `SV_VulkanVertexID`, which needs no `shaderDrawParameters`.
  The statistics gain `skin_uploads` and `pose_writes`.
- **Shader.** `shaders/skinning.slang`, included by `mesh.slang` and
  `mtoon.slang`, blends the joints' matrices by weight, moves the rest point
  and normal into skeleton space, then into the mesh's space — UsdSkel's
  `classicLinear`, as usdSkelImaging's GLSL kernel computes it. The normal
  is moved by the blended matrix's 3x3, exact for rotation and uniform
  scale.
- **`hdToon`.** A mesh whose points are an ext computation primvar reads
  usdSkelImaging's two computations itself: the aggregator's rest points,
  influences, geometry bind and blend shapes become the skin, and the
  skinning computation's scene inputs — `skinningXforms`, `skelLocalToWorld`,
  `primWorldToLocal` and `blendShapeWeights` — the pose. Its ext computation
  Sprim counts the syncs that changed its inputs, and since Sprims sync
  first, a mesh re-reads its aggregator only when that count moved.
  `HdExtComputation` leaves `DirtySceneInput` set for whoever consumes the
  values; the Sprim clears it once counted, or a pose change would look like
  a rest change on every later frame. A skinning computation with
  `skinningDualQuats`, or inputs of another type, runs the CPU kernel as
  before. The host frame evidence adds `skin_uploads`, `pose_writes` and
  `draws_skinned`, and `HdToonRenderDelegate::CommitScene` exposes the scene
  the next frame would draw to a check that has no GPU.

## 2. What was run

```sh
ost build --jobs auto && ost test && ost validate
ost renderer viewport -- --frames 8 --hidden
ost validate --intent renderer-viewport
ost build --profile lookdev --intent hydra --jobs auto
ost test --profile lookdev --intent hydra && ost validate --profile lookdev --intent hydra
ost package --profile lookdev --intent hydra
ost artifact import dist/toon/0.1.0/cy2026-windows-x86_64-py313-lookdev--hydra
# report 08's two Formations, with toon at the new digest
ost formation lock formation.toml
ost formation run formation.toml -- testusdview <avatar.usdz> \
    --renderer Toon --testScript vrm_skinning_check.py
```

The new package is `sha256:7a679c13a049cd4658a4e63a587801ba2e8e29ca91aa969d6c1552dc7c81fde5`,
in this workstation's registry only. `vrm_skinning_check.py` takes three
shots: the bind pose; after binding a `SkelAnimation`, authored on the
session layer, that holds the rest pose with the first upper arm joint
(`LeftArm`) turned 30°; and after turning it to 60°, a value-only edit. It
asserts that the last edit advanced `pose_writes` and no other counter.

## 3. Results

| Build | Result |
| --- | --- |
| `core` | `ost test` 4/4; `ost validate` passed |
| `hydra` intent, canonical `lookdev` | `ost test` 10/10; `ost validate` passed with 16 of 16 renderer assertions |
| `renderer-viewport` | 8 frames presented, no swapchain recreation; `ost validate --intent renderer-viewport` passed |

The new assertion, `renderer.skinning.gpu`, binds every point of the
bootstrap triangle to joint 1 with a geometry bind 1.2 to the right:

| Frame | Pose | Centre pixel |
| --- | --- | --- |
| 1 | joint 1 1.2 to the left, unlit | the triangle's colour: the joint undoes the bind |
| 2 | joint 1 at identity | background: the skinned points moved right |
| 3 | skeleton-to-mesh 1.2 to the left, through `mtoon_opaque` | lit red: both pipelines skin, and the last transform applies |

`skin_uploads` is 1 and `pose_writes` 1, 2, 3 across the frames; points,
topology and pipelines stay where they were, and the validation capture is
empty.

The new CTest `toon-renderer-hydra-skinning` runs
`adapters/hydra2/tests/skinning.usda` through UsdImaging: a quad skinned by
two joints with a blend shape, and a quad skinned by dual quaternions. At
time 1 the linear quad commits a skin of one influence per point and two
joints, and its skinned points, evaluated on the CPU from the commit, are the
rest points. At time 2 only the tip joint moves: its pose revision advances,
its points, skin and topology revisions do not, and the skinned points reach
the moved tip. The dual quaternion quad is not skinned by the GPU; its points
come from the CPU kernel and change. At time 3 only the blend shape weight
moves: the points change and the pose and skin do not.

The avatar, the evidence at each shot:

| Formation | Shot | `draws_skinned` | `skin_uploads` | `pose_writes` | point / topology uploads | `material_writes` | `texture_uploads` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| runtime + `vrmImaging` + `toon` | bind | 20 of 20 | 20 | 20 | 20 / 20 | 13 | 6 |
| | animation bound | 20 | 20 | 40 | 40 / 40 | 13 | 6 |
| | joint turned | 20 | 20 | 60 | 40 / 40 | 13 | 6 |
| runtime + `toon` | bind | 20 of 20 | 20 | 20 | 20 / 20 | 13 | 0 |
| | animation bound | 20 | 20 | 40 | 40 / 40 | 13 | 0 |
| | joint turned | 20 | 20 | 60 | 40 / 40 | 13 | 0 |

`materials_mtoon` was 12 with `vrmImaging` and 0 without, and `pipelines`
2 throughout. The bind shot matched report 08's frame; in the last shot the
left arm, its sleeve and hand hang 60° lower, textured and shaded through
`mtoon_opaque` with `vrmImaging` and flat through the unlit pipeline
without.

## 4. Observations

- **A pose is 20 joint buffer writes.** Turning a joint on the bound
  animation re-read no rest data and uploaded nothing else.
- **Binding an animation is structural.** Authoring `skel:animationSource`
  resyncs the skinned meshes, so the second shot re-uploaded every mesh's
  points and topology once; the influences were unchanged and were not
  uploaded again.
- **Skinned normals are the rest pose's, skinned.** Storm, without
  `USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS`, derives smooth normals from the
  skinned points; here the rest pose's smooth normals are moved by the
  blended joint matrix, as a game engine's skinning does. Under non-uniform
  joint scale the two differ.
- **The skeleton's guide is a mesh too.** usdSkelImaging turns a `Skeleton`
  into a guide mesh, and the CTest's stage commits it as a third mesh.
  `hdToon` does not read render tags yet, so a session showing guides would
  draw it.
- **Every mesh needs a collection to sync.** A render index syncs Rprims
  only for a collection a pass enqueued, so the CTest enqueues one; the
  material CTest needed none because it reads Sprims.
- **The toon-headless and toon-viewport objects went stale again.** Both
  `main.cpp.obj`s record no ninja dependencies, so after the core header
  changed the viewport crashed until its object was deleted.

## 5. Not checked

Linux. Constant (rigid) influences and more than one influence per point on a
real asset: the avatar's meshes use per-point influences, and the headless
check covers one. A skeleton whose skinning method changes after the first
sync. Performance: no timing was taken; a claim about pose-to-submit latency
waits for renderer telemetry.
