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
and a disabled-MIDI-output fast path.

Optimized Core build `b74f879` measured 24-25% stopped and approximately 53%
with all eight heads running. The active-head increment is about 3.5 percentage
points per head, leaving roughly 47% headroom in the eight-head test patch.
Independent MIDI start/stop on channels 1-8 was verified after correcting the
DROID remote patch.

1. Remove the old `SpaceTimeProbe.mmplugin`, install `SpaceTime.mmplugin`, then
   add **SpaceTime Core**. Its defaults are Program channel 16 and stage-slider
   channel 15; head channels are fixed at 1-8.
2. Patch SELECTED V and SELECTED T to a scope. On MIDI channel 15 send CC 0 =
   127. Stage 1 must show 10.00 V and SELECTED V must output 10 V. Send CC 64 =
   32; Stage 1 time must show about 0.252 and SELECTED T about 2.52 V.
3. On channel 16 send CC 65 = 127. The display and STAGE control must advance
   together. Verify CC 89, 90 and 91 update key, scale and the RETRIGGER switch.
4. On channel 1 send CC 9 = 127 to select virtual clock, CC 1 = 127 to start
   Head 1, then isolated CC 0 = 127 messages. The RUN light and first character
   of the `RUN 1-8` display row must turn on, and
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

## Timing Monitor hardware test

The optional 12 HP Timing Monitor compares clock-source events delivered to all
eight heads with actual stage entries. It judges timing only while a head is
active, uses MIDI or Virtual clock, and has its divider at `x1`. Other modes show
`READY` rather than reporting a false failure. Green means at least one tested
event and matching totals; red is a latched mismatch until RESET.

1. Add Core and Timing Monitor. Both default to Instrument ID A. The monitor
   must show `A LINK`; STATUS must be +5 V. A mismatched ID shows `WAIT`/0 V and
   duplicate Cores on one ID show `DUP`/-5 V.
2. For Head 1, send channel 1 CC 9 = 127 (Virtual), CC 10 = 64 (`x1`) and
   CC 1 = 127 (Start). Select Head 1 on the monitor and press RESET.
3. Send separated CC 0 = 127 virtual ticks. Each tick must increase both `CLK`
   and `STP` by one, DIFF must remain zero and Head 1 must turn green. CLOCK and
   STEP outputs provide a conventional two-channel scope pair if desired.
4. As a monitor sanity check, send two virtual ticks within one millisecond or
   in one USB packet. The source count must increase by two while only one edge
   is consumed; DIFF becomes -1 and Head 1 latches red. Press RESET afterward.
5. Send channel 1 CC 9 = 64 (MIDI Clock), confirm `MIDI x1`, press RESET and
   send manual F8 messages. Repeat with a continuous DROID MIDI clock.
6. Configure channels 1-8 for MIDI Clock at `x1`, start all heads, press RESET
   and run the clock. All eight indicators must remain green. Select each head
   to inspect counts; record any red indicator, non-zero DIFF or unexpected
   HOLD/STOP state.
7. Repeat representative save/load, ID change and Core/Monitor deletion and
   reinsertion checks. Record combined CPU load and processor-core allocation.

Manual Advance, mode changes, stop/sustain stages and dividers other than `x1`
change the expected event relationship. Press RESET after configuring a clean
test state; use Clear Program if necessary.

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
