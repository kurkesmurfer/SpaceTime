# SpaceTime MetaModule Implementation Plan

**Created:** 2026-07-13  
**Target SDK:** local MetaModule Plugin SDK 2.2 (`97ee128`)  
**Feasibility:** **GO as Core plus remote panels, gated by a shared-bus hardware probe**

## Feasibility result

MetaModule SDK 2.2 explicitly does not support Rack expander communication.
The current firmware loader nevertheless loads one copy of a plugin and
initializes its globals once. SpaceTime can potentially use that shared plugin
memory as a private, lock-free bus between its own module instances. This is an
implementation property rather than a supported SDK contract, so a hardware
probe is mandatory before product code depends on it.

The other relevant SDK capabilities are present:

- Rack-style modules, params, jacks, lights and JSON state are supported.
- `midi::InputQueue` and `midi::Output` are available; MetaModule's own MIDI-CV
  module uses the same input queue for CC, notes, clock and transport.
- VCV-port context menus are supported on the GUI thread.
- Dynamic text displays are supported through `MetaModule::VCVTextDisplay`.
- Panels and component artwork must be PNG; SVG and NanoVG text are not rendered.

This makes a Rack-adaptor fused module the lowest-risk first implementation.
A native `CoreProcessor` rewrite would add a second UI/persistence abstraction
without solving a problem that the adaptor leaves open.

## Product shape

The first product module is **SpaceTime Core**. It owns the complete engine,
MIDI implementation, physical I/O and persistent musical state. The engine
remains dimensioned for 64 stages and eight heads. The initial Core panel may
expose only essential status and setup because MIDI remote control is the
primary operating surface.

Optional smaller modules then act as control surfaces rather than DSP owners:

- STAGE4 Remote edits one assigned four-stage block.
- HEAD Remote edits one assigned head.
- PROGRAM Remote can provide a larger direct programming surface if useful.
- All modules select a saved SpaceTime instrument ID; multiple instruments may
  coexist in one patch without depending on adjacency.

This avoids both an unusably wide literal panel and deeply banked controls. A
missing remote never stops Core processing. A missing or duplicate Core is
shown clearly by every remote.

## Architecture

Add a platform-neutral `dsp/SpaceTimeEngine.hpp` owned only by Core. It contains
the state currently distributed through expanders:

- `StageTable` with 64 active stages and slider-takeover state;
- `ProgramLogic`, preset row, globals and key/scale;
- eight `HeadDSP` instances and their configurations/status;
- `MidiCore`, including incoming edits/transport and outgoing lanes;
- selected stage, stage bank, selected head and display-feedback state.

The engine API accepts fixed-size input/config snapshots and returns fixed-size
head outputs/status. It performs no allocation in `process()` and contains no
Rack or MetaModule headers. `Chain.hpp` remains the VCV transport only; the fused
engine calls the same underlying logic directly and does not simulate expanders.

Add a MetaModule Core adapter under `metamodule/src/` that:

- maps params and virtual jacks into the engine;
- drains raw MIDI into `MidiCore` and sends its output through `midi::Output`;
- runs heads at audio rate and scans controls/lights at the established divided
  control rate;
- saves program words, slider values/takeover, presets, global settings, MIDI
  setup and bank/head selection in patch state;
- provides MetaModule text displays and context-menu configuration where useful.

The private bus uses fixed-size storage, atomic ownership tokens, lock-free
remote-to-Core command queues, atomic Core-to-remote feedback and generation
counters. It allocates nothing and takes no locks in the audio thread. Core is
the sole authority for musical state; remotes persist only identity and local
editing choices.

The MetaModule target gets its own registration source. It must not compile the
VCV expander transport merely to reuse `vcv/plugin.cpp`.

## Work packages

### MM0 - Shared-bus proof

Create a deliberately disposable native plugin containing Core Probe and
Remote Probe. Each module publishes one input voltage through shared plugin
memory and exposes the voltage received from the other module. Include owner
generation, heartbeat, duplicate-Core detection and clean unregister behavior.

Exercise the probes on hardware with enough surrounding modules to invoke
multi-core balancing. Test save/reload, module deletion/reinsertion, two plugin
reloads, duplicate Core, stale-owner recovery and two independent bus IDs.

**Exit:** bidirectional values remain correct without virtual cables, across
patch reload and multi-core processing; duplicate and missing endpoints fail
visibly; no stale owner survives removal. If any item fails, use explicit LINK
cables or return to a fused Core rather than weakening synchronization.

### MM1 - Core engine

Implement `SpaceTimeEngine` and host tests. Start with four stages/one head in
tests, then exercise the full dimensions of 64/eight. Reuse `StageTable`,
`ProgramLogic`, `HeadDSP`, `PresetRowLogic` and `MidiCore`; do not duplicate their
algorithms.

**Exit:** golden VCV-vs-fused traces match for stage CV, timing, pulses, clock
sources, transport, key/scale, preset recall and MIDI edits.

### MM2 - Core panel mockup

Produce the smallest useful Core panel with fixed I/O, status and setup. MIDI
must be sufficient to program and operate the engine without remote panels.
Convert at 240 px high and inspect both 240 px and 180 px display scales. Freeze
the Core slug only after this review.

**Exit:** human layout gate; every control and jack has a stable ID and label.

### MM3 - Core control and stage integration

Wire PROGRAM, stage bank, selected-stage feedback, slider takeover, presets,
key/scale and pulse-retrigger behavior. Keep MIDI updates visually reflected in
the currently visible bank, just as CC 89-91 now update VCV feedback.

**Exit:** all 64 stages can be programmed through banked panel controls and
MIDI; save/reload is lossless; changing banks does not alter values.

### MM4 - Heads and I/O

Wire all eight heads, all four clock modes, dedicated CV inputs/outputs, status
lights and head selection. Verify that banking edits only the selected head
while every head continues processing continuously.

**Exit:** the eight-head manual patch passes internal, external CV, MIDI-clock
and virtual-clock tests, including tick-for-tick virtual-clock verification.

### MM5 - MIDI completion

Port the accepted implementation chart unchanged: channel 16 default for
PROGRAM/stages, head channels 1-8, CC 0-91, realtime clock/transport and outgoing
note/CC lanes. Expose settings that cannot sensibly be inherited from the patch
through the module action menu or compact panel controls.

**Exit:** DROID remote controls the fused module; outgoing note on/off and CC are
verified on hardware. Fourteen-bit CV output remains explicitly later.

### MM6 - Performance and hardware validation

Measure release and stress configurations on actual MetaModule. Record average
and peak CPU, MIDI-clock stability, event loss under dense CC traffic and patch
load/save behavior. Run at supported sample rates and with all eight heads
active.

**Exit:** no audio-thread allocation, no dropped transport/clock events in the
test patch, and documented CPU headroom. VCV's M3 figures are useful context but
are not a substitute for this measurement.

### MM7 - Optional remote panels

Implement STAGE4, HEAD and PROGRAM remotes only after Core is accepted. Reuse
the proven bus primitives and give every remote explicit assignment and link
status. Verify that adding, removing or duplicating a remote cannot change DSP
state except through a valid command.

### MM8 - Packaging and manual

Finalize PNG assets, metadata, versioning, install instructions, limitations and
the MetaModule-specific control map. Build a clean `.mmplugin` without modifying
`vcv/plugin.json` as a side effect of configuration.

## Testing matrix

| Layer | Verification |
|---|---|
| Shared engine | Existing host suite plus fused-engine unit/golden tests |
| Adapter | SDK build with warnings reviewed; package contents inspected |
| Persistence | Save/reload/reset tests for stages, presets, banks, heads and MIDI |
| MIDI in | CC map, PC 1-12, F8/FA/FB/FC, channel filtering, dense DROID stream |
| MIDI out | Note-on/off for P1/P2/ALL and throttled 7-bit CC on all eight heads |
| Timing | External, MIDI and virtual clock edge counts compared to source logs |
| UI | 240/180 px screenshots; bank/head state and remote feedback remain legible |
| Performance | Average/peak CPU and event-loss test on MetaModule hardware |

## Decision gates

1. Accept or reject the private shared bus from MM0 hardware evidence.
2. Freeze the Core slug after the Core panel mockup.
3. Decide which controls, if any, Core needs beyond MIDI and essential setup.
4. Add each remote type only in response to a demonstrated workflow need.

## Known risks

- Shared plugin memory is not a documented inter-module API. Firmware changes
  could invalidate it; keep the bus isolated and retain the explicit-cable
  fallback.
- MetaModule processes patches across two cores. Every shared field must use a
  defined atomic protocol; ordinary globals are not acceptable.
- Direct USB MIDI is supported by the adaptor API and used by a core port, but
  third-party input/output routing still needs a hardware proof in MM0.
- The DROID's direct MIDI map remains non-banked and is the primary remote
  surface, so Core must remain fully operable without UI remotes.
- The current MIDI clock interpretation advances on each F8 edge. Its intended
  musical relationship to 24 PPQN must be settled by the existing DROID test;
  the MetaModule port must preserve, not silently redefine, that behavior.
- Context-menu-only settings are less immediate on hardware. Performance-critical
  choices belong on the panel; setup choices may remain in the action menu.
