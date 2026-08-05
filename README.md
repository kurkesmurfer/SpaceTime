# SpaceTime

**A multi-playhead arbitrary function generator family for VCV Rack 2**
Kurkesmurfer · GPL-3.0-or-later

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

## Preview status and safety

SpaceTime 2.0.0 is currently a release candidate and is not yet distributed
through the VCV Library. The Apple Silicon build and principal workflows have
received extensive hands-on testing. The Intel macOS, Windows, and Linux
packages are cross-compiled with the official VCV Plugin Toolchain and still
require platform-specific acceptance testing.

Preview builds may contain defects, including incorrect control or MIDI
behaviour, crashes, or loss of unsaved patch state. Keep backups of important
patches and do not rely on a preview build as the sole copy of musical work.
The software is provided without warranty under the GPL-3.0-or-later licence.
Please report reproducible problems through
[GitHub Issues](https://github.com/kurkesmurfer/SpaceTime/issues).

## Install a preview build

Download the package for your platform from
[GitHub Releases](https://github.com/kurkesmurfer/SpaceTime/releases):

| Package suffix | Platform |
|---|---|
| `mac-arm64` | Apple Silicon macOS |
| `mac-x64` | Intel macOS |
| `win-x64` | 64-bit Windows |
| `lin-x64` | 64-bit Linux |

1. In Rack, choose **Help > Open user folder**, then quit Rack.
2. Copy the downloaded `.vcvplugin` file into the matching
   `plugins-<OS>-<CPU>` directory inside that user folder. Do not unpack it.
3. Restart Rack. Rack extracts the package and adds the Kurkesmurfer modules
   to the Module Browser.

This is the standard manual installation method documented by the
[VCV Rack manual](https://vcvrack.com/manual/Installing#Installing-plugins-not-available-on-the-VCV-Library).
Remove the extracted `SpaceTime` plugin directory before installing a different
preview package with the same version number.

## The family

| Module | HP | Role |
|---|---|---|
| **PROGRAM** | 18 | The programming section: stage select, voltage/time/mode modifiers, pulses, Clear, presets + key/scale, external inputs A–D, poly output |
| **STAGE4** | 10 | Four stages: voltage slider, time slider, LED cluster. Chain up to 16 blocks (4–64 stages) |
| **HEAD** | 10 | One function generator: transport, addressing, direction, clocking, full output complement. Chain up to 8 |
| **HEAD ALL** | 10 | Common transport, playback modes and normalled CV inputs for every connected HEAD |
| **MIDI** | 4 | Transparent controller/MIDI gateway between HEAD and PROGRAM |
| **GLUE RIGHT / LEFT** | 2 each | Paired SpaceTime-only virtual expander bridge for separating chain fragments |

Placement: `[HEAD ALL][HEAD]…[HEAD][PROGRAM][STAGE4]…[STAGE4]`, all touching — a gap or
foreign module ends the chain on that side. Stage numbering runs
left-to-right across the blocks; head 1 sits next to PROGRAM, counting
upward leftward. Blocks own their stage data: reordering STAGE4 modules
reorders the sequence, and a block carries its programming with it.
PROGRAM's display shows the stage count and appends `!` when the chain looks
broken.

### Panel theme

SpaceTime follows Rack's global light/dark panel preference by default. Every
SpaceTime module context menu also provides **Panel theme > Follow Rack / Light
/ Dark**. The override is plugin-wide, so changing it from one module updates
the complete SpaceTime family and is stored as a Rack plugin preference rather
than in individual patches.

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

Place HEAD–PROGRAM–STAGE4 (four stages; add more blocks as you like). Set a
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
region's First stage). Each action has a matching input jack. The START jack doubles as the Sustain gate and the
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
After a 1-SHOT run completes and parks on its final stage, the next START
returns to the region's First stage and begins a fresh pass.

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

## HEAD ALL

Place one HEAD ALL immediately left of the furthest HEAD. It is a common
control source, not a ninth playhead, and therefore has no CV/gate outputs or
Display ownership. START, STOP, ADV and RST buttons and inputs address every
connected HEAD. ADDRESS, address source/mode, Direction, clock source,
DIV/MULT, TIME CV amount and Loop are copied to every HEAD when the common
control changes; individual HEAD controls may diverge again afterward.

The common START input is also the shared Sustain/Enable gate. Common ADDRESS,
CLK and TIME inputs are normalled to each HEAD: an individually patched HEAD
jack overrides the common signal for that HEAD. The common STROBE input and
momentary address lever strobe every HEAD. HEAD ALL may sit across a correctly
paired HEAD-side GLUE link and must remain the far-left terminal.

MIDI channel 9 addresses all HEADs directly through the MIDI module, whether
or not HEAD ALL is installed. When HEAD ALL is present, persistent channel-9
controls also move its panel controls so the common state remains visible. It
reuses HEAD CC 0-12. CC 13 Display is excluded because SpaceTime Display
ownership is intentionally exclusive.

## Deviations from the hardware

VCV extensions: up to 64 stages (16 blocks) and 8 heads instead of 16/2;
per-HEAD Reset inputs; optional HEAD ALL common control;
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

Code and artwork GPL-3.0-or-later, © Peet Siemensz / Kurkesmurfer.
Bundled fonts Fraunces and Barlow Condensed under the SIL Open Font License
(see `vcv/res/fonts/`). Inspired by Don Buchla's Model 248 and the Tiptop
Audio/Buchla 248t; no original assets or firmware were used.
The current 248t manual is available from
[Tiptop Audio's official Buchla 200t page](https://tiptopaudio.com/buchla/).

## Branding

The panel mark is Kurkesmurfer's continuous bent-corkscrew logo: its handle
also suggests a `K`. SpaceTime uses the subdued VCV treatment from
`vcv/res/kurkesmurfer-panel-logo.svg`, with a 65% spiral and 75% handle and
reinforced joint. The full-strength canonical logo belongs in manuals,
repository artwork, and other supporting material instead of on panels.

The shared `CornerMark` helper in `vcv/src/paneltheme.hpp` positions the logo
from its painted right and bottom bounds. Stage4 uses a 3.21 mm right margin;
the narrow Glue panels use 1.04 mm. Both use a 4 mm bottom margin. These
placements are intentional and should be changed only after checking Rack
screenshots at 100% and 200% zoom.

## Development

Build: `cd vcv && make` (Rack SDK 2.x at `~/Development/Rack-SDK` or set
`RACK_DIR`). Host-side unit tests without the SDK: `make -C test test`, or
run `./check.sh` for both. The DSP core lives in `dsp/` (Rack-free,
unit-tested, golden traces in `test/golden/`); the manual integration patch
suite is documented in `test/patches/README.md`.

Library submission checklist: slugs frozen (`SpaceTime`/`Program`/`Stage4`/
`Head`/`HeadAll`/`Midi`/`GlueLeft`/`GlueRight`); version major 2; WidgetTest hidden;
licence and font notices included; panel screenshots at 100% zoom; release
metadata points to the Kurkesmurfer site and planned public repository.
