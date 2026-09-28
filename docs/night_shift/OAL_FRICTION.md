# Night Shift -- creator (Open Asset Lab) friction

What building a real slice with Open Asset Lab felt like. Observed only;
severity as in ENGINE_FRICTION (HIGH / MEDIUM / LOW). The workflow itself
is [AUTHORING_WORKFLOW](AUTHORING_WORKFLOW.md).

## What OAL did well

- **Validation in the engine's own words.** The package refusals, binding
  affordance checks and requirement resolution matched the engine exactly;
  after the first build, the engine never refused a Night Shift package
  that OAL had accepted. The one rule the project tripped
  (`a world with props ... gives every mover a definition`) was named
  precisely.
- **Keys agree.** OAL's world key equals the engine's on every build and
  every mutation; compatibility behaved without any special handling.
- **Deterministic output.** Two builds are byte-identical, so tests can
  pin and diff packages.
- **Prefabs + bindings as data** composed well: the security door is
  authored once and behaves identically as D1, D3 and the lift gate.
- **`megamod-resources --bundle`** answers "what did my prefab become?"
  (expanded IDs, entity indices, compiled bindings by origin).

## Friction

| # | Friction | Severity | Needed |
|---|---|---|---|
| O1 | Worlds are Python with hand-typed coordinates; no plan view, no preview | **HIGH** | a top-down plan/preview of boxes, entities and prefab footprints |
| O2 | No reachability check: three crates blocked routes (the passage mouth, the tunnel exit, the lift approach), each found only by walking a scripted joiner into it | **HIGH** | "is every interactable / destination reachable from a start" at build time (the engine's own nav grid already computes reachability: `10651 of 33458 nav nodes reachable`) |
| O3 | No way to build an original world except registering it in `fixtures.py` | MEDIUM -> **fixed** | `assetlab project build/budget` (added) |
| O4 | No entity budget until the engine loads the world | MEDIUM -> **fixed** | budget by kind and prefab in the project report (added) |
| O5 | `requires` must list every imported resource by hand | MEDIUM | infer imports from use (Night Shift computes them in its own code) |
| O6 | A box model's texture orientation and readable face are undocumented (first textures upside down; text reads correctly only on the +x face) | MEDIUM | document; a texture/model preview |
| O7 | What a prop looks like in game (the scene light brightens it ~3x) cannot be seen before playing | MEDIUM | preview with the engine's lighting (ties to E3) |
| O8 | Sounds cannot be auditioned in OAL (and an agent cannot listen at all) | MEDIUM | a sound preview/listing with waveforms and levels |
| O9 | Placing a door needs manual checks that its gap exists and its slide path is solid wall (door into a room, shutter out of the map) | MEDIUM | a prefab-in-wall check: gap vs. footprint, slide path vs. solid |
| O10 | Prefab-space rotations done by hand (front toward -x; yaw 90 maps local -y to world +x) to find where buttons end up | MEDIUM | show expanded child positions in the report / preview |
| O11 | Bindings in Python are verbose; the project wrote its own helpers (`eid`, `child`, `rs`, `snd`) | MEDIUM | a compact text form or editor for bindings |
| O12 | The look-at-it loop is long: edit, build, host, scripted joiner with `--shot`, convert PPM, open | MEDIUM | live reload of a bundle in a running host; a screenshot command in the tool |
| O13 | Project modules import by bare name (`art`, `facility`): two projects with an `art.py` in one process would collide | LOW | package-qualified imports (known limitation of `assetlab.project`) |

## Commands repeated most

1. `assetlab project build projects/night_shift --output ...` (every change)
2. host + joiner with `--route ... --shot` and a PPM -> PNG conversion (every visual check)
3. `megamod-content ... --world night_shift` for the key
4. the Android loop: stage, `scripts/emu/start.sh`, taps

## What needed direct editing

Everything: geometry, placements, bindings, textures and sounds are all
code. No step had a tool other than the text editor and a screenshot.

## What needs visual tooling first (from this slice)

1. **A plan view** with boxes, solid vs. non-solid, entity markers, prefab
   footprints and door slide paths (would have prevented O2, O9, O10).
2. **Reachability overlay** from the starts (O2).
3. **Binding graph view** -- the state graph in STATE_GRAPH.md was drawn by
   hand from `megamod-resources`.
4. **Asset preview** with the engine's lighting, and sound audition (O6-O8).
