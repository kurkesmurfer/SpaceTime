# SpaceTime — WP7 integration patch suite

Manual verification patches for the integrated plugin. Run after
`make install` from `vcv/`. All chains must be contiguous:
`[HEAD]…[HEAD][PROGRAM][STAGE4]…[STAGE4]`.

## Patch 1 — MARF-equivalent (2 heads / 4 blocks)

Layout: `HEAD HEAD PROGRAM STAGE4 STAGE4 STAGE4 STAGE4` (16 stages).

1. PROGRAM's display shows **16**; both HEAD id LEDs show their colour
   (red = head 1 next to PROGRAM, green = head 2).
2. Set a voltage ramp across the sliders; press START on head 1:
   green RUN LED, position dot walks the blocks, CV OUT steps the ramp,
   REF OUT saws downward per stage.
3. Head 2 runs independently (different direction/loop mode); the second
   colour dot moves independently on the same blocks.
4. SELECT scroll: edit LED follows across all four blocks, wraps circularly,
   autoscrolls when held (~2.5 s across 16 stages).
5. Modifier gestures on the selected stage: press QUANT up -> LED on;
   stage 1 sounds quantized; SLOPE up twice -> LED 1 and 2, CV glides.
6. Hold SELECT and press PULSE 1 up: pulse 1 LED lights for EVERY stage
   (bulk edit); PULSE 1 out gates on all stages.
7. STOP flag on stage 4 + START: head halts on stage 4 after its interval
   (red), retriggering START continues from stage 5.
8. FIRST on stage 5, LAST on stage 12, loop mode F-L: cycle bounded 5..12.
9. Pulse EXT A with an LFO; set stage 3 voltage source EXT and its slider
   to the bottom quartile: stage 3's CV follows input A through the range
   modifiers.
10. POLY OUT: 2 channels, channel 1 = head 1 CV, channel 2 = head 2 CV.

## Patch 2 — 8-head stress

`HEAD ×8, PROGRAM, STAGE4 ×8` (32 stages, 8 heads).

- Display shows 32. All 8 head-id LEDs show distinct colours
  (red green blue yellow magenta cyan orange white, nearest first).
- Start all heads with mixed directions incl. random/brownian; all 8 dot
  colours move on the blocks; POLY OUT has 8 channels.
- CPU check (Rack menu > Performance meters): full chain must stay under
  the agreed budget on the M3 Max reference (record the number here: ___%).

## Patch 3 — live block reorder

- With patch 1 running, drag STAGE4 block 2 to position 4 (Rack shoves the
  row): the 4-stage groups swap wholesale — sliders and per-stage programming
  travel WITH the block (blocks own their data). Display stays 16.
- Drag a block out (gap): display drops to the reachable count with a `!`
  broken-chain mark; heads keep running on the shortened table.
- Insert a foreign module mid-chain: same truncation behaviour.

## Patch 4 — preset recall during playback

1. Program a distinctive 16-stage sequence, SAVE -> slot 3 (LED marks used).
2. Change many sliders and programming; LOAD -> slot 3 while heads run:
   programming is restored immediately; CV output follows the SAVED slider
   values although the physical sliders sit elsewhere.
3. Slider takeover: move a voltage slider toward its saved value — output
   only follows the physical slider after it CROSSES the saved value.
4. KEY -> 5 (F), SCALE -> 11 (Minor): quantized stages re-pitch.

## JSON round-trip (scripted)

```
# save the patch in Rack, then:
cd ~/Library/Application\ Support/Rack2
python3 - <<'EOF'
import json, zipfile, sys
# .vcv files are (zstd) tar archives in Rack 2; use autosave for a diff:
a = json.load(open('autosave/patch.json'))
blobs = {m['id']: m.get('data') for m in a['modules']
         if m['model'] in ('Program','Stage4','Head') and m['plugin']=='SpaceTime'}
json.dump(blobs, open('/tmp/spacetime_blobs_1.json','w'), sort_keys=True, indent=1)
EOF
# quit Rack, reopen the patch, repeat into _2.json, then:
diff /tmp/spacetime_blobs_1.json /tmp/spacetime_blobs_2.json && echo ROUND-TRIP OK
```

Verifies: STAGE4 program words + takeover state, PROGRAM presets/key/scale/
globals survive save-load byte-identically. (Slider params are covered by
Rack's own param persistence.)

## Patch 5 — FG Display and bulk edit (post-WP7 conformance additions)

1. **DISPLAY (per head):** press DISP next to a head's status LEDs — its LED
   latches, and PROGRAM's edit LED + all modifier LEDs now follow that head's
   CURRENT stage live (manual: "shows the current stage position without
   interrupting operation"). Gestures edit the displayed stage.
2. Press DISP on another head: it takes over; only one Display LED is active
   at a time (manual).
3. Flip the SELECT scroll while displaying: Display cancels and the selection
   jumps to stage 1 (manual). Pressing DISP again also releases.
4. **Bulk edit without two hands:** right-click PROGRAM -> "Apply next edit
   to ALL stages" (display shows AL); the next gesture applies to all stages
   and disarms. The hardware hold-scroll-and-press gesture still works too.

## Patch 6 — MIDI Part I input

Layout: `HEAD HEAD MIDI PROGRAM STAGE4 STAGE4` (8 stages). The MIDI module
sits directly left of PROGRAM, between the anchor and the heads. It is
transparent for head enumeration: PROGRAM still shows two heads and 8 stages.

1. MIDI module context menu shows the Rack MIDI input device menu. The
   PROGRAM/stage channel defaults to MIDI channel 16 and can be changed;
   HEAD channels are fixed to MIDI channels 1-8.
2. On the PROGRAM/stage channel, send CC 0..7: effective voltage values for
   stages 1..8 update with normal takeover behaviour. The Rack slider handles
   do not move until manually crossed. Send CC 32..39: effective time values
   for stages 1..8 update the same way.
3. Send Program Change 1..12 on the PROGRAM/stage channel: preset slots load.
4. Send PROGRAM modifier CCs 64..88: selected-stage edit, bulk-arm, Clear,
   Limited, Time range and Pulse edits behave like panel gestures.
5. Send HEAD CCs 0..13 on MIDI channels 1 and 2: the two heads respond
   independently. Verify virtual clock on CC 0 with each head's MIDI clock
   source set to Virtual.
6. Set one head's MIDI clock source to MIDI clock and send MIDI Clock: it
   advances with the existing DIV/MULT control. Stop the MIDI clock and
   verify the same HOLD indication used by other missing clocks.
7. Enable "Follow MIDI transport" on one head only. MIDI Start/Continue
   starts that head, MIDI Stop stops it, and the other head is unaffected.
8. Remove the MIDI module and verify the original non-MIDI chain still works.

## Patch 7 — MIDI stress / MetaModule portability note

Layout: `HEAD x8, MIDI, PROGRAM, STAGE4 x8` (32 stages, 8 heads).

- Send MIDI clock plus several CC lanes from the controller/DAW while all
  heads run. No broken-chain mark; head ids and stage dots stay correct.
- CPU check on the tested M3 laptop: MIDI, HEAD and STAGE4 modules each show
  0.1%; PROGRAM shows 0.2%. This is well within the expected MetaModule
  portability budget.

## Patch 8 — MIDI Part II output smoke

Layout: `HEAD MIDI PROGRAM STAGE4` (4 stages), with the MIDI module's output
device set to an IAC bus, DAW, hardware synth, or MIDI monitor.

1. Program stage 1 as Quantized and Pulse 1; set its voltage to 0 V. Program
   stage 2 as Quantized and Pulse 1; set its voltage to 1 V.
2. MIDI module context menu -> HEAD 1 output:
   - Mode = Notes.
   - Channel = 1.
   - Note gate source = Pulse 1.
3. Start HEAD 1. Expected MIDI monitor output: note 60 on stage 1 and note 72
   on stage 2, with note-off on Pulse 1 gate fall. The MIDI OUT LED blinks.
4. Change HEAD 1 output gate source to ALL. Expected: a short 30 ms note on
   each stage change, still following the quantized stage CV.
5. Set HEAD 1 output Mode = CC 7-bit and CC number = 20. Expected: CC 20 on
   channel 1 follows HEAD 1 CV, send-on-change with a small throttle.
6. Disable the lane. Expected: no further output; any active note is released.

Later gates not covered here: 14-bit CC, NRPN, and MIDI-clock-out.

## Known WP7 interpretation notes

- Strobe via the panel: flipping the CONT/SEQ/STRB switch DOWN fires one
  strobe (latching stub); the STROBE jack is the precise path. A latch-up/
  spring-down widget variant is a WP8 polish item.
- The broken-chain `!` relies on count mismatch or >8 modules; Rack expander
  links cannot see across a physical gap.
