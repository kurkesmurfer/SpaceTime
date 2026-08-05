#pragma once

// Generic lock-free transport primitives shared by every MetaModule Core/
// Remote bus. Two distinct shapes, not one: ExpanderMailbox is a
// change-tracked single word (publish/read tells the reader whether a new
// value has arrived since it last looked); ExpanderSnapshot is an
// always-current multi-field seqlock (publish/read always hands back the
// latest coherent value, whether or not it changed since the last read).
// Collapsing these into a single template would blur that distinction, so
// this header keeps them separate and lets each bus pick the one it
// actually needs.
//
// Both are single-writer/single-reader. Two writers on one instance is a
// race; use one instance per direction, exactly as the probe and timing
// buses already do (coreToRemote / remoteToCore, one telemetry publisher
// per instrument).

#include <atomic>
#include <cstdint>
#include <cstring>

namespace spacetime {

// Reinterpret a float as its raw bit pattern and back, for packing float
// fields into a flat uint32_t array (see MetaModuleRemoteBus.hpp). Exposed
// as free functions because more than one bus now needs exactly this
// conversion; ExpanderMailbox<float> keeps its own inline copy below rather
// than being refactored to call these, to avoid re-touching already-shipped
// EB1 code for a pure DRY tidy-up.
inline uint32_t floatToBits(float value) {
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

inline float bitsToFloat(uint32_t bits) {
	float value = 0.f;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

// Change-tracked single word. T must fit in 32 bits and be trivially
// copyable (float, uint32_t, or a small POD of the same size). publish()
// bumps a sequence counter; read() reports true only if the sequence has
// moved since the caller's last read. This is ProbeMailbox's original
// behavior, generalized over the payload type.
template <typename T>
class ExpanderMailbox {
public:
	static_assert(sizeof(T) <= sizeof(uint32_t),
		"ExpanderMailbox only fits word-sized payloads; use ExpanderSnapshot for larger types.");

	void publish(T value) {
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(T));
		valueBits_.store(bits, std::memory_order_relaxed);
		sequence_.fetch_add(1, std::memory_order_release);
	}

	bool read(uint32_t& lastSequence, T& value) const {
		uint32_t sequence = sequence_.load(std::memory_order_acquire);
		if (sequence == lastSequence)
			return false;
		uint32_t bits = valueBits_.load(std::memory_order_relaxed);
		std::memcpy(&value, &bits, sizeof(T));
		lastSequence = sequence;
		return true;
	}

	uint32_t sequence() const {
		return sequence_.load(std::memory_order_acquire);
	}

private:
	std::atomic<uint32_t> valueBits_{0};
	std::atomic<uint32_t> sequence_{0};
};

// Always-current multi-field seqlock. Storage is a flat array of
// kFieldCount independent uint32_t atomics rather than one struct copy, so
// every field access stays within the C++ memory model — no torn or
// data-raced whole-struct copy — exactly as the original hand-written
// TimingSnapshot/TimingTelemetry did. A typed wrapper (see
// MetaModuleTimingBus.hpp) marshals its real fields into and out of this
// flat array; ExpanderSnapshot itself knows nothing about their meaning.
// Before the first publish(), read() returns the zero-initialized default
// rather than failing — an unclaimed/unpublished slot reads as defined
// zero data, not as an error (see METAMODULE_EXPANDER_BUS_PLAN.md, EB5).
template <unsigned kFieldCount, unsigned kMaxAttempts = 4>
class ExpanderSnapshot {
public:
	void publish(const uint32_t (&fields)[kFieldCount]) {
		sequence_.fetch_add(1, std::memory_order_acq_rel);
		for (unsigned i = 0; i < kFieldCount; i++)
			fields_[i].store(fields[i], std::memory_order_relaxed);
		sequence_.fetch_add(1, std::memory_order_release);
		heartbeat_.fetch_add(1, std::memory_order_release);
	}

	bool read(uint32_t (&fields)[kFieldCount]) const {
		for (unsigned attempt = 0; attempt < kMaxAttempts; attempt++) {
			uint32_t before = sequence_.load(std::memory_order_acquire);
			if (before & 1u)
				continue;
			for (unsigned i = 0; i < kFieldCount; i++)
				fields[i] = fields_[i].load(std::memory_order_relaxed);
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
	std::atomic<uint32_t> fields_[kFieldCount]{};
};

} // namespace spacetime
