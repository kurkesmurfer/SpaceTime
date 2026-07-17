#pragma once

// Lock-free read-only telemetry bus used by the MetaModule Core and optional
// diagnostic/control surfaces. The seqlock keeps each eight-head snapshot
// coherent when Core and a monitor are scheduled on different processors.

#include <atomic>
#include <cstdint>

#include "StageTable.hpp"

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
		sequence_.fetch_add(1, std::memory_order_acq_rel);
		for (int h = 0; h < kMaxHeads; h++) {
			sourceEvents_[h].store(snapshot.sourceEvents[h], std::memory_order_relaxed);
			stageEntries_[h].store(snapshot.stageEntries[h], std::memory_order_relaxed);
			runState_[h].store(snapshot.runState[h], std::memory_order_relaxed);
			stage_[h].store(snapshot.stage[h], std::memory_order_relaxed);
			clockSource_[h].store(snapshot.clockSource[h], std::memory_order_relaxed);
			clockDivision_[h].store(snapshot.clockDivision[h], std::memory_order_relaxed);
		}
		sequence_.fetch_add(1, std::memory_order_release);
		heartbeat_.fetch_add(1, std::memory_order_release);
	}

	bool read(TimingSnapshot& snapshot) const {
		for (int attempt = 0; attempt < 4; attempt++) {
			uint32_t before = sequence_.load(std::memory_order_acquire);
			if (before & 1u)
				continue;
			for (int h = 0; h < kMaxHeads; h++) {
				snapshot.sourceEvents[h] = sourceEvents_[h].load(std::memory_order_relaxed);
				snapshot.stageEntries[h] = stageEntries_[h].load(std::memory_order_relaxed);
				snapshot.runState[h] = (uint8_t)runState_[h].load(std::memory_order_relaxed);
				snapshot.stage[h] = (uint8_t)stage_[h].load(std::memory_order_relaxed);
				snapshot.clockSource[h] = (uint8_t)clockSource_[h].load(std::memory_order_relaxed);
				snapshot.clockDivision[h] = (uint8_t)clockDivision_[h].load(std::memory_order_relaxed);
			}
			uint32_t after = sequence_.load(std::memory_order_acquire);
			if (before == after && !(after & 1u))
				return true;
		}
		return false;
	}

	uint32_t heartbeat() const {
		return heartbeat_.load(std::memory_order_acquire);
	}

private:
	std::atomic<uint32_t> sequence_{0};
	std::atomic<uint32_t> heartbeat_{0};
	std::atomic<uint32_t> sourceEvents_[kMaxHeads]{};
	std::atomic<uint32_t> stageEntries_[kMaxHeads]{};
	std::atomic<uint32_t> runState_[kMaxHeads]{};
	std::atomic<uint32_t> stage_[kMaxHeads]{};
	std::atomic<uint32_t> clockSource_[kMaxHeads]{};
	std::atomic<uint32_t> clockDivision_[kMaxHeads]{};
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
