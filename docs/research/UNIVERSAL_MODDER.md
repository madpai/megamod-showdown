# Universal Modder: lessons for Megamod

Study date: **2026-09-30**. Source: [rehan-remade/universal-modder](https://github.com/rehan-remade/universal-modder/tree/8ad57ae7fc922172609328e01f9fc5dc1ceeb8ba),
revision `8ad57ae7fc922172609328e01f9fc5dc1ceeb8ba`, MIT licensed.
This note studies its agent workflows and selected tooling. It does not
reproduce the donor games' mods or verify their reported performance.

## Observed source and evidence limits

**Fact:** the source has separate skills for recon, reverse engineering,
asset preparation, runtime automation, mashups and publishing, coordinated
by `skills/mod-any-game/SKILL.md`. Its sequence carries a small working
feature through an actual game before expanding content.

**Fact:** `skills/reverse-engineering/SKILL.md` calls for a file-format round
trip and real traces as checks on an interpretation. The AoE2 case study
reports a lossy SLD round trip; that accuracy number is the author's
reported result, not a measurement repeated here.

**Fact:** `skills/asset-pipeline/SKILL.md` specifies frame size, orientation,
alpha, pivot, palette and camera before transforming assets. `um/fal.py`
records generations in `fal_manifest.jsonl`. These are useful precedents
for provenance and reproducible preparation, without making fal a required
Megamod service.

**Fact:** `skills/game-automation/SKILL.md`, `skills/showcase-video/SKILL.md`
and the Terraria case study favor repeatable scenes, logs, observed
screenshots and external capture. The study describes a backbuffer-capture
memory failure and a recorder blocking the simulation thread; these are
case-study reports, not bugs established in Megamod.

**Fact:** `um/publish.py` performs heuristic checks for content copied from
an install, secret patterns, decompiler markers and archives. A successful
lint cannot establish rights to changed or generated content. Our existing
public-content guards and provenance requirements remain necessary.

**Unknown:** the upstream's broad game coverage, current generator endpoint
availability, community loader compatibility and social-media mashup
architecture claims have not been independently reproduced. Investigate
the particular source/tool/version when a concrete feature needs it.

## Translation into this ecosystem

**Inference:** the strongest transfer is a reusable evidence workflow.
Our engine already has a native resource inspector, a headless match host,
a sandbox control channel, Android reports and production scenarios. Reuse
them rather than adding Windows automation or a second runtime protocol.

| Upstream lesson | Megamod application | Status in this work |
|---|---|---|
| Recon before coding | Search existing research and production evidence; state the actual missing behavior and its owner | `megamod-capability` skill |
| Actual code/data as reference | Facts carry exact revisions/locations; hypotheses carry a decisive validation step | `megamod-capability` skill |
| One feature through the whole pipeline | Original Asset Lab project → existing package compiler → native loader → Showdown test | Existing Night Shift/Racing reused |
| Format and runtime checks | Compare repeated package bytes, dependency closure and native world identity/state budget | New `assetlab project verify` |
| Asset conventions and generation history | Preserve axes/units/pivots, transforms, hashes, origin and inferred values | `assetlab-content` skill |
| Repeatable runtime proof | Select the existing harness, preserve logs, check outcomes and distinguish physical-device evidence | `megamod-playtest` skill |

The new skills are repository-owned in `.agents/skills/` (content preparation
in Open Asset Lab). Local Codex discovery links point to those maintained
copies. `AGENTS.md` routes agents to them; existing `.claude/skills/`
verification, conversion, reporting and publication procedures still supply
their project-specific commands and gates.

## First implemented improvement

**[Now]** Open Asset Lab's `project verify DIR --output NEW_DIR --engine EXE`
builds a trusted local Python content project in two fresh interpreters and checks every emitted
package's SHA-256. It checks dependencies and references through existing
validators, then loads each world through the supplied `megamod-resources`.
It compares world key and digest, declared package identity, dependency
closure, bindings, protocol and world replication counts/byte budgets.

The evidence directory retains the first compiled bundle, compiler report,
native stdout/stderr, executable SHA-256 and `verification.json`. A changed
repeat build, native refusal, timeout, malformed response or disagreement
fails the command. Existing run directories are refused to preserve prior
evidence. The command compiles local project Python just like `build` and
`budget`; it is not an untrusted-project sandbox.

Racing's existing gameplay test now uses this command before its authored
race checks, full three-lap/reset route, incompatible-track refusal and UDP
peer test. Mode-specific expectations remain in the Racing test.

Local package evidence from this study:

| Project | Repeated packages | Native world key | Runtime entities |
|---|---:|---|---:|
| Night Shift, original project | 3 identical | `356266bd` | 62 |
| Racing: Cinder Circuit | 3 identical | `87915509` | 27 |

These results establish compiler/loader agreement. Rendering, complete
gameplay, multiplayer and phone performance require their separate gates.
The physical Racing feedback in the current handoff remains outstanding.

### Validation of the implemented workflow

- Open Asset Lab: **215 synthetic tests passed**, including ten tests for
  successful agreement, drift hidden by cached imports, process failures,
  mismatches, timeouts, multiple worlds and evidence preservation. CI passed
  on Python 3.11 and 3.12 at `97d45b2`.
- Megamod: **99 checks passed, zero failures** in `scripts/verify.sh` with
  private Trial and imported bundle inputs and software Vulkan. This
  included X1–X9, Night Shift's crew/late-join scenario, the updated Racing
  test's full three-lap/reset and host/peer route, rendered checks, desktop
  Blood Gulch peers and the Android build/shareable-APK content checks.
- Inspected X6's actual screenshots: a closed striped door in one world
  and a door displaced clear of the opening in the second world.
- All three skill definitions passed the skill-creator validator.
- The first gate attempt used a nonexistent historical Vulkan ICD path;
  correcting it to the available software driver enabled the rendered
  tests. This was a setup failure, not a gameplay result.
- Night Shift's optional archived-X6-engine comparison was skipped because
  that binary was absent. Physical S24+ Racing feel/performance was not
  measured by this work. Desktop/software Vulkan evidence cannot replace it.

## Next opportunities, still proposals

1. **An original glTF asset slice:** one original mesh/material through a
   real importer, package and renderer before claiming general glTF support.
   This provides the second source family that can justify a shared mesh IR.
2. **Trace comparisons for a requested mechanic:** capture an observable
   donor behavior and compare a generic native capability in an original
   Showdown scenario. Choose a specific mechanic first; a runtime rewrite
   or passthrough system is not required by this study.
3. **Generation and conversion provenance:** extend existing asset provenance
   when an actual generated-content consumer needs new fields. Keep model
   suggestions distinct from deterministic validation and authored tuning.
4. **Scenario demonstrations:** record a proven route with externally
   captured frames/audio if a demonstration is requested. A clip illustrates
   the verified feature; it cannot replace correctness or device evidence.

The active product priorities remain in `MEGAMOD_VISION.md`, Asset Lab's
vision and the current testing objective. This study adds methods and a
verification command, not a claim that all future capabilities are built.

## Attribution and reuse boundary

Credit Rehan and Universal Modder contributors for the workflow ideas.
The skills and verification command here were written for Megamod; no
upstream implementation, donor game assets or decompiled code were copied.
Universal Modder's MIT license permits code reuse with its notice retained
if a future task actually adopts source. Its dependencies, referenced
projects, generator services and donor assets have separate terms.
