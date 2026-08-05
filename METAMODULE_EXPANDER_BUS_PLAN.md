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

MM0 (shared-bus proof) is accepted on firmware 2.2.0. EB1-EB5 below are done
at the host-test level; EB6-EB7 are not started.

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

**Status:** Not started.

Core surfaces missing and duplicate slots through the same `!` chain-broken
convention Program already uses on the VCV panel, rather than inventing a
second warning language for the MetaModule side. Extend Core's status display
and action menu to report which bank/head index is missing or duplicated.

**Exit:** MM2 panel mockup gate revisited to confirm the warning glyph and any
per-slot detail remain legible at 240 px and 180 px display scale alongside
existing status fields.

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

## Explicitly out of scope for this plan

Bidirectional command queues - a Remote pushing an edit (slider moved, flag
toggled) back to Core, with the generation counters needed to detect a stale
command in flight - are not covered here. EB1-EB7 establish the addressing,
duplicate/missing semantics, and transport primitive that a command queue
would sit on top of; the queue design itself (ordering guarantees under
concurrent edits from multiple Remotes, coalescing, and Core's arbitration
policy when two Remotes edit the same stage) is a separate work package once
STAGE4 Remote reaches read-only parity.

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
