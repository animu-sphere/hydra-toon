# Documentation guidelines

Documentation is part of the implementation contract. A change is incomplete
if it changes a public boundary, implemented architecture or a capability
without updating the page that owns it.

**One concept, one owning repository, one canonical document.** Everything
below follows from that. These rules follow the sibling repositories'
([`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/contributing/documentation.md)),
so a reader moving between them finds the same shape.

## Category ownership

| Category | Put this here | Not this |
| --- | --- | --- |
| root `README.md` | What this repository is, what it owns and does not, a small diagram, a component table, links, a minimal build entry point, the licence | See [root README](#root-readme) |
| `docs/README.md` | Which document owns which subject | Content of its own |
| `design/` | Intended contracts, their rationale, open questions | Claims that something is implemented |
| `architecture/` | Target identities, directories, dependency edges, packaging — the binding structural contract | Rationale; plans |
| `reference/` | What is implemented now (the capability matrix), and where it was measured (supported configurations) | Plans; a sibling repository's status |
| `roadmap/` | What is built next: the work of each coming milestone, and planned work no milestone carries yet | Status marks; completed work; rationale; a sibling's roadmap |
| `guides/` | How to accomplish a task, with commands that have been run | Commands nobody has run |
| `releases/` | Versioning and the release gate; one immutable record per released version, saying what it established | Work in progress |
| `reports/` | Dated evidence: what was verified, under which conditions, and how — builds, benchmarks, hardware sessions, `ost` dogfooding | Current-state claims |
| `archive/` | Plans and documents that were once authoritative and no longer are | Anything a reader should act on |
| `contributing/` | How to maintain this repository | End-user tasks |

The same fact is not maintained independently in two categories.

## Status

Current status is stated in one place, the
[capability matrix](../reference/CAPABILITY_MATRIX.md). Nothing else carries a
status mark or a "done" or "in progress" claim about this repository's work —
not the roadmap, a design document, the root README or the changelog.

- When work is finished, its row in the capability matrix changes, its entry
  goes to the changelog, and its item is deleted from the roadmap in the same
  change.
- Planned work that no milestone carries yet is listed once, in the
  roadmap's *not yet in a milestone* section, and moves to a milestone's
  page when one takes it. A gap the tree has is also a ⬜ or ⚠️ row in the
  capability matrix, which states the gap, not the plan; an idea nobody
  has planned is an issue.
- A milestone's roadmap page is deleted when that version is released; its
  release record says what it established.

## Cross-repository contracts

`hydra-toon` consumes contracts owned by `usd-vrm-plugins`, `usd-mmd-plugins`,
`usd-motion-plugins` and `motion-connectors`
([integration scope §3](../design/INTEGRATION_SCOPE_POLICY.md#3-what-it-consumes-and-from-whom)).

> A repository may describe how it consumes a sibling repository's contract,
> but must not redefine that contract.
>
> Link to the owning repository instead of copying its API semantics,
> capability status, roadmap, or implementation state.

Good: "MToon is recognized by `VrmMToonAPI`
([`usd-vrm-plugins` material policy §6](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/design/MATERIAL_ARCHITECTURE_POLICY.md#6-canonical-vrm-material-semantics))."
Bad: a table of every `inputs:vrm:mtoon:*` attribute and its fallback,
maintained here.

Link to a sibling's **canonical** document, never to one it has archived or
superseded. A discrepancy found in a sibling's documents is recorded in
[integration scope §6](../design/INTEGRATION_SCOPE_POLICY.md#6-cross-repository-observations)
and raised with the owner, not corrected by restating.

## Root README

The root README is an entry point. It follows the shared shape — **Scope**,
**Architecture**, **Components**, **Documentation**, **Build**, **License** —
and stays short. It does not carry release-by-release history, version status
prose, status columns in the component table, detailed dependency graphs,
contract definitions, or a sibling repository's contracts or status.

## Reports and the archive

A **report** is dated, append-only evidence: what was measured, where and
when. It is never authoritative for current status. A later finding gets a
new report and a one-line forward note on the old one; the only edit an
existing report receives is a link repair.

The **archive** holds intent that has been done or replaced. Every archived
document opens with a *Historical only* banner. A superseded **design**
document is not archived; it stays at its path as a short stub — status,
former purpose, current owner, links to the replacement.

## Metadata

A design document, a superseded stub and an archived document carry YAML
front matter:

```yaml
---
status: accepted       # proposed | accepted | binding | superseded | rejected | historical
owner: hydra-toon      # the repository that owns the subject
canonical: X.md        # superseded only: the replacement, relative
---
```

## Naming

- A milestone is named by its version: v0.2.0. The retired Renderer Phase 0–7
  appear only in history, and
  [design policy §25](../design/DESIGN_POLICY.md#25-implementation-phases)
  maps them onto milestones. A sibling's sequence carries the sibling's name
  ("`usd-vrm-plugins` Product P5").
- Section numbers in design documents are stable, so they can be cited.
  [DESIGN_POLICY.md](../design/DESIGN_POLICY.md) keeps the implementation
  policy's §1–§29 numbering; §30 onward are this repository's own, and a new
  section is appended, never inserted.

## Language and form

- Repository documents are in English.
- Relative links for everything in the repository; code spans for commands,
  paths, targets, types and attribute names.
- Keep each category index (`docs/README.md`, `roadmap/README.md`,
  `archive/README.md`, `releases/README.md`, `reports/README.md`) in sync
  with its files.
- Never commit machine-local paths, or a model, texture or capture whose terms
  do not allow redistribution — MMD's shared toon ramps included.

## Change checklist

1. Planned behaviour is not presented as implemented.
2. Every new page appears in its category index.
3. Relative links resolve.
4. Implementation changes update `architecture/` and `reference/`.
5. Completed work leaves `roadmap/`, and no status is written outside the
   capability matrix.
6. A sibling's contract is linked, not restated.
