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

// ---- Stage banks (StageRemote, kBankCount slots per instrument) -----------

struct MetaModuleStageBankBus {
	static const unsigned kFieldCount = kStagesPerBlock * 3;  // voltage, time, program.bits

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
	void publishBank(unsigned busIndex, unsigned bankIndex, const BlockSegment& segment) {
		uint32_t fields[MetaModuleStageBankBus::kFieldCount];
		packBlockSegment(segment, fields);
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
	bool readBank(unsigned busIndex, unsigned bankIndex, BlockSegment& out) {
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
		unpackBlockSegment(fields, out);
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

	static void packBlockSegment(const BlockSegment& segment,
			uint32_t (&fields)[MetaModuleStageBankBus::kFieldCount]) {
		for (int i = 0; i < kStagesPerBlock; i++) {
			fields[i * 3 + 0] = floatToBits(segment.voltage[i]);
			fields[i * 3 + 1] = floatToBits(segment.time[i]);
			fields[i * 3 + 2] = segment.program[i].bits;
		}
	}

	static void unpackBlockSegment(const uint32_t (&fields)[MetaModuleStageBankBus::kFieldCount],
			BlockSegment& segment) {
		for (int i = 0; i < kStagesPerBlock; i++) {
			segment.voltage[i] = bitsToFloat(fields[i * 3 + 0]);
			segment.time[i] = bitsToFloat(fields[i * 3 + 1]);
			segment.program[i] = ProgramWord(fields[i * 3 + 2]);
		}
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
	static const unsigned kFieldCount = 8;  // HeadConfig's eight fields

	std::atomic<uint32_t> owner{0};
	std::atomic<uint32_t> count{0};
	ExpanderSnapshot<kFieldCount> config;
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
