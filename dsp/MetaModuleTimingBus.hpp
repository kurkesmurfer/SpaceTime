#pragma once

// Lock-free read-only telemetry bus used by the MetaModule Core and optional
// diagnostic/control surfaces. The seqlock keeps each eight-head snapshot
// coherent when Core and a monitor are scheduled on different processors.
// Storage and seqlock retry logic now live in the shared ExpanderSnapshot
// primitive (see ExpanderLink.hpp); this header defines the typed snapshot
// and marshals it to and from the flat field array ExpanderSnapshot stores.

#include "ExpanderLink.hpp"
#include "StageTable.hpp"

#include <atomic>
#include <cstdint>

namespace spacetime {

struct TimingSnapshot {
	uint32_t sourceEvents[kMaxHeads];
	uint32_t stageEntries[kMaxHeads];
	uint8_t runState[kMaxHeads];
	uint8_t stage[kMaxHeads];
	uint8_t clockSource[kMaxHeads];
	uint8_t clockDivision[kMaxHeads];

	TimingSnapshot() {
		for (int h = 0; h < kMaxHeads; h++) {
			sourceEvents[h] = 0;
			stageEntries[h] = 0;
			runState[h] = 0;
			stage[h] = 0;
			clockSource[h] = 0;
			clockDivision[h] = 4;
		}
	}
};

class TimingTelemetry {
public:
	void publish(const TimingSnapshot& snapshot) {
		uint32_t fields[kFieldCount];
		pack(snapshot, fields);
		link_.publish(fields);
	}

	bool read(TimingSnapshot& snapshot) const {
		uint32_t fields[kFieldCount];
		if (!link_.read(fields))
			return false;
		unpack(fields, snapshot);
		return true;
	}

	uint32_t heartbeat() const {
		return link_.heartbeat();
	}

private:
	static const unsigned kFieldsPerHead = 6;
	static const unsigned kFieldCount = kMaxHeads * kFieldsPerHead;

	static void pack(const TimingSnapshot& snapshot, uint32_t (&fields)[kFieldCount]) {
		for (int h = 0; h < kMaxHeads; h++) {
			fields[h * kFieldsPerHead + 0] = snapshot.sourceEvents[h];
			fields[h * kFieldsPerHead + 1] = snapshot.stageEntries[h];
			fields[h * kFieldsPerHead + 2] = snapshot.runState[h];
			fields[h * kFieldsPerHead + 3] = snapshot.stage[h];
			fields[h * kFieldsPerHead + 4] = snapshot.clockSource[h];
			fields[h * kFieldsPerHead + 5] = snapshot.clockDivision[h];
		}
	}

	static void unpack(const uint32_t (&fields)[kFieldCount], TimingSnapshot& snapshot) {
		for (int h = 0; h < kMaxHeads; h++) {
			snapshot.sourceEvents[h] = fields[h * kFieldsPerHead + 0];
			snapshot.stageEntries[h] = fields[h * kFieldsPerHead + 1];
			snapshot.runState[h] = (uint8_t)fields[h * kFieldsPerHead + 2];
			snapshot.stage[h] = (uint8_t)fields[h * kFieldsPerHead + 3];
			snapshot.clockSource[h] = (uint8_t)fields[h * kFieldsPerHead + 4];
			snapshot.clockDivision[h] = (uint8_t)fields[h * kFieldsPerHead + 5];
		}
	}

	ExpanderSnapshot<kFieldCount> link_;
};

struct MetaModuleTimingBus {
	std::atomic<uint32_t> coreOwner{0};
	std::atomic<uint32_t> coreCount{0};
	std::atomic<uint32_t> monitorCount{0};
	TimingTelemetry telemetry;
};

class MetaModuleTimingBusRegistry {
public:
	static const unsigned kBusCount = 4;

	uint32_t makeToken() {
		uint32_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
		return token == 0 ? nextToken_.fetch_add(1, std::memory_order_relaxed) : token;
	}

	MetaModuleTimingBus& bus(unsigned index) {
		return buses_[index < kBusCount ? index : 0];
	}

	bool registerCore(unsigned index, uint32_t token) {
		MetaModuleTimingBus& target = bus(index);
		target.coreCount.fetch_add(1, std::memory_order_acq_rel);
		return claim(target.coreOwner, token);
	}

	bool tryClaimCore(unsigned index, uint32_t token) {
		return claim(bus(index).coreOwner, token);
	}

	void unregisterCore(unsigned index, uint32_t token) {
		MetaModuleTimingBus& target = bus(index);
		release(target.coreOwner, token);
		target.coreCount.fetch_sub(1, std::memory_order_acq_rel);
	}

	void registerMonitor(unsigned index) {
		bus(index).monitorCount.fetch_add(1, std::memory_order_acq_rel);
	}

	void unregisterMonitor(unsigned index) {
		bus(index).monitorCount.fetch_sub(1, std::memory_order_acq_rel);
	}

	// EB4 auto-bind: read-only query for a fresh Remote deciding which
	// Instrument ID to join. Returns true and sets instrumentId only when
	// exactly one Instrument ID currently has a live (single-owner) Core;
	// returns false when none or more than one exist, so the caller falls
	// back to explicit A-D selection rather than guessing among ambiguous
	// candidates. Claims nothing and changes no ownership -- callers still
	// register/tryClaim through the normal entry points once they have the
	// resolved instrumentId (see METAMODULE_EXPANDER_BUS_PLAN.md, EB4).
	//
	// This queries the real Core's registry (the one Core.cpp actually
	// registers into), not MetaModuleBusProbeRegistry -- that one only
	// tracks the disposable MM0 probe modules and has never been the real
	// Core's bus.
	bool findSoleCore(unsigned& instrumentId) {
		unsigned found = kBusCount;
		unsigned liveCount = 0;
		for (unsigned i = 0; i < kBusCount; i++) {
			if (buses_[i].coreCount.load(std::memory_order_acquire) == 1) {
				liveCount++;
				found = i;
			}
		}
		if (liveCount != 1)
			return false;
		instrumentId = found;
		return true;
	}

private:
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
	MetaModuleTimingBus buses_[kBusCount];
};

} // namespace spacetime
