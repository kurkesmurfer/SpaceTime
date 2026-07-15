# SpaceTime — Implementation Plan (work packages for the agent street)

**Created:** 2026-07-06
**Updated:** 2026-07-13 (WP9 feasibility completed; MetaModule implementation plan added)
**Status:** WP0–WP8b implemented and under manual verification; WP9 complete
**Spec:** `marf-vcv.md` (this directory) — rev 4 is the contract; open questions Q1–Q4 are hardware-observable and non-blocking (see WP5 note)
**Repo target:** `~/Development/SpaceTime/` — standard DSP plugin development directory:

```
~/Development/SpaceTime/
├── dsp/        # generic, Rack-free DSP and logic headers (unit-tested host-side)
├── vcv/        # VCV Rack plugin base: plugin.json, Makefile, src widgets/adapters, res/ panel SVGs
│   └── res/
└── test/       # doctest runner building against dsp/ only
```

The VCV plugin builds from `~/Development/SpaceTime/vcv` (its `Makefile`, `RACK_DIR ?= ~/Development/Rack-SDK`) and includes `../dsp`. Future targets (MetaModule, Daisy) would sit as siblings of `vcv/`, all sharing `dsp/` — same pattern as Siren.

---

## Management Summary

The implementation is split into nine work packages sized for independent agents. Two principles govern the split. First, **mockup-first**: every module begins as a visual mockup — SVG panel plus a ModuleWidget with all components placed and inert — because panel/widget work has historically been the highest-risk part; mockups pass a human gate before any DSP is wired. Second, **host-testable DSP**: all sequencing, slew, quantization and protocol logic lives in pure C++ headers under `dsp/` with no Rack dependency, no dynamic allocation in the process path, unit-tested on macOS/Ubuntu with a plain Makefile test runner. Rack-side code is a thin adapter, verified with per-module manual test patches and a scripted persistence check. Dependency order: scaffold → (mockups ∥ data model) → widgets/protocol/head-DSP/program-logic in parallel → integration → release polish. The MetaModule feasibility spike is a separate optional package that must not delay the Rack build.

---

## Dependency graph

```
WP0 scaffold
 ├── WP1a mockup PROGRAM ─┐
 ├── WP1b mockup STAGE4  ─┤── human gate: panel review ──┐
 ├── WP1c mockup HEAD    ─┘                              │
 └── WP2 stage table / data model                        │
      ├── WP4 expander protocol                          ├── WP7 integration ── WP8 release
      ├── WP5 head DSP (state machine, slew, quantize)   │
      └── WP6 program-section logic (edit ops, presets)  │
 WP3 custom widgets ─────────────────────────────────────┘
 WP9 MetaModule spike (optional, independent)
```

WP2 can start immediately in parallel with the mockups. WP3–WP6 are mutually independent once their inputs exist — four agents in parallel.

---

## WP0 — Repository scaffold

**Scope:** Repo at `~/Development/SpaceTime` with the layout above. `vcv/plugin.json`: plugin slug **`SpaceTime`** (permanent), plugin name "SpaceTime", `"brand": "Kurkesmurfer"`; module slugs **`Program`**, **`Stage4`**, **`Head`** (permanent; display names PROGRAM / STAGE4 / HEAD — no MARF in slugs or names, trademark question moot). Rack `Makefile` in `vcv/` with `RACK_DIR ?= ~/Development/Rack-SDK` and `../dsp` on the include path. `test/Makefile` building host-side unit tests (doctest single-header, vendored) compiling only `dsp/` + `test/` — no Rack SDK include path. Empty `dsp/` headers with include guards. CI-style script `./check.sh` at repo root: builds plugin, builds and runs tests.

**Depends on:** —
**Deliverables:** building, loadable empty plugin; green empty test run.
**Tests:** `make` produces installable plugin; `test/run` exits 0; both on macOS (primary) — keep Ubuntu-portable.
**Agent notes:** read `~/.claude/skills/dsp-targets/` first; copy structure from the Siren repo, not from memory.

---

## WP1a/b/c — Visual mockups (one agent per module)

**Scope:** For each module: panel SVG + `ModuleWidget` with every control, jack and LED placed per spec section, correct param/port/light enums, correct tooltips, spring-return behaviour stubbed on the 3-pos momentaries (WP3 widget may be stubbed with CKSSThree initially). No DSP: `process()` empty except LED test pattern cycling all lights (proves every light is addressable and positioned).

- **WP1a PROGRAM (14–18HP):** stage scroll, Clear, voltage modifier group (Quantize, Sloped, Range incl. five Limited switches, Source), operating mode group (Stop, Sustain, Enable, First/Last), time programming (4 range switches, Source), Pulse 1/2, preset row (12 + Load/Save/Key/Scale), EXT A–D jack column, stage-count display, POLY OUT.
- **WP1b STAGE4 (10–12HP):** 4 columns × (voltage slider, time slider, LED cluster: edit-select + 8 head-position dots). Slider travel and pitch decisions here set the family look.
- **WP1c HEAD (8–10HP):** Start/Stop, Advance, Reset, Address knob, Int/Ext latch, Cont/Strobe 3-pos, status LEDs (G/Y/R), Direction, Clock source + div/mult, Loop; jacks: START, STOP, ADVANCE, STROBE, ADDRESS, CLK, TIME CV; outs: CV, TIME, REF, PULSE 1, PULSE 2, ALL, EOC.

**Depends on:** WP0.
**Deliverables:** three modules that load, look right, and light up.
**Tests:** visual review in Rack at 100% and 150% zoom (screenshots attached to the gate); every param shows a correct tooltip; LED test pattern reaches every light.
**Human gate:** Peet reviews panels before any wiring. Layout changes after DSP wiring are expensive; changes here are cheap.
**Agent notes:** mandatory: `~/.claude/skills/vcv-panel-design/` and `dsp-targets/references/vcv-rack.md`. Widgets concrete, never abstract; register Models exactly as in the Siren repo (Collide plugin post-mortem applies).

---

## WP2 — Stage table and data model (`dsp/StageTable.hpp`)

**Scope:** The shared data contract. `program[32]` bitfield layout (quantize, slew level 0–2, range full/half/limited×5, voltage source, stop, sustain, enable, first, last, pulse1, pulse2, time range 0–3, time source) with typed accessors. Edit-op struct `{stageIndex, field, value}` + apply function. Stage-table concatenation from per-block segments. Defaults matching the hardware Clear state (Continuous, Full Range, Internal, time range 2–30 s, pulses off). Fixed-size everything.

**Depends on:** WP0.
**Deliverables:** header + exhaustive unit tests.
**Tests:** bitfield round-trip for every field and boundary value; edit-op application incl. out-of-range rejection; concatenation of 1–8 blocks; Clear-state conformance table copied from the manual.
**Agent notes:** this header is the interface for WP4/5/6 — freeze it early, version any later change explicitly.

---

## WP3 — Custom widgets

**Scope:** Reusable widget library in `vcv/widgets/`: three-position spring-return momentary (param as up/down trigger pair, matching hardware feel), LED cluster (8 colour-coded head dots + edit-select), Limited-range switch bank, preset button row behaviour (Load/Save/Key/Scale modal presses). Pure UI; state driven via params/lights only.

**Depends on:** WP0 (and informs WP1; mockups may start with placeholders and swap in).
**Deliverables:** widgets + a `WidgetTest` dev-only module exercising each.
**Tests:** manual checklist on the WidgetTest module: spring return, no stuck states on fast clicking, correct light mapping; undo/redo of momentary edits behaves (Rack history integration).
**Agent notes:** momentary switches must not create a history event per animation frame — one event per press.

---

## WP4 — Expander protocol

**Scope:** Message structs (fixed size): leftward stage table `{voltage[32], time[32], program[32], count}`, rightward edit-ops + selected-stage, rightward head status `{headId, currentStage, phase, runState}` merge/relay. Chain enumeration and renumbering on `onExpanderChange` as **pure functions over an abstract neighbour interface** so the logic unit-tests without Rack; thin Rack adapter using `leftExpander/rightExpander` + `requestMessageFlip()`.

**Depends on:** WP2.
**Deliverables:** `dsp/Chain.hpp` (pure logic) + `vcv/src/ChainAdapter.hpp` + tests.
**Tests:** unit: enumeration for chains of 0–8 blocks/heads, gap detection, foreign-module termination, reorder renumbering, status merge with 8 heads; simulated per-hop one-sample latency (assert data coherence after N ticks). Integration: manual patch — add/remove/reorder blocks live, verify stage-count display and LED continuity.
**Agent notes:** producer/consumer buffers allocated in constructors; never in `process()`.

---

## WP5 — Head DSP (`dsp/HeadDSP.hpp`)

**Scope:** The Function Generator core as a pure, tick-driven state machine: sequential/strobe/continuous addressing, Int/Ext address, directions (fwd/rev/pendulum/random/brownian, seeded RNG), first/last loop bounds, one-shot, stop/sustain/enable semantics per manual (stop respects interval time; sustain holds while START gate high; enable waits for >5 V), interval timing with the four ranges and external time source (no-CV → fastest), slew engine (levels 1/2 as configurable fractions of interval — hardware values from Q1 slot in later as constants), quantizer (1 V/oct, major/minor/chromatic + key), REF downward ramp, pulse/ALL/EOC generation, run/hold/stop status output.

**Depends on:** WP2.
**Deliverables:** header + golden-trace test suite.
**Tests:** golden CSV traces (stage index, CV, ramp, pulses per tick) for scripted scenarios: plain 4-stage loop, first/last subrange, stop stage retrigger, sustain gate hold, enable threshold, pendulum + random determinism with fixed seed, strobe capture, continuous addressing (clock stopped, output follows address), external time fallback, slew levels at multiple clock rates (proportionality check), quantize round-trip against a semitone table. Traces reviewed once, then locked as regression goldens.
**Agent notes:** everything sample-rate-agnostic via dt parameter; no allocation; behaviour switches for Q1/Q2 outcomes behind named constants in one place.

---

## WP6 — Program-section logic

**Scope:** Stage-select scroll model (circular, hold-to-autoscroll timing, bulk-apply-while-scrolling), Clear semantics, edit-op emission from switch gestures, preset store (12 slots: full stage table + slider values; slider takeover rule — saved value active until physical slider crosses it), key/scale selection plumbing to the quantizer config.

**Depends on:** WP2.
**Deliverables:** `dsp/ProgramLogic.hpp` + tests.
**Tests:** unit: scroll wrap both directions, autoscroll timing model, bulk-edit emits ops for all N stages, Clear produces exact default table, preset save/load round-trip incl. slider-takeover state machine.
**Agent notes:** logic emits edit-ops only; it never touches block state directly (blocks own their data — spec rev 4).

---

## WP7 — Integration

**Scope:** Wire mockups (WP1, gate-approved) to logic (WP4/5/6) and widgets (WP3). Persistence: dataToJson/FromJson per module; verify sequence survives save/load and block reorder. POLY OUT channel mapping. Per-head colour assignment. Performance pass: control-rate scanning (every 16 samples), audio-rate only where required.

**Depends on:** WP1 (gated), WP3, WP4, WP5, WP6.
**Deliverables:** feature-complete plugin.
**Tests:** scripted JSON round-trip diff (save patch, reload, compare module data blobs); manual integration patch suite (documented in `test/patches/README.md`): 2-head/4-block MARF-equivalent patch, 8-head stress patch, live block reorder, preset recall during playback; CPU meter budget: full 8+8 chain under agreed % on the M3 Max reference.
**Agent notes:** single integrator agent; no parallel edits to `vcv/` during this WP.

---

## WP8 — Release polish

**Scope:** Manual (`README.md` with panel legends and deviations-from-hardware list), tooltips audit, context-menu options audit (globals per spec), plugin metadata, tag/screenshot for library submission decision, licence check (behavioural reimplementation, original panel artwork, no Buchla/Tiptop assets; plugin name SpaceTime — inspiration credited in the manual text only).

**Depends on:** WP7.
**Tests:** fresh-user walkthrough script; Rack library submission checklist.

---

## WP8b — MIDI control (design first, then implement)

**Scope (design phase — this WP starts with a spec addendum, human-gated, before any code):**

Produce a MIDI addendum to `marf-vcv.md` (rev 6) that decides:

- **Surface.** Preferred direction: a dedicated chain module **MIDI** (expander; slug fixed at design time — permanent, choose once) rather than menu-level mappings, keeping the approved PROGRAM/STAGE4/HEAD panels untouched. Decide its chain position (candidate: leftmost, beyond the heads, or a second anchor port on PROGRAM's right end) and HP.
- **Input mappings** (candidate list to accept/reject per item in the addendum):
  - MIDI clock → EXT clock for heads (respecting per-head div/mult), transport Start/Stop/Continue → head transport;
  - Program Change 1–12 → preset recall (a natural fit for the existing 12 slots);
  - Notes → stage addressing: strobe-on-note with note number → stage index, and/or note → transpose for quantized stages (keyboard plays the sequence root);
  - CC map → voltage/time sliders (64 targets: CC-per-stage vs. 14-bit CC pairs vs. selected-stage-only CC pair — decide) and → EXT A–D as virtual CV inputs;
  - Channel/device/driver selection Core-style (context menu).
- **Output mappings (stretch, may be cut):** pulses → notes/gates, head CV → note+pitch-bend.
- **Protocol impact.** Guiding principle: the MIDI module is *another edit-op emitter* — preset recall, slider writes and programming edits reuse the existing `Field`/`pushOp` path unchanged. Only clock/transport-to-heads needs a protocol decision (new broadcast fields vs. the MIDI module sitting in the head chain and injecting messages). The addendum settles this before implementation; any `Chain.hpp` change stays append-only.

**Implementation phase (after the addendum is approved):** mockup-first like WP1 (panel gate before wiring); pure mapping logic in `dsp/MidiMap.hpp` (note→stage math, CC scaling, PC→slot, clock follower) with host-side unit tests; Rack adapter using `midi::InputQueue` (events arrive on the audio thread — no allocation, clock handled sample-accurately, CC/PC at control rate).

**Depends on:** WP8 (released baseline).
**Split (2026-07-11, Peet):** Part I = MIDI in (control from the DROID-based
controller) ships first; Part II = MIDI out (CC/NRPN for CV, notes for
quantized output, P1/P2/ALL gate lanes, note-off timing policies) is
separately gated after Part I. Both share the one MIDI chain module.
**Deliverables:** spec addendum (human gate) → MIDI module mockup (human gate) → wired module + `dsp/MidiMap.hpp` + tests.
**Tests:** unit: mapping tables, clock division under simulated MIDI-clock jitter, PC→slot, note→stage/transpose round-trip; manual: DAW clock sync patch, keyboard strobe playing, controller-driven preset switching (documented in `test/patches/README.md`).
**Agent notes:** slug is permanent — decide in the addendum, not in code review. Do not touch the frozen program-word layout; new fields append-only. MIDI feature ideas beyond the accepted mapping list go to the addendum's "rejected/later" table, not into scope.

---

## WP9 — MetaModule feasibility spike (optional, parallel)

**Scope:** Verify expander message support in the current MetaModule SDK; if absent, estimate the fused-monolith fallback (PROGRAM + 4 blocks + 2 heads in one module). Timeboxed; outcome is a written go/no-go note appended to the spec, not code.
**Depends on:** WP0 only.

**Completed 2026-07-13 — qualified GO.** MetaModule Plugin SDK 2.2 explicitly
does not support Rack expander communication, so the VCV chain is a no-go as
separate modules. A fused 16-stage/two-head module is feasible: Rack-style
params/state, raw MIDI input/output, context menus and dynamic text displays are
available. SVG/NanoVG artwork must become baked PNG. The proposed architecture,
work packages, test matrix and decision gates are in
`METAMODULE_IMPLEMENTATION_PLAN.md`.

---

## Test strategy summary

| Layer | Method | Runs where |
|-------|--------|-----------|
| dsp/ headers (WP2/4/5/6) | doctest unit + golden traces | host, `./check.sh`, no Rack |
| Widgets (WP3) | WidgetTest module + manual checklist | Rack |
| Panels (WP1) | screenshot review, human gate | Rack |
| Integration (WP7) | scripted JSON round-trip + documented patch suite + CPU budget | Rack |

Rule for all agents: if a behaviour can be expressed as a pure function of (state, inputs, dt), it belongs in `dsp/` and gets a unit test; Rack code is adapters only.

## Suggested agent assignment

Wave 1: WP0 (one agent, short) → Wave 2: WP1a, WP1b, WP1c, WP2 (four agents) → human gate on panels → Wave 3: WP3, WP4, WP5, WP6 (four agents; WP5 is the largest — consider splitting state machine vs slew/quantize if needed) → Wave 4: WP7 (single integrator) → WP8 → WP8b (MIDI: addendum gate → mockup gate → wiring). WP9 anytime after WP0.

Open spec questions Q1–Q4 (hardware observations) feed WP5 constants and can arrive any time before WP7 without rework. Naming (former Q8) is resolved: plugin **SpaceTime**, modules PROGRAM / STAGE4 / HEAD.
