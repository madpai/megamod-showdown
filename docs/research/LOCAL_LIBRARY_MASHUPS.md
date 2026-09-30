# Local library mashups and the first Oblivion static proof

**Date:** 2026-09-30. **Status:** installed-content reconnaissance and a
private static import experiment. No production importer, Engine code,
Showdown mode, shipped bundle or phone build changed. The owner selected
classic Oblivion as the leading donor during this scan.

The work applies the `megamod-capability` and `assetlab-content` skills
adapted from [Universal Modder](UNIVERSAL_MODDER.md). Its scanner at
`8ad57ae7fc922172609328e01f9fc5dc1ceeb8ba` supplied initial fingerprints;
installed file headers and data corrected several guesses. Raw inventories,
source paths, extracted metadata, prototype code, packages and previews
remain in the owner's private content workspace. No proprietary data or
game Lua is reproduced here.

## Scope and evidence

**Fact:** Steam manifests and their corresponding directories identify
15 distinct games, one Abiotic Factor dedicated server, and a duplicate
Heartopia installation in a second library. The scanner processed 17
non-tool installation directories. Seven Steam/Proton runtime/tool records
were excluded from game counts. The owner's existing Halo Trial data and
Minecraft Bedrock server directory are additional donors. An authored
GoldSrc map project also has BSP/MAP/WAD output.

**Fact:** six Steam Workshop application directories contain **226 item
folders**: Space Engineers 148, Garry's Mod 40, Arma 3 16, Project Zomboid
10, Natural Selection 2 eight, Black Mesa four. These are folders on disk,
not a claim that every payload is complete or active. Twenty plain GMod
GMA archives were indexed; the other twenty item folders have BIN payloads
whose contents were not unpacked in this pass.

**Limits:** scanning covered identified Steam libraries, common local
launcher/install roots, the known Trial data and Bedrock server, and the
authored map project. It excluded archival migrated homes, backups,
save contents and server credentials. Universal Modder's file index is
bounded to 80,000 entries and depth six; its format counts describe that
index, not every asset inside archives. Separate probes inspected the
Oblivion BSAs, Black Mesa VPKs and plain GMod GMAs. This is not a claim of
exhaustive coverage of every disk or executable.

## Installed donor catalog

“Existing family” means OAL has a reader for the format family; every asset
still needs dependency, rendering, collision and resource-limit checks.
Readable mechanics data is study material; foreign scripts do not execute
unchanged in MegaMod's host Lua.

| Installed title | Observed content | Useful pieces or lessons | Present route / missing work |
|---|---|---|---|
| **Oblivion GOTY, classic (2009 Steam edition)** | 11 BSA v103 archives, TES4 ESM/ESP; NIF, DDS, KF | Modular ruins, gates, props, weapons, creatures, spells, NPC definitions and AI packages | Three static models proved privately; supported BSA/NIF importer and TES4 cell assembly still needed |
| Counter-Strike: Source | 20 loose BSPs: 14 v19, six v20; VPKs | Compact competitive spaces, office hostage-rescue staging, firearms | Existing Source family; gameplay rules need authoring |
| Team Fortress 2 | 233 loose BSP v20 files; VPKs | Industrial and absurd arenas, class silhouettes, weapons, payload/territory ideas | Existing family; entity/material variants and game-specific objectives need checks |
| Garry's Mod | Two loose BSP v20 maps, Source models, Lua, 40 Workshop item folders | Props, familiar sandbox maps, magic/parkour ideas, crossover roster assets | Existing BSP/MDL/GMA family; SWEP/addon behavior must be rebuilt |
| Black Mesa | 110 BSP v20 maps in map VPK; model/texture VPKs, including Xen map names | Labs, aliens, alien landscapes, atmosphere | Existing family; v20 header alone does not prove game lumps, shaders, geometry budget or entity compatibility |
| Half-Life | 156 loose BSP v30, 793 MDL files, WADs | Low-poly scientists, headcrabs, laboratory corridors | GoldSrc reader needed; current Source BSP/MDL readers do not support these versions |
| Zombie Panic! | 17 BSP v30, 241 MDL files, WADs | Original GoldSrc infection, survivor pressure, zombie environments | This install is the GoldSrc mod, not Zombie Panic! Source; new reader and infection rules |
| Natural Selection 2 | 1,914 indexed Lua files, Spark MODEL assets, DDS textures | Wall traversal, leap, builder/support roles, asymmetric teams | Spark asset adapter/export needed; study installed Lua and rebuild generic mechanics |
| Project Zomboid | Java JARs, shipped Lua; Workshop also has FBX/X models and textures | Timed scavenging/repair actions, vehicle servicing, survival pressure | No current PZ/model importer; PvE/survival behavior and animation adaptation remain new work |
| Factorio | 394 indexed Lua files and prototype definitions | Belts, inserters, assembly, turrets, recipes and production chains | Strong mechanics donor; art is not a ready 3D model library; new simulation and original 3D presentation |
| Space Engineers | 22,923 indexed MWM files, 963 SBC definitions, DDS; 148 Workshop folders | Modular industrial props, ship interiors, vehicle/block composition | MWM adapter/export needed; construction, structural damage and ship simulation are separate capabilities |
| Arma 3 | 488 indexed PBO files; Workshop adds 916 PBOs | Military props, terrain, convoys, locality/authority lessons | Real Virtuality content reader/export needed; large terrains and vehicle complexity need budgeted slices |
| Terraria | 15,994 XNB files | Loot, boss phases, readable enemy telegraphs, chaotic events | XNB decoding and 2D art adaptation needed; no installed tModLoader inferred from Terraria alone |
| Abiotic Factor | Unreal PAK/UTOC/UCAS, with a separate dedicated-server install | Laboratory crafting, scavenging, improvised equipment | Unreal asset export/dependency interpretation needed; no current OAL route |
| Heartopia, two installs | UnityPlayer/GameAssembly, StreamingAssets AssetBundle directory | Cozy scenery and deliberately mismatched presentation | Unity bundle/export compatibility untested; the 58 counted PAK files are Chromium resources/locales, not proof of game asset archives |
| Minecraft Bedrock server (additional) | Behavior/resource pack manifests, loot tables, block metadata, feature definitions, BRARCHIVE text resources | Block layouts, crafting/loot rules, procedural placement ideas | The server is not a complete client art library; ordinary creature geometry/textures and behavior JSON were not found in the inspected pack roots |
| Halo Trial (existing baseline) | Owner's existing cache maps | Blood Gulch, current combat/vehicle baseline | Existing compatibility path |
| Authored GoldSrc maps (additional project) | Nightspire/Ravedojo BSP, MAP and WAD output | Owner-authored layout source as a second GoldSrc consumer | GoldSrc parsing still new; dependency rights need checking before any public content claim |

### Corrections to the scanner

**Fact:** Half-Life and Zombie Panic! were guessed as id Tech because of
WAD signals; their inspected BSP headers are integer **30**, confirming
the GoldSrc family. Arma was guessed as a .NET application; its installed
title/content are **Real Virtuality 4**. NS2 is **Spark**, Space Engineers
is **VRage 2**, Factorio has its own engine, and classic Oblivion is
**Gamebryo/TES4**. Unity's presence in Heartopia is supported by its actual
UnityPlayer and GameAssembly files. Loader guesses and scanner confidence
scores are not treated as proven compatibility or statistical probabilities.

## Oblivion: measured opportunity

**Fact:** the 11 archives indexed successfully as **BSA v103**. The probe
checked file counts, folder/name lengths, payload bounds and overlapping
payload ranges. Their directories contain **146,934 entries**, including:

| Entry type | Count | Meaning / qualification |
|---|---:|---|
| NIF | 9,545 | Model-family files, including skeletons; not all are independent static meshes |
| DDS | 19,983 | Texture files, including maps requiring material interpretation |
| KF | 2,457 | Animation files; not imported in this proof |
| WAV | 2,071 | Audio entries; not imported |
| MP3 | 50,725 | Mostly voice assets; not unique NPC/dialogue counts |

Counts are archive entries across base and installed expansions, without
deduplicating asset paths. Sixteen selected NIF payloads decompressed and
had readable headers: 15 were **20.0.0.4**, one was **10.2.0.0**. A
single-version NIF importer would already miss a sampled shipped asset.

**Fact:** a bounded TES4 record walk reconciled group and record lengths
through the entire master and two plugins. The master contains **6,014
STAT**, **1,137 SPEL**, **145 MGEF**, **2,482 NPC_**, **914 CREA**, **7,209
PACK**, **390 QUST**, **35,494 CELL**, and **1,025,617 REFR** records.
These are stored definitions/records, including variants and test content,
not unique playable spells, people or simultaneously active objects.
Shivering Isles' small ESP contains only a TES4 header record in this
installation; do not infer expansion ownership/load behavior from its
filename without resolving the master data.

**Fact:** directory/record inspection found **408 base-archive Ayleid NIF
paths**, Vilverin interior cell definitions, the Adoring Fan NPC definition,
Daedroth creature definitions, Oblivion-plane rocks and cheese models.
Finding a cell or NPC definition does not import its placements, assembled
appearance, behavior or dependencies.

### Static import evidence

Three actual owner-supplied assets were decoded privately with
[NifTools PyFFI](https://github.com/niftools/pyffi/tree/7f4404dbb8cf832dadd4b3150819340b8764f9b0),
revision `7f4404dbb8cf832dadd4b3150819340b8764f9b0`, and its pinned
[nifxml](https://github.com/niftools/nifxml/tree/f265c56482c728c6877e45d5b5993d3bff83670a)
submodule. NIF scene transforms, normals, triangle strips and first UV sets
became existing OAL `Model` resources; Pillow decoded referenced base DDS
textures. The prototype rejects skins, animated geometry and alpha
properties rather than claiming to support them.

| Model | Vertices | Triangles |
|---|---:|---:|
| Ayleid floor tile | 4 | 2 |
| Ayleid exterior arch | 678 | 574 |
| Cheese | 157 | 212 |

**Measured:** the existing `assetlab project verify` built the private
project in two fresh interpreters. Both packages were byte-identical. The
asset library has three models, six materials and six textures; the
native resource loader accepted its dependencies and gallery world with
key **`4e099307`**. `verification.json` retains package and executable
hashes, compiler reports and native logs.

**Measured:** a separate baked static preview uses the same decoded model
data and existing OALMAP packaging functions. The Engine's existing
`open-halo-map-test` loaded **788 triangles**, drew actual textured arch
and cheese through Vulkan/llvmpipe, and tested **one usable spawn, zero
bodies falling out**. The two views had 12.79% and 56.02% image coverage.
Images were inspected. This is a real rendering/collision proof, not a
claim of the Oblivion Arena, Vilverin, NPCs or a new Showdown mode working.

**Limitations:** Havok collision, normal maps, vertex colours, original
material lighting, skeletons, KF playback, morphs, particles, SpeedTree,
scripts and AI are absent. The library gallery's props collide as model
bounds: an arch placed as such a prop is not automatically a passable
doorway. The separate preview uses visual triangles as static collision.
Preview scale **1/128** and cheese enlargement **8x** are authored tuning,
not calibrated Oblivion-to-MegaMod units. No phone or LAN performance was
measured and no assets were added to the shipping bundle.

PyFFI is a private research dependency here, not a new OAL requirement.
Its NIF support and BSA layout documentation are primary format-tool
evidence; its BSA implementation itself labels production readiness as
unfinished. Any supported reader needs bounded parsing, synthetic fixtures,
version rejection and a reproducible dependency strategy.

## Mashups worth making

All rows below are **proposals**, with their missing work stated. The
existing roster includes Goku and the project already demonstrates imported
Source environments; those are the starting pieces.

| Experience | Concrete pieces | First useful playable slice | Remaining work |
|---|---|---|---|
| **Ayleid Showdown** — leading choice | Oblivion ruin kit + existing roster/combat | One compact ruin room, grounded starts and passable archways | Supported static NIF/DDS path, measured scale/collision and either authored placement or one TES4 cell adapter |
| **Oblivion Gate on Dust2** | Gate/Daedric scenery + Dust2 + mixed roster | Imported gate landmark and an existing trigger/teleport encounter | Gate asset/material/controller handling; later creature importer, PvE behavior and wave rules |
| **Sheogorath's Cheese Emergency** | Actual cheese props + giant scale + hero roster | Cheese scenery in an arena; later a scored collection round | Static resources proved; pickups/inventory/collection rules need an implemented API, moving hazards need separate physics/authority work |
| **McDonald's After Midnight** | Installed `gm_mcronalds` or `gm_abandoned_mall` + Zomboid-inspired pressure | A compact search-and-escape scenario using present interactions | Map conversion checks first; real zombie hordes require model/animation adaptation, PvE AI and a bounded director |
| **Quidditch Gone Wrong** | Installed Quidditch models + Goku + Superman 64 player model | A themed arena using existing flight/combat | Each model's reader/rig/dependency checks; actual team ball/scoring rules and broom handling are new |
| **Xen Infestation** | Black Mesa Xen map + NS2 traversal/team ideas | A bounded Xen arena before introducing one traversal ability | Source compatibility/budgets; Spark creature conversion, wall locomotion, attachment/animation and replication |
| **Factory of Bad Decisions** | Factorio recipes/belts + Space Engineers industrial presentation | Original switches/movers stage an assembly-line hazard prototype | Movers do not already convey players/items; belt forces, recipes, inventories and MWM conversion need separate work |
| **Daedra vs the Military** | Oblivion enemies + Arma convoy props + Halo weapons | A small arena with scenery first, one enemy type later | NIF creatures/animations, PBO/P3D interpretation, PvE and convoy vehicle simulation |
| **Low-Poly Disaster** | Half-Life scientists/headcrabs + original Zombie Panic maps + Goku | One small GoldSrc map/model sample | GoldSrc BSP/WAD/MDL adapters, then creature movement and infection rules |

The installed GMod archives also expose Cryo Magic, Backrooms, a liminal
hotel, graffiti, fishing, drinks and parkour addons. Their names/assets
suggest experiences; their Lua is neither a ready MegaMod ability nor
permission to copy the implementation.

## What to learn from Oblivion

**Inference:** modular dungeon geometry and **base definition versus
placed reference** are the first useful lessons. OAL should resolve a
specific title's form/master context offline and emit ordinary meshes,
materials, placements and diagnostics. Raw FormIDs and TES4 terminology
must not become generic Engine identities. This extends the
[Bethesda/xEdit research](BETHESDA_XEDIT.md) with actual installed evidence.

**Inference:** subsequent mechanics can be generic **effect definitions**
(delivery, magnitude, duration, cost, stacking), **actor goals** (patrol,
follow, flee, investigate), **interaction/loot definitions**, and
**objective stages**. The observed SPEL/MGEF/PACK/QUST records motivate
study; their counts do not establish decoded semantics or runtime support.
Original placeholder content should prove each mechanic before broadening
to imported actors and spells. Current host Lua does not supply the full
Oblivion simulation.

**Unknown:** full NPC assembly/FaceGen, body/armor selection, KF retargeting,
controller timing, Havok-to-generic collision equivalence, master/override
resolution, cell enable states, teleport-door semantics, quest/script
translation and traversal budgets. Oblivion's SCPT records are its own
compiled script system; later Creation Kit/Papyrus examples in general
Bethesda research do not describe classic Oblivion's script runtime.

### Recommended next sequence

1. Promote the small static proof into a supported, versioned OAL reader:
   bounded BSA acquisition/resolution, restricted NIF mesh decoding,
   explicit material losses, source hashes and original synthetic tests.
2. Calibrate units, axes, root transforms, texture conventions and collision
   on several independent assets. Assemble a compact Ayleid room and
   prove traversal in the native runtime; use the visual mesh or generated
   collision deliberately, not a doorway-blocking prop AABB.
3. Resolve **one interior cell's** static placements, initially Vilverin as
   a candidate. Report every missing model and every omitted interactive,
   disabled or scripted object. Avoid starting with all Cyrodiil terrain.
4. Run the current combat/roster in that room, check host/peer package
   identity and behavior, then measure an actual Android device.
5. Select one spell effect or one creature after that content slice. Study
   its behavior, implement the minimum generic capability, and give it an
   original second consumer. Quests, NPC schedules and full open-world
   simulation remain later decisions.

Game data, extracted assets and previews stay private. Public research
documents facts and proposed mechanics. Tool licenses do not confer rights
to redistribute the games' assets or scripts.
