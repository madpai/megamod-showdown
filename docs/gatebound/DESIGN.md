# Gatebound: The Borrowed Apocalypse

## Player loop

A cooperative wave survival mode for Showdown. Solo characters belong to the
local installation. Online characters belong to the server that hosts the
match. Changing roster appearance keeps the same progression.

Survive enemies arriving from several gates, collect gold for kills and
cleared waves, then shop, refill ammunition and recover before the next wave.
Fallen players return between waves; late arrivals wait for that recovery.
A party wipe starts a fresh run after a short defeat screen. Saved progression
survives both defeat and quitting.

## Progression

Athletics grows from grounded movement. Acrobatics grows from jumping and
landing. Unarmed and Blade grow from successful melee damage. Marksmanship
and Destruction grow from gun and spell damage. Restoration grows from actual
healing; Defense from damage taken. Misses, friendly fire, empty healing,
flight and teleports do not grant practice.

Skill rank is floor(sqrt(skill XP / 100)). Character level is
1 + floor(sqrt(level XP / 1000)). There is no designed skill or level ceiling;
64-bit storage saturates rather than wrapping. Benefits diminish as ranks rise.
At level 50, optional prestige resets level XP only. Skills, currency, upgrades
and equipment stay. Prestige gives an authored XP bonus with diminishing
returns. This choice was confirmed by the owner.

Gold buys Vitality, Stamina, Magicka, Power and Fortune tiers. Tier prices grow
quadratically, bonuses with square roots. Equipment is separate: guns and
blades, damage-reducing armor, selected spells and consumable potions. Equip a
weapon into the currently selected hand; armor and the active spell have their
own slots. Stable item IDs survive catalogue reordering and preserve items
that a later content pack removes.

## Content direction

The original arena is the reproducible public mechanics fixture. The personal
APK replaces both Gatebound map entries with the private donor arena: actual
Oblivion gate props, tiled Ayleid floor meshes, castle textures, Daedroth,
cheese, iron sword and male hand geometry. The inventory uses extracted
Oblivion parchment and item icons; the HUD uses its health/magicka/fatigue
artwork. Layout, spell behavior and the cross-game enemy catalogue are authored.

Desired mashups: Daedroths with shotgun guards; a cheese-themed boss wave;
Goku defending an Ayleid courtyard against Workshop zombies; chain lightning
and swords beside imported guns. Those are Showdown content choices. Native
Engine definitions remain generic.

The native survival configuration is world_entities schema 9, included in the
existing world key. Open Asset Lab validates the same limits. Foreign scripts
and game data stay in Asset Lab/private working folders. A donor's artwork does
not imply its original AI, Havok collision, quests or animations were imported.

## Authority and persistence

Only the host awards XP/gold, applies spells, accepts purchases and writes
characters. Clients send a stable 128-bit bearer identity and numbered intents.
The server refuses simultaneous use of one identity. Requests survive UDP loss;
repeated/stale request counters cannot buy twice. Progress snapshots are sent
only to their owner and are tied to the session token.

This is the existing LAN protocol, without account authentication or encryption.
Online saves never accept a client's offline statistics. Corrupt or unsupported
saves are preserved and refused. Purchases commit through a temporary file,
fsync and atomic replacement; a failed save rolls back the transaction.
Practice checkpoints every ten seconds and at wave transitions/disconnect/exit.
Unexpected termination can lose practice since the last checkpoint.

## Initial authored tuning

30 second breaks; 1.5 second spawn intervals; 4 enemies in the first wave,
2 more per wave; additional players increase the queue by 50 percent each.
At most eight enemies are alive at once within the existing sixteen-unit pool.
Enemy health/damage increase with sqrt(wave), avoiding exponential inflation.
Three gates; shop reach four world units. A wipe pauses eight seconds.

The public fixture starts with a sidearm; the private arena starts with the
Oblivion iron sword, spell hands and Ember Bolt for free. Basic armor reduces damage 15 percent,
plate 40 percent, ward adds 35 percent; combined reduction is bounded at 85.
Magicka starts at 100 and regenerates 5 per second. Stamina starts at 100,
regenerates 15 per second, and jumping spends 10. Spell reuse delay is .6 seconds.
These values are ours, not recovered Oblivion constants.

## Play

Choose **Gatebound** or **Gatebound Mashup** from the personal APK's map list.
The map selects survival automatically. Tap **Inventory** to see practiced
ranks and the categorized shop. Purchases require the counter during a
break; equipment and potions can be used while alive. **CAST** casts
the selected spell. Prestige becomes available at level 50 during a shop break.

Desktop: `megamod-join --trial <maps-dir> --bundle <private-bundle> --world
<gatebound|gatebound_mashup> <host> <port>`. B opens the shop, arrows select,
Enter buys, Tab equips, H drinks, P prestiges, Q casts. The title shows the
selected item and profile totals. `megamod-match --host <port> --cache
<writable-server-dir>` hosts persistent characters; use different writable
server folders for distinct servers. An identity file is a bearer credential:
keep it to retain the character, and run one process per save directory.

## Import scope

The optional Asset Lab `oblivion-model` command reads local classic v103 BSA
archives and NIF 20.0.0.4 diffuse geometry. Static models use existing libraries;
explicit frozen actors use existing character/weapon packages. Daedroth has
no skeletal clips. Gate particles, Havok, KF, quests and plugin records are
unsupported. Original sword/hand meshes use authored root-motion clips for
draw, swing and casting; original Oblivion finger/skeletal KF animation is
not imported. Workshop claws retain their attack animations after Asset Lab
combines their animation-only model with the actual Source citizen hands. The Source zombie
uses its walk sequence for the authored run roles. Armor purchases currently
change damage reduction without changing worn appearance.

## Verification

Original and current private projects build twice identically and match native
world keys bc17ebe0 and a0dab19c. Original native combat cleared two waves
with ten kills, 490 gold and 1,015 level XP. An earlier private smoke test did
not load the imported roster; it proved survival behavior with fallback art,
and is not evidence of donor actors. The corrected match tool loads that
roster before starting. A matched host/peer connected with the actual gate
props and imported actors; a subsequent native run killed an imported Daedroth
and preserved 145 gold and 287 XP through a wipe. These values are evidence
from the recorded run, not balance targets.

Android emulator screenshots exposed overflowing native status, oversized
Android dialogue text, missing CAST, Halo sky/HUD overlays and an inverted
sword. The corrected custom HUD/menu and camera placement are checked in the
actual Android renderer. Emulator timings do not establish phone performance.
Profile and UDP tests cover purchase replay, invalid gold, recovery, wipe,
missed spell mana consumption without XP, actual spell practice, friendly fire,
missing-content refusal, duplicate identities, private snapshots and reconnect
persistence. Protocol fuzzing includes RPG random packets and canonical bit
flips under AddressSanitizer.
Damage and healing practice are normalized to 100 XP per full target vitality;
spell power is a fraction of the base vitality template.
Physical touch feel, wave balance and long-session grinding need owner playtests.
