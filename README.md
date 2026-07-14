# SpaceTime

**A multi-playhead arbitrary function generator family for VCV Rack 2**
Towering Inferno · GPL-3.0-or-later

SpaceTime is a behavioural reinterpretation of the Buchla Model 248 "MARF"
(Multiple Arbitrary Function Generator, 1977) and its Eurorack incarnation,
the Tiptop Audio / Buchla 248t. Where the hardware fixes 16 stages and two
function generators in one 72 HP panel, SpaceTime splits the instrument into
a modular family: one programming section, a chainable stage surface, and up
to eight independent playheads.

This is an original work: original panel artwork, original code, no Buchla or
Tiptop assets. The 248/248t are credited as inspiration only; SpaceTime is
not affiliated with or endorsed by Buchla USA or Tiptop Audio. Several
behaviours that the hardware manual leaves undocumented were verified on an
actual 248t (with thanks to the demonstrations of Stazma the Junglechrist).

## The family

| Module | HP | Role |
|---|---|---|
| **PROGRAM** | 18 | The programming section: stage select, voltage/time/mode modifiers, pulses, Clear, presets + key/scale, external inputs A–D, poly output |
| **STAGE4** | 10 | Four stages: voltage slider, time slider, LED cluster. Chain up to 8 blocks (4–32 stages) |
| **HEAD** | 10 | One function generator: transport, addressing, direction, clocking, full output complement. Chain up to 8 |
| **MIDI** | 4 | Transparent controller/MIDI gateway between HEAD and PROGRAM |
| **GLUE RIGHT / LEFT** | 2 each | Paired SpaceTime-only virtual expander bridge for separating chain fragments |

Placement: `[HEAD]…[HEAD][PROGRAM][STAGE4]…[STAGE4]`, all touching — a gap or
foreign module ends the chain on that side. Stage numbering runs
left-to-right across the blocks; head 1 sits next to PROGRAM, counting
upward leftward. Blocks own their stage data: reordering STAGE4 modules
reorders the sequence, and a block carries its programming with it.
PROGRAM's display shows the stage count and appends `!` when the chain looks
broken.

### GLUE virtual bridge

GLUE RIGHT sits immediately to the right of the left fragment; its matching
GLUE LEFT sits immediately to the left of the right fragment. Select the same
link number (1–8) from both context menus. The pair automatically recognizes a
HEAD-side or STAGE-side cut and is transparent to numbering:

`HEAD HEAD [GLUE RIGHT] ... [GLUE LEFT] MIDI PROGRAM STAGE4`

`HEAD MIDI PROGRAM STAGE4 [GLUE RIGHT] ... [GLUE LEFT] STAGE4 STAGE4`

The link LED is green when paired and compatible, red while waiting, duplicated
or mismatched, and off without a valid SpaceTime neighbour. The readout shows
the link number plus `H` for HEAD-side, `S` for STAGE-side, or `-` when no side
is detected. GLUE only transports SpaceTime's internal expander protocol inside
one VCV Rack patch; it is not a generic bridge for third-party expanders.

## Quick start

Place HEAD–PROGRAM–STAGE4 (12 stages? add more blocks as you like). Set a
few voltage sliders to taste, press START on the head, and patch its CV OUT
to a pitch input and REF OUT to an LPG or VCA. The head walks the stages at
the times set by the lower sliders (2–30 s by default — press a TIME RANGE
button with faster values for audible movement, using the ALL-stages trick
below). Scroll SELECT to a stage, flip QUANT up, and that stage snaps to the
scale set in the preset row. Flip PULSE 1 up on a few stages and patch P1
out to an envelope. Add a second HEAD, press its DISP button, give it a
different direction, and you have two independent playheads on one sequence.

## PROGRAM

**Stage select.** The SELECT lever scrolls the edit selection (circular;
hold to autoscroll). All modifier LEDs below show the selected stage's
programming. While the lever is held, any programming press applies to
**all stages** (the hardware bulk gesture). Because that needs two hands
with a mouse, the context menu offers *Apply next edit to ALL stages* — the
display shows `AL` until the next gesture. CLEAR resets every stage's
programming to defaults; sliders are never moved by Clear.

**Output voltage modifiers** (per stage; up adds, down removes):

| Control | Function |
|---|---|
| QUANT | Quantize to 1 V/oct in the key/scale set in the preset row |
| SLOPE | Add/remove slew: stepped → slew 1 → slew 2. Slew time is proportional to the stage interval (fractions in the context menu) |
| RANGE | Up = Full 0–10 V, down = Half 0–5 V |
| LIMITED −2…+2 | Engage the Limited range: 2 V slider span plus the chosen octave offset |
| SOURCE | Internal (slider) / External: inputs A–D are routed through the modifiers and the stage's **voltage slider selects which input** (quartiles) |

**Operating mode** (per stage): STOP (head halts after this stage's interval
until a start pulse), SUST (head holds here while its START gate is high),
ENBL (head waits on entry until START exceeds 5 V), FIRST/LAST (cycle
bounds — see *Regions* below).

**Interval time** (per stage): four ranges (.002–.03 / .02–.3 / .2–3 /
2–30 s) and a time SOURCE lever: external time reads CV from A–D — the
stage's **time slider selects the input**, and an unpatched input gives the
fastest value of the range.

**Pulses.** PULSE 1/2 add a gate on the stage, high for the stage's full
interval. PROGRAM's PULSE RETRIG switch defaults to a short low-going notch
between consecutive flagged stages, so each stage produces a fresh edge
(hardware-verified). Switch it off to merge adjacent pulse stages into one
continuous gate.

**Presets, key, scale.** LOAD or SAVE, then a numbered button (12 slots,
stored in the patch). KEY then 1–12 selects C…B; SCALE then 10/11/12 selects
Major/Minor/Chromatic. Recalled slider values stay active until the physical
slider crosses them (takeover), so nothing jumps.

**POLY OUT** carries every head's CV, channel number = head number.

**Context menu:** bulk-edit arm, slew fractions, ADDRESS scaling
(self-normalizing 0–10 V across the chain, or fixed 0.5 V/stage), slope law
(linear; exponential reserved).

## STAGE4

Top sliders set stage voltage (0–10 V), bottom sliders the interval time
within the stage's range. The A–D letters beside the travel mark the
quartile mapping used when a stage's voltage or time source is External. The
LED cluster shows the edit selection and up to eight colour-coded head
positions (dimmed when that head is stopped). Blocks may be added, removed
and reordered live.

## HEAD

**Transport:** START, STOP, ADV (force next stage), RST (return to the
region's First stage). The START jack doubles as the Sustain gate and the
Enable >5 V threshold, as on the hardware.

**Status LEDs:** green = running; yellow = holding (Sustain/Enable waiting —
or the external clock has stopped arriving); red = stopped (stop control,
Stop stage, one-shot done, or Continuous mode).

**DISP** latches this head as the displayed one (only one at a time):
PROGRAM's LEDs and edits follow this head's current stage live. Scrolling
SELECT releases it and returns the selection to stage 1.

**Addressing:** the ADDRESS knob (or ADDRESS CV with INT/EXT up) positions
the head. The three-position lever latches up for **Continuous** (clock
stopped, the address sweeps the stages, red status), latches center for
**Sequential**, and is momentary down for **Strobe** (load the addressed
stage once).

**Playback extensions (not on the hardware):** DIRECTION
(forward/reverse/pendulum/random/brownian), clock source INT/EXT/MIDI/VIRTUAL with
/16…×16 DIV/MULT, TIME CV attenuverter scaling all stage times, and a loop
lever: ALL (full chain) / F-L (obey First/Last) / 1-SHOT.

**Outputs:** CV (1 V/oct when quantized), TIME (the stage's time slider as a
CV — a free second row when time source is External), REF (downward ramp
spanning the interval, drives an LPG directly), ALL (pulse on every new
stage), P1/P2 (programmed stage gates), EOC (cycle end / stop).

### Regions (First/Last)

First and Last flags form **regions**: a head's cycle is bounded by the
First at/below and the Last at/above its current position. With First on
stages 1 and 9 and Last on 8 and 16, a head started low loops 1–8 while a
head strobed to 9 loops 9–16 — independent sequencers on one surface
(hardware-verified). RST stays within the head's region.

## Deviations from the hardware

VCV extensions: up to 32 stages (8 blocks) and 8 heads instead of 16/2;
direction modes; external clock with div/mult; per-head TIME CV; loop mode
selection; EOC output; POLY OUT; presets stored per patch; DISP is latching
instead of momentary; bulk edit via context menu. Dropped: ART outputs and
auto-tune (259t-specific), time multiplier knob (TIME CV covers it).
Scaling is 1 V/oct (originals used 2.4 V/oct; the 248t already converts).
Undocumented behaviours pending hardware measurement are named constants:
slew fractions (menu), continuous-mode interpolation (off), pendulum
endpoint repeat (off), limited-range octave offsets (−2…+2 assumed), pulse
retrigger notch length (1 ms).

## Licence and credits

Code and artwork GPL-3.0-or-later, © Peet Siemensz / Towering Inferno.
Bundled fonts Fraunces and Barlow Condensed under the SIL Open Font License
(see `vcv/res/fonts/`). Inspired by Don Buchla's Model 248 and the Tiptop
Audio/Buchla 248t; no original assets or firmware were used.

## Development

Build: `cd vcv && make` (Rack SDK 2.x at `~/Development/Rack-SDK` or set
`RACK_DIR`). Host-side unit tests without the SDK: `make -C test test`, or
run `./check.sh` for both. The DSP core lives in `dsp/` (Rack-free,
unit-tested, golden traces in `test/golden/`); the manual integration patch
suite is documented in `test/patches/README.md`.

Library submission checklist: slugs frozen (`SpaceTime`/`Program`/`Stage4`/
`Head`/`Midi`/`GlueLeft`/`GlueRight`); version major 2; WidgetTest hidden; licence and font notices
included; panel screenshots at 100 % zoom; `pluginUrl`/`sourceUrl` to be
filled in once the repository is published.
