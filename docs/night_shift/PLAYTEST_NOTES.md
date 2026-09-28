# Night Shift -- playtest notes

Production notes, not marketing. **How it was played:** by the agent that
built it, through the real game: a headless host (`megamod-match --host
--trace-events`) and desktop joiners walking scripted routes, pressing,
waiting and taking screenshots (`megamod-join --route --shot`), over a
dozen host sessions; then the Android emulator hosting with its own player in the
dock and a desktop crew joining. The agent sees frames and reads the
host's event trace; it cannot hear, and it does not hold a joystick. **A
hands-on session by a person -- ideally two phones -- is the open check**
(CURRENT TESTING OBJECTIVE in HANDOFF.md). Everything below is what the
screenshots and traces showed.

## What worked

- **The opening teaches itself.** Both dock doors buzz and stay shut; the
  only open hole has a sign over it. After the breaker, the door you
  couldn't open has a green strip over it. No text was needed to connect
  "power" with "the button works now".
- **The lamp strip is the most useful object in the game.** Green over a
  door = it has power. It is visible from the spawn (7 wu), from behind
  the door, and on the lift. Cheap to read, 1 entity each.
- **The AND gate reads.** The board's two lamps light one at a time; the
  second one lands with a power-up tone where you stand and at D3.
- **Readable silhouettes in the dark:** consoles, breakers, the valve and
  the glowing core are bright against near-black walls (the scene light
  that makes doors too bright makes interactables pop).
- **Lockdown changes the world visibly:** from the dock after lockdown the
  door lamp is dead, the ceiling light is gone and a new black opening
  (the tunnel shutter) is there. Late joiners see exactly that.
- **The cold spot is the best moment.** Walking the wing alone, you are
  simply somewhere else -- a black room, a chord of breath -- with one
  way out into the specimen hall. With a friend beside you it only stirs.
  It makes staying together a real decision without any UI.
- **Steam timing** in the tunnel works as a pacing beat: the plume drops
  on the klaxon; wait, then go.

## Good moments (observed in traces/shots)

- The knock behind D3 while it is still dead.
- The core rising for 1.7 s before the alarm -- the gap is the scare.
- The dock seen again at the end, dark, with the lift lit green.

## Confusion (likely for a human)

- **The aux breaker is behind the generator**; from the aux room's door
  you see a green-grey block and no breaker. Intended as a small search,
  but in real darkness it may be a stall.
- **The pump room's live water** has no warning sign; the dangling cable
  and spark are the only tell. First contact costs 20 health (by design,
  once).
- **The research board's empty sockets** only make sense once one lamp is
  lit; before that it is a sign with a blank strip.
- **The core's use** has no prompt beyond its glow; players must try it.
- **The surface** ends nothing: no fade, no score screen, the match goes
  on (rules friction E4). Players may wonder if they are done.
- Pressing a dead door's back button from the corridor buzzes like the
  front -- consistent, but nothing says *where* the power comes from.

## Boring sections

- **The main corridor** (20 wu, one event -- the knock) and the
  **tunnel's south leg** (26 wu) are long, empty walks.
- **Backtracking** from the aux room to D1 (~20 wu, nothing new).
- All three are where more content would go; the entity budget has 2 left.

## Bugs found and fixed (content)

| Found | Fix |
|---|---|
| crates across the maintenance passage's mouth | moved (a route got stuck) |
| a crate across the tunnel's dock exit | moved (route stuck at the shutter) |
| the moved crate then on the lift approach | moved again, west side |
| every texture upside down (hazard band at a door's head) | rows written bottom-up (box faces sample v = 0 at the foot) |
| doors and shutters washed out (lit ~3x) | textures authored ~45% darker |
| sign text tiny; the `<` arrow pointed the wrong way | 2x font; arrow removed |
| glyphs J, 3, 5, - malformed; S read as 5 | glyphs redrawn; a 5x7 assertion in art.py and a test |
| plant noise every 2.5 s | heartbeat slowed to 4 s a swing |
| nothing said why the crew is here | the work order sign facing the spawn |
| finishing the research gate was only audible at D3 | power-up tone at the panel that completed it |
| the escape had no pressure | a stinger behind you in the tunnel after lockdown |

## Engine limitations felt in play

- Players carry an assault rifle and pistol and can shoot each other; the
  HUD says "In 1st place with 0 Frags" (E4). This hurts the tone more than
  anything else on screen.
- No real darkness or light changes (E3): the "lights going out" is one
  strip moving. The dark colours carry the mood; the emulator's HUD and
  bright weapon model fight it.
- Sounds pass through walls (no occlusion, E10): the alarm at the core is
  as clear from the tunnel as in the room.
- Standing in the live water or under steam is safe after the first touch
  (E8).

## Engine / tooling confusion (not the player's fault)

- None observed for a player that the content could not route around. For
  the author: see OAL_FRICTION (placement mistakes were only visible by
  walking into them).
