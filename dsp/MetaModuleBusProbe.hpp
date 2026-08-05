#pragma once

#include "ExpanderLink.hpp"

#include <atomic>
#include <cstdint>

namespace spacetime {

// Word-sized single-writer/single-reader mailbox, specialized to the float
// payload every probe and future command channel carries today.
using ProbeMailbox = ExpanderMailbox<float>;

// Roles that can claim an exclusive slot on a bus. Core and Remote are the
// original MM0-verified pair; StageRemote/HeadRemote/ProgramRemote are the
// EB3 per-bank/per-head roles this registry is being generalized to serve.
// Each role owns an independent owner/count pair so unrelated remote types
// sharing an Instrument ID can never satisfy each other's peer-presence
// check (see METAMODULE_EXPANDER_BUS_PLAN.md, EB2).
enum class BusRole {
	Core,
	Remote,
	StageRemote,
	HeadRemote,
	ProgramRemote,
};

struct MetaModuleProbeBus {
	// Core/Remote: exclusive-owner slots exactly as originally hardware-
	// verified in MM0. Field names and layout are unchanged so existing
	// behavior and tests cannot regress.
	std::atomic<uint32_t> coreOwner{0};
	std::atomic<uint32_t> remoteOwner{0};
	std::atomic<uint32_t> coreCount{0};
	std::atomic<uint32_t> remoteCount{0};
	std::atomic<uint32_t> coreHeartbeat{0};
	std::atomic<uint32_t> remoteHeartbeat{0};
	ProbeMailbox coreToRemote;
	ProbeMailbox remoteToCore;

	// StageRemote/HeadRemote/ProgramRemote: additional exclusive-owner
	// slots, purely additive. Nothing above this line changes shape or
	// meaning.
	static const unsigned kAuxRoleCount = 3;
	std::atomic<uint32_t> auxOwner[kAuxRoleCount]{};
	std::atomic<uint32_t> auxCount[kAuxRoleCount]{};
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

	// --- Original Core/Remote entry points: unchanged behavior. ---

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

	// --- Role-generic entry points. Core/Remote dispatch to the methods
	// above so their MM0 hardware-verified behavior is untouched; the aux
	// roles use the new per-role slots. This is what EB3's per-bank/
	// per-head registries build on. ---

	bool registerRole(BusRole role, unsigned index, uint32_t token) {
		if (role == BusRole::Core)
			return registerCore(index, token);
		if (role == BusRole::Remote)
			return registerRemote(index, token);
		MetaModuleProbeBus& target = bus(index);
		unsigned r = auxIndex(role);
		target.auxCount[r].fetch_add(1, std::memory_order_acq_rel);
		return claim(target.auxOwner[r], token);
	}

	bool tryClaimRole(BusRole role, unsigned index, uint32_t token) {
		if (role == BusRole::Core)
			return tryClaimCore(index, token);
		if (role == BusRole::Remote)
			return tryClaimRemote(index, token);
		return claim(bus(index).auxOwner[auxIndex(role)], token);
	}

	void unregisterRole(BusRole role, unsigned index, uint32_t token) {
		if (role == BusRole::Core) {
			unregisterCore(index, token);
			return;
		}
		if (role == BusRole::Remote) {
			unregisterRemote(index, token);
			return;
		}
		MetaModuleProbeBus& target = bus(index);
		unsigned r = auxIndex(role);
		release(target.auxOwner[r], token);
		target.auxCount[r].fetch_sub(1, std::memory_order_acq_rel);
	}

	uint32_t roleCount(BusRole role, unsigned index) {
		MetaModuleProbeBus& target = bus(index);
		if (role == BusRole::Core)
			return target.coreCount.load(std::memory_order_acquire);
		if (role == BusRole::Remote)
			return target.remoteCount.load(std::memory_order_acquire);
		return target.auxCount[auxIndex(role)].load(std::memory_order_acquire);
	}

private:
	static unsigned auxIndex(BusRole role) {
		switch (role) {
			case BusRole::StageRemote: return 0;
			case BusRole::HeadRemote: return 1;
			case BusRole::ProgramRemote: return 2;
			default: return 2;
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
	MetaModuleProbeBus buses_[kBusCount];
};

} // namespace spacetime
