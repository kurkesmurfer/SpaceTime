# SpaceTime — Multi-Playhead Arbitrary Function Generator for VCV Rack

**Plugin name:** SpaceTime (Kurkesmurfer) — modules PROGRAM / STAGE4 / HEAD / MIDI / GLUE; slugs `SpaceTime` / `Program` / `Stage4` / `Head` / `Midi` / `GlueLeft` / `GlueRight`
**Created:** 2026-07-06
**Updated:** 2026-07-11 (rev 6 — MIDI addendum added for WP8b, draft at design gate; rev 5 renamed to SpaceTime; rev 4 aligned with 248t manual)
**Status:** Concept / Specification
**Inspired by:** Buchla Model 248 / Tiptop Audio & Buchla 248t MARF (Multiple Arbitrary Function Generator)
**Brand:** Kurkesmurfer
**Primary source:** `Buchla__Tiptop_Audio_248t.pdf` (this directory) — Tiptop function reference
**Related:** `DROID-248t-Patch` (memory graph) — DROID-based sibling concept

---

## Management Summary

A VCV Rack 2 plugin that reinterprets the 248t MARF as a fully modular family, mirroring the hardware's actual architecture. The 248t consists of three sections: a stage surface (dual slider rows), a **shared programming section** (one set of modifier switches plus a stage-select scroll; modifier LEDs display the selected stage's programming), and two Function Generators. The VCV family maps these onto: **PROGRAM** (anchor — the complete programming section, external inputs A–D, presets/scale/key, globals), **STAGE4** blocks (pure slider surface, 4 stages each, chained as right-side expanders, 1–8 blocks for 4–32 stages), and **HEAD** playhead expanders (the Function Generators, up to 8 on the left, each with independent transport, addressing, direction and clocking plus full output complement). The hardware's two FGs are the degenerate case (anchor + 4 blocks + 2 heads).

Per-stage programming state lives in the STAGE4 blocks (clean persistence; sequence travels with the block); the anchor edits it via expander edit-ops. Interpolation per the owner's hardware: three-position momentary, stepped / slew 1 / slew 2, slew time proportional to the stage interval (manual describes binary sloped/stepped; hardware shows two levels — owner-verified). External-source selection follows the hardware mechanism: the modifier is binary INT/EXT and the stage's voltage slider selects which of inputs A–D is active. Target: VCV Rack 2 first, MetaModule stretch goal. Codebase per Siren canon (`dsp/` headers, `vcv/` subtree).

---

## Module Family

| Module | HP (est.) | Role |
|--------|-----------|------|
| PROGRAM | 14–18 | Programming section: stage-select scroll, voltage/time/mode modifier switches with LEDs, pulses, Clear, presets + scale/key, EXT A–D input column, globals, POLY OUT |
| STAGE4 | 10–12 | 4 stages: voltage slider, time slider, stage LED (selection + playhead positions). Chainable ×8 right of PROGRAM |
| HEAD | 8–10 | One Function Generator: transport, addressing, direction, clock, outputs. Chainable ×8 left of PROGRAM |

Rack placement: `[HEAD]…[HEAD][PROGRAM][STAGE4]…[STAGE4]`, contiguous; a gap or foreign module terminates the chain. Stage index runs left-to-right across blocks; head index 1 adjacent to PROGRAM, increasing leftward. Reordering STAGE4 blocks reorders the sequence (blocks own their stage data).

VCV layout extension: a numbered GLUE RIGHT / GLUE LEFT pair may replace one
physical adjacency. The pair transports SpaceTime's bidirectional typed
expander protocol without adding a logical head or stage hop. It automatically
detects HEAD-side and STAGE-side cuts; mismatched, duplicate or unpaired links
invalidate the chain. GLUE is VCV-only and cannot bridge third-party expander
protocols.

---

## STAGE4 (stage block, ×1–8)

Per stage column:
- **Voltage slider** — output level 0..10 V (subject to Range modifier). In EXT voltage-source mode, the slider instead **selects which external input (A–D) is active** for this stage (quartile mapping), per hardware.
- **Time slider** — interval time within the stage's selected Time Range (hardware default range 2–30 s; four ranges spanning 2 ms .. ~2 min). Slider value also appears at the head's TIME OUT.
- **Stage LED cluster** — selected-for-edit indication (from PROGRAM's stage scroll) + up to 8 colour-coded playhead position dots.

Blocks hold all per-stage state (slider params + program word) → JSON persistence per module. Program words are edited remotely by PROGRAM via expander edit-ops.

---

## PROGRAM (anchor — the programming section)

### Stage select
- **Stage Number** 3-position momentary scroll (left/right), circular across the full chain; hold to auto-scroll (~2–3 s over 16 stages). Selected stage's programming is displayed on all modifier LEDs below.
- **Bulk edit (hardware feature):** while scrolling is active, pressing any programming function applies it to **all** stages.
- **Clear:** momentary; resets all stages to defaults (pulses off, Continuous, Full Range, Internal source, mode flags off, time range 2–30 s, sliders to physical position).

### Voltage programming modifiers (per selected stage; 3-pos momentaries, up=add / down=remove, LED feedback)
| Switch | Values | Semantics (per manual) |
|--------|--------|------------------------|
| Quantize / Continuous | off (default) / on | Quantizes output to 1 V/oct; scale+key set via preset section |
| Sloped / Stepped | stepped (default) / slew 1 / slew 2 | Up adds slew, down reduces. Slew time derived from the stage's interval time. Manual documents binary sloped/stepped; hardware shows two slew levels (owner-verified) — VCV implements 3 states, level fractions menu-tunable |
| Range | Full 0–10 V (default) / Half 0–5 V / Limited ×5 | Limited: 2 V span with octave offsets (five offset switches) |
| Source (Voltage) | Internal (default) / External | External: inputs A–D routed through the voltage modifiers; **stage's voltage slider selects which input is active** |

### Operating mode modifiers (per selected stage)
| Switch | Semantics |
|--------|-----------|
| Stop | FG halts on this stage until a start pulse; the stop stage still respects its interval time |
| Sustain | FG holds on this stage as long as a high gate is present at the head's START input |
| Enable | FG pauses on this stage until voltage >5 V appears at the head's START input |
| Cycle First / Last | Define cycle boundaries; FGs start at First and loop back after Last |

### Time programming (per selected stage)
| Control | Semantics |
|---------|-----------|
| Time Range ×4 switches | Select one of four interval ranges, 0.002 s .. ~2 min |
| Source (Time) | Internal (default; slider + range) / External: A–D CV controls interval time; no CV → fastest value of selected range |

### Output pulses (per selected stage)
Pulse 1 and Pulse 2: 3-pos momentaries, up adds the pulse to the stage (LED on), down removes.
PROGRAM's global **Pulse Retrig** switch defaults On: adjacent stages carrying
the same pulse are separated by the hardware-compatible short low notch. Off
merges adjacent pulse stages into a continuous gate for non-retriggering use.

### Presets, scale and key
Row of 12 buttons + Load/Save (hardware: non-volatile; VCV: 12 internal slots on top of rack save). Alternate function: Key (C..B) and Scale (Major/Minor/Chromatic) for quantized stages. Auto-tune/ART: not applicable in VCV — dropped.

### External inputs and globals
- **EXT A–D:** vertical CV input jack column (±10 V tolerated).
- **Globals:** panel Pulse Retrig switch; context-menu slew level fractions,
  slope law (linear/exp), ADDRESS scaling mode, default time endpoints.
- **Stage-count display:** N = 4 × block count; warns on broken chain.
- **POLY OUT:** polyphonic output carrying all heads' voltage CVs as channels 1..N.

---

## HEAD (Function Generator expander, ×8)

### Hardware-faithful controls
| Control | Function |
|---------|----------|
| Start / Stop | Manual buttons + pulse inputs; controls the internal clock |
| Advance | Manual button + pulse input; force next stage |
| Reset | Return to First-programmed stage (or stage 1 if none) |
| Address knob | Stage position in continuous/strobe addressing |
| Int / Ext | Latching: address from knob or external ADDRESS CV |
| Cont / Strobe | 3-pos: latching up = Continuous (address sweeps stages directly, internal clock stopped), latching center = sequential, momentary down = Strobe (load stage at current address value) |
| Status LEDs | Green = run, Yellow = hold (Sustain/Enable waiting), Red = stop (stop control/pulse, Stop stage, or Continuous mode) |

### VCV extensions
| Control | Function |
|---------|----------|
| Direction | Forward / Reverse / Pendulum / Random / Brownian |
| Clock source | INT (per-stage times) / EXT CLK input / MIDI clock / virtual clock, with /16..×16 div/mult on clocked sources |
| Time CV | Attenuverted CV scaling all stage times for this head |
| Loop | Obey First/Last flags / full chain / one-shot |

### Inputs
START (pulse/gate — also serves Sustain gate and Enable >5 V threshold, per hardware), STOP, ADVANCE, STROBE, ADDRESS, CLK, TIME CV.

### Outputs
| Jack | Function |
|------|----------|
| CV OUT | Stage voltage after modifiers; 1 V/oct when quantized |
| TIME OUT | CV proportional to current stage's time slider; usable as second CV row when time source = External |
| REF OUT | Downward ramp spanning the stage interval (drives an LPG directly) |
| PULSE 1 / PULSE 2 | Gates on stages programmed with the respective pulse |
| ALL PULSE | Pulse on every new stage addressed (clock source) |
| EOC | Trigger at cycle end / stop (VCV extension) |

### ADDRESS scaling (variable stage count)
Default: 0–10 V spans the full current chain (self-normalizing). Menu alternative: fixed 0.5 V/stage.

---

## Expander Protocol (VCV Rack)

- Standard `Module::leftExpander`/`rightExpander` producer/consumer with `requestMessageFlip()`; fixed-size structs, **no dynamic allocation**.
- **Blocks → anchor (leftward):** concatenated stage table `{ float voltage[32]; float time[32]; uint32_t program[32]; uint8_t count }`.
- **Anchor → blocks (rightward):** edit-ops `{ stageIndex, field, value }` from the programming section + selected-stage index for LED display; blocks apply ops to their own state.
- **Anchor → heads (leftward):** stage table + `{ float ext[4]; globals; scaleKey }` relayed head-to-head.
- **Heads → anchor → blocks (rightward):** status `{ headId, currentStage, phase, runState }` merged/relayed for position LEDs.
- Latency: one sample per hop, control-rate data only — irrelevant. Slew, ramps and pulses generated **locally in each head**, sample-accurate.
- `onExpanderChange`: anchor renumbers both chains; modules display index/colour.

## DSP / Implementation Notes

- Repo `~/Development/SpaceTime/`: `dsp/` generic Rack-free headers (`StageTable.hpp`, `HeadDSP.hpp`, `ProgramLogic.hpp`, `Chain.hpp`), `vcv/` plugin base, `test/` host-side runner — see `marf-vcv-plan.md`. Manifest `"brand": "Kurkesmurfer"`, plugin slug `SpaceTime`.
- Program scanning at control rate (every 16 samples); slew/ramp/pulse at audio rate in heads.
- Custom 3-pos spring-return momentary widget, reused across the programming section.
- Quantizer: 1 V/oct semitone snap with Major/Minor/Chromatic scale + key from PROGRAM.
- MetaModule stretch: verify expander support in SDK; fallback fused monolith. RTTI off, FTZ per process() per dsp-targets skill.

---

## MIDI Addendum (rev 6 — WP8b, design gate)

**Status:** Part I incoming MIDI implemented. Part II outgoing MIDI has a
first implemented subset: per-head note lanes and 7-bit CC lanes. 14-bit CC,
NRPN and MIDI-clock-out remain later gates.
**Split:** Part I = MIDI in (control SpaceTime from the DROID-based
controller) — implemented first. Part II = MIDI out (SpaceTime controls
external gear) — harder, separately gated.

### Surface: one MIDI module (Part I and II share it)

A dedicated chain module **MIDI** (slug `Midi` — permanent, **accepted**),
sitting **directly left of PROGRAM**, between the anchor and head 1, as a
**transparent chain member** (**accepted**):

- The leftward anchor broadcast passes through unchanged (no hop-count
  increment — head ids are unaffected).
- The rightward status merge passes through unchanged — giving the MIDI
  module a view of **every head's CV, run state, stage and phase** exactly
  where they all converge: this is what Part II consumes.
- PROGRAM-side MIDI events travel rightward inside the existing
  **HeadsToAnchorMsg** path as sequenced `MidiProgramEvent` batches; the
  anchor converts them into the same edit-op path used by panel gestures.
- `enumerateChain` and the neighbour checks learn `ModuleType::Midi` as
  transparent (append-only protocol change).

Panel: ~4 HP (**accepted**). Driver/device selection and editable
MIDI settings remain via context menu (Core MIDI style). The panel carries
activity LEDs (IN, OUT, CLK) plus small visible readouts for values needed
when programming the external controller: the configurable PROGRAM/stage
channel. OUT can be present as a reserved activity LED for Part II. If no MIDI
module is present, nothing changes anywhere (zero cost in the chain).

Channel model for Part I:

- The MIDI module has a **control channel** for PROGRAM-side commands:
  Program Change preset recall, slider CCs, and selected-stage modifier CCs.
  Default = MIDI channel 15 (0-based; displayed to users as channel 16), and
  it is configurable from the MIDI module context menu.
- SpaceTime has at most eight heads; per-head channel targeting is a natural
  fit for head-specific MIDI. For Part I, each HEAD gets a target channel
  equal to its 0-based head id (head 0..7 -> MIDI channel 0..7, displayed as
  channels 1..8) and reuses the same HEAD CC map on that channel.
  PROGRAM/stage edits remain on the module's control channel.
- MIDI Start/Stop/Continue are System Real-Time messages and carry **no MIDI
  channel**. Heads therefore respond by per-head opt-in, not by channel.

### Part I — MIDI in (accept/reject per line at the gate)

| # | Mapping | Decision |
|---|---------|----------|
| I-1 | **Program Change 1–12 → preset recall** | **Accepted.** Direct fit for the 12 slots; PC 1–12 on the module's control channel. |
| I-2 | **MIDI clock → head clock** | **Accepted.** Broadcast gains appended `midiClock/Start/Stop` counters; each HEAD has a panel clock-source switch: Internal / External CLK input / MIDI clock / Virtual clock (with the existing DIV/MULT applying to external, MIDI and virtual clocks). Starving MIDI clock shows the same yellow HOLD. |
| I-3 | **Start/Stop/Continue → head transport** | **Accepted with per-head opt-in.** Start/Stop/Continue are channel-less System Real-Time messages: Start/Continue = start pulse, Stop = stop pulse for heads that opt in. |
| I-4 | **CC → stage sliders** | **Accepted.** Fixed map on the PROGRAM/stage channel: CC 0..31 = voltage sliders 1..32, CC 32..63 = time sliders 1..32 (7-bit, scaled onto 0-10 V / 0-1). Slider takeover applies, same as preset recall. DROID is programmable, so a clean fixed map beats MIDI-learn. |
| I-5 | **CC → selected-stage modifiers** | **Implemented.** PROGRAM-section controls target the currently selected stage and honour the bulk window. |
| I-6 | **CC → HEAD controls** | **Implemented.** Each HEAD receives the fixed 14-CC map, including virtual clock, on its own head channel. This is distinct from channel-less MIDI realtime transport. |
| I-7 | **Notes → strobe/transpose** | **Postponed/rejected for Part I.** Controller MIDI starts with PC/CC/clock/transport. Note input can be reconsidered later only if a concrete controller workflow needs it; generated notes belong to outgoing MIDI Part II. |
| I-8 | MIDI-learn, arbitrary mapping UI | **Rejected/later** — scope; the fixed map + DROID programmability covers it. |
| I-9 | NRPN input | **Rejected/later** — 7-bit CC resolution on 0–10 V is 78 mV; fine for sliders. Revisit with Part II experience. |

Generated MIDI notes are an outgoing-MIDI problem, not a Part I input
default. Part II must separate the **pitch source** from the **gate source**:
pitch comes from the head's current stage/CV result, while note timing comes
from a selected gate lane.

### Part I CC layout

The PROGRAM/stage channel uses fixed CC numbers (no base CC offset):

- CC 0..31: voltage sliders 1..32.
- CC 32..63: time sliders 1..32.
- CC 64: select previous stage.
- CC 65: select next stage.
- CC 66: Clear.
- CC 67: bulk-arm next edit to all stages.
- CC 68: Quantize / Continuous.
- CC 69: Slope.
- CC 70: Range full/half.
- CC 71: Voltage source internal/external.
- CC 72..76: Limited octave buttons 1..5.
- CC 77: Stop.
- CC 78: Sustain.
- CC 79: Enable.
- CC 80: First.
- CC 81: Last.
- CC 82..85: Time range buttons 1..4.
- CC 86: Time source internal/external.
- CC 87: Pulse 1.
- CC 88: Pulse 2.
- CC 89: Key C..B (12 equal bins over 0..127).
- CC 90: Scale Major / Minor / Chromatic (3 equal bins over 0..127).
- CC 91: Pulse Retrig Off / On (value <64 / >=64).

Key and Scale set the current PROGRAM state directly; they do not simulate the
panel's modal KEY/SCALE button sequence. Pulse Retrig moves the visible PROGRAM
switch. Key/Scale MIDI changes show one second of non-modal feedback on their
mode and selection LEDs. Presets stay on Program Change 1..12.

For gesture CCs, value >=64 means up/add/set/press; value <64 means
down/remove/unset where that makes sense. Radio controls such as Limited
octave and Time range select on value >=64.

HEAD-side controls are separate from the PROGRAM/stage map. They use fixed CC
numbers on the head's own MIDI channel. Each HEAD exposes 13 panel controls
plus virtual clock:

- Virtual clock edge.
- Start, Stop, Advance, Reset.
- Address, Address source, Address mode.
- Direction, Clock source, Clock div/mult.
- Time CV amount, Loop mode, Display.

Across eight heads that is 112 logical controls, but the MIDI map does not
spend 112 unique CC numbers on one channel. Head 0..7 use MIDI channels 0..7
and all heads reuse this 14-CC map:

- CC 0: virtual clock edge.
- CC 1: Start.
- CC 2: Stop.
- CC 3: Advance.
- CC 4: Reset.
- CC 5: Address.
- CC 6: Address source.
- CC 7: Address mode.
- CC 8: Direction.
- CC 9: Clock source.
- CC 10: Clock div/mult.
- CC 11: Time CV amount.
- CC 12: Loop mode.
- CC 13: Display.

Button-like controls fire on value >=64. Continuous controls scale their MIDI
value onto the corresponding Rack param range.

### Part II — MIDI out

Per-head **output lanes** (8), each on its own channel (default = head
number), each with a mode. Implemented modes are Off, Notes and CC 7-bit,
configured from the MIDI module context menu along with output device,
channel, gate source and CC number. For note output, the pitch source is the
head's current CV after the whole SpaceTime path: stage voltage source
(internal slider or external A-D), PROGRAM voltage modifiers, and HEAD
playback state. Quantized stages map naturally to MIDI notes; unquantized
stages remain silent for note mode in the first implementation.

| Mode | Behaviour |
|------|-----------|
| **Notes** | **Implemented.** For quantized stages: note = round(CV·12) + 60. Timing comes from a per-lane **gate source**: P1 / P2 / ALL. P1/P2 notes end on gate fall; ALL uses a short 30 ms note length. |
| **CC** | **Implemented.** CV → 7-bit CC, send-on-change, throttled per lane (DIN-safe ≥ 3 ms between messages). |
| **CC 14-bit** | Later. MSB/LSB pair for 78 mV-isn't-enough cases. |
| **NRPN** | Later. 14-bit NRPN for gear that wants it. Same throttle. |

Plus one global lane: **ALL pulses → clock-ish trigger notes or MIDI clock
out** remains later. Deriving stable MIDI clock from irregular stage times is
questionable; trigger notes on a drum channel may be more honest.

Implemented by appending to `HeadStatus`: `pulse1/pulse2/all` levels plus a
`quantized` flag mirroring the current stage's program. Control rate (÷16 ≈
0.33 ms) is well inside MIDI's own 1 ms/message granularity, so head-status
sampling is not the bottleneck; the throttle is.

### Protocol impact summary (all append-only)

- `HeadsToAnchorMsg` += sequenced PROGRAM-side MIDI event batches.
- `AnchorToHeadsMsg` += global MIDI realtime counters and per-head CC value/counter arrays.
- `HeadStatus` += pulse gate levels + quantized flag (Part II).
- `ModuleType::Midi`, transparent relay in the MIDI module.
- Program-word layout and `Field` enum: **unchanged**.

### Gate questions for Peet

1. ~~Slug/name `Midi` / "MIDI", 4 HP, left-of-PROGRAM transparent position — OK?~~ **Accepted.**
2. ~~I-5 (CC → modifiers): worth it, or reject?~~ **Implemented.**
3. ~~I-6 vs I-7 default (strobe vs transpose/off) for note input — still to choose.~~ **Removed from Part I; notes are outgoing MIDI unless revived later.**
4. ~~Part I per-head MIDI clock/transport opt-in UI~~ **Implemented.**
5. Part I HEAD CC layout: **accepted as head channels 0..7 reusing fixed CC 0..13, including virtual clock.**
6. Part I PROGRAM/stage CC layout: **accepted as fixed CC 0..91 on configurable PROGRAM/stage channel, default channel 15.**
7. Part II note gate/timing: **implemented first pass** as per-lane P1/P2/ALL; P1/P2 note-off on gate fall, ALL fixed 30 ms.
8. NRPN out confirmed needed for your gear (which parameter resolution)? **Still postponed.**
9. MIDI clock OUT: wanted at all, or trigger-notes lane only? **Still postponed.**

---

## MetaModule feasibility (2026-07-13)

MetaModule Plugin SDK 2.2 does not support Rack expander communication. The
PROGRAM / STAGE4 / MIDI / HEAD chain therefore cannot be ported as separate
cooperating modules. The target is a fused 16-stage/two-head module sharing the
Rack-free DSP and MIDI core. MIDI input/output, JSON state, context menus and
dynamic text displays are available through the Rack adaptor; panel graphics
must be baked PNG. See `METAMODULE_IMPLEMENTATION_PLAN.md` for the implementation
work packages and the panel/slug review gates.

---

## Open Questions

1. **Slew levels on hardware:** confirm what distinguishes level 1 from level 2 on the 248t (fraction of interval? law?) — manual documents only binary sloped/stepped.
2. **Slew + Continuous addressing:** with the internal clock stopped in Continuous mode, is output interpolation between adjacent stages driven by the fractional address position? Verify on hardware (manual is silent).
3. **Limited range offsets:** exact five octave-offset values for the Limited range switches — read from panel.
4. **EXT slider quartile mapping:** confirm slider→A/B/C/D breakpoints (quartiles assumed).
5. **Pendulum endpoints (VCV extension):** repeat end stages or reverse without repeat; menu option?
6. **Master transport on PROGRAM:** global START/STOP for all heads — useful or clutter?
7. **Per-stage program visibility:** hardware shows one stage's programming at a time via the shared LEDs. Optional VCV extra: compact per-stage program glyph row on STAGE4 blocks (always-on at-a-glance) — worth the panel space?
8. ~~Module names~~ **Resolved:** plugin **SpaceTime**, modules PROGRAM / STAGE4 / HEAD — no MARF in names or slugs; 248t inspiration credited in the manual only.

---

## Next Steps (when active)

- [ ] Verify slew level behaviour and Continuous-mode interpolation on the 248t hardware (Q1, Q2)
- [ ] Read Limited range offsets from panel (Q3)
- [ ] Prototype dual-direction expander relay: anchor + 2 blocks + 2 heads; edit-op round trip and onExpanderChange renumbering
- [ ] Define `program[32]` bitfield layout in `dsp/StageTable.hpp`
- [ ] Custom momentary 3-pos switch widget
- [ ] Panel sketches via vcv-panel-design skill: PROGRAM first (switch-group layout), then STAGE4 column
- [x] Check MetaModule SDK expander support — absent in SDK 2.2; use fused module
- [ ] Cross-check against DROID-248t-Patch concept (direction set, time modifier)

---

## References

- `Buchla__Tiptop_Audio_248t.pdf` (this directory) — Tiptop 248t manual / function reference; video manual chapter links inside
- Hardware: 72 HP, +12 V 340 mA / −12 V 30 mA; original Buchla scaling 2.4 V/oct — VCV uses 1 V/oct native
- Buchla Model 248 (1977), Buchla & Associates
- VCV Rack expander API: `Module::leftExpander/rightExpander`, `requestMessageFlip()`
- Memory graph: `DROID-248t-Patch` (multi-playhead DROID sibling)
