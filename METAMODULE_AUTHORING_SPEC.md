# SpaceTime MetaModule Authoring Architecture

**Status:** Accepted product direction; implementation specification and plan  
**Date:** 2026-08-27
**Scope:** MetaModule runtime modules, their VCV authoring twins, preset export,
controller bindings, shared state, MIDI parity, and hardware verification  
**Related documents:** `docs/ARCHITECTURE.md`,
`METAMODULE_IMPLEMENTATION_PLAN.md`, `METAMODULE_EXPANDER_BUS_PLAN.md`, and
`MIDI_IMPLEMENTATION_CHART.md`

## 1. Purpose

SpaceTime's native VCV implementation discovers its shape from an expander
chain. A PROGRAM module sees the attached STAGE4 and HEAD modules, so the number
of stages and heads is implicit in the patch topology. MetaModule cannot use
Rack expander adjacency and must instead bind modules through shared plugin
memory and an Instrument ID.

The first MetaModule implementation proved that shared-memory binding works
across both processor cores. It also fused the complete stage table, eight
heads, Program logic and MIDI into one Program/Core module, then added Stage4
and Head modules as remote surfaces. That implementation is useful as a proof,
but it is not yet a sound VCV preset-authoring target:

- VCV and MetaModule modules with the same slugs have different parameter and
  jack indexes.
- MetaModule Program and MetaModule Head currently run separate copies of a
  head's DSP.
- Stage4 can publish stale physical parameter values after Program has changed
  its authoritative table.
- MetaModule Program does not yet contain the complete VCV controller-feedback
  path needed by the DROID remote.

This specification replaces that provisional arrangement with one portable,
explicit module contract that can be instantiated in VCV, exported to a
MetaModule preset, tested in the desktop simulator, and loaded unchanged on
hardware.

## 2. Accepted Product Requirements

The following decisions are accepted and are not implementation options:

1. Program and MIDI form one authoritative MetaModule module.
2. SpaceTime has a fixed capacity of 64 stages and eight heads.
3. MetaModule Program has a visible **Number of Stages** control from 4 through
   64, incrementing in groups of four.
4. Number of Stages is patch configuration. Loading one of the twelve musical
   Program presets does not change it.
5. Program owns all stage values, program words, presets, globals, MIDI state
   and authoritative per-head configuration.
6. A MetaModule stage surface has no jacks. It is a monitor/editor for Program
   state, not an independent stage owner.
7. The normal local stage surface shows four stages: four voltage sliders and
   four time sliders, selected from the 16 possible banks.
8. Bindings made to those eight viewer sliders follow the viewer's selected
   bank. They do not remain tied to the bank visible when the binding was made.
9. Full-stage visualization and normal performance programming are expected to
   use DROID and the existing bidirectional MIDI protocol. The local Stage4
   viewer is optional.
10. The MetaModule MIDI CC, Program Change, realtime, outgoing-instrument and
    feedback maps are behaviorally identical to the native VCV implementation.
11. A Head is a real processing and I/O module because its eight inputs and
    seven outputs form part of the saved patch topology.
12. Up to eight Head modules can be present. A Head claims one head index and
    runs exactly one HeadDSP instance.
13. Program must not run a duplicate HeadDSP for a head claimed by a Head
    module.
14. Head All is an optional singleton per Instrument ID. It has the same common
    controls and eight inputs as native VCV HEAD ALL, but no signal outputs and
    no HeadDSP.
15. Native VCV's existing expander-chain modules and old patches remain
    unchanged. A parallel, fully functional VCV Rack authoring family provides
    the exact MetaModule module contracts.
16. The portable authoring slugs are frozen as `MMProgram`, `MMStage4` and
    `MMHead`. `MMHeadAll` is reserved for the deferred Head All module.
17. The VCV authoring modules are complete playable SpaceTime modules, not
    export-only parameter shells. A patch made from them must run and be
    testable in VCV before it is exported to MetaModule.
18. The existing MetaModule MIDI implementation is the accepted foundation.
    Complete MetaModule controller snapshot/live-feedback support follows after
    the three-module authoring family is operational.
19. Head All is deferred and does not block the first portable authoring set.

## 3. Target Module Family

### 3.1 Program/MIDI

There is exactly one Program/MIDI owner for each Instrument ID A-D. It owns:

- the fixed-capacity 64-stage table;
- the active stage count;
- selected stage, key, scale and global Function Generator settings;
- twelve Program preset slots;
- authoritative configuration for eight logical heads;
- incoming MIDI, MIDI clock and transport;
- outgoing note/CC lanes;
- DROID/controller feedback and snapshot requests;
- shared-memory ownership, duplicate detection and remote-module telemetry.

Program provides the four external A-D CV inputs and the complete Program
editing interface. It publishes coherent table/context snapshots to Stage4 and
Head modules. It accepts edit commands rather than continuously trusting remote
parameter positions.

Program's module state is the only serialized owner of the stage table and
Program presets. Stage4 must never serialize another copy.

### 3.2 Stage4 Viewer

Stage4 Viewer is an optional control-rate module with no jacks and no audio-rate
DSP. Its minimum interface is:

| Control | Range | Purpose |
|---|---:|---|
| Instrument ID | A-D | Select the Program owner |
| Bank | 1-16 | Select stages 1-4, 5-8, ... 61-64 |
| Focus | 1-4 | Select the stage targeted by Program modifiers |
| Voltage 1-4 | 0-10 V | Edit the selected bank's stage voltages |
| Time 1-4 | 0-1 | Edit the selected bank's normalized times |

It displays bank range, focus, link/duplicate state, pitch/cents, nominal time
and relevant stage flags. Moving one of its eight sliders also focuses that
slider's stage. Explicit Focus remains available so flags can be edited without
first disturbing a continuous value.

On a bank change or initial bind, Stage4 Viewer first reads Program and updates
its eight parameters. Those programmatic parameter changes are marked as
reflection, not edits. Only subsequent user/controller changes generate edit
commands back to Program.

Banks above Number of Stages display `INACTIVE`. Their sliders do not publish
edits. Values remain stored in Program and become visible again if Number of
Stages is increased.

Multiple viewers may bind to different banks. Two writable viewers claiming the
same Instrument ID and bank report `DUP`; neither may silently race as a second
writer. A later read-only monitor mode can relax this if there is a real use
case.

### 3.3 Head

Each Head module claims one tuple `(Instrument ID, Head index 1-8)`. Its saved
patch identity is its module instance plus that tuple. It exposes the complete
native Head interface:

- Start, Stop, Advance and Reset buttons;
- Address, Address Source and Address Mode;
- Direction, Clock Source, Clock Divide/Multiply, Time CV and Loop Mode;
- Start, Stop, Advance, Strobe, Address, Clock, Time CV and Reset inputs;
- CV, Time, Reference, All, Pulse 1, Pulse 2 and EOC outputs.

The Head runs one HeadDSP at audio rate and writes its seven local outputs
directly. Local input jacks override common Head All inputs in the same manner
as the native VCV chain.

Program owns the persistent HeadConfig. A newly bound Head pulls that config;
its controls reflect it. A local control change is sent to Program, accepted
there as the new authoritative value, and reflected back with a generation
number. This avoids two independent persistent copies.

Head publishes two kinds of information:

1. **Latest state:** stage, run/hold/stop state, phase, CV and configuration
   acknowledgement.
2. **Events:** stage entries, gate transitions, All/Pulse1/Pulse2/EOC edges and
   transport acknowledgements.

Events must use monotonic counters or a bounded single-producer/single-consumer
queue. One-sample pulses must never be represented only as an occasionally
polled Boolean snapshot. Program consumes the events for outgoing MIDI,
feedback and timing diagnostics.

### 3.4 Head All

Head All claims one Instrument ID and is unique within that instrument. It
contains the native VCV HEAD ALL controls and inputs:

- Start, Stop, Advance and Reset;
- Address, Address Source and Address Mode;
- Direction, Clock Source, Clock Divide/Multiply, Time CV and Loop Mode;
- common Start, Stop, Advance, Strobe, Address, Clock, Time CV and Reset inputs.

Persistent changes are commands to Program, which updates all eight
authoritative HeadConfig records. Momentary events carry sequence counters so
no edge is lost across cores.

Common continuous/gate inputs are published over a dedicated latest-value bus.
Heads consume those values only where the corresponding local input is
unpatched. This signal path may run at audio rate and must be benchmarked on
hardware.

Head All reflects aggregate state:

- one value when all relevant heads agree;
- `MIX` or an unselected indication when they differ;
- all-running, all-stopped and mixed transport states;
- `WAIT`, `LINK` and `DUP` ownership states.

Channel 9 MIDI remains the direct all-head route whether or not a Head All
module is present. Head All mirrors that state when installed; it does not
create a second MIDI map.

## 4. Number of Stages

Program stores a capacity of 64 stages at all times. Number of Stages changes
only the active prefix:

```text
4, 8, 12, ... 60, 64
```

The implementation should store the parameter as a snapped bank count 1-16 and
display it multiplied by four, or store 4-64 directly with a four-unit snap.
The former is less prone to invalid values.

Changing the active count has these effects:

- selected stage is clamped into the active range;
- every Head is brought into a valid active stage deterministically;
- navigation and wrapping use the active count;
- First/Last region discovery cannot cross the active boundary;
- Clear and bulk operations affect active stages only;
- inactive Stage4 banks reject writes;
- data above the active count is retained, not cleared.

The count is serialized in the complete patch. A Program preset records the
count that existed when saved for bounds information, but loading it applies
only the intersection with the current active count and does not change the
Number of Stages parameter. This mirrors native VCV, where a preset cannot add
or remove physical Stage4 modules.

The recommended initial default is 64 because it preserves the current
MetaModule behavior and the full DROID map. This default remains a product
checkpoint before the new parameter contract is frozen.

## 5. Controller-Binding Semantics

MetaModule mappings are stored as:

```text
panel control -> module instance ID + parameter ID
```

They contain no SpaceTime stage number. Consequently:

- a binding to Stage4 Viewer Voltage 1 always targets that viewer parameter;
- Bank 1 makes it Stage 1;
- Bank 6 makes the same binding Stage 21;
- changing Bank does not rewrite or duplicate the mapping record.

The normal Stage4 mapping therefore needs eight pots once, not 128 stage
parameters. Bank and Focus can be mapped if desired. Mapping remains a user
operation in VCV or on MetaModule; SpaceTime must not automatically consume all
eight available knob sets.

The firmware used during this design has eight knob sets, twelve pots and the
available button controls per set. Program's large control surface should be
mapped deliberately across those pages rather than auto-assigned wholesale.

When Bank changes, Stage4's reflected parameters jump to the new stored values
while physical pots do not move. The manual and test patch should recommend
MetaModule's `ResumeOnEqual` catch-up mode for absolute pots. `ResumeOnMotion`
intentionally jumps the stage to the physical pot on first movement. Relative
MIDI encoders and DROID rings use the MIDI feedback protocol instead and do not
have this physical-position problem.

Direct fixed-stage MetaModule alternate parameters are deferred. They are not
needed for the accepted DROID workflow and would add 128 parameter objects and
a difficult manual selection surface. The existing fixed MIDI CC map remains
the supported blind per-stage control path.

## 6. MIDI Compatibility Contract

`MIDI_IMPLEMENTATION_CHART.md` remains authoritative. MetaModule Program/MIDI
must use the same platform-neutral `MidiCore` and `MidiFeedbackCore` behavior as
native VCV.

Required routes include:

- Program controls: configurable channel, default displayed channel 16;
- stage sliders: configurable channel, default displayed channel 15;
- Heads 1-8: fixed displayed channels 1-8;
- Head All: fixed displayed channel 9, CC 0-12;
- controller feedback requests: displayed channel 10;
- global F8 clock and FA/FB/FC transport;
- Program Change preset recall;
- per-head outgoing Notes/CC/Off lanes;
- controller capability negotiation, head/program/stage snapshots and live
  deltas.

No MetaModule-only CC offset or alternate map is introduced. Number of Stages
is a MetaModule patch parameter, not a new incoming CC. Messages addressed to
inactive stages must match the behavior of native VCV with fewer attached
Stage4 modules; this behavior needs a parity test before implementation rather
than a new interpretation in the adapter.

VCV MIDI device identifiers are host-specific and must not leak into portable
MetaModule state. Channel configuration, output-lane configuration and feedback
settings are portable; physical endpoint selection is target-specific.

MetaModule may have one physical outgoing route where VCV permits separate
performance and feedback devices. Merging those streams is acceptable only if
loop prevention and DROID feedback are verified. The final routing behavior is
an implementation checkpoint.

## 7. Shared State and Transport

### 7.1 Ownership

Every Instrument ID has at most one Program owner, one Head All owner, one
owner per Head index and one writable Stage4 owner per bank. Duplicate claims
are visible and suppress writes. Removing an owner releases its claim without
leaving stale `LINK` state.

### 7.2 Transport classes

Use different primitives for different semantics:

- seqlock snapshots for always-current stage/config/display state;
- monotonic counters for isolated repeatable edges;
- bounded SPSC queues for events that carry payload and may occur more than
  once between control ticks;
- audio-rate atomics only where an actual jack signal must cross processor
  cores.

Do not use a control-rate Boolean snapshot for pulses. Do not allocate, resize
or lock from the audio callback.

### 7.3 Program poly output

The provisional Program exposes a four-channel poly output for Heads 1-4.
After HeadDSP ownership moves to Head modules, an accurate Program poly output
requires the remote Head CV to cross the shared bus at audio rate. A
control-rate copy is not acceptable for slewed Function Generator output.

Before freezing the new Program output contract, benchmark one of:

1. one audio-rate atomic CV value per claimed Head, retaining the existing
   Heads 1-4 poly output;
2. two four-channel poly outputs for Heads 1-4 and 5-8;
3. removal of aggregate poly output from the MetaModule-authoring family,
   requiring the individual Head CV jacks.

Existing user value and compatibility favor retaining at least option 1, but
correct signal rate and CPU cost take precedence.

## 8. Persistence and Presets

Program serializes:

- Number of Stages;
- all 64 live stage values and words, including dormant stages;
- key, scale and globals;
- twelve Program preset slots;
- eight authoritative HeadConfig records;
- MIDI channels, output lanes and portable feedback settings;
- Instrument ID.

Stage4 serializes only ordinary parameters such as Instrument ID, Bank and
Focus. Its eight displayed values are reflections of Program and are not an
independent JSON table.

Head serializes its Instrument ID and Head index as parameters. Persistent head
configuration lives in Program. A Head may serialize only genuinely local UI
state that has no Program equivalent.

Head All serializes Instrument ID and local presentation state. Its effective
common settings come from Program's authoritative head configurations.

Patch load order must not matter. Remote modules remain `WAIT` until Program
has restored state and published a complete generation. They then pull state
before enabling local edits. This specifically prevents stale Stage4 slider
parameters from overwriting a freshly loaded Program preset.

## 9. VCV Authoring Twins

The existing VCV modules keep their current slugs and expander behavior. New
authoring modules have unique slugs shared exactly with their MetaModule
counterparts. The frozen slugs are:

```text
MMProgram
MMStage4
MMHead
MMHeadAll
```

`MMProgram`, `MMStage4` and `MMHead` form the first implementation set.
`MMHeadAll` reserves the namespace for the deferred fourth module. Once
implemented, ParamId, InputId and OutputId orders are append-only.

The VCV twins use the same Instrument-ID shared bus and the same ownership and
reflection behavior as MetaModule. They are not expander-chain adapters or
passive export facades. They run the real Program/MIDI, stage-editing and Head
DSP paths in VCV Rack, expose the complete accepted controls and jacks, save and
restore their musical state, and produce inspectable audio/CV behavior. A VCV
preset built from them must therefore be playable and testable before export,
then behave equivalently on MetaModule.

Shared headers define all parameter, input and output indexes. Both adapters
include those headers, and compile-time/static unit tests assert every index and
count. Matching slugs without matching indexes is explicitly forbidden.

The provisional MetaModule slugs `Program`, `Stage4`, `Head` and `Midi` already
exist locally. Migration must be deliberate: either recreate development
presets once under the new slugs, or retain hidden legacy wrappers long enough
to load and convert them. Brand aliases cannot migrate module slugs.

## 10. Memory and CPU Budget

The design uses fixed capacity and does not allocate stage storage as Number of
Stages changes.

Approximate raw domain-state costs are modest:

- live StageTable: about 0.8 KB;
- twelve raw stage-table preset slots: about 9-10 KB;
- Stage4 viewer shadows and selectors: negligible;
- a cached full table in a viewer or Head: under 1 KB;
- eight HeadDSP/controller instances: expected to be tens of KB, not megabytes.

Parameter metadata, JSON construction and shared registry snapshots add more
than the raw arrays and must be measured rather than guessed. The implementation
report must include:

- `size` output for text/data/bss before and after each module phase;
- plugin arena or loader failures, if any;
- stopped and all-running CPU on hardware;
- processor-core allocation for eight Heads;
- audio-rate shared-bus cost for Head All common signals and Program poly CV.

The current measured baseline is approximately 24-25% stopped and 53% with all
eight provisional internal heads running. Removing duplicated heads should not
regress those figures. No dynamic allocation or lock is allowed in audio-rate
processing.

## 11. Implementation Plan

### Phase 0 - Baseline and contracts

1. Commit and tag the accepted pre-refactor baseline separately. **Done:**
   `mm-eb12-baseline`.
2. Add shared module-ID contract headers.
3. Add compile-time index/count tests.
4. Capture current ARM `size`, package size and hardware CPU figures.
5. Freeze new slugs and the Number of Stages default. **Slugs done:**
   `MMProgram`, `MMStage4`, `MMHead`; `MMHeadAll` reserved. Stage-count default
   remains to be frozen.

**Exit:** contracts compile on host, VCV and ARM; no current VCV patch changes.

### Phase 1 - Active stage count

1. Add snapped Number of Stages to Program.
2. Make table count, selection, navigation, regions, clear and bulk operations
   respect it.
3. Clamp/reset Head stage state deterministically after reduction.
4. Persist count in patches but not as a preset-recalled setting.
5. Add tests for every count from 4 to 64.

**Exit:** behavior matches native VCV chains with 1-16 Stage4 modules.

### Phase 2 - Portable Program/MIDI owner

1. Separate portable Program state from target MIDI endpoint state.
2. Retain the accepted incoming, clock/transport and outgoing MIDI behavior.
3. Verify byte-for-byte incoming route parity against native VCV.
4. Add generation-based Program publication after patch/preset load.
5. Build the fully functional `MMProgram` VCV authoring twin against the shared
   contract.

**Exit:** `MMProgram` owns identical portable musical state and MIDI behavior in
VCV and MetaModule; physical endpoint selection remains target-specific.

### Phase 3 - Stage4 Viewer

1. Replace continuous Stage4 publication with reflection plus edit commands.
2. Add Bank, Focus, inactive-bank handling and duplicate claims.
3. Implement reflected-parameter suppression during bind/bank/load changes.
4. Make slider movement focus the edited lane.
5. Build the exact VCV authoring twin.
6. Test mappings across bank changes with all three MetaModule catch-up modes.

**Exit:** one set of eight bindings edits every active bank without stale writes.

### Phase 4 - Head ownership relocation

1. Make Program authoritative for HeadConfig but remove duplicate DSP for
   claimed heads.
2. Complete Head claim/config/ack transport.
3. Add reliable Head event counters/queues.
4. Drive outgoing MIDI and feedback from remote Head events.
5. Resolve and benchmark Program poly output.
6. Build the exact VCV authoring twin.

**Exit:** local Head jacks, outgoing MIDI, feedback and Program telemetry report
one identical head state; eight Heads distribute across hardware cores.

### Phase 5 - Portable preset authoring

1. Build a VCV patch with `MMProgram`, `MMStage4` and one to eight `MMHead`
   instances.
2. Map representative knobs, buttons and hardware jacks.
3. Export YML and inspect module/parameter/jack indexes.
4. Load in the desktop MetaModule simulator and compare behavior.
5. Load unchanged on hardware and repeat.
6. Decide whether provisional development presets are recreated or migrated
   through hidden legacy wrappers.

**Exit:** one preset passes VCV, simulator and hardware without manual index or
state repair.

### Phase 6 - MetaModule controller feedback

1. Integrate full `MidiFeedbackCore` into MetaModule `MMProgram`.
2. Preserve the accepted controller capability negotiation and snapshot
   protocol.
3. Verify DROID snapshot and live-update parity for all 64 stages and eight
   heads.
4. Verify feedback routing and loop prevention with the hardware MIDI endpoint.

**Exit:** DROID can control and visualize MetaModule Program exactly as native
VCV without changing the established performance MIDI map.

### Phase 7 - Deferred Head All

1. Add singleton-per-Instrument ownership.
2. Port all twelve controls and eight inputs.
3. Add aggregate/mixed-state reflection.
4. Add common-signal normaling to unpatched Head inputs.
5. Preserve channel 9 MIDI behavior with or without the module.
6. Build the fully functional `MMHeadAll` VCV authoring twin.

**Exit:** panel, CV and channel 9 operations produce the same all-head results.

This phase is explicitly deferred and does not block Phases 0-6.

### Phase 8 - Cleanup and release documentation

1. Remove disposable bus probes from the musical package or move them to a
   diagnostic package.
2. Reconcile architecture and implementation-plan documents with the finished
   design.
3. Update the manual, MIDI chart and website.
4. Publish a complete compatibility and migration note.

## 12. Verification Matrix

### Host tests

- all stage counts and boundary transitions;
- inactive-stage MIDI parity;
- Stage4 bind, bank switch, focus and stale-write suppression;
- duplicate Program/Stage4/Head/Head All ownership;
- Head config generation and command acknowledgement;
- repeated pulse/event delivery without collapse;
- Head All normaling and mixed-state reduction;
- JSON round trips with arbitrary module construction/load order;
- byte-for-byte VCV/MetaModule MIDI route and feedback parity;
- shared parameter/jack index assertions.

### VCV manual tests

- build a preset using only the MM authoring family;
- map eight pots to Stage4 and move through several banks;
- verify mappings follow the viewer and values remain stage-specific;
- test `ResumeOnMotion`, `ResumeOnEqual` and `LinearFade`;
- run DROID against the authoring family;
- exercise one, four and eight Heads plus Head All;
- export and inspect YML.

### Simulator tests

- preset load and module discovery;
- all parameter mappings and static values;
- MIDI CC, clock, transport and feedback;
- Program preset save/load and full patch save/load;
- duplicate and missing Instrument-ID diagnostics;
- dynamic displays and inactive-bank presentation.

### Hardware tests

- firmware version and sample rate recorded;
- one through eight Heads distributed across both processor cores;
- 3-6 representative jacks used per Head;
- Head All common-input override/normal behavior;
- DROID USB MIDI control and complete feedback;
- eight Stage4 bindings across banks 1 and 16;
- save, power cycle and reload;
- delete/reinsert/rebind each remote module;
- stopped, partial and all-running CPU;
- no missing note, pulse, clock or transport events;
- plugin memory/package figures recorded.

## 13. Known Risks and Required Checkpoints

1. Shared plugin globals are proven on firmware 2.2.0 but remain an
   implementation property rather than a formal expander API.
2. Program poly output may require audio-rate cross-core state and must not be
   approximated at control rate.
3. Head event transport must survive multiple events between Program polls.
4. Stage4 parameter reflection must cooperate with MetaModule catch-up logic.
5. Portable state must exclude VCV-only MIDI device identifiers.
6. Existing provisional MetaModule presets use incompatible slugs/indexes.
7. Construction and patch-load order must never choose the initial writer.
8. Duplicate modules must fail visibly rather than produce nondeterministic
   last-writer behavior.

## 14. Decision Log

Accepted on 2026-08-22/23:

- Program/MIDI combined;
- fixed 64-stage capacity with explicit active count in groups of four;
- active count is patch configuration, not Program-preset content;
- DROID/MIDI is the primary full-stage visualization surface;
- optional four-stage/eight-slider local viewer;
- viewer bindings follow its selected bank;
- no automatic 128-stage-control mapping;
- real Head modules remain because their jacks define patch topology;
- one DSP owner per claimed head;
- optional Head All singleton per Instrument ID;
- MIDI behavior remains identical to native VCV;
- a separate, index-identical and fully functional VCV authoring family is
  required.

Accepted on 2026-08-27:

- authoring slugs are `MMProgram`, `MMStage4` and `MMHead`;
- `MMHeadAll` is reserved but its implementation is deferred;
- the VCV authoring modules must be playable and testable in VCV Rack;
- the existing MetaModule MIDI solution is retained;
- complete MetaModule controller feedback follows after the initial portable
  authoring set.

Open checkpoints before contract freeze:

- Number of Stages default (64 recommended);
- Program poly-output strategy;
- MetaModule performance/feedback MIDI output routing;
- recreation versus migration of provisional MetaModule presets.
