# SpaceTime for MetaModule

The development package contains the first fused SpaceTime Core and the accepted
shared-memory bus probes. Core owns all 64 stages, all eight continuously running
heads, Program state, presets and the MIDI implementation. The probes remain in
the package until the first remote control surface is complete.

## Build

MetaModule requires ARM GNU Toolchain 12.2 or 12.3. VCV and Daisy may continue
using their existing ARM GNU 10 toolchains; this target does not change them.

```sh
cmake --fresh -S metamodule -B metamodule/build -G Ninja \
  -DMETAMODULE_SDK_DIR=/Users/peet/Development/metamodule-plugin-sdk \
  -DTOOLCHAIN_BASE_DIR=/Applications/ArmGNUToolchain/12.3.rel1/arm-none-eabi/bin
cmake --build metamodule/build
```

The installable package is written to
`metamodule/metamodule-plugins/SpaceTime.mmplugin`.

## Core hardware test

This test is now required before further remote-panel work.

Initial Core build `40813fd` loaded successfully on firmware 2.2.0 but reported
59% CPU at startup in an otherwise empty patch. That is the performance baseline,
not an accepted result. The following build adds stopped-head control-rate refresh
and a disabled-MIDI-output fast path; record its empty-patch CPU before continuing.

1. Remove the old `SpaceTimeProbe.mmplugin`, install `SpaceTime.mmplugin`, then
   add **SpaceTime Core**. Its defaults are Program channel 16 and stage-slider
   channel 15; head channels are fixed at 1-8.
2. Patch SELECTED V and SELECTED T to a scope. On MIDI channel 15 send CC 0 =
   127. Stage 1 must show 10.00 V and SELECTED V must output 10 V. Send CC 64 =
   32; Stage 1 time must show about 0.252 and SELECTED T about 2.52 V.
3. On channel 16 send CC 65 = 127. The display and STAGE control must advance
   together. Verify CC 89, 90 and 91 update key, scale and the RETRIGGER switch.
4. On channel 1 send CC 9 = 127 to select virtual clock, CC 1 = 127 to start
   Head 1, then isolated CC 0 = 127 messages. The RUN light must turn on and
   HEAD 1 ALL must produce exactly one pulse per message. Send CC 2 = 127 and
   confirm that RUN turns off.
5. Send MIDI F8 and verify the CLK light. In the module action menu enable
   **Follow MIDI transport** for Head 1, then verify FA/FB start and FC stops.
6. Choose a preset, press SAVE, alter Stage 1 over MIDI, then press LOAD. The
   display and both selected-stage outputs must restore. Save/reload the patch
   and verify the 64-stage table, key/scale, retrigger state, head configuration,
   MIDI output lanes and MIDI port routing recover.
7. In the action menu configure Head 1 output as Notes, choose ALL as its gate
   source, and select channel 1. Enable quantize on the active stage with Program
   CC 68, run the head, and verify note-on/note-off plus the OUT light. Repeat
   once with CC 7-bit mode and the chosen CC number.
8. Run dense DROID slider traffic while all eight heads process. Record firmware,
   sample rate, average/peak CPU and any lost clock, transport or CC events.

The Core screen and action menu are the MM2 layout review. Note anything clipped,
ambiguous or too small at the normal 240 px and reduced 180 px display scales.

## Bus-probe hardware test

1. Install `SpaceTime.mmplugin` and add one BUS PROBE CORE and one BUS
   PROBE REMOTE to a patch.
2. Both probes default to Instrument ID A. To change it, open the probe's module
   view, rotate through its element list to `Instrument ID`, press the rotary,
   and choose A-D. This is an alternate parameter, so it appears in the element
   list rather than on the panel image. With matching IDs, the displays must
   change from `WAIT Cn` to `LINK Cn`; both STATUS outputs must read +5 V. `C1`
   and `C2` identify the processor core actually running that probe.
3. Patch a changing voltage into CORE PUBLISH. REMOTE SEEN CORE must reproduce
   it without a virtual cable between the probes.
4. Patch a different voltage into REMOTE SEND. CORE SEEN REMOTE must reproduce
   it.
5. There is no user-facing CPU map or manual core assignment in firmware 2.2.
   Check the suffix on each probe's display. For the cross-core test, one must
   show `C1` and the other `C2`. If both show the same suffix, add processing
   modules of different CPU weights and reload the patch until automatic load
   balancing separates the probes. Repeat both directions under load.
6. Save and reload the patch. Link and values must recover without intervention.
7. Add a second Core on ID A. Displays must show `DUP` and STATUS must be -5 V.
   Remove either Core; the remaining Core must claim the bus and relink.
8. Change both probes to ID B and repeat. A probe on A must remain unlinked.
9. Delete and re-add each endpoint, then unload/reload the plugin twice. No
   stale owner or false `LINK` state may survive.

Record firmware version, sample rate, CPU allocation/load and any dropped or
stale updates. The Core engine may start after the cross-core principle passes;
all lifecycle checks must pass before the private bus is treated as release-ready.

## Hardware result

Principle accepted on 2026-07-16 with firmware 2.2.0:

- Core and Remote were automatically distributed one per processor core.
- Sweet Sixteen drove both inputs and both received outputs were inspected on an
  18vert scope.
- `LINK`, `DUP` and mismatched-ID `WAIT` states behaved correctly.
- STATUS output was verified at +5 V for `LINK`, 0 V for `WAIT` and -5 V for
  `DUP`.
- Instrument IDs A, B, C and D were verified, including matching and mismatched
  pair behavior.
- Save and reload recovered the link and state.
- Endpoint deletion, reinsertion and reconnection recovered correctly.
- Reported load for the two-core probe/scope test patch was 25%.

Duplicate-owner removal and two full plugin unload/reload cycles remain
product-bus robustness checks. They do not block the MM1 Core engine now that
cross-core communication itself has passed.
