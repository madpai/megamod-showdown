# Megamod Showdown gameplay review — 2026-09-24

## Verdict

The crossover premise works, but the engine currently gives most characters a shared shooter controller plus stat multipliers and repeated weapon effects. The next milestone should make Goku, Superman and one ordinary human satisfying opponents on one larger, properly lit arena. Expand the roster after that standard is repeatable.

This review examined the current dirty halo-sandbox checkout, the twelve character packages in the private bundle, map package headers, an offscreen Construct render, and host bot simulations. It is not a hands-on phone playtest. Existing uncommitted hero, gib, audio and UI changes were preserved. The initial review was followed by the owner reporting bot kit theft and spammable one-shot abilities; the combat fixes below address those reports.

## Findings and priorities

| Priority | Evidence | Required work |
| --- | --- | --- |
| P0: flight presentation | `src/game/view.c:imported_update` explicitly selects idle and tilts it 0.12 radians while flying; airborne attack posing is excluded. This avoided a broken Superman jump clip. | Separate hover, acceleration, cruise, braking, ascent and attack poses. Blend between them; bank with turns, aim head/torso independently. Author or retarget skeleton-specific flight poses; do not restore the broken jump clip as a shortcut. |
| P0: ability feel | `game.c:hta_game_ability` fires immediately and repeats shots during a timed active window. Beams share a 100-unit reach and add 0.48 units to target hit radius. | Add charge/windup, active, recovery and cancel states with clear animation/audio cues. Goku: two-hand charge, committed release, recoil. Superman: eye tracking, controlled sweep, heat buildup. Tune collision against visible beams and keep server authority. |
| P0: lighting | OALMAP v1 loader assigns albedo material groups without lightmap bindings. Android explicitly uses generic lighting for imported worlds. Construct visibly lacks grounding shadows. | Extend importer/package/loader together to preserve Source baked lightmaps, UVs and material assignment. Add local light sampling for characters, then contact shadows and bounded ability lights. Preserve brightness/color-space conventions. A global brightness adjustment cannot restore missing lighting. |
| P0: balance | Bundled Superman: 8x health, 6x shields, 3x damage, 8.2 flight speed. Goku: 6x, 5x, 2.6x, 7.8. Urban: 1.05x health, no shield. Several advantages stack simultaneously. | Establish match roles and effective-health/damage budgets; measure actual time-to-kill, damage per activation, uptime, mobility and objective value. Keep spectacular attacks but give opponents warning, cover, escape and punish windows. Ordinary humans need useful gadgets or objectives. Separate unrestricted chaos from balanced rules if both are wanted. |
| P1: physics | `game.c:corpse_move` integrates one body position and tumble angle; stops motion after 2.6 seconds even if airborne. Ground bounce is not a joint solver; the corpse path has no wall sweep. `view.c` adds cubic debris. | Swept corpse collision first, then pelvis/torso/head/limb constraints, angular limits, friction and energy-based sleep. Inherit death pose and momentum. Limit active ragdolls, sleep settled ones and allow cosmetic client simulation from authoritative death impulses. Test stairs, walls, high falls and respawn cleanup. |
| P1: audio | All twelve bundled character manifests have zero sounds. Android generates power sounds, shout-like barks and a broom tune; imported weapon hooks currently cover fire/reload. | Import available character voice clips and dedicated effects; add charge, loop, release, impact, pain, death, dash, flight and landing events. Use spatial attenuation, concurrency limits, variation and dialogue cooldowns. Keep voice lines occasional so combat cues remain audible. |
| P1: maps | Five maps in inspected bundle; full geometry bounds below. Small interiors constrain fast flyers. | Add a larger outdoor arena with a skyline/vertical routes, ground cover, interiors, multiple routes and safe spawns. Target roughly 1.5–2x current usable combat span as an initial design experiment. Measure encounter times and flight crossing times; do not uniformly scale existing geometry. |
| P2: roster | Twelve bundled characters; engine character capacity 32. Presentations and move sets are mostly shared. | Build reusable brawler, beam caster, gadget human, robot and alien archetypes. Require complete animation, sound and balance coverage per addition. Expand in batches, not an untested list of skins. |
| P2: more bots | Menus and Android clamp at seven bots; network world encoding also rejects more than seven. Engine/network entity capacity is 16 and renderer dynamic capacity is 32. | Profile 8/12/15 bots on target phone with hero effects and ragdolls. Update UI/native validation/protocol together, reserve human slots, and check held-weapon/dynamic mesh budgets. Do not simply raise one constant. |

Ability ticks previously discarded timing overshoot (`hta_game_update`). This review now fixes that and tests matching whole-channel damage at 30/60/120 Hz. Hitch testing and device balance playtesting remain useful follow-ups. Local and remote corpse movement also use different paths and need parity checks.

## Map inventory

Bounds are complete geometry extents in runtime world units, not verified navigable arena dimensions. Large sky/hidden geometry can inflate them. At Superman's configured 8.2 units/s, a 65-unit span is only about eight seconds of unobstructed flight.

| Map | X × Y × Z | Spawn records | Triangles |
| --- | --- | --- | --- |
| Office | 37.5 × 32.2 × 4.9 | 40 | 284,460 |
| 2Fort | 51.3 × 78.7 × 19.1 | 32 | 1,243,866 |
| Aztec | 68.7 × 42.0 × 28.6 | 40 | 51,272 |
| Dust2 | 81.5 × 43.4 × 19.3 | 40 | 93,858 |
| Construct | 65.1 × 92.0 × 99.9 | 33 | 38,371 |

McRonalds appears in the previous handoff, but was not present in the inspected bundle root. This inventory is not a claim about every asset inside the previously published APK. 2Fort's triangle count also makes it a useful rendering stress case before increasing match size.

## Proposed roster direction

First fill missing play styles: a second Saiyan with a distinct attack pattern; an agile superhero; a ground-based gadget hero; a regular civilian with improvised equipment; a robot with heat/ammo management; and an alien with unusual movement. Candidate identities can include Vegeta/Piccolo, Spider-Man/Batman, Gordon Freeman, a citizen, a Combine robot and a Halo Elite. These are design candidates, not installed or verified assets. Each character needs a recognizable silhouette, a useful basic attack, a signature move, a weakness and readable sound cues.

## Implementation sequence and acceptance

1. **Combat vertical slice:** Goku, Superman and a human on Construct. Distinct hover/cruise/attack poses, readable ability states, consistent tick damage and real sound coverage. Phone and LAN testing must verify aim, interruptions, cooldown and remote effects.
2. **World presentation:** one larger imported map with baked lighting, matched character lighting, safe spawning and cover against flyers. Inspect interiors/exteriors on phone; verify navigation and collision independently of rendering.
3. **Physics slice:** collision-safe knockback and articulated ragdolls under a measured CPU budget. No tunneling, suspended corpses or exploding joints; graceful sleep and cleanup.
4. **Expansion:** add archetypes and maps in tested batches, then offer larger bot counts after device profiling. Track simulation, animation/skinning, rendering, audio and network costs separately.

## Initial review changes (before flight/content follow-up)

- Flight target velocity now respects the configured speed cap when forward/strafe/ascent inputs combine. Partial analog steering and external impulse decay are preserved. Regression tests exercise 30/60/120 Hz and quarter input.
- `htamatch` imported-character fallback lighting now matches Android (0.46 directional / 0.30 ambient), replacing the obsolete 1.0 / 0.7 desktop values. This improves review fidelity; it does not add map lightmaps.
- Character bots now keep their authored weapon kits across spawn selection, inventory grants, map pickups and dropped pickups; AI no longer chooses forbidden weapon goals. Fallback bots cannot borrow imported gear listed in another character's kit. Human players can still scavenge physical weapons. Hidden abilities and innate fist/ki/repulsor weapons cannot drop as loot.
- Beam/pulse damage is now a whole-activation budget, without the extra basic-attack hero multiplier. Beam budgets are capped at baseline health + shields; crowd-control pulse budgets at 60% of baseline health, before distance falloff. These are deliberately invented initial balance limits, not source-game stats. Projectile abilities still use base-weapon multipliers and need a separate balance pass (e.g. Leet's salvo).
- Recovery lasts at least ten seconds after the active phase; the LAN client HUD uses the same floor plus channel duration. Cadence retains timing overshoot, with no extra endpoint shot. The tests cover a strong shout leaving an unshielded target alive and piercing beams damaging multiple targets without first-tick kills.
- No flight animations, jointed ragdolls, real character clips, larger maps or new roster entries are claimed as completed here.

## Validation

- Before changes: full verification 80/80.
- After flight fixes and again after the bot/ability fixes: full verification 80/80, including Android build and APK asset-boundary checks. Combat log: `scratch/review-combat-verify.log`.
- Flight regression/player collision suite after changes: 91 checks passed.
- Construct, eight ordinary bots, 30 simulated seconds: 18 kills, 1.209 ms simulation per tick on this desktop. This excludes phone GPU performance and is not a hero balance benchmark.
- Construct, sixteen ordinary bots, 30 simulated seconds: 51 kills, 2.297 ms simulation per tick. Host stress test only; it bypasses the seven-bot menu/protocol limit and uses all sixteen unit slots.
- Review artifacts/logs are in `scratch/review-*` (private, untracked).

Published personal and asset-free guest APKs as **Hero kit ownership and ability balance**, `ed4585f-dirty`, at http://100.89.1.14:8733/. Both served files pass SHA256 checks. Local backup completed. First publication failed reading the personal APK; a materialized copy verified before replacement resolved publication. Phone feel remains unverified.

## 2026-09-25 follow-up

Flight and beam poses now have procedural arm/head articulation with smoothed
cruise pitch and banking, visually checked on Goku, Superman and Iron Man.
Added Chell, Combine Elite (with real source hurt/death sounds) and Compound.
Port remains outside the bundle because bots reached its seabed without water
physics. Compound: all 184 materials resolved, 33/33 spawn tests, eight bots
completed 60 seconds with 24 kills. Engine gate 80/80, importer suite 36/36.
These additions do not complete baked map lighting or jointed ragdolls.
