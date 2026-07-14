#pragma once
// Platform-neutral transport used by the VCV-only SpaceTime Glue endpoints.

#include <atomic>
#include <cstdint>
#include "Chain.hpp"

namespace spacetime {

enum GlueMode : uint8_t {
	GLUE_MODE_NONE = 0,
	GLUE_MODE_HEAD,
	GLUE_MODE_STAGE
};

static const int kGlueLinks = 8;
static const int kGlueQueueCapacity = 16;

template <typename T, int Capacity = kGlueQueueCapacity>
class GlueQueue {
public:
	GlueQueue() : write_(0), read_(0), drops_(0) {}

	bool push(uint32_t epoch, const T& value) {
		uint32_t write = write_.load(std::memory_order_relaxed);
		uint32_t read = read_.load(std::memory_order_acquire);
		if (write - read >= Capacity) {
			drops_.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		Slot& slot = slots_[write % Capacity];
		slot.epoch = epoch;
		slot.value = value;
		write_.store(write + 1, std::memory_order_release);
		return true;
	}

	// Stale envelopes from an earlier pairing are discarded automatically.
	bool pop(uint32_t epoch, T& value) {
		for (;;) {
			uint32_t read = read_.load(std::memory_order_relaxed);
			uint32_t write = write_.load(std::memory_order_acquire);
			if (read == write)
				return false;
			Slot& slot = slots_[read % Capacity];
			uint32_t slotEpoch = slot.epoch;
			if (slotEpoch == epoch)
				value = slot.value;
			read_.store(read + 1, std::memory_order_release);
			if (slotEpoch == epoch)
				return true;
		}
	}

	uint32_t drops() const { return drops_.load(std::memory_order_relaxed); }

private:
	struct Slot {
		uint32_t epoch;
		T value;
	};

	Slot slots_[Capacity];
	std::atomic<uint32_t> write_;
	std::atomic<uint32_t> read_;
	std::atomic<uint32_t> drops_;
};

struct GlueBus {
	std::atomic<int64_t> leftOwner;
	std::atomic<int64_t> rightOwner;
	std::atomic<uint8_t> leftMode;
	std::atomic<uint8_t> rightMode;
	std::atomic<uint32_t> epoch;

	// Left fragment -> right fragment.
	GlueQueue<HeadsToAnchorMsg> headsRight;
	GlueQueue<AnchorToBlocksMsg> blocksRight;
	// Right fragment -> left fragment.
	GlueQueue<AnchorToHeadsMsg> headsLeft;
	GlueQueue<BlockToAnchorMsg> blocksLeft;

	GlueBus()
		: leftOwner(-1), rightOwner(-1), leftMode(GLUE_MODE_NONE),
		  rightMode(GLUE_MODE_NONE), epoch(1) {}

	bool linked() const {
		uint8_t lm = leftMode.load(std::memory_order_acquire);
		uint8_t rm = rightMode.load(std::memory_order_acquire);
		return leftOwner.load(std::memory_order_acquire) >= 0 &&
			rightOwner.load(std::memory_order_acquire) >= 0 &&
			lm != GLUE_MODE_NONE && lm == rm;
	}
};

} // namespace spacetime
