#pragma once

// Per-bank and per-head indexed sub-addressing for MetaModule Core/Remote
// communication (METAMODULE_EXPANDER_BUS_PLAN.md, EB3). Builds on
// ExpanderSnapshot (ExpanderLink.hpp) for transport and on the same CAS
// ownership pattern MM0 established for Core/Remote (MetaModuleBusProbe.hpp),
// generalized from one exclusive slot per role to kBankCount/kHeadCount
// independently addressable slots per Instrument ID.
//
// Scope: this is the addressing, ownership, and duplicate/missing-slot
// transport only. A StageRemote or HeadRemote always republishes its
// complete current state (a whole BlockSegment or HeadConfig), exactly as a
// VCV STAGE4 block already owns and republishes its complete segment to the
// expander chain. Incremental single-field edit messages with staleness
// generation counters remain explicitly out of scope for this plan.
//
// Duplicate handling follows the discipline MM0 already established in
// Probe.cpp: a module that loses the ownership CAS must not call publish()
// at all (check registerBank/registerHead's return value first). Readers
// re-verify single ownership on every read anyway, as defense in depth --
// the same pattern Core.cpp already applies to the timing bus
// (`bus.coreCount.load(...) != 1`).
//
// Note on EB2: BusRole::StageRemote/HeadRemote and MetaModuleProbeBus's aux
// slots remain valid and tested, but a real StageRemote/HeadRemote module
// should bind through this registry instead -- EB2's single-owner-per-role
// shape doesn't fit 16 banks or 8 heads. BusRole::ProgramRemote remains the
// live consumer of EB2's aux mechanism, since PROGRAM has no per-index
// addressing to do.

#include "ExpanderLink.hpp"
#include "StageTable.hpp"
#include "HeadDSP.hpp"

#include <atomic>
#include <cstdint>

namespace spacetime {

// ---- Full stage table (Core -> HeadRemote, EB8) ----------------------------
// Head relocation (METAMODULE_EXPANDER_BUS_PLAN.md, EB8): a HeadRemote owns
// and runs its own HeadDSP tick rather than Core computing all eight heads
// internally, so it needs Core's complete 64-stage table to address into.
// This is the mirror image of EB3's bank direction (there, a StageRemote
// publishes and Core aggregates via readAllBanks/concatenate); here, Core is
// the single publisher and any number of HeadRemotes read the same snapshot
// (TimingBus's monitorCount already establishes that an unbounded number of
// readers off one Core-published snapshot is fine -- no CAS needed on the
// read side, only on the one real writer).
//
// Deliberately has no ownership CAS of its own: "is there a valid Core to
// trust this table from" is already answered by
// MetaModuleTimingBusRegistry's coreOwner/coreCount (the same fact
// coreConflict() and findSoleCore() already key off). Re-deriving that here
// would track the same real-world fact in two places. Per the EB4 test's own
// precedent, neither registry knows about the other -- the caller composes
// them, passing the already-known core-validity bool in.
class MetaModuleStageTableRegistry {
public:
	static const unsigned kBusCount = 4;  // Instrument ID A-D
	static const unsigned kFieldCount = kMaxStages * 3 + 1;  // voltage, time, program.bits, count

	// Caller is responsible for only calling this from the module that holds
	// Core ownership (mirrors publishBank/publishHead's "caller must hold
	// ownership" discipline, just against a different registry's CAS).
	void publishTable(unsigned busIndex, const StageTable& table) {
		uint32_t fields[kFieldCount];
		pack(table, fields);
		bus(busIndex).publish(fields);
	}

	// Unlike readBank/readHead, this cannot check its own owner/count --
	// there is none here by design (see class comment). `coreValid` is the
	// caller's own freshly-checked MetaModuleTimingBusRegistry result
	// (coreCount(busIndex) == 1); this only adds the heartbeat check, the
	// same "claimed but never published" guard EB5 established elsewhere.
	bool readTable(unsigned busIndex, bool coreValid, StageTable& out) {
		out = StageTable();
		if (!coreValid)
			return false;
		if (bus(busIndex).heartbeat() == 0)
			return false;
		uint32_t fields[kFieldCount];
		if (!bus(busIndex).read(fields))
			return false;
		unpack(fields, out);
		return true;
	}

	uint32_t tableHeartbeat(unsigned busIndex) {
		return bus(busIndex).heartbeat();
	}

	// Companion channel, same class (same registry, still no CAS of its
	// own), separate ExpanderSnapshot: ExtInputs/Globals/ScaleKey, the rest
	// of what AnchorToHeadsMsg bundles alongside its table (Chain.hpp) --
	// the small pieces of Program-level context HeadDSP::tick() also needs
	// (quantize scale/key, slew fractions, pulse-retrig behavior, external
	// A-D CV) that aren't the table itself. Kept as a second field rather
	// than folded into the table's own field array so a HeadRemote that only
	// cares about one of the two (unlikely, but cheap to allow) isn't forced
	// to pay for both on every read, and so neither payload's field count
	// has to change if the other one ever does.
	static const unsigned kContextFieldCount = 8 + 5 + 2 + 1;  // ExtInputs, Globals, ScaleKey, selection

	void publishContext(unsigned busIndex, const ExtInputs& ext, const Globals& globals,
			const ScaleKey& scaleKey, uint8_t selectedStage) {
		uint32_t fields[kContextFieldCount];
		packContext(ext, globals, scaleKey, selectedStage, fields);
		contextBus(busIndex).publish(fields);
	}

	bool readContext(unsigned busIndex, bool coreValid, ExtInputs& ext, Globals& globals,
			ScaleKey& scaleKey, uint8_t* selectedStage = nullptr) {
		ext = ExtInputs();
		globals = Globals();
		scaleKey = ScaleKey();
		if (!coreValid)
			return false;
		if (contextBus(busIndex).heartbeat() == 0)
			return false;
		uint32_t fields[kContextFieldCount];
		if (!contextBus(busIndex).read(fields))
			return false;
		unpackContext(fields, ext, globals, scaleKey, selectedStage);
		return true;
	}

	uint32_t contextHeartbeat(unsigned busIndex) {
		return contextBus(busIndex).heartbeat();
	}

private:
	ExpanderSnapshot<kFieldCount>& bus(unsigned index) {
		return buses_[index < kBusCount ? index : 0];
	}

	ExpanderSnapshot<kContextFieldCount>& contextBus(unsigned index) {
		return contextBuses_[index < kBusCount ? index : 0];
	}

	static void packContext(const ExtInputs& ext, const Globals& globals, const ScaleKey& scaleKey,
			uint8_t selectedStage,
			uint32_t (&fields)[kContextFieldCount]) {
		for (int i = 0; i < 4; i++) {
			fields[i] = floatToBits(ext.v[i]);
			fields[4 + i] = ext.connected[i] ? 1u : 0u;
		}
		fields[8] = floatToBits(globals.slewFrac1);
		fields[9] = floatToBits(globals.slewFrac2);
		fields[10] = globals.slopeLaw;
		fields[11] = globals.addressScale;
		fields[12] = globals.pulseRetrig ? 1u : 0u;
		fields[13] = scaleKey.key;
		fields[14] = scaleKey.scale;
		fields[15] = selectedStage;
	}

	static void unpackContext(const uint32_t (&fields)[kContextFieldCount], ExtInputs& ext,
			Globals& globals, ScaleKey& scaleKey, uint8_t* selectedStage) {
		for (int i = 0; i < 4; i++) {
			ext.v[i] = bitsToFloat(fields[i]);
			ext.connected[i] = fields[4 + i] != 0;
		}
		globals.slewFrac1 = bitsToFloat(fields[8]);
		globals.slewFrac2 = bitsToFloat(fields[9]);
		globals.slopeLaw = (uint8_t)fields[10];
		globals.addressScale = (uint8_t)fields[11];
		globals.pulseRetrig = fields[12] != 0;
		scaleKey.key = (uint8_t)fields[13];
		scaleKey.scale = (uint8_t)fields[14];
		if (selectedStage)
			*selectedStage = (uint8_t)fields[15];
	}

	static void pack(const StageTable& table, uint32_t (&fields)[kFieldCount]) {
		for (int i = 0; i < kMaxStages; i++) {
			fields[i * 3 + 0] = floatToBits(table.voltage[i]);
			fields[i * 3 + 1] = floatToBits(table.time[i]);
			fields[i * 3 + 2] = table.program[i].bits;
		}
		fields[kMaxStages * 3] = table.count;
	}

	static void unpack(const uint32_t (&fields)[kFieldCount], StageTable& table) {
		for (int i = 0; i < kMaxStages; i++) {
			table.voltage[i] = bitsToFloat(fields[i * 3 + 0]);
			table.time[i] = bitsToFloat(fields[i * 3 + 1]);
			table.program[i] = ProgramWord(fields[i * 3 + 2]);
		}
		table.count = (uint8_t)fields[kMaxStages * 3];
	}

	ExpanderSnapshot<kFieldCount> buses_[kBusCount];
	ExpanderSnapshot<kContextFieldCount> contextBuses_[kBusCount];
};

// ---- Per-head MIDI CC + shared transport (Core -> HeadRemote, EB8) --------
// VCV's Midi.cpp is the sole MIDI ingestion point and forwards per-head CC
// plus the shared transport counters through the expander chain, addressed
// by headId -- MidiCore::injectMidi() already builds exactly this payload
// (a subset of Chain.hpp's AnchorToHeadsMsg) for that purpose. This is the
// same fields, over the MetaModule bus instead of an expander pointer, so a
// relocated HeadRemote's own applyHeadCc-equivalent (mirroring Head.cpp's,
// not yet written -- that logic lives at the Rack/MetaModule-adapter layer
// in both places, not in dsp/) can apply whatever's new to its own local
// HeadConfig exactly as Head.cpp already does with bm->headCcValue[headId][c].
//
// Deliberately does NOT carry AnchorToHeadsMsg's table/ext/globals/scaleKey/
// selectedStage/display-arbitration fields: the table already has its own
// channel (MetaModuleStageTableRegistry, right above) and duplicating it
// here would mean two sources of truth for the same 64 stages. HEAD ALL's
// headAllCcSeq/headAllCcValue are left out too -- no HeadAllRemote exists to
// either side of this channel yet; adding those fields costs nothing to do
// later, when they'd have an actual producer and consumer.
//
// No ownership CAS of its own, same reasoning as MetaModuleStageTableRegistry:
// this is Core's own state, and "is there a valid sole Core" is already
// MetaModuleTimingBusRegistry's fact to answer.
struct MetaModuleHeadMidiSnapshot {
	uint32_t midiClockSeq;
	uint32_t midiStartSeq;
	uint32_t midiStopSeq;
	uint32_t midiContinueSeq;
	uint32_t headCcSeq[kMaxHeads][kMidiHeadControls];
	float headCcValue[kMaxHeads][kMidiHeadControls];

	MetaModuleHeadMidiSnapshot()
		: midiClockSeq(0), midiStartSeq(0), midiStopSeq(0), midiContinueSeq(0) {
		for (int h = 0; h < kMaxHeads; h++) {
			for (int c = 0; c < kMidiHeadControls; c++) {
				headCcSeq[h][c] = 0;
				headCcValue[h][c] = 0.f;
			}
		}
	}
};

class MetaModuleHeadMidiRegistry {
public:
	static const unsigned kBusCount = 4;  // Instrument ID A-D
	static const unsigned kFieldCount = 4 + kMaxHeads * kMidiHeadControls * 2;

	void publishMidi(unsigned busIndex, const MetaModuleHeadMidiSnapshot& snapshot) {
		uint32_t fields[kFieldCount];
		pack(snapshot, fields);
		bus(busIndex).publish(fields);
	}

	// Same composition as readTable: `coreValid` is the caller's own
	// MetaModuleTimingBusRegistry check, not re-derived here.
	bool readMidi(unsigned busIndex, bool coreValid, MetaModuleHeadMidiSnapshot& out) {
		out = MetaModuleHeadMidiSnapshot();
		if (!coreValid)
			return false;
		if (bus(busIndex).heartbeat() == 0)
			return false;
		uint32_t fields[kFieldCount];
		if (!bus(busIndex).read(fields))
			return false;
		unpack(fields, out);
		return true;
	}

	uint32_t midiHeartbeat(unsigned busIndex) {
		return bus(busIndex).heartbeat();
	}

private:
	ExpanderSnapshot<kFieldCount>& bus(unsigned index) {
		return buses_[index < kBusCount ? index : 0];
	}

	static void pack(const MetaModuleHeadMidiSnapshot& snapshot, uint32_t (&fields)[kFieldCount]) {
		fields[0] = snapshot.midiClockSeq;
		fields[1] = snapshot.midiStartSeq;
		fields[2] = snapshot.midiStopSeq;
		fields[3] = snapshot.midiContinueSeq;
		unsigned i = 4;
		for (int h = 0; h < kMaxHeads; h++) {
			for (int c = 0; c < kMidiHeadControls; c++) {
				fields[i++] = snapshot.headCcSeq[h][c];
				fields[i++] = floatToBits(snapshot.headCcValue[h][c]);
			}
		}
	}

	static void unpack(const uint32_t (&fields)[kFieldCount], MetaModuleHeadMidiSnapshot& snapshot) {
		snapshot.midiClockSeq = fields[0];
		snapshot.midiStartSeq = fields[1];
		snapshot.midiStopSeq = fields[2];
		snapshot.midiContinueSeq = fields[3];
		unsigned i = 4;
		for (int h = 0; h < kMaxHeads; h++) {
			for (int c = 0; c < kMidiHeadControls; c++) {
				snapshot.headCcSeq[h][c] = fields[i++];
				snapshot.headCcValue[h][c] = bitsToFloat(fields[i++]);
			}
		}
	}

	ExpanderSnapshot<kFieldCount> buses_[kBusCount];
};

// ---- MIDI activity/channel status (Core -> MidiMonitor) --------------------
// VCV's Program and Midi are separate modules; Midi.cpp owns the real MIDI
// device queues, the DROID feedback protocol, and per-head MIDI-out lane
// config, and exposes its own panel (IN/CLK/OUT activity lights, PROGRAM/
// slider channel readout, last-event line). MetaModule's Core fuses that
// ingestion in permanently (no expander support to split it back out, see
// this plan's EB8 section) -- there is no possible separate MetaModule
// module that actually OWNS MIDI I/O the way Midi.cpp does; only one thing
// can bind the physical MIDI ports for a given Instrument ID.
//
// What IS portable is Midi.cpp's panel: a small, read-only monitor that
// mirrors the same activity/channel information, sourced from whatever Core
// already tracks. One-way, same no-CAS-of-its-own shape as
// MetaModuleStageTableRegistry/MetaModuleHeadMidiRegistry -- "is there a
// valid sole Core" is still MetaModuleTimingBusRegistry's fact to answer,
// not re-derived here.
//
// Deliberately publishes event *counters*, not the already-decayed light
// brightness Core.cpp computes for its own panel: a float light value is a
// presentation detail of one particular consumer (Core's own LEDs, decayed
// against Core's own sample clock), whereas a monotonic sequence number is
// the same kind of durable, replay-safe fact every other EB-series bus in
// this file already carries (midiClockSeq, tableHeartbeat, ...). A reader
// (MidiMonitor.cpp) derives its own decayed flash locally from the seq
// delta, the same pattern TimingMonitor.cpp's clockPulse/stepPulse already
// use against sourceClockEvents/stageEntries deltas.
struct MetaModuleMidiStatusSnapshot {
	uint32_t inSeq = 0;    // any inbound MIDI message
	uint32_t clkSeq = 0;   // inbound 0xF8 clock byte specifically
	uint32_t outSeq = 0;   // any outbound MIDI message (notes, CC, feedback)
	uint8_t controlChannel = 15;
	uint8_t sliderChannel = 14;
	int32_t lastStatus = -1;
	int32_t lastChannel = -1;
	int32_t lastNumber = -1;
	int32_t lastValue = -1;
	uint8_t lastRoute = 0;  // MidiRoute (MidiCore.hpp)
};

class MetaModuleMidiStatusRegistry {
public:
	static const unsigned kBusCount = 4;  // Instrument ID A-D
	static const unsigned kFieldCount = 10;

	void publish(unsigned busIndex, const MetaModuleMidiStatusSnapshot& snapshot) {
		uint32_t fields[kFieldCount];
		pack(snapshot, fields);
		bus(busIndex).publish(fields);
	}

	// Same composition as readTable/readMidi: `coreValid` is the caller's
	// own MetaModuleTimingBusRegistry check, not re-derived here.
	bool read(unsigned busIndex, bool coreValid, MetaModuleMidiStatusSnapshot& out) {
		out = MetaModuleMidiStatusSnapshot();
		if (!coreValid)
			return false;
		if (bus(busIndex).heartbeat() == 0)
			return false;
		uint32_t fields[kFieldCount];
		if (!bus(busIndex).read(fields))
			return false;
		unpack(fields, out);
		return true;
	}

	uint32_t heartbeat(unsigned busIndex) {
		return bus(busIndex).heartbeat();
	}

private:
	ExpanderSnapshot<kFieldCount>& bus(unsigned index) {
		return buses_[index < kBusCount ? index : 0];
	}

	static void pack(const MetaModuleMidiStatusSnapshot& snapshot, uint32_t (&fields)[kFieldCount]) {
		fields[0] = snapshot.inSeq;
		fields[1] = snapshot.clkSeq;
		fields[2] = snapshot.outSeq;
		fields[3] = snapshot.controlChannel;
		fields[4] = snapshot.sliderChannel;
		fields[5] = (uint32_t)snapshot.lastStatus;
		fields[6] = (uint32_t)snapshot.lastChannel;
		fields[7] = (uint32_t)snapshot.lastNumber;
		fields[8] = (uint32_t)snapshot.lastValue;
		fields[9] = snapshot.lastRoute;
	}

	static void unpack(const uint32_t (&fields)[kFieldCount], MetaModuleMidiStatusSnapshot& snapshot) {
		snapshot.inSeq = fields[0];
		snapshot.clkSeq = fields[1];
		snapshot.outSeq = fields[2];
		snapshot.controlChannel = (uint8_t)fields[3];
		snapshot.sliderChannel = (uint8_t)fields[4];
		snapshot.lastStatus = (int32_t)fields[5];
		snapshot.lastChannel = (int32_t)fields[6];
		snapshot.lastNumber = (int32_t)fields[7];
		snapshot.lastValue = (int32_t)fields[8];
		snapshot.lastRoute = (uint8_t)fields[9];
	}

	ExpanderSnapshot<kFieldCount> buses_[kBusCount];
};

// ---- Stage banks (StageRemote, kBankCount slots per instrument) -----------

struct MetaModuleStageBankBus {
	// The final two fields carry an event-style focus request. The sequence
	// distinguishes an intentional viewer action from reflected Program state.
	static const unsigned kFieldCount = kStagesPerBlock * 3 + 2;

	std::atomic<uint32_t> owner{0};
	std::atomic<uint32_t> count{0};
	ExpanderSnapshot<kFieldCount> segment;
};

class MetaModuleStageBankRegistry {
public:
	static const unsigned kBusCount = 4;            // Instrument ID A-D
	static const unsigned kBankCount = kMaxBlocks;   // 16

	uint32_t makeToken() {
		uint32_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
		return token == 0 ? nextToken_.fetch_add(1, std::memory_order_relaxed) : token;
	}

	MetaModuleStageBankBus& bank(unsigned busIndex, unsigned bankIndex) {
		return buses_[clampBus(busIndex)][clampBank(bankIndex)];
	}

	bool registerBank(unsigned busIndex, unsigned bankIndex, uint32_t token) {
		MetaModuleStageBankBus& target = bank(busIndex, bankIndex);
		target.count.fetch_add(1, std::memory_order_acq_rel);
		return claim(target.owner, token);
	}

	bool tryClaimBank(unsigned busIndex, unsigned bankIndex, uint32_t token) {
		return claim(bank(busIndex, bankIndex).owner, token);
	}

	void unregisterBank(unsigned busIndex, unsigned bankIndex, uint32_t token) {
		MetaModuleStageBankBus& target = bank(busIndex, bankIndex);
		release(target.owner, token);
		target.count.fetch_sub(1, std::memory_order_acq_rel);
	}

	uint32_t bankLinkCount(unsigned busIndex, unsigned bankIndex) {
		return bank(busIndex, bankIndex).count.load(std::memory_order_acquire);
	}

	// EB5: raw publish counter, for a caller building its own freshness
	// judgement -- mirrors TimingMonitor.cpp's existing pattern exactly
	// (`bus.telemetry.heartbeat()` compared against a locally-tracked last
	// value over real elapsed time, e.g. `coreCount == 1 && staleTime <
	// 0.25f`). Deliberately not folded into readBank's own return value:
	// ownership validity (owner/count) and publish freshness (heartbeat
	// advancing over time) are different questions, and different callers
	// may want to answer the freshness question on different timescales
	// (or not at all, for something read every audio tick).
	uint32_t bankHeartbeat(unsigned busIndex, unsigned bankIndex) {
		return bank(busIndex, bankIndex).segment.heartbeat();
	}

	// Caller must hold ownership (registerBank/tryClaimBank returned true)
	// before calling this -- exactly the discipline ProbeModule already
	// follows. This call does not itself check.
	void publishBank(unsigned busIndex, unsigned bankIndex, const BlockSegment& segment,
			uint8_t focusedStage = 0, uint32_t focusSequence = 0) {
		uint32_t fields[MetaModuleStageBankBus::kFieldCount];
		packBlockSegment(segment, focusedStage, focusSequence, fields);
		bank(busIndex, bankIndex).segment.publish(fields);
	}

	// Fills `out` with the bank's published segment only when exactly one
	// live owner currently holds this slot *and* that owner has actually
	// published at least once. Unclaimed, duplicated, or claimed-but-never-
	// published slots all leave `out` at BlockSegment()'s defaults and
	// return false (EB5). The heartbeat==0 check matters on its own:
	// without it, a bank that is validly registered but hasn't published
	// yet would fall through to ExpanderSnapshot's own pre-publish default
	// (all-zero bits) instead of BlockSegment()'s real default (0 V, 0.5
	// time, cleared program word) -- those two defaults disagree, and only
	// the latter is correct here. The same gap, and the same fix, applies
	// to a bus struct left in a structurally-claimed-looking state by an
	// unclean plugin unload/reload (EB7): looking claimed is not the same
	// as having ever actually received real data.
	bool readBank(unsigned busIndex, unsigned bankIndex, BlockSegment& out,
			uint8_t* focusedStage = nullptr, uint32_t* focusSequence = nullptr) {
		out = BlockSegment();
		MetaModuleStageBankBus& target = bank(busIndex, bankIndex);
		if (target.owner.load(std::memory_order_acquire) == 0)
			return false;
		if (target.count.load(std::memory_order_acquire) != 1)
			return false;
		if (target.segment.heartbeat() == 0)
			return false;
		uint32_t fields[MetaModuleStageBankBus::kFieldCount];
		if (!target.segment.read(fields))
			return false;
		unpackBlockSegment(fields, out, focusedStage, focusSequence);
		return true;
	}

	// Aggregates all kBankCount slots for one instrument into a full
	// StageTable -- the same shape Chain.hpp's VCV-side concatenate()
	// already produces from a physically walked expander chain. Missing or
	// duplicated banks contribute BlockSegment()'s defaults rather than
	// stale data. Always reports kBankCount * kStagesPerBlock stages; a
	// shorter "connected chain" indication is a higher-level Core concern
	// (EB6), not this registry's.
	int readAllBanks(unsigned busIndex, StageTable& out) {
		BlockSegment blocks[kBankCount];
		for (unsigned b = 0; b < kBankCount; b++)
			readBank(busIndex, b, blocks[b]);
		return concatenate(blocks, (int)kBankCount, out);
	}

private:
	static unsigned clampBus(unsigned index) { return index < kBusCount ? index : 0; }
	static unsigned clampBank(unsigned index) { return index < kBankCount ? index : 0; }

	static void packBlockSegment(const BlockSegment& segment, uint8_t focusedStage,
			uint32_t focusSequence,
			uint32_t (&fields)[MetaModuleStageBankBus::kFieldCount]) {
		for (int i = 0; i < kStagesPerBlock; i++) {
			fields[i * 3 + 0] = floatToBits(segment.voltage[i]);
			fields[i * 3 + 1] = floatToBits(segment.time[i]);
			fields[i * 3 + 2] = segment.program[i].bits;
		}
		fields[kStagesPerBlock * 3] = focusedStage;
		fields[kStagesPerBlock * 3 + 1] = focusSequence;
	}

	static void unpackBlockSegment(const uint32_t (&fields)[MetaModuleStageBankBus::kFieldCount],
			BlockSegment& segment, uint8_t* focusedStage, uint32_t* focusSequence) {
		for (int i = 0; i < kStagesPerBlock; i++) {
			segment.voltage[i] = bitsToFloat(fields[i * 3 + 0]);
			segment.time[i] = bitsToFloat(fields[i * 3 + 1]);
			segment.program[i] = ProgramWord(fields[i * 3 + 2]);
		}
		if (focusedStage)
			*focusedStage = (uint8_t)fields[kStagesPerBlock * 3];
		if (focusSequence)
			*focusSequence = fields[kStagesPerBlock * 3 + 1];
	}

	static bool claim(std::atomic<uint32_t>& owner, uint32_t token) {
		if (owner.load(std::memory_order_acquire) == token)
			return true;
		uint32_t empty = 0;
		return owner.compare_exchange_strong(
			empty, token, std::memory_order_acq_rel, std::memory_order_acquire);
	}

	static void release(std::atomic<uint32_t>& owner, uint32_t token) {
		uint32_t expected = token;
		owner.compare_exchange_strong(
			expected, 0, std::memory_order_acq_rel, std::memory_order_acquire);
	}

	std::atomic<uint32_t> nextToken_{1};
	MetaModuleStageBankBus buses_[kBusCount][kBankCount];
};

// ---- Heads (HeadRemote, kHeadCount slots per instrument) -------------------

struct MetaModuleHeadConfigBus {
	static const unsigned kFieldCount = 8;        // HeadConfig's eight fields
	static const unsigned kOutputFieldCount = 10; // HeadOut's ten fields

	std::atomic<uint32_t> owner{0};
	std::atomic<uint32_t> count{0};
	ExpanderSnapshot<kFieldCount> config;
	// EB8: the same claimed slot also carries the HeadRemote's computed
	// output back out, once it's the one actually running HeadDSP (head
	// relocation -- see MetaModuleStageTableRegistry's header comment).
	// One ownership claim per real entity (the HeadRemote instance bound to
	// this head index), not two -- config and output are different payloads
	// riding the same claim, not two things that could disagree about who
	// owns head h.
	ExpanderSnapshot<kOutputFieldCount> output;
};

class MetaModuleHeadRegistry {
public:
	static const unsigned kBusCount = 4;           // Instrument ID A-D
	static const unsigned kHeadCount = kMaxHeads;  // 8

	uint32_t makeToken() {
		uint32_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
		return token == 0 ? nextToken_.fetch_add(1, std::memory_order_relaxed) : token;
	}

	MetaModuleHeadConfigBus& head(unsigned busIndex, unsigned headIndex) {
		return buses_[clampBus(busIndex)][clampHead(headIndex)];
	}

	bool registerHead(unsigned busIndex, unsigned headIndex, uint32_t token) {
		MetaModuleHeadConfigBus& target = head(busIndex, headIndex);
		target.count.fetch_add(1, std::memory_order_acq_rel);
		return claim(target.owner, token);
	}

	bool tryClaimHead(unsigned busIndex, unsigned headIndex, uint32_t token) {
		return claim(head(busIndex, headIndex).owner, token);
	}

	void unregisterHead(unsigned busIndex, unsigned headIndex, uint32_t token) {
		MetaModuleHeadConfigBus& target = head(busIndex, headIndex);
		release(target.owner, token);
		target.count.fetch_sub(1, std::memory_order_acq_rel);
	}

	uint32_t headLinkCount(unsigned busIndex, unsigned headIndex) {
		return head(busIndex, headIndex).count.load(std::memory_order_acquire);
	}

	// EB5: raw publish counter; see bankHeartbeat's comment for the
	// TimingMonitor-style freshness pattern this is meant to support.
	uint32_t headHeartbeat(unsigned busIndex, unsigned headIndex) {
		return head(busIndex, headIndex).config.heartbeat();
	}

	// Caller must hold ownership before calling this; see publishBank.
	void publishHead(unsigned busIndex, unsigned headIndex, const HeadConfig& config) {
		uint32_t fields[MetaModuleHeadConfigBus::kFieldCount];
		packHeadConfig(config, fields);
		head(busIndex, headIndex).config.publish(fields);
	}

	// Fills `out` only when exactly one live owner currently holds this
	// slot *and* that owner has actually published at least once.
	// Unclaimed, duplicated, or claimed-but-never-published slots all leave
	// `out` at HeadConfig()'s defaults and return false (EB5) -- see
	// readBank's comment for why the heartbeat==0 check is required and not
	// redundant with the owner/count check: HeadConfig()'s real default
	// (clkDivIndex 4 = x1, loopMode LOOP_FIRST_LAST) does not match
	// ExpanderSnapshot's own pre-publish all-zero-bits default.
	bool readHead(unsigned busIndex, unsigned headIndex, HeadConfig& out) {
		out = HeadConfig();
		MetaModuleHeadConfigBus& target = head(busIndex, headIndex);
		if (target.owner.load(std::memory_order_acquire) == 0)
			return false;
		if (target.count.load(std::memory_order_acquire) != 1)
			return false;
		if (target.config.heartbeat() == 0)
			return false;
		uint32_t fields[MetaModuleHeadConfigBus::kFieldCount];
		if (!target.config.read(fields))
			return false;
		unpackHeadConfig(fields, out);
		return true;
	}

	// EB8: HeadRemote -> Core direction, same claimed slot as publishHead/
	// readHead (see MetaModuleHeadConfigBus's comment). This is what gives
	// Core (or anyone else reading it) the head's actual computed output --
	// necessary now that Core no longer runs HeadDSP itself -- and its
	// heartbeat doubles as the per-head "ack" the tick/ack discussion
	// wanted: a head whose output heartbeat is advancing within a caller's
	// own staleness window is alive and current, no separate request/
	// response round trip required, same TimingMonitor-style two-axis
	// freshness check (ownership here, elapsed-time judgement is the
	// caller's).
	//
	// Caller must hold ownership before calling this; see publishHead.
	void publishHeadOutput(unsigned busIndex, unsigned headIndex, const HeadOut& out) {
		uint32_t fields[MetaModuleHeadConfigBus::kOutputFieldCount];
		packHeadOut(out, fields);
		head(busIndex, headIndex).output.publish(fields);
	}

	// Same claimed-and-published discipline as readHead: unclaimed,
	// duplicated, or claimed-but-never-published all leave `out` at
	// HeadOut()'s defaults (silent/stopped) and return false.
	bool readHeadOutput(unsigned busIndex, unsigned headIndex, HeadOut& out) {
		out = HeadOut();
		MetaModuleHeadConfigBus& target = head(busIndex, headIndex);
		if (target.owner.load(std::memory_order_acquire) == 0)
			return false;
		if (target.count.load(std::memory_order_acquire) != 1)
			return false;
		if (target.output.heartbeat() == 0)
			return false;
		uint32_t fields[MetaModuleHeadConfigBus::kOutputFieldCount];
		if (!target.output.read(fields))
			return false;
		unpackHeadOut(fields, out);
		return true;
	}

	uint32_t headOutputHeartbeat(unsigned busIndex, unsigned headIndex) {
		return head(busIndex, headIndex).output.heartbeat();
	}

private:
	static unsigned clampBus(unsigned index) { return index < kBusCount ? index : 0; }
	static unsigned clampHead(unsigned index) { return index < kHeadCount ? index : 0; }

	static void packHeadConfig(const HeadConfig& config,
			uint32_t (&fields)[MetaModuleHeadConfigBus::kFieldCount]) {
		fields[0] = config.continuous ? 1u : 0u;
		fields[1] = config.addrExt ? 1u : 0u;
		fields[2] = floatToBits(config.addressKnob);
		fields[3] = config.direction;
		fields[4] = config.clkExt ? 1u : 0u;
		fields[5] = config.clkDivIndex;
		fields[6] = floatToBits(config.timeCvAmount);
		fields[7] = config.loopMode;
	}

	static void unpackHeadConfig(const uint32_t (&fields)[MetaModuleHeadConfigBus::kFieldCount],
			HeadConfig& config) {
		config.continuous = fields[0] != 0;
		config.addrExt = fields[1] != 0;
		config.addressKnob = bitsToFloat(fields[2]);
		config.direction = (uint8_t)fields[3];
		config.clkExt = fields[4] != 0;
		config.clkDivIndex = (uint8_t)fields[5];
		config.timeCvAmount = bitsToFloat(fields[6]);
		config.loopMode = (uint8_t)fields[7];
	}

	static void packHeadOut(const HeadOut& out,
			uint32_t (&fields)[MetaModuleHeadConfigBus::kOutputFieldCount]) {
		fields[0] = floatToBits(out.cv);
		fields[1] = floatToBits(out.timeOut);
		fields[2] = floatToBits(out.ref);
		fields[3] = out.pulse1 ? 1u : 0u;
		fields[4] = out.pulse2 ? 1u : 0u;
		fields[5] = out.allPulse ? 1u : 0u;
		fields[6] = out.eoc ? 1u : 0u;
		fields[7] = out.currentStage;
		fields[8] = floatToBits(out.phase);
		fields[9] = out.runState;
	}

	static void unpackHeadOut(const uint32_t (&fields)[MetaModuleHeadConfigBus::kOutputFieldCount],
			HeadOut& out) {
		out.cv = bitsToFloat(fields[0]);
		out.timeOut = bitsToFloat(fields[1]);
		out.ref = bitsToFloat(fields[2]);
		out.pulse1 = fields[3] != 0;
		out.pulse2 = fields[4] != 0;
		out.allPulse = fields[5] != 0;
		out.eoc = fields[6] != 0;
		out.currentStage = (uint8_t)fields[7];
		out.phase = bitsToFloat(fields[8]);
		out.runState = (uint8_t)fields[9];
	}

	static bool claim(std::atomic<uint32_t>& owner, uint32_t token) {
		if (owner.load(std::memory_order_acquire) == token)
			return true;
		uint32_t empty = 0;
		return owner.compare_exchange_strong(
			empty, token, std::memory_order_acq_rel, std::memory_order_acquire);
	}

	static void release(std::atomic<uint32_t>& owner, uint32_t token) {
		uint32_t expected = token;
		owner.compare_exchange_strong(
			expected, 0, std::memory_order_acq_rel, std::memory_order_acquire);
	}

	std::atomic<uint32_t> nextToken_{1};
	MetaModuleHeadConfigBus buses_[kBusCount][kHeadCount];
};

} // namespace spacetime
