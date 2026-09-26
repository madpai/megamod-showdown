# GameNetworkingSockets and MegaMod transport

**Evidence/version:** Valve's [GameNetworkingSockets README](https://github.com/ValveSoftware/GameNetworkingSockets) and [message-type API](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/steamnetworkingtypes.h), read 2026-09-26; local [MegaMod vision/current v9 baseline](../MEGAMOD_VISION.md) and [network architecture](../NETWORK_ARCHITECTURE.md). The GitHub repo is public with third-party dependencies; review its exact license/dependency tree before any adoption. No adoption or code copying is proposed here.

## Architecture, messages, and authority

**Fact.** GameNetworkingSockets supplies connection-oriented UDP messaging with reliable and unreliable modes, sequencing, fragmentation/reassembly for reliable messages, retransmission, encryption, and NAT traversal through ICE. These are transport services. They do not define a game's authoritative state, entity serialization, custom mod schema, prediction, interpolation, or content compatibility policy. MegaMod v9 already has nonblocking UDP, bounded/versioned decoders, host-owned game decisions, snapshots/events, interpolation, per-source rate limiting, and LAN/direct-IP connectivity; its vision states authentication is not yet present. These are different layers.

**Inference.** Keep current transport for LAN while measuring actual loss/latency. For Internet play, MegaMod must eventually address connection identity/authentication, NAT traversal/relay or rendezvous, encryption, MTU-aware packetization, reliable control/event channels, retransmission/backpressure, congestion/abuse limits, reconnect, and content matching. A library can help some transport items but cannot solve gameplay authority or package distribution.

## Entity/network design implications

**Fact.** The public API distinguishes message delivery policy. It does not require sending every entity field reliably. MegaMod's current match model already uses replaceable snapshots and feature-specific messages; [archived network notes](../NETWORK_PROGRESS.md) describe earlier protocol versions and must not be mistaken for v9.

**Inference.** Classify outgoing messages: frequent transforms/velocities as unreliable sequenced snapshots; creation/destruction, inventory changes, round state, and break decisions as reliable or acknowledged events; cosmetic impacts as lossy when safe. Give each modded definition a startup-compiled schema ID and ensure both peers have the same package lockfile/content hash before gameplay. Never accept a Lua-supplied arbitrary packet type or unbounded payload.

## Content, world, physics, scripting, tooling

**Fact.** GNS is intentionally below content systems. It offers no mod package format, scripting API, world map, physics representation, or authoring tool. Its relevance to physics is delivery priority: host body state and event messages must respect packet loss and bandwidth.

**Inference.** OAL should compute a canonical package lockfile/hash and network schema digest from compiled definitions. MegaMod should log per-channel send rates, retransmissions, RTT, loss, and reconciliation error. A transport swap should be a separate experiment behind the existing `src/net` interface, only after a measured Internet-play requirement justifies the Android binary/dependency cost.

## MegaMod conclusions

- **Adopt:** explicit delivery classes and connection-state thinking; measure packet loss, size, and backpressure.
- **Consider later:** GNS or an equivalent mature transport for Internet service after a compatibility and Android size audit.
- **Avoid:** replacing the working LAN stack by default, coupling gameplay packets to one library, trusting client-authored mod events, sending every physics body reliably.
- **For Open Asset Lab:** deterministic package and replication-schema digests for join compatibility; validate field widths and max event sizes.
- **Prototype:** a loss/latency simulator around the existing protocol plus a package-hash handshake; record event delivery, bandwidth, and host/client divergence before choosing a new transport.
