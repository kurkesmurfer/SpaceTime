# SpaceTime MetaModule Expander-Parity Bus Plan

**Created:** 2026-08-01
**Relationship to METAMODULE_IMPLEMENTATION_PLAN.md:** this plan details and
supersedes work package MM7 ("Optional remote panels"). MM0-MM6 are unaffected.
**Design principle:** preserve the VCV expander model's *data contracts* and
*failure semantics* (duplicate detection, missing-neighbor detection, chain-
broken indication) as closely as possible. Do not preserve positional
discovery, because it is architecturally unavailable: MetaModule SDK 2.2 does
not expose panel adjacency to plugin code, and its two-core scheduler gives
physical panel position no relationship to memory locality. Where positional
discovery cannot be ported, replace it with the closest behavioral equivalent
rather than a platform-specific redesign, so that `Chain.hpp` and the shared
`dsp/` engine bend as little as possible to accommodate the second platform.

## Current state

MM0 (shared-bus proof) is accepted on firmware 2.2.0. EB1-EB5 are done at the
host-test level; EB6 is done, narrowed to its currently-real scope (see EB6
below); EB7 is not started and needs hardware. EB8 (head relocation -- heads
move out of Core entirely, into HeadRemote modules) is done at the host-test
level, `Core.cpp` now publishes over all three EB8 buses, and a real
`HeadRemote` module/panel exist -- none of it has been compiled (no ARM
toolchain in this environment) or hardware-verified yet. Core.cpp and
Singularity.cpp (its VCV twin) no longer expose any per-head jacks at all
(0 heads in core, completed 2026-08-09); Accretion (VCV) and its
SingularityBus were removed as a direct consequence -- its whole premise was
covering the heads Singularity's panel had no room for, which stopped being
a real gap once Singularity has zero head jacks either. EB9 (below) adds a
read-only MIDI activity/channel monitor, `MetaModuleMidiStatusRegistry`,
mirroring VCV's separate Midi module's panel. EB10 (below, same day)
renames every metamodule module to match its VCV role-equivalent's slug
exactly (`Core`->`Program`, `HeadRemote`->`Head`, `MidiMonitor`->`Midi`) and
adds `Stage4`, wiring EB3's until-then-unused `MetaModuleStageBankRegistry`
to a real producer/consumer pair for the first time. References to `Core`/
`HeadRemote`/`MidiMonitor` in EB1-EB9 below are the historical record as
written at the time and are left as-is rather than retitled. EB11 (below,
same day) retires VCV's `Singularity.cpp` entirely, now orphaned by EB10's
slug rename -- the modular VCV family is the only supported VCV patch shape
going forward.

### EB9 - MidiMonitor: read-only MIDI activity/channel status

**Status:** Done at the host-test level, 2026-08-09. `Core.cpp` publishes,
`MidiMonitor.cpp`/panel exist. Not compiled (no ARM toolchain here) or
hardware-verified.

VCV keeps Program and Midi as separate modules; Midi.cpp owns the real
device queues, the DROID feedback protocol and per-head MIDI-out lanes, and
has its own panel (IN/CLK/OUT activity, PROGRAM/slider channel readout,
last-event line). Core.cpp fuses that ingestion in permanently -- no
possible MetaModule module can actually own MIDI I/O the way Midi.cpp does,
only one thing can bind the physical ports for a given Instrument ID. What's
portable is the panel: `MetaModuleMidiStatusRegistry`
(`dsp/MetaModuleRemoteBus.hpp`) is a one-way, no-CAS-of-its-own broadcast
(same shape as `MetaModuleStageTableRegistry`/`MetaModuleHeadMidiRegistry`)
carrying event *counters* (inSeq/clkSeq/outSeq), not Core's own already-
decayed light brightness -- a reader derives its own flash locally from the
seq delta, same as `TimingMonitor.cpp`'s `clockPulse`/`stepPulse` already do
against `sourceClockEvents`/`stageEntries` deltas. `MidiMonitor.cpp` is a
read-only companion (no MIDI jacks), Instrument-ID-bound, same LINK/WAIT/DUP
convention as `TimingMonitor.cpp`, reusing Core's own
`"MIDI CH%02d %s%03d V%03d %s"` display line format against the same
underlying `MidiCore` state, now read back over the bus.

Deliberately not slugged `Midi` (VCV's real module's slug): matching slugs
would let a MetaModule patch import silently resolve a VCV patch's real Midi
module -- device routing, DROID feedback, MIDI-out lanes -- onto this
read-only stand-in, discarding all of it with no warning. The cross-platform
identification lives in the module's description text instead.

**Exit (host-verified):** round-trip/gating tests for
`MetaModuleMidiStatusRegistry` in `test/MetaModuleRemoteBusTest.cpp`,
including a real `SpaceTimeEngine`-driven parity test (its actual
`MidiCore` last-event/channel state survives the bus round trip exactly).
169 cases, 6955 assertions, 0 failures.

**Exit (still open):** nothing in EB8/EB9 has been compiled -- no ARM
toolchain in this environment. `metamodule/plugin.json`/`plugin.cpp`/
`CMakeLists.txt` register `MidiMonitor`; `artwork/MidiMonitor.svg` exists,
authored with the legibility priority from the start (Peet, 2026-08-09).

Superseded same day by EB10 below: `Core`/`HeadRemote`/`MidiMonitor` are
renamed to `Program`/`Head`/`Midi`, and `MidiMonitor`'s collision-avoidance
naming (the previous two paragraphs) is deliberately reversed. Left as
written above rather than rewritten, per this doc's own convention of
recording corrections as annotations rather than silently editing history.

### EB10 - Slug parity: Program absorbs Core, Head/Midi/Stage4 match VCV

**Status:** Done at the host-test level (registry side only -- EB3's
`MetaModuleStageBankRegistry` was already tested; nothing new added to
`test/`), 2026-08-09. Not compiled -- no ARM toolchain in this environment,
same standing caveat as EB8/EB9.

Decision (Peet, discussed directly): every metamodule parallel module MUST
carry the same slug as its VCV role-equivalent, full stop -- "that the
mechanics are all different is irrelevant, as that does not show up in the
yml." This reverses EB9's own collision-avoidance reasoning for `Midi`, and
supersedes the "Core" slug chosen for the fused engine module. The VCV side
is completely unchanged by this decision (`vcv/src/Program.cpp`,
`Stage4.cpp`, `Head.cpp`, `Midi.cpp`, `HeadAll.cpp`, `GlueLeft.cpp`,
`GlueRight.cpp` -- none touched); only `metamodule/` renames and gains one
new module.

Renamed, mechanically identical apart from the slug/identifier change and
the additions below:

- `Core.cpp`/slug `Core` -> `Program.cpp`/slug `Program`. Also absorbs the
  rest of VCV `Program.cpp`'s role that Core didn't already have: `KEY_PARAM`/
  `SCALE_PARAM` (direct knobs, not VCV's press-then-digit gesture -- matches
  `STAGE_PARAM`/`PRESET_PARAM`'s existing direct-knob convention), a
  `BULK_PARAM` button (`ProgramLogic::armBulkOnce()`), and the twelve VCV
  gesture-modifier fields (Quantize/Slew/Range/VoltageSource/Stop/Sustain/
  Enable/First/Last/TimeSource/Pulse1/Pulse2) via one `MOD_FIELD_PARAM`
  select knob plus `MOD_UP_PARAM`/`MOD_DOWN_PARAM` apply buttons, rather than
  a 1:1 port of VCV's twelve spring switches -- `spacetime::SpringSwitch3`
  lives in `vcv/widgets/` and isn't available to this build, and a literal
  port (twelve springs + five Limited-octave buttons + four Time-range
  buttons, ~30 widgets) would fight the legibility priority this panel
  already follows. `LIMITED_OCTAVE_PARAM`/`LIMITED_APPLY_PARAM` and
  `TIME_RANGE_PARAM`/`TIME_RANGE_APPLY_PARAM` cover the two radio-group
  gestures the same way. Every `dsp::ProgramLogic` emit* entry point VCV's
  `Program.cpp` calls is reachable from this panel; MIDI already exercised
  all of them via `SpaceTimeEngine::applyMidiProgramEvent` (`SET_KEY`/
  `SET_SCALE`/`BULK_ARM`/`GESTURE`/`LIMITED`/`TIME_RANGE`), so this is new
  panel wiring onto an already-correct dsp/ path, not new dsp/ logic.
  `artwork/Program.svg` grows from 128.5mm to 195mm to fit the new PROGRAM
  section at the same legibility floor as the rest of the panel.
- `HeadRemote.cpp`/slug `HeadRemote` -> `Head.cpp`/slug `Head`. No behavior
  change, mechanical rename throughout (struct/widget/model identifiers,
  `res/Head.svg`, panel subtitle).
- `MidiMonitor.cpp`/slug `MidiMonitor` -> `Midi.cpp`/slug `Midi`. No
  capability change (still read-only, no MIDI jacks, still mirrors
  `MetaModuleMidiStatusRegistry`) -- only the slug and identifiers change,
  deliberately reversing EB9's collision-avoidance choice per the decision
  above.

New:

- `Stage4.cpp`/slug `Stage4`, matching VCV's real `Stage4.cpp`. Per Peet,
  directly correcting an earlier assumption in this plan: "a stage does not
  have much data... 2 sliders, and a bunch of flags. Best to keep that all
  in core... Basically the metamodule variant of Program contains all
  stages as well, so Stage4 only visualises the stages and does not own the
  data." Confirmed against VCV's real `Stage4.cpp`: it owns only
  `VOLTAGE_PARAMS`/`TIME_PARAMS` (2 sliders x 4 stages) as real panel
  widgets -- the program-word flags are never directly editable on Stage4's
  own panel either, only reachable via edit-ops arriving from Program. So
  this module's shape mirrors that exactly: two knobs per stage (4 x
  voltage, 4 x time), Instrument ID + bank-index selector, and nothing
  else -- no local mirror/shadow state, because there is exactly one
  legitimate writer to each of its own sliders (the knob itself); unlike
  `Head.cpp`'s MIDI-vs-panel-knob contention, there is no local contention
  here to arbitrate.
- Wires up `MetaModuleStageBankRegistry` (EB3, built 2026-08-02, unused by
  any real module until now) for the first time: `Stage4.cpp` calls
  `registerBank`/`unregisterBank`/`tryClaimBank`/`publishBank` each control
  tick, the same claimed-and-published discipline `Head.cpp` already uses
  for `headRegistry`. `Program.cpp` gains `syncStageBanks()`, called once
  per control tick: for each of the 16 bank slots, `readBank()` (already
  encapsulates the ownership+heartbeat check, EB3/EB5) and, if valid,
  arbitrates each of the bank's 4 stages' voltage/time against Program's own
  table via a per-(bank,stage,field) shadow -- the identical feed-forward
  trick `Head.cpp` already uses for MIDI-vs-knob contention (EB8), just
  running on Program's side this time since Program, not Stage4, is the
  authoritative table owner: if Program's own table value moved since the
  last sync (MIDI, preset, gesture), that wins and the shadow follows it
  (Stage4's slider display goes stale until physically touched, same
  limitation `Head.cpp`'s knobs already accept under MIDI control); else if
  Stage4's published value moved, it's folded in via exactly one
  `EditOp`/`engine.applyEdit()` call -- the same call MIDI's own slider CC
  already makes (`MIDI_PROG_SLIDER` -> `applyMidiProgramEvent` ->
  `apply(table_, EditOp(...))`). This closes the "Explicitly out of scope"
  bidirectional-command-queue item below for the voltage/time case
  specifically: no generation counters were needed, because bank ownership
  is already exclusive (one CAS'd owner per bank, EB3) so the only real
  contention is Stage4-vs-MIDI on the same stage, and a plain feed-forward
  shadow resolves that with no new primitive, the same way it already did
  for `Head.cpp`. On first bind (a bank transitioning from unowned to
  owned), the shadow is seeded from Stage4's own currently-published
  sliders rather than Program's pre-existing table value -- deliberately
  matching what plugging in a physical slider module would do (its position
  IS the value, not something to reconcile against table history) rather
  than guessing which side should win.
- `Stage4.cpp` never republishes the program-word (flags) portion of its
  `BlockSegment` -- it reads Program's table (`stageTableRegistry`, EB8) for
  its own status-display reference only, and `syncStageBanks()` only ever
  compares/arbitrates voltage and time. There is no path by which Stage4 can
  write a stage's flags; matches VCV's real `Stage4.cpp` exactly, where the
  flags are edit-op-only too.

**Exit (host-verified):** none of this pass added new `test/` coverage --
`MetaModuleStageBankRegistry`'s own publish/read/ownership/heartbeat
behavior was already exercised in EB3/EB4/EB5's existing tests
(`test/MetaModuleRemoteBusTest.cpp`), and this pass only wires a real
consumer/producer pair onto that already-tested registry, so the 169-case/
6955-assertion host suite from EB9 is unaffected and still passes clean
(confirmed by a fresh run after this change touched no `dsp/` file).

**Exit (still open):** nothing in this pass has been compiled -- no ARM
toolchain in this environment, so `Program.cpp`'s `syncStageBanks()`,
`metamodule/src/RemoteBus.{hpp,cpp}`'s new `stageBankRegistry` extern, and
`metamodule/src/Stage4.cpp` are unverified beyond careful review against the
already-reviewed `Head.cpp`/`Core.cpp` patterns they mirror. `artwork/
Stage4.svg` is a first pass (60.96mm x 128.5mm, matching `Midi.svg`'s
proportions) -- not iterated against the ~5.35mm legibility floor the way
`Head.svg`/`Program.svg` have been; worth a pass once Peet has seen it.
`metamodule/plugin-mm.json` (a separate, apparently build-inert manifest --
nothing in `CMakeLists.txt` references it) had its stale `Core` entry
updated to `Program` for consistency but was not otherwise reconciled
(it's missing `Head`/`Midi`/`Stage4` entries too, predating even the
original `HeadRemote`/`MidiMonitor` additions) -- not fixed here without
first confirming the file is actually dead, which its total absence from
the build graph strongly suggests but wasn't separately verified.

### EB11 - Singularity retired (VCV)

**Status:** Done, 2026-08-09.

Once EB10 gave MetaModule's `Program` module the entire fused role
(`Program`+`Stage4` data+`Head`s+`Midi` status, all under matching slugs),
VCV's `Singularity.cpp` -- the VCV twin of the old fused `Core`, deliberately
slugged `Core` so a MetaModule patch's `Core` module would resolve onto it --
lost its reason to exist. Its slug no longer matches anything on the
MetaModule side (`Program` does the resolving now), so nothing would ever
bind to it via a `.yml` import; it had become a dead module carrying real
but orphaned functionality. Confirmed directly with Peet: of three options
(leave it as a VCV-only convenience module / retire it entirely / rename its
slug to track `Program`), his call was retirement -- "I think 2 makes the
most sense."

Removed: `vcv/src/Singularity.cpp`, `vcv/res/Singularity.svg`,
`vcv/res/Singularity-light.svg`, the `Core`-slugged module entry in
`vcv/plugin.json`, `extern Model* modelSingularity;` in `vcv/src/plugin.hpp`,
and `p->addModel(modelSingularity);` in `vcv/src/plugin.cpp`. No `dsp/` file
touched; the modular VCV family (`Program.cpp`, `Stage4.cpp`, `Head.cpp`,
`Midi.cpp`, `HeadAll.cpp`, `GlueLeft.cpp`, `GlueRight.cpp`) is unaffected and
remains the only supported VCV patch shape. Verified no capability is lost:
`Program.cpp` already receives head run-state via `HeadsToAnchorMsg`
(reconstructing Singularity's "RUN 1-8" line for free if ever wanted), and
`Midi.cpp` already has its own MIDI-channel/last-event readout
(`MidiReadout` widget) covering the other half of what
`SingularityDisplay` consolidated. What's lost is only the *consolidation*
of both into one panel, not the information itself.

**Idea kept for later (not built, per Peet -- "just delete it for now, we
do not need it anymore"):** `SingularityDisplay`'s technique -- a single
NanoVG-drawn multi-line status widget consolidating what would otherwise be
several separate small readouts -- is worth remembering as a general
panel-decluttering pattern for any future plugin/module where a lot of
information is available but spreading it across individual widgets would
clutter the panel. Not scoped to any current module; noted here purely so
the idea isn't lost.

**Exit:** `grep -rn "Singularity"` across `vcv/src` and `vcv/res` returns
nothing; `vcv/plugin.json`, `plugin.hpp`, `plugin.cpp` all clean. Stale
`vcv/build/`/`vcv/dist/` artifacts from the last compile were also removed;
`vcv/dist/SpaceTime/plugin.json` (a copied build product, not source) still
has a stale reference and will be overwritten on the next `make install`.

### EB12 - Full VCV Program parity on MetaModule Program; Stage4 sliders

**Status:** Done at the code-review level, 2026-08-09. Not compiled --
`cmake`/`ninja` aren't available in this environment, so this is verified by
careful review and by the fact the untouched `dsp/` host suite still passes
(169 cases, 6955 assertions, 0 failures -- unaffected, since this pass only
touches `metamodule/src/Program.cpp`, `metamodule/src/Stage4.cpp`, and their
artwork). Needs a real build on Peet's machine to confirm.

First live look at the simulator (2026-08-09, after EB10/EB11 were finally
running) surfaced two real gaps, both Peet-reported directly rather than
found by review:

1. Program's EB10 revision -- the field-select-knob + Apply+/- shortcut for
   the twelve VCV gesture fields, plus a similar shortcut for Limited-octave
   and Time-range -- undershot what was asked: "did not carry over
   completely... my intention was to copy the program UI including
   everything, incl polyphonic cv." Fixed: `Program.cpp` now gives all
   twenty-one of those VCV controls their own button (twelve gesture fields
   as explicit Down/Up `LEDButton` pairs -- `spacetime::SpringSwitch3` is
   still unavailable to this build, so each three-position spring becomes
   two buttons calling the same `emitModifier()`/`emitLimited()`/
   `emitTimeRange()` entry points, just per-field/per-button instead of
   shared through one selector), plus a real polyphonic `POLY_OUTPUT`
   (`Port::setChannels`/`setVoltage` -- confirmed real in the SDK,
   `PORT_MAX_CHANNELS = 4` in `rack-interface/include/engine/Port.hpp`).
   VCV's own `POLY_OUTPUT` carries one channel per head, up to 8; asked
   Peet how to handle the gap against MetaModule's 4-channel cap, and per
   his choice this covers heads 1-4 only, fixed (not the first 4 *active*
   heads, not a second jack for 5-8) -- heads 5-8 simply have no CV output
   on MetaModule. `PRESET_PARAM`/`SAVE_PARAM`/`LOAD_PARAM` were deliberately
   *not* expanded to VCV's twelve-button press-mode-then-digit pad
   (`dsp::PresetRowLogic`, a second modal class beyond `dsp::ProgramLogic`
   that this module doesn't use) -- the existing direct knob+two-button
   design reaches the same 12 slots and full key/scale range with no loss
   of range, just a different control. Flagged to Peet as the one
   remaining intentional compaction, distinct from the gap this pass
   closes. `artwork/Program.svg` grows 195mm -> 305mm to fit twenty-one
   more individual controls plus the new jack.
2. Stage4 used `RoundSmallBlackKnob` for its voltage/time rows; VCV's real
   `Stage4.cpp` uses `VCVSlider` for both (confirmed by reading
   `vcv/src/Stage4.cpp:293-294`). Fixed: `metamodule/src/Stage4.cpp` swaps
   to `VCVSlider` for `VOLTAGE_PARAMS`/`TIME_PARAMS`. `artwork/Stage4.svg`
   grows 128.5mm -> 180mm: a slider needs real travel room (VCV's own
   Stage4 gives each slider row roughly a 48mm header-to-header lane; the
   old 30mm-apart knob rows would have visually collided with real
   sliders).

Also fixed in the same session, upstream of either module compiling
correctly: the simulator's asset-copy step (`create_plugin` in
`simulator/plugin.cmake`) tracks its output as a directory, so once
`build/assets/SpaceTime/` existed from an earlier build, ninja never
re-copies it no matter what changes underneath in `metamodule/assets/` --
this is why newly-generated per-module faceplate PNGs (via
`scripts/SvgToPng.py`, requires Inkscape) didn't show up until that stale
directory was manually removed and a full (not `--target simulator`-scoped)
rebuild run, which also repacks `assets.uimg` via the separate `asset-image`
target that `--target simulator` alone doesn't depend on. Neither is a
SpaceTime-side bug, both are simulator/build-tooling quirks worth
remembering for the next module addition. Separately, `siren`'s
metamodule/vcv sources are mid-rename to `Muse` (copyright reasons, per
Peet) and don't compile (`sirenui::createThemedPanel` missing) --
disabled in `simulator/ext-plugins.cmake` (commented out, not deleted) so
it stops blocking the simulator target; this is siren's own repair to do,
not SpaceTime's.

**Exit (verified, 2026-08-09):** built and run on Peet's machine.
`Stage4`'s sliders render correctly, no collision with INSTRUMENT/BANK or
the display. `Program`'s new sections are confirmed present and correctly
wired via the on-device knob-mapping roller (arrow-key/encoder navigation),
which lists params in exactly the coded `ParamId` order -- Save, Load,
Clear, Pulse retrigger, Key, Scale, Arm bulk edit, then the gesture/LTD/
TRANGE fields. Two real, non-SpaceTime build/tooling issues surfaced and
were fixed along the way: literal `--` inside XML comments in the artwork
SVGs (invalid XML; Inkscape's parser choked on it, likely truncating the
exported PNG -- fixed by replacing `--` with a plain hyphen in all four
affected files), and `metamodule/plugin-mm.json` (assumed build-inert at
EB10/EB11, now confirmed *not* dead -- it's missing entries for `Head`/
`Stage4`/`Midi`, fixed to list all seven modules).

Follow-up, 2026-08-10: Peet asked to correct the non-standard panel sizes
after seeing the current SpaceTime family in MetaModule. This supersedes the
temporary decision above. `Program` is now 32 HP x 128.5 mm (304 x 240 px),
using the full MetaModule display width to retain every EB12 control without
vertical overflow. `Stage4` is back to 12 HP x 128.5 mm (114 x 240 px); the
SDK's `VCVSlider` is only about 20 mm tall, so both slider rows fit without
the former 180 mm artwork. All seven SpaceTime faceplates now use the same
light theme and exact native-height raster assets. The reproducible renderer
is `metamodule/scripts/render_panels.py`.

## Work packages

### EB1 - Generalize the transport primitive

**Status:** Done, 2026-08-02. `dsp/ExpanderLink.hpp` added, holding
`ExpanderMailbox<T>` (word-sized, change-tracked - generalizes the original
`ProbeMailbox`) and `ExpanderSnapshot<kFieldCount>` (multi-word seqlock -
generalizes the original `TimingSnapshot`/`TimingTelemetry` storage). Kept as
two templates rather than one: the mailbox is change-tracked ("tell me if
something new arrived"), the snapshot is always-current ("give me the latest
coherent value, changed or not") - collapsing them would have hidden that
distinction rather than removed duplication. `ProbeMailbox` is now
`using ProbeMailbox = ExpanderMailbox<float>;`; `TimingTelemetry` wraps
`ExpanderSnapshot<kMaxHeads*6>` and marshals its typed fields to/from the flat
array. `ExpanderSnapshot` keeps the original per-field-atomic-array storage
(not a whole-struct memcpy) so every access stays inside what the C++ memory
model actually guarantees.

`Probe.cpp`, `Core.cpp`, and `TimingMonitor.cpp` required zero changes -
verified by diffing every symbol each file touches against the new headers
before applying.

**Exit:** `MetaModuleBusProbeTest.cpp` and `MetaModuleTimingBusTest.cpp` pass
unmodified against the refactored primitive; new `ExpanderLinkTest.cpp` added
for direct coverage of the shared primitive itself. Host suite (135 cases,
6298 assertions) and a full local VCV plugin build both pass clean.

### EB2 - Role-tagged registry

**Status:** Done, 2026-08-02. Added `BusRole` enum
(`Core`/`Remote`/`StageRemote`/`HeadRemote`/`ProgramRemote`) to
`dsp/MetaModuleBusProbe.hpp`, plus `registerRole`/`tryClaimRole`/
`unregisterRole`/`roleCount` entry points on `MetaModuleBusProbeRegistry`.
`Core`/`Remote` dispatch to the original methods unchanged, so MM0's
hardware-verified behavior cannot regress; the three new roles use additive
`auxOwner`/`auxCount` slots on `MetaModuleProbeBus`. Nothing about the
original struct layout or method set was removed or renamed.

**Exit:** new host test ("MetaModule probe bus partitions StageRemote and
HeadRemote roles") proves a StageRemote and a HeadRemote registered on the
same Instrument ID do not link to each other, and each links correctly to a
Core, exercised directly against the registry API since neither module exists
yet.

### EB3 - Per-slot sub-addressing for stage banks and heads

**Status:** Done at the host-test level, 2026-08-02. Hardware verification
still open (see Exit).

Correction to this section as originally written: it does *not* build on
`BusRole`/`auxOwner`/`auxCount` from EB2 after all. EB2's aux slots are one
exclusive owner per role per Instrument ID, which fits Core/Remote/
ProgramRemote but not "16 independently addressable banks" or "8
independently addressable heads" - forcing that shape would have meant either
16 separate `BusRole` values or a second parameter EB2 wasn't designed to
carry. Instead, `dsp/MetaModuleRemoteBus.hpp` adds two dedicated registries,
`MetaModuleStageBankRegistry` and `MetaModuleHeadRegistry`, each following the
same `compare_exchange_strong(0 -> token)` ownership pattern as a 2D array of
independent slots (Instrument ID x bank/head index) rather than one slot per
role. EB2's `BusRole::StageRemote`/`HeadRemote` and their aux slots remain in
place, tested, and harmless, but a real StageRemote/HeadRemote module should
bind through this registry instead. `BusRole::ProgramRemote` remains the one
EB2 aux role a real module will actually use, since PROGRAM has no per-index
addressing to do. Worth a deliberate decision later: leave the now-unused
StageRemote/HeadRemote aux slots as documented dead capacity, or trim them
from `BusRole` - not done here without discussing it first.

A StageRemote publishes its own bank index (0-15) as a complete
`BlockSegment` (voltage/time/program, the same struct `Chain.hpp`'s
`concatenate()` already consumes) rather than a placeholder payload - the
struct already existed as exactly the right shape, so there was no reason to
invent a stand-in. A HeadRemote publishes its own head index (0-7) as a
complete `HeadConfig` (the same struct `HeadDSP` already takes as panel-control
input). Both are always-current full-state publishes via `ExpanderSnapshot`,
not incremental single-field edits - the incremental edit-op command queue
with staleness generation counters remains the explicitly out-of-scope item
noted below, not something this work package quietly absorbed.

`readAllBanks()` aggregates all 16 slots into a `StageTable` by calling the
existing `concatenate()` unchanged - this is the direct behavioral substitute
for `Chain.hpp` walking expander pointers, and the one place this plan
deliberately diverges from VCV's mechanism, because MetaModule exposes no
positional signal to preserve.

Duplicate handling follows the discipline MM0 already established in
`Probe.cpp`, generalized: a Remote that loses the ownership CAS must not call
`publishBank`/`publishHead` at all (check the registration return value
first). Readers (`readBank`/`readHead`) independently re-verify exactly one
live owner before trusting a slot's payload regardless - defense in depth,
the same pattern `Core.cpp` already applies to the timing bus
(`bus.coreCount.load(...) != 1`). An unclaimed or duplicated slot reads as the
struct's own defaults (EB5's principle, arrived at naturally rather than
deferred): `BlockSegment()` is zero voltage / mid time / cleared program,
`HeadConfig()` is the documented power-on defaults.

**Exit (host-verified):** `test/MetaModuleRemoteBusTest.cpp` - publish/read
round-trip for both bank and head payloads; unclaimed slot reads as struct
defaults for both; duplicate slot reports the correct link count and is not
trusted on read for both, and recovers correctly once the loser unregisters;
`readAllBanks` concatenates claimed banks correctly with an unclaimed bank in
the middle reading as a default gap; Instrument ID isolation for stage banks.
143 cases, 6374 assertions, 0 failures; clean local VCV plugin build.

**Exit (still open, hardware):** the MM0-style hardware pass - LINK/DUP/WAIT
on real bank/head sub-slots specifically (not just Core/Remote), save/reload,
deletion/reinsertion, and confirming on a scope that a DUP Remote's data
genuinely never reaches an audio-rate output, not just the host-test
read path.

### EB4 - Auto-bind default policy

**Status:** Done at the host-test level, 2026-08-02. Hardware/UI verification
still open (see Exit).

One correction to this section as originally written: auto-bind cannot query
`MetaModuleBusProbeRegistry` (the registry EB2's `BusRole` lives on), because
that registry only tracks the disposable MM0 probe modules -- `Core.cpp` has
never registered on it. Verified directly against `Core.cpp`: it only ever
calls `timingBusRegistry.registerCore(...)`. So the real "does a Core exist"
signal is `MetaModuleTimingBusRegistry`, and that's what `findSoleCore()` was
added to, not the Probe registry. This also means there are now three
independent registries that each track "what's registered per Instrument ID"
(Probe's, Timing's, and the EB3 remote-bus's) and only Timing's reflects the
real Core -- worth remembering if a fourth is ever added.

`findSoleCore(unsigned& instrumentId)` is a read-only scan across all four
Instrument IDs' `coreCount`: returns true and the resolved id only when
exactly one Instrument ID has a live, single-owner Core; returns false for
zero or for more than one (including a duplicated Core on one instrument,
which correctly still counts as ambiguous rather than resolvable). It claims
nothing and changes no ownership -- the calling module still does its own
`registerBank`/`registerHead`/`registerRole` with the resolved id afterward,
exactly as it would with a manually-picked id. No convenience method that
fuses "find" and "register" into one call was added: that call would have to
overload its return value across two different situations a real module needs
to tell apart ("no Core to bind to" vs. "found the Core, but lost the
ownership CAS on this specific bank to another Remote") -- better to leave
that composition to the calling module's own code once it exists, which the
new end-to-end test demonstrates directly.

**Exit (host-verified):** `findSoleCore` resolves correctly when a Core sits
on a non-default instrument (not just index 0); correctly declines when zero,
two, or a duplicated Core are present; correctly re-resolves once ambiguity
clears. A composed end-to-end test (`MetaModuleRemoteBusTest.cpp`) shows the
intended real sequence -- resolve via `MetaModuleTimingBusRegistry`, then bind
and publish through `MetaModuleStageBankRegistry` using the resolved id -- and
confirms an already-bound Remote is unaffected when a second Core later makes
auto-bind ambiguous for any *new* Remote. 145 cases, 6396 assertions, 0
failures; clean local VCV plugin build; zero changes to `Core.cpp`,
`Probe.cpp`, or `TimingMonitor.cpp`.

**Exit (still open):** there is no real StageRemote/HeadRemote/ProgramRemote
module yet to wire this into, so "placing one Core and one Remote links with
no configuration step" can't be verified end-to-end until one exists. The
UI-level behavior (when to surface the A-D picker vs. suppress it) is also
unverified against a real panel/context-menu, and the reload-ordering risk
noted below needs a real hardware pass, not just host logic.

### EB5 - Unclaimed-slot default read path

**Status:** Done at the host-test level, 2026-08-02, and it found a real bug
in EB3 rather than just formalizing already-correct behavior.

The bug: `readBank`/`readHead` already checked `owner != 0` and `count == 1`
before trusting a slot (EB3), but a bank/head that is validly registered and
has simply never had `publishBank`/`publishHead` called on it yet passes both
of those checks -- it looks claimed. It then fell through to
`ExpanderSnapshot::read()`, which correctly returns its own pre-publish
default (all-zero bits, by EB1's design) rather than failing. The problem is
that the all-zero-bits default and the *typed* struct's real default
disagree: `BlockSegment()` defaults to 0.5 for `time`, not 0; its program word
defaults to `kClearWord` (196672), not raw 0; `HeadConfig()` defaults
`clkDivIndex` to 4 (x1), not 0 (/16), and `loopMode` to `LOOP_FIRST_LAST` (1),
not 0. A caller would have silently received the wrong defaults for exactly
as long as a bank/head was registered but not yet publishing -- and, more
importantly, this is also the closest host-testable stand-in for what stale
leftover memory from an unclean plugin unload/reload (EB7) would look like:
structurally claimed, never actually populated.

Fix: both `readBank` and `readHead` now also require
`target.segment.heartbeat() != 0` / `target.config.heartbeat() != 0` --
`ExpanderSnapshot`'s own publish counter, which EB1 already had and exposed
but nothing previously consulted for this purpose. Confirmed the fix is real,
not just documentation, by temporarily disabling the heartbeat check and
re-running the suite: the two new regression tests below failed with exactly
the predicted wrong values (`time == 0` instead of `0.5`, `clkDivIndex == 0`
instead of `4`, etc.), then passed again once restored.

Also added `bankHeartbeat`/`headHeartbeat` accessors, documented as the
freshness half of a two-axis model: ownership validity (`owner`/`count`,
answered by `readBank`/`readHead` themselves) and publish freshness
(heartbeat advancing over real time, answered by the caller). These are kept
separate deliberately, mirroring `TimingMonitor.cpp`'s own existing pattern
(`coreCount == 1 && staleTime < 0.25f` -- two ANDed conditions, not one fused
check) rather than inventing a new convention. `readBank`/`readHead` do not
themselves judge staleness-over-time, since only a caller with access to real
elapsed time or sample rate can do that correctly -- exactly the same
platform-neutral/platform-adapter split EB1-EB4 already established.

This mirrors the existing MIDI protocol convention -- "requests for stages
beyond the connected chain return 0 so controllers can clear stale rings" --
applied to the internal bus instead of the MIDI feedback wire.

**Exit:** `test/MetaModuleRemoteBusTest.cpp` -- a claimed-but-never-published
bank and head both read as their struct's real defaults, not
`ExpanderSnapshot`'s raw zero-bits default (this is the regression test that
would have failed before the fix, and was confirmed to); bank/head heartbeats
advance only on publish, not on read. 149 cases, 6432 assertions, 0 failures;
clean local VCV plugin build; zero changes to `Core.cpp`, `Probe.cpp`, or
`TimingMonitor.cpp`.

**Exit (still open):** the actual "stale memory from an unclean plugin
unload/reload" scenario remains only approximated by the host test above
(a registered-but-unpublished slot), not reproduced from a real unload/reload
cycle -- that still needs the MM0-style hardware pass tracked under EB7.

### EB6 - Chain-broken indication parity

**Status:** Done, 2026-08-05, narrowed from the original wording (see
correction below). This is the first EB to touch `Core.cpp` itself, rather
than only `dsp/` headers - a deliberate, necessary step for a display/menu
feature, not a lapse in the "zero `.cpp` changes" discipline EB1-EB5 held to
for purely additive transport work.

Correction to this section as originally written: it does *not* surface
"missing and duplicate slots" from `MetaModuleStageBankRegistry`/
`MetaModuleHeadRegistry` (EB3-EB5). Checked `Core.cpp` directly (per
`metamodule/README.md`: "Core owns all 64 stages, all eight continuously
running heads... The probes remain in the package until the first remote
control surface is complete") - Core never defers to those registries for its
own state today, so every one of their 16 bank slots and 8 head slots reads as
unclaimed by design, not by fault. Wiring "missing" into the `!` glyph as
originally planned would have made it permanently lit and meaningless the
moment this shipped. The one hazard that is real today is a second Core bound
to the same Instrument ID - `MetaModuleTimingBus`'s own `coreCount`, the same
signal `publishTiming()` and `TimingMonitor.cpp` already rely on
(`coreCount == 1`). `Core::coreConflict()` checks exactly that
(`coreCount != 1`) and needed no new `dsp/` primitive, since EB4 already built
and tested the state it reads.

The `!` appears inline next to `STAGE %02d V %.2f T %.3f` on the panel
display - the same placement discipline as Program's stage-count widget
(`string::f("%d%s", displayCount, displayBroken ? "!" : "")`,
`vcv/src/Program.cpp`), a bare glyph next to the field it qualifies, no second
warning language and no separate status line competing for the panel's
already-dense four-line, 128-byte display. The context menu's existing
"Instrument ID" submenu label gets the same condition spelled out as
`" (duplicate Core!)"`, since the menu has room the display doesn't.

The registries' duplicate-slot detection (`bankLinkCount`/`headLinkCount`
`> 1`, already built and tested in EB3-EB5) becomes a real, easily-wired-in
extension of `coreConflict()`'s pattern the day an actual StageRemote/
HeadRemote module exists to make "duplicate" possible - noted here rather than
spread across two future PRs, since the check itself does not need to change,
only when it's meaningful to run.

**Exit (implemented, not hardware-verified):** `Core::coreConflict()` added;
`get_display_text` and the "Instrument ID" context menu item both surface it.
Zero changes to `dsp/`, so the 149-case/6432-assertion host suite is unaffected
and still passes clean - confirmed by a fresh rebuild after this change. No
`.cpp`/`.hpp` file outside `metamodule/src/Core.cpp` was touched.

**Exit (still open):** this cannot be compiled here - `metamodule/CMakeLists.txt`
requires the ARM GNU 12.3 toolchain and a local `metamodule-plugin-sdk`
checkout, neither available off Peet's machine - so the change is unverified
beyond careful manual review of the diff (format-string argument count and
buffer length rechecked by hand). The MM2 panel/menu legibility gate from the
original wording still applies: confirm the `!` and the menu's
"(duplicate Core!)" text are legible at 240 px and 180 px display scale, most
directly by forcing `coreConflict()` to return true and reloading the patch
rather than waiting for a real duplicate-Core scenario on the bench.

### EB7 - Plugin unload/reload robustness (carried over from MM0)

**Status:** Open, severity reduced now that EB5 has landed.

The two full plugin unload/reload hardware cycles flagged as outstanding in
MM0 apply equally to the new per-slot registries, and remain untestable off
hardware because they depend on undocumented loader behavior (whether
`.bss`-equivalent storage is freshly zeroed on reload). EB5 changes what a
failure here costs, via the same `heartbeat() == 0` gate that fixed the
claimed-but-never-published bug: stale leftover memory that happens to have a
nonzero `owner` and `count == 1` (looking claimed) but a zero heartbeat (never
actually published since this bus struct was last legitimately live) is read
as unclaimed and returns struct defaults, not whatever bytes are resident.
The remaining gap is memory that's stale in a way that *also* has a nonzero
heartbeat left over from before the unclean reload -- EB5's host test can't
produce that state, since it can only simulate "never published," not "was
published once, a while ago, by a since-vanished owner." That specific case
still needs the hardware cycle test from MM0; no longer a correctness
blocker for MM7.

### EB8 - Head relocation: HeadRemote owns and runs its own head

**Status:** Done at the host-test level, 2026-08-08. `Core.cpp` publishes,
and a `HeadRemote` module/panel exist (same day) -- neither has been
compiled or run yet; see "Exit (still open)" below.

Supersedes the original EB1-EB7 assumption that Core always computes all
eight heads internally regardless of remotes. Decision (discussed directly,
not inferred): Core stays fused for Program -- all 64 stages, presets,
globals, MIDI ingestion -- but heads are relocated wholesale, not partially.
No head is special-cased as Core-resident; a `HeadRemote` with no panel
placed means that head simply doesn't exist, the same as any other head
without a `HeadRemote`. Rejected an intermediate design (Core keeps Head 1
built in, heads 2-8 relocate) specifically to avoid an arbitrary asymmetry
with no principled boundary.

This is a materially different kind of change than EB1-EB7: those were
additive transport with zero `dsp/`/`Core.cpp` changes (EB6 aside, and even
that was display-only). EB8 changes what actually computes a head's output,
so it gets its own MM1-style parity gate rather than inheriting EB1-EB7's
"zero regression risk by construction" argument.

Two new pieces in `dsp/MetaModuleRemoteBus.hpp`, both reusing
`ExpanderSnapshot`/heartbeat exactly as EB1-EB5 established -- no new
transport primitive invented:

- `MetaModuleStageTableRegistry`: Core -> HeadRemote direction, the mirror
  image of EB3's bank direction. Publishes the complete 64-stage table so a
  HeadRemote can address into it without Core computing anything. Has no
  ownership CAS of its own by design -- "is there a valid sole Core to trust
  this from" is already `MetaModuleTimingBusRegistry`'s `coreOwner`/
  `coreCount` fact (the same one `coreConflict()` and `findSoleCore()` key
  off), and re-deriving it a second time would track one real-world fact in
  two places. `readTable()` takes the caller's already-checked core-validity
  bool, per the same "neither registry knows about the other" composition
  principle EB4's own test already demonstrated for banks.
- `MetaModuleHeadConfigBus` gained a second `ExpanderSnapshot` (`output`,
  10 fields) alongside its existing `config` one, both under the *same*
  ownership claim -- one real entity (the `HeadRemote` bound to head index h)
  publishing two different payloads, not two claims that could disagree
  about who owns head h. `publishHeadOutput`/`readHeadOutput` mirror
  `publishHead`/`readHead`'s claimed-and-published discipline exactly
  (unclaimed/duplicated/never-published all read as `HeadOut()` defaults).

This also resolves the tick/ack question raised in design discussion:
no new request/response primitive is needed. A head's output heartbeat
advancing within a caller's own staleness window (the same
`coreCount == 1 && staleTime < 0.25f` two-axis pattern `TimingMonitor.cpp`
already uses) *is* the ack, and however many heads have a fresh heartbeat in
a given window is the detected-head-count for free -- no separate mechanism
for either.

One thing this step deliberately did *not* need: HeadConfig no longer has to
round-trip through the bus for a HeadRemote's own computation the way this
was first framed. A HeadRemote reads its own panel knobs locally (same as
VCV's `Head.cpp`, no bus involved) and only *publishes* `HeadConfig`
outward for whatever wants to display/monitor it -- `publishHead`/`readHead`
already existed for exactly that and needed no change.

**MIDI forwarding (addendum, same day):** how MIDI-driven head control
reaches a relocated head is resolved, not just flagged, once its real VCV
precedent was checked rather than assumed. Per-head MIDI CC on VCV does not
arrive at each `Head` module through its own MIDI port -- `Midi.cpp` is the
sole ingestion point and forwards per-head CC plus the shared transport
counters (`midiClockSeq`/`midiStartSeq`/`midiStopSeq`/`midiContinueSeq`)
through the expander chain, addressed by `headId`
(`AnchorToHeadsMsg::headCcSeq`/`headCcValue`, `Chain.hpp`); `Head.cpp` tracks
its own `lastHeadCcSeq[c]` and applies whatever's new locally. The
MetaModule port is the same mechanism over the bus instead of an expander
pointer: `MetaModuleHeadMidiRegistry` (`dsp/MetaModuleRemoteBus.hpp`), one
more no-CAS-of-its-own broadcast in the same shape as
`MetaModuleStageTableRegistry`, carrying only the MIDI subset of
`AnchorToHeadsMsg` (not the table, not HEAD ALL's `headAllCcSeq`/
`headAllCcValue` -- no `HeadAllRemote` exists yet to produce or consume
those). `MidiCore::injectMidi()` already builds the exact `AnchorToHeadsMsg`
payload this reuses, so the test drives a real `SpaceTimeEngine` through
`engine.handleMidi()` -- the same call `Core.cpp` makes -- rather than a
hand-built stand-in for what `MidiCore`'s state looks like.

No new primitive category needed here either: this is one-directional
broadcast (Core publishes, any number of `HeadRemote`s read), not the
bidirectional command queue this plan's own "Explicitly out of scope"
section defers. A `HeadRemote`'s own `applyHeadCc`-equivalent (mirroring
`Head.cpp`'s, which is Rack-adapter-layer code, not `dsp/`) is not written
yet -- there's no `HeadRemote` module for it to live in -- flagged as the
next real gap once a `HeadRemote` panel actually gets built.

**Exit (host-verified):** `test/MetaModuleRemoteBusTest.cpp` -- stage-table
publish/read round-trips all 64 stages losslessly; read is gated on
caller-supplied core validity independent of publish state, and separately
on heartbeat; head output publish/read round-trips without disturbing the
config half of the same slot; unclaimed/duplicated head output behaves
exactly like the existing config-side tests. The load-bearing test: a
`HeadDSP` driven entirely by registry-round-tripped table and config
(published by one side, read by the other, two independent `HeadDSP`
instances) produces identical output, tick for tick across a real
start-and-advance sequence, to the same `HeadDSP` driven directly by the
original objects -- proving the bus round trip costs nothing in behavior.
The MIDI-forwarding addendum adds: `MetaModuleHeadMidiSnapshot` round-trips
all 8 heads x 14 CCs plus the 4 shared transport counters losslessly; same
core-validity/heartbeat gating as the table; a real `SpaceTimeEngine` driven
through `engine.handleMidi()` has its actual `MidiCore` state (via
`injectMidi()`) survive the bus round trip exactly, not a hand-built
approximation of what that state looks like. A later addition (also
host-verified): a standalone, Rack-free `dsp/HeadRemoteController.hpp`
(single-head MIDI-to-signals controller, deliberately duplicating rather
than refactoring `SpaceTimeEngine`'s internal per-head logic -- see its own
header comment) produces byte-identical `HeadConfig`/signal/output behavior
to a real `SpaceTimeEngine` driven through `engine.handleMidi()` for the
same CC stream (`test/HeadRemoteControllerTest.cpp`), including a genuine
correctness bug this exposed and fixed: the idle-refresh gate inherited from
`SpaceTimeEngine::processHeads()` never accounted for an active *local* jack
signal, because Core/Singularity have no real per-head jacks for MIDI to
compete with -- HeadRemote, by design, does. 165 cases, 6928 assertions, 0
failures; clean local VCV plugin build (untouched by this change).

**Exit (still open):** Nothing in this pass has been compiled -- there is no
ARM toolchain in this environment, so `Core.cpp`'s new publish calls,
`metamodule/src/RemoteBus.{hpp,cpp}`, and `metamodule/src/HeadRemote.cpp`
are unverified beyond careful review against the already-working `Core.cpp`/
`TimingMonitor.cpp` patterns they mirror. Specifically still open:
`Core.cpp` now calls `stageTableRegistry.publishTable`/`publishContext` and
`headMidiRegistry.publishMidi` from `publishTiming()`, gated by the same
sole-Core check that already guarded the timing bus publish -- but this has
not been built or run. `HeadRemote.cpp` exists (params: Instrument ID/Head
index selector knobs, START/STOP/ADVANCE/RESET buttons; inputs:
START/STOP/ADVANCE/CLK/RESET, deliberately 5 rather than VCV Head.cpp's 8 --
ADDRESS/TIMECV/STROBE input jacks are dropped since MIDI CC already covers
those `HeadConfig` fields; outputs: full parity, all 7) but is deliberately
missing local ADDRESS/DIRECTION/CLK_DIV/LOOP knobs that VCV's Head.cpp has --
see the module's own header comment for why (those fields' sole writer is
MIDI CC into `HeadRemoteController`'s internal `config_`, and a physical
knob would be a second, uncoordinated writer to the same field; Core.cpp
itself has zero per-head knobs today for the identical reason). A proper
two-way sync (MIDI pushes into a Param, panel motion pushes into `config_`)
is a legitimate follow-up, not decided here. `artwork/HeadRemote.svg` exists
with meaningfully larger text than `Core.svg`/`TimingMonitor.svg` throughout
(2.0mm floor vs their 1.7mm, most labels 2.3-3.0mm vs 1.7-2.7mm) but still
does not reach the ~5.35mm legibility floor computed earlier against the
240px hardware display convention -- the four-status-light row is the
tightest remaining compromise (see the SVG's own comment). No
`assets/HeadRemote.png` browser thumbnail exists; the other four modules'
thumbnails aren't produced by anything in this repo either (no `.svg` under
`assets/`, only `.png`), so whatever renders those needs to run for
HeadRemote too. `metamodule/plugin.json`, `metamodule/src/plugin.cpp`, and
`metamodule/CMakeLists.txt` are updated to register `HeadRemote` and build
`RemoteBus.cpp`/`HeadRemote.cpp`. Memory footprint:
`MetaModuleStageTableRegistry` alone is `kMaxStages * 3 + 1` = 193 words x 4
Instrument IDs ~= 3 KB of static storage, on top of everything EB1-EB7
already committed -- worth checking against MetaModule's per-plugin budget
now that a real `HeadRemote` panel exists, per the standing note under Known
risks. Per-`HeadRemote`-module-instance framework overhead (panel/param/jack
bookkeeping, as distinct from the `HeadDSP` tick cost itself) is unmeasured
and can only be measured on hardware. Core's own `HEAD1_CV_OUTPUT`/
`HEAD1_ALL_OUTPUT` jacks and internal head-1 computation are deliberately
untouched -- completing "0 heads in core" is a separate, patch-compatibility-
affecting decision, not made here.

## Explicitly out of scope for this plan

Bidirectional command queues - a Remote pushing an edit (slider moved, flag
toggled) back to Core, with the generation counters needed to detect a stale
command in flight - are not covered here. EB1-EB7 establish the addressing,
duplicate/missing semantics, and transport primitive that a command queue
would sit on top of; the queue design itself (ordering guarantees under
concurrent edits from multiple Remotes, coalescing, and Core's arbitration
policy when two Remotes edit the same stage) is a separate work package once
STAGE4 Remote reaches read-only parity.

Narrowed by EB10 (2026-08-09): the voltage/time case turned out not to need
this after all -- bank ownership is already exclusive (one CAS'd owner per
bank, EB3), so the only real contention is a bound `Stage4` vs. MIDI on the
same stage, and `Program.cpp`'s `syncStageBanks()` resolves that with a
plain feed-forward shadow-compare, no generation counters, no queue. What
remains genuinely out of scope: `Stage4` cannot write a stage's program-word
flags at all (matching VCV's real `Stage4.cpp`, which can't either -- those
are edit-op-only, driven by Program's gestures), so there is still no
"multiple Remotes editing the same field with ordering guarantees" case
anywhere in this plan, because none exists yet that isn't already resolved
by exclusive-ownership-plus-shadow-compare. If a future module needs to
push edits where per-bank ownership isn't already exclusive, this section's
original concern still applies in full.

## Decision gates

1. ~~EB1 and EB2 land together before any Remote-specific work begins~~ - done,
   2026-08-02.
2. EB3's duplicate/missing behavior is reviewed against the VCV chain-broken
   `!` convention (EB6) before the first STAGE4 Remote panel mockup is frozen,
   so the warning language is decided once rather than retrofitted.
3. EB4's auto-bind default is a UX decision, not an engineering one - confirm
   it against real multi-instrument patches (2+ Cores) before freezing, since
   that is the case where auto-bind must correctly decline to guess.

## Known risks

- EB3's duplicate/missing semantics are new hardware-verified behavior, not a
  port of anything VCV already tested - budget a full pass of the MM0-style
  hardware test script (LINK/DUP/WAIT, save/reload, deletion/reinsertion) for
  the bank and head sub-slots specifically, not just Core/Remote.
- Auto-bind (EB4) trades explicit configuration for inferred behavior. If a
  patch is loaded with a Core temporarily absent (e.g., mid-reload), a Remote
  set to auto could theoretically bind to nothing and then to the wrong thing
  once the Core reappears, if two Cores exist and only one is present at the
  moment of binding. EB4's exit criteria must include this reload-ordering
  case, not just the steady-state single-Core case.
- The role enum (EB2) and per-slot addressing (EB3) both grow the static
  global footprint the whole bus depends on. Re-confirm total memory usage
  against MetaModule's per-plugin budget once all 16 stage-bank and 8 head
  sub-slots exist for all 4 Instrument IDs, alongside Core/Remote/Timing
  Monitor state.
