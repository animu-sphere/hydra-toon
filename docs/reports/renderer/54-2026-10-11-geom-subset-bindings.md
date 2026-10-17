# `hdToon` draws a mesh once per `GeomSubset` material binding, and every MMD material of 41 local stages becomes one draw

- Date: 2026-10-11
- Machine: Windows 11 x86_64, NVIDIA RTX A5000; MSVC 14.51 / Visual Studio 18
- Tooling: `ost 0.23.14`; the canonical runtime
  `openstrata-runtime-cy2026-lookdev:26.08-gl-windows-x86_64`
  (`sha256:b982656c…`); `usd-mmd-plugins` v0.2.0's `mmdImaging`, carrying
  `mmdSchema`, as packaged on this workstation for
  [report 53](53-2026-10-11-mmd-material-read.md)
  (`sha256:39f3e24d…`); this branch's `toon` `hydra` package, built here
- Occasion: v0.4.0's MMD importer authors one mesh per model and binds each
  material to a face subset of it
  ([`usd-mmd-plugins` stage contract §8.1](https://github.com/animu-sphere/usd-mmd-plugins/blob/main/docs/design/STAGE_CONTRACT.md#81-one-mesh-material-subsets)).
  `hdToon` read only a mesh's own binding, so after report 53 a 20-material
  model was one draw whose 20 materials nothing sampled
  ([roadmap](../../releases/v0.4.0.md)).

## TL;DR

**A mesh whose `GeomSubset`s bind materials now draws once per such subset,
with that subset's material, and once for the faces none of them binds, with
the mesh's own binding, as Storm draws them. The division is generic USD: the
core carries it as parts of the mesh's one triangle list, and the backend
issues one indexed draw per part over the mesh's one set of buffers, so a
binding edit uploads no geometry. A divided mesh renders the same colour,
depth and hulls as separate meshes. On the 41 local MMD stages with
`mmdImaging`, each model's mesh divides into exactly one part per MMD
material, 495 in all, none unbound; in `usdview` the 20-material model is 20
draws instead of 1. The parts still draw their display colour, unlit:
`mmd_opaque`, `mmd_transparent` and `mmd_outline` come next.**

## 1. The change

- **Core.** `RenderWorld::SetMeshSubsets` takes a mesh's face subsets as
  ranges of its triangle list with their own material ids. At commit the
  list is divided into `MeshSnapshot::parts`: each valid subset with its
  material, each gap with the mesh's material, adjacent parts of one material
  merged. A range that is empty, not whole triangles, past the list or
  overlapping an earlier one is ignored, so subsets and topology can arrive
  in either order. Subsets are kept sorted, so the same ranges in another
  order change nothing, and a new parts array is published only when the
  parts change; a topology, mesh-material or subsets edit divides again.
- **Vulkan backend.** `MeshCache::Record` iterates a mesh's parts in every
  pass: unlit, the opaque hulls, the opaque surfaces, and the transparent
  pass, which now sorts (mesh, part) pairs by render queue, stable in mesh
  and then part order. Each part is a `vkCmdDrawIndexed` at its first index;
  buffers and descriptor sets stay per mesh. A snapshot a `RenderWorld` did
  not commit has no parts and draws whole with its material.
- **Late frames and occlusion.** `ApplyFastSnapshot` rejects a late frame
  whose parts array changed. A mesh establishes opaque-triangle occlusion
  only when every part's material could.
- **Hydra adapter.** `HdToonMesh` reads Hydra's `HdGeomSubsets` with the
  topology. `HdMeshUtil`'s primitive parameters give each triangle's
  authored face; the triangles of each face subset that binds a material are
  grouped, in the subsets' order, into one range, and the rest follow with
  the mesh's own binding. A face two subsets name stays with the first. The
  adapter state keeps each subset's material path and resolves it to an id
  as it does a mesh's binding, again when a material prim is created or
  destroyed.
- **Evidence.** `TOON_HYDRA_EVIDENCE` and the viewport's scene summary count
  draws as parts; for a mesh without subsets that is the mesh, as before.
  The viewport's Materials panel lists a mesh that binds a material through
  a subset. `toon-hydra2-material-test --stage` prints each mesh's parts and
  how many bind an MMD material.

## 2. Tests

- `toon-render-world` divides a four-triangle mesh: subsets given out of
  order, gaps taking the mesh's material, a mesh-material edit reaching the
  gaps and merging them, equal subsets changing no revision or array,
  invalid ranges ignored, a shorter topology dropping a subset past its end,
  and cleared subsets drawing the mesh whole.
- `toon-subset-gpu` renders one mesh divided into an opaque MToon part with
  a hull, a blended double-sided part and a Mask gap, then with the gap
  unbound, against the same triangles as three separate meshes: colour and
  depth are byte-identical, the hull counts equal, and the rebind uploads no
  topology, points or material slot.
- `toon-renderer-hydra-geom-subsets` runs `geom-subsets.usda` through
  UsdImaging's scene indices: a quad and a triangle in `Red`, a triangle in
  `Blue`, and one in a subset without a binding commit the parts
  `{0, 9, Red}`, `{9, 3, Blue}`, `{12, 3, Base}`, each holding its own faces'
  points. Removing `Blue`'s prim leaves its part unbound without touching the
  topology; binding the `Blue` subset to `Base` merges it with the unbound
  faces; removing every subset draws the mesh whole.
- `toon-renderer-hydra-morph-normals` replaced a committed mesh's triangle
  list by hand and now replaces its parts with it.

The OpenStrata gates passed: `core` 15/15, `hydra` 24/24, `viewport-usd`
55/55, each with `ost validate`.

## 3. Local stages

The 41 local MMD USDZ files of [report 53](53-2026-10-11-mmd-material-read.md),
copied to ASCII paths, read through `mmdImaging`'s session into `hdToon`:

```sh
# in usd-mmd-plugins
ost plugin run plugins/mmdImaging --profile lookdev -- \
    toon-hydra2-material-test.exe --stage <model.usdz>
```

| Files | MMD materials | Model meshes | Triangles | Parts | MMD parts | Unbound parts |
| --- | --- | --- | --- | --- | --- | --- |
| 41 | 495 | 41 | 1,142,781 | 495 | 495 | 0 |

Every model is one mesh, and its parts are its materials, one each; the
start and end time codes agree. Each stage also has usdSkelImaging's
skeleton guide mesh, which binds no material and which `usdview` does not
draw.

## 4. The `usdview` session

The untracked Formation of [report 53](53-2026-10-11-mmd-material-read.md),
repinned to this branch's `toon` package
(`sha256:5fbc0bd9c25df58b224c80a9e7249a31a3d21bf3c5a26ccfca20c2c8ac917267`),
passed `ost formation lock` and `doctor`, and `testusdview` with
`mmd_material_check.py` and `TOON_EXPECT_MMD=20` passed on the 20-material
model:

```text
renderer: HdToonRendererPlugin
topology_uploads=1 materials_preview=1 materials_mtoon=0 materials_mmd=20 draws=20 draws_mtoon=0 draws_skinned=20
```

Report 53 had `draws=1` on the same model. The 20 draws share the mesh's one
topology upload and skin. The image is the model's silhouette in its display
colour, as before: an MMD material is not MToon, so each part takes the
unlit path.

## 5. What it does not do yet

- **Draw MMD.** The parts sample nothing yet; `mmd_opaque`,
  `mmd_transparent` and `mmd_outline` are next, after MAT-Q2 and MAT-Q3
  ([material policy §9](../../design/MATERIAL_POLICY.md#9-open-questions)).
- **Order by `drawOrder`.** Parts draw in the subsets' order; MMD's
  `drawOrder` is read, not yet used to sort.
- **Resident targets.** `DescribeResidentTargets` still describes a mesh's
  own binding, not its subsets'. A runtime binds materials by path, so no
  host needs it yet.
