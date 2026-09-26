# Bethesda plugin data model / xEdit

**Evidence/version:** [xEdit source](https://github.com/TES5Edit/TES5Edit) and [record/masters documentation](https://github.com/TES5Edit/docs/blob/master/_pagebuilder/8-managing-mod-files.txt), [conflict resolution guide](https://tes5edit.github.io/docs/5-conflict-detection-and-resolution.html), [Creation Kit file UI](https://ck.uesp.net/wiki/File_menu), read 2026-09-26. xEdit supports multiple Bethesda titles whose record variants differ; examples below are general concepts, not one universal binary specification. **License:** xEdit is MPL-2.0; Bethesda game code/content are proprietary. Architecture only.

## Architecture and entity/data split

**Fact.** Plugins contain typed records and references. A base form defines a content object; placed references in world/cells point to forms and carry instance data. Forms cover NPCs, weapons, statics, doors, containers, quests, and other categories. FormIDs identify records within a load-order context; the plugin's masters list translates its file-local references. [xEdit's implementation](https://github.com/TES5Edit/TES5Edit/blob/dev/wbImplementation.pas) contains explicit file/load-order FormID conversion, master handling, and winning-override paths; [its guide](https://github.com/TES5Edit/docs/blob/master/_pagebuilder/8-managing-mod-files.txt) explains that changing masters can require remapping to preserve meaning. These are data-model facts, not evidence of an ECS or physics architecture.

**Inference.** MegaMod should give every content definition a stable globally namespaced string (`weapon:mega.ion_rifle` conceptually), with compact runtime numeric IDs assigned only after dependency resolution. An instance needs a separate persistent world ID and transient network ID. Never put load-order ordinal into the stable content identity.

## Dependencies, overrides, conflicts, loading

**Fact.** Plugins list masters they reference. Load order selects a winning override for a record; [xEdit's conflict view](https://tes5edit.github.io/docs/5-conflict-detection-and-resolution.html) shows record-level differences and supports patch plugins to reconcile them. The cost is visible: load-order changes can change winners, references, and saves; conflicts may need manual resolution. [xEdit releases](https://github.com/TES5Edit/TES5Edit/releases) document continuing title-specific FormID/load-order edge cases.

**Inference.** MegaMod should support explicit dependencies and deterministic topological ordering, but avoid default last-wins override behavior. Typed patches should name target package, target ID, compatible version range, changed fields, and authoring provenance. Two patches to the same field should fail or require a user-authored merge package. A mod can always add a new definition under its own namespace.

## Scripting, networking, world, physics

**Fact.** Creation Kit objects can have Papyrus scripts and event-driven quest/object logic; plugin records and save-game changed state are distinct. World/cell placement records connect static content definitions to dynamic world references. The studied xEdit/Creation Kit material is focused on plugin data, not rendering internals, transport, prediction, interpolation, constraint physics, or multiplayer package matching. Those fields are **unknown/not applicable** for this comparison.

**Inference.** The relevant lesson is save stability. MegaMod saves should record package lockfile/hash, definition IDs, world placement IDs, and mutable component state. A save referencing a missing definition needs an explicit migration or placeholder policy. Do not infer network IDs from persistent IDs; allocate network IDs per session.

## Tooling and Open Asset Lab

**Fact.** xEdit makes masters, references, overrides, and conflicts inspectable. It detects record conflicts at field/subrecord depth and gives creators a workflow to make explicit patches. That tooling is part of why a complex ecosystem remains manageable, even though the conflict system itself is expensive.

**Inference.** OAL should provide a dependency graph and definition diff/provenance view before runtime. It can normalize imported NPC/weapon/material references into native IDs and validate referential integrity. Importing Bethesda plugin formats someday would require title- and version-specific parsers, master resolution, and a legal asset acquisition policy; never treat FormIDs alone as portable global IDs.

## MegaMod conclusions

- **Adopt:** stable namespaced definition IDs, separate instance IDs, explicit dependency references, conflict visibility, save lockfile.
- **Consider later:** explicit typed patch packages and field-level merge UI in OAL.
- **Avoid:** load-order ordinal IDs, silent last-wins override rules, broad record compatibility promises, or copying Bethesda records/asset data into public tests.
- **For Open Asset Lab:** dependency/override viewer, per-field provenance, migration/renaming assistance, importer feasibility audit by game/version.
- **Prototype:** resolve two packages with a common master and conflicting edits; show OAL's field-level report, require an explicit merge, and demonstrate that saved IDs remain stable when unrelated packages reorder.
