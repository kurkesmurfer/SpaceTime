#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace spacetime {

inline uint32_t probeFloatBits(float value) {
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

inline float probeBitsFloat(uint32_t bits) {
	float value = 0.f;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

class ProbeMailbox {
public:
	void publish(float value) {
		valueBits_.store(probeFloatBits(value), std::memory_order_relaxed);
		sequence_.fetch_add(1, std::memory_order_release);
	}

	bool read(uint32_t& lastSequence, float& value) const {
		uint32_t sequence = sequence_.load(std::memory_order_acquire);
		if (sequence == lastSequence)
			return false;
		value = probeBitsFloat(valueBits_.load(std::memory_order_relaxed));
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

struct MetaModuleProbeBus {
	std::atomic<uint32_t> coreOwner{0};
	std::atomic<uint32_t> remoteOwner{0};
	std::atomic<uint32_t> coreCount{0};
	std::atomic<uint32_t> remoteCount{0};
	std::atomic<uint32_t> coreHeartbeat{0};
	std::atomic<uint32_t> remoteHeartbeat{0};
	ProbeMailbox coreToRemote;
	ProbeMailbox remoteToCore;
};

class MetaModuleBusProbeRegistry {
public:
	static const unsigned kBusCount = 4;

	uint32_t makeToken() {
		uint32_t token = nextToken_.fetch_add(1, std::memory_order_relaxed);
		return token == 0 ? nextToken_.fetch_add(1, std::memory_order_relaxed) : token;
	}

	MetaModuleProbeBus& bus(unsigned index) {
		return buses_[index < kBusCount ? index : 0];
	}

	bool registerCore(unsigned index, uint32_t token) {
		MetaModuleProbeBus& target = bus(index);
		target.coreCount.fetch_add(1, std::memory_order_acq_rel);
		return claim(target.coreOwner, token);
	}

	bool registerRemote(unsigned index, uint32_t token) {
		MetaModuleProbeBus& target = bus(index);
		target.remoteCount.fetch_add(1, std::memory_order_acq_rel);
		return claim(target.remoteOwner, token);
	}

	bool tryClaimCore(unsigned index, uint32_t token) {
		return claim(bus(index).coreOwner, token);
	}

	bool tryClaimRemote(unsigned index, uint32_t token) {
		return claim(bus(index).remoteOwner, token);
	}

	void unregisterCore(unsigned index, uint32_t token) {
		MetaModuleProbeBus& target = bus(index);
		release(target.coreOwner, token);
		target.coreCount.fetch_sub(1, std::memory_order_acq_rel);
	}

	void unregisterRemote(unsigned index, uint32_t token) {
		MetaModuleProbeBus& target = bus(index);
		release(target.remoteOwner, token);
		target.remoteCount.fetch_sub(1, std::memory_order_acq_rel);
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
	MetaModuleProbeBus buses_[kBusCount];
};

} // namespace spacetime
