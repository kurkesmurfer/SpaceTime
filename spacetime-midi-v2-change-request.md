# SpaceTime MIDI: channel consolidation and instance routing (development protocol)

Change request for the SpaceTime MIDI module and `MIDI_IMPLEMENTATION_CHART.md`.
From: IpadForge / DROID integration, 2026-11-10.

## Management summary

SpaceTime currently occupies **twelve** of the sixteen MIDI channels on its input: 1–8 (HEADs), 9 (HEAD ALL), 10 (requests), 15 (stage sliders) and 16 (PROGRAM). Channels 9 and 10 carry very little traffic, and channel 10 is the General MIDI drum channel, which every DROID drum track uses by default.

Proposal: move the channel-9 HEAD ALL controls and the channel-10 request/reply traffic onto the configurable PROGRAM channel (16 by default). Give each role a distinct CC block. Make PROGRAM feedback follow the PROGRAM channel and Stage feedback follow the configurable stage-slider channel. With the defaults, SpaceTime occupies **1–8, 15 and 16**; channels **9–14** — six channels instead of four, including drum channel 10 — are free for other instruments on the same USB MIDI port.

Existing PROGRAM CCs and Program Change assignments stay in place. CC 93 adds the new Pulse Time control. Add a configurable **HEAD channel start** to the MIDI module; each Head uses the start channel plus its zero-based Head index. Assign unique HEAD ranges, PROGRAM routes, and Stage routes between instances; a PROGRAM route may share a channel with one of its own Heads. The feedback version response may report 2 during development to identify this protocol revision; its first-release value is not committed and may be reset to 1 if no earlier version has shipped.

## 1. Current state (protocol v1)

| Channel | Direction | Content |
|---:|---|---|
| 1–8 | in | HEAD 1–8 controls, CC 0–13 and CC 18 TIME MULT |
| 1–8 | out | HEAD 1–8 feedback, CC 1–18 |
| 9 | in | HEAD ALL controls: CC 0–12 broadcast to all HEADs (CC 13 ignored) |
| 9 | out | reserved for future HEAD ALL status; nothing emitted in v1 |
| 10 | in | requests: CC 0 HEAD snapshot (0–7, 8 = all), CC 1 PROGRAM snapshot (64–127), CC 2 stage page (0–7), CC 3 protocol probe (64–127) |
| 10 | out | CC 3 version reply (value 1), CC 118 / 119 snapshot begin / end (value = area code) |
| 15 | in | stage sliders: CC 0–63 voltage, CC 64–127 time |
| 15 | out | stage feedback: CC 0–63 voltage, CC 64–127 time |
| 16 | in | PROGRAM CC 64–91, CC 92 (Select hold), Program Change 1–12 |
| 16 | out | PROGRAM feedback CC 0–4 and CC 16–29 |

Free space on channel 16 today: **in** CC 0–63, **out** CC 5–15 and CC 30–127. Channel 15 is full in both directions.

## 2. Proposed state (protocol v2)

### 2.1 Channel allocation

| Channel | Role |
|---:|---|
| Configured HEAD range | HEAD controls and feedback, contiguous from the MIDI module's HEAD channel start (default 1) |
| 9–14 | unused by the default single-instance layout (free for other instruments) |
| 15 | stage sliders and stage feedback (configurable, default 15) |
| 16 | PROGRAM controls and feedback, requests/replies, and HEAD ALL controls (configurable, default 16) |

With the default settings and eight Heads, the HEAD range is 1–8. Channels 9–14 are unused at those defaults; they become available for additional SpaceTime instances or other instruments. The PROGRAM, Stage, and HEAD-start settings determine actual channel use.

### 2.2 PROGRAM channel, input (controller → SpaceTime)

| CC | Meaning | Values | Was |
|---:|---|---|---|
| 32–44 | **HEAD ALL controls**: CC 32 + n behaves exactly as v1 channel-9 CC n (n = 0…12) | as HEAD CC n | ch 9 CC 0–12 |
| 45 | ignored (HEAD Display, CC 13, is never broadcast — as in v1) | — | ch 9 CC 13 |
| 46–49 | reserved | — | — |
| 50 | HEAD snapshot request | 0–7 = HEAD 1–8, 8 = all HEADs | ch 10 CC 0 |
| 51 | PROGRAM snapshot request | 64–127 | ch 10 CC 1 |
| 52 | stage page snapshot request | 0–7 = stages 1–8 … 57–64 | ch 10 CC 2 |
| 53 | protocol probe | 64–127 | ch 10 CC 3 |
| 54–63 | reserved | — | — |
| 64–92 | existing PROGRAM controls — unchanged | | |
| 93 | Pulse/retrigger time | continuous 0–127; same scale as the panel control | new |
| 94–111 | reserved | — | — |
| 112–116 | reserved | — | — |
| 117–119 | protocol replies/framing; input ignored | — | — |
| Program Change 1–12 | presets — unchanged | | |

So HEAD ALL Start is CC 33, Stop CC 34, Advance CC 35, Reset CC 36, Address CC 37, Address source CC 38, Addressing CC 39, Direction CC 40, Clock source CC 41, Div/Mult CC 42, Time CV amount CC 43, Loop mode CC 44, and the virtual-clock edge CC 32 (still effective only on HEADs whose clock source is Virtual). Value encodings and bins are exactly those of the HEAD CCs in §4.1/4.2 of the chart. Pulse Time CC 93 uses the same 0–127 normalized control as the panel, mapping 0.1 ms at minimum, 1 ms at midpoint, and 15 ms at maximum.

### 2.3 PROGRAM channel, output (SpaceTime → controller, feedback output)

| CC | Meaning | Values | Was |
|---:|---|---|---|
| 94–113 | PROGRAM feedback, including Pulse Time at CC 113 | 0–127; Pulse Time uses the panel's normalized scale | remapped for channel sharing |
| 117 | **version reply** to a probe | development value currently 2; release value TBD | ch 10 CC 3, value 1 |
| 118 | snapshot begins | area code (unchanged: 0–7 HEAD, 8 all HEADs, 16 PROGRAM, 32–39 stage pages) | ch 10 CC 118 |
| 119 | snapshot ends | same area code | ch 10 CC 119 |

PROGRAM feedback moves to CC 94–113 so it cannot be mistaken for per-HEAD feedback when a PROGRAM channel shares a channel with a HEAD. Its previous output map used CC 0–4 and 16–29. The new block preserves the previous value order: CC 94–98 are the previous CC 0–4 values, CC 99–112 are the previous CC 16–29 values, and CC 113 is Pulse Time. Requests use CC 50–53; the version reply uses CC 117; framing uses CC 118/119. These Program/control-plane input and output blocks do not overlap. As before, do not route all MIDI feedback back into SpaceTime's input: per-HEAD feedback intentionally reuses per-HEAD control CCs.

| CC | Meaning | Values | Was |
|---:|---|---|---|
| 94 | Selected stage | exact zero-based index 0–63 | ch 16 CC 0 |
| 95 | Key | exact index 0–11 for C–B | ch 16 CC 1 |
| 96 | Scale | 0 Major, 1 Minor, 2 Chromatic | ch 16 CC 2 |
| 97 | Pulse Retrig | 0 Off, 127 On | ch 16 CC 3 |
| 98 | Bulk arm active | 0 Off, 127 On | ch 16 CC 4 |
| 99–112 | Selected-stage settings | same encodings as v1 PROGRAM feedback CC 16–29 | ch 16 CC 16–29 |
| 113 | Pulse/retrigger time | continuous 0–127; same scale as panel CC 93 | new |

Framing rules are unchanged: begin/end only around explicit snapshots, never around live deltas. The 1 ms message spacing (needed for DROID blue-7) still applies to all feedback, including the framing.

### 2.4 Which channel exactly

PROGRAM controls and feedback, HEAD ALL controls, requests, version reply, and snapshot framing all ride on the **PROGRAM-controls channel setting** (context menu, default 16). Stage input and feedback both follow the **stage-slider channel setting** (default 15). Per-HEAD input and feedback follow the contiguous range starting at the MIDI module's configurable HEAD channel start (default 1).

The PROGRAM channel may share a channel with a HEAD: their input CC blocks do not overlap, and the separate output CC blocks keep their feedback distinguishable. The Stage channel must differ from the PROGRAM channel and every active HEAD channel because Stage values use the full CC range. Stage feedback follows this setting too. The MIDI module's HEAD-start setting maps Heads contiguously from that channel. Its valid range must keep every active Head within channels 1–16, without wrapping around; if the active Head count grows beyond the available channel range, the configuration must report or prevent that overflow. Across instances, HEAD ranges, PROGRAM routes, and Stage channels must be allocated so that no two instances claim the same control/state route.

With per-instance HEAD channel assignment, this layout can support eight one-HEAD instances by pairing each instance's PROGRAM channel with its HEAD channel and assigning each a distinct Stage channel. A denser-head example is two six-HEAD instances: twelve distinct HEAD channels plus two distinct Stage channels, with each PROGRAM channel sharing one of its own HEAD channels. These are capacity examples for the proposed routing; they depend on assigning unique HEAD ranges, PROGRAM routes, and Stage channels across instances.

### 2.5 Behaviour on the old channels

- At the default settings, SpaceTime **ignores** channels 9 and 10 in both directions. A drum track on channel 10 sending CC 0 must not trigger a HEAD snapshot; CC 1 must not trigger a PROGRAM snapshot. If a user explicitly assigns the PROGRAM or Stage route to either channel, that route is active there.
- No v1 compatibility mode is required before the first SpaceTime release. Existing development patches and controllers will be migrated to v2 before release.

### 2.6 Probe and discovery

A controller probes with PROGRAM-channel CC 53 = 127. SpaceTime answers CC 117 with the current protocol-version byte on the configured PROGRAM channel. During development that may be 2; the first-release value may be reset to 1 if no earlier protocol has shipped. No v1 probe fallback is needed before the first release.

## 3. Implementation notes

1. Input routing: the MIDI input is forced to All channels. Route independently by channel and CC: the contiguous HEAD range begins at the configured HEAD channel start and uses CC 0–13 plus CC 18 TIME MULT; the PROGRAM channel uses CC 32–44 for HEAD ALL, CC 50–53 for requests, and CC 64–93 for PROGRAM; the stage channel uses CC 0–127. If a PROGRAM channel equals a HEAD channel, both non-overlapping maps must be serviced. The Stage channel must not overlap any active HEAD, PROGRAM, or other SpaceTime route.
2. HEAD ALL handler: reuse the v1 channel-9 code path with `cc - 32`. The "HEAD ALL module installed or not" behaviour stays as in v1: the broadcast addresses all HEADs directly through the MIDI module.
3. Feedback encoder: map each Head's input and feedback channel from the configured HEAD start plus its index; make PROGRAM feedback follow the PROGRAM channel on CC 94–113; make Stage feedback follow the stage channel; move the version reply to CC 117 and framing to the PROGRAM channel. Use version byte 2 during development if useful; choose the first-release value separately.
4. Add Pulse Time input at PROGRAM CC 93 and feedback at PROGRAM CC 113. Include Pulse Time in PROGRAM snapshots and deltas. Update `MIDI_IMPLEMENTATION_CHART.md`, the manual's MIDI page, and the website's SpaceTime documentation before release; these materials do not yet describe the Pulse Time control.
5. HEAD TIME MULT is per Head and uses CC 18 on the configured HEAD channel. It spans ×0.5 to ×4 logarithmically, with ×1 at the control midpoint. It is excluded from HEAD ALL because Heads may need different tempo trims.
6. Update the DROID files in `droid/` (`SpaceTimeStage*.ini`, README): requests from channel 10 to the configured PROGRAM channel using CC 50–53; HEAD ALL uses CC 32–44 on that same channel.

## 4. Tests

- Requests on PROGRAM CC 50–53 produce the same snapshots as v1 channel-10 requests, framed with CC 118/119 on the configured PROGRAM channel.
- Probe on PROGRAM CC 53 → CC 117 returns the configured protocol-version byte on the configured PROGRAM channel.
- PROGRAM CC 32–44 act on all HEADs exactly like v1 channel-9 CC 0–12; CC 45 does nothing.
- PROGRAM CC 64–92 and Program Change behave exactly as before; CC 93 sets Pulse Time. PROGRAM feedback CC 94–113 reports all PROGRAM state, including Pulse Time.
- Combining PROGRAM and HEAD on one channel keeps all HEAD and PROGRAM input actions distinct and all output feedback distinguishable.
- Changing HEAD channel start moves all HEAD input and feedback channels contiguously; all active Head channels remain within 1–16 without wrapping. The setting supports channel 13 for an instance with up to four Heads and channel 14 for an instance with up to three Heads.
- Two MIDI modules on one MIDI interface with distinct HEAD ranges and Program/Stage assignments respond only to their own Head and Program CCs; snapshot requests use local zero-based Head indices and replies return on that instance's configured Head channels.
- Stage input and feedback follow the configured stage channel; changing either configured channel moves both its input and output traffic.
- With default channel settings, channel 10 notes and CCs 0–127 cause no SpaceTime action or feedback; same for channel 9.
- Moving the PROGRAM channel setting moves PROGRAM input/output, requests, HEAD ALL, version reply and framing with it; moving the Stage channel setting moves Stage input/output with it.
- An 18-message snapshot burst still arrives complete at a DROID blue-7 (1 ms spacing).

## 5. Why

**One USB port, all channels.** A DROID with an X7 has one USB MIDI port to the computer. In VCV Rack, SpaceTime's MIDI input listens on all sixteen channels of that port, and the DROID sends its own instruments — drum tracks, melodic sequencers, control pages — to VCV through the same port. Every channel SpaceTime listens on is a channel no other DROID instrument can use without driving SpaceTime by accident.

**Channel 10 is the drum channel.** General MIDI, nearly every drum machine and the DROID tooling (IpadForge allocates drums to 10, then 11) treat channel 10 as percussion. With requests on 10, a drum track has to be moved off its natural channel, and any drum track that sends a CC 0–3 — bank select is CC 0 — fires a SpaceTime snapshot request.

**Channel 9 and 10 carry almost nothing.** Channel 9 is a convenience (the same effect as eight messages on 1–8) and sends nothing back in v1. Channel 10 carries four request CCs, one version reply and two framing CCs. Neither needs a channel of its own.

**The PROGRAM channel has the room.** Input CC 32–44 carries HEAD ALL; 50–53 carries requests; 64–93 carries PROGRAM controls. Output CC 94–113 carries PROGRAM feedback and 117–119 carries protocol replies/framing. The Stage channel remains a separate configurable channel because it uses all CC numbers.

**What it buys.** v1 leaves 11–14 — four channels — for everything else on the port, without the drum channel. v2 leaves 9–14 — six channels — including channel 10. For a typical DROID patch (drums plus a handful of melodic sequencers plus one control page) that is the difference between fitting and not fitting.

**One control plane, configurable as a unit.** With requests, framing and HEAD ALL on the PROGRAM channel, SpaceTime's controller traffic is HEAD channels plus two configurable channels. Moving PROGRAM to another channel no longer leaves fixed channels behind.

**Several SpaceTimes can coexist on one interface.** Each MIDI module gets a HEAD channel-start setting, while PROGRAM and Stage channels remain configurable. Assign each instance a unique contiguous HEAD range, a unique PROGRAM route, and a unique Stage channel; a PROGRAM channel may share one of its own Head channels. This allows up to eight one-HEAD instances by using HEAD/PROGRAM channels 1–8 and Stage channels 9–16. A two-instance, six-HEAD-per-instance layout can use HEAD channels 1–6 and 7–12, with Stage channels 13 and 14 and each PROGRAM channel sharing one of its own Head channels. These dense layouts use most or all MIDI channels and leave less room for other instruments. For example, a second instance with two Heads can start at channel 13, use its PROGRAM channel on 13, and use a distinct Stage channel such as 12.

## 6. Consequences on the DROID / IpadForge side (for information)

When v2 is in, the IpadForge `SpaceTime` template changes in three places:

1. `# midi: none reserves=1-10,15,16` becomes `reserves=1-8,15,16` for the default single-instance layout, so IpadForge allocates DROID tracks to 9–14, with drums on 10 again;
2. snapshot requests move to the configured PROGRAM channel, using CC 50–53;
3. "All heads" (B8.2) sends on the configured PROGRAM channel using CC 32–44.
