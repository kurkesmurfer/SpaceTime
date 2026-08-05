#include "doctest.h"
#include "MetaModuleBusProbe.hpp"

using namespace spacetime;

TEST_CASE("MetaModule probe bus transfers values in both directions") {
	MetaModuleBusProbeRegistry registry;
	uint32_t core = registry.makeToken();
	uint32_t remote = registry.makeToken();
	CHECK(registry.registerCore(0, core));
	CHECK(registry.registerRemote(0, remote));

	MetaModuleProbeBus& bus = registry.bus(0);
	bus.coreToRemote.publish(7.25f);
	bus.remoteToCore.publish(-3.5f);

	uint32_t coreSeq = 0;
	uint32_t remoteSeq = 0;
	float coreValue = 0.f;
	float remoteValue = 0.f;
	CHECK(bus.coreToRemote.read(remoteSeq, remoteValue));
	CHECK(bus.remoteToCore.read(coreSeq, coreValue));
	CHECK(remoteValue == doctest::Approx(7.25f));
	CHECK(coreValue == doctest::Approx(-3.5f));
	CHECK_FALSE(bus.coreToRemote.read(remoteSeq, remoteValue));
	CHECK_FALSE(bus.remoteToCore.read(coreSeq, coreValue));
}

TEST_CASE("MetaModule probe bus detects duplicate endpoint roles") {
	MetaModuleBusProbeRegistry registry;
	uint32_t core1 = registry.makeToken();
	uint32_t core2 = registry.makeToken();
	CHECK(registry.registerCore(0, core1));
	CHECK_FALSE(registry.registerCore(0, core2));
	CHECK(registry.bus(0).coreCount.load() == 2);
	CHECK(registry.bus(0).coreOwner.load() == core1);

	registry.unregisterCore(0, core1);
	CHECK(registry.tryClaimCore(0, core2));
	CHECK(registry.bus(0).coreOwner.load() == core2);
	registry.unregisterCore(0, core2);
	CHECK(registry.bus(0).coreCount.load() == 0);
	CHECK(registry.bus(0).coreOwner.load() == 0);
}

TEST_CASE("MetaModule probe buses isolate independent instruments") {
	MetaModuleBusProbeRegistry registry;
	uint32_t a = registry.makeToken();
	uint32_t b = registry.makeToken();
	CHECK(registry.registerCore(0, a));
	CHECK(registry.registerCore(1, b));
	registry.bus(0).coreToRemote.publish(1.f);
	registry.bus(1).coreToRemote.publish(9.f);

	uint32_t seqA = 0;
	uint32_t seqB = 0;
	float valueA = 0.f;
	float valueB = 0.f;
	CHECK(registry.bus(0).coreToRemote.read(seqA, valueA));
	CHECK(registry.bus(1).coreToRemote.read(seqB, valueB));
	CHECK(valueA == doctest::Approx(1.f));
	CHECK(valueB == doctest::Approx(9.f));
}

TEST_CASE("MetaModule probe bus partitions StageRemote and HeadRemote roles") {
	MetaModuleBusProbeRegistry registry;
	uint32_t core = registry.makeToken();
	uint32_t stage = registry.makeToken();
	uint32_t head = registry.makeToken();

	CHECK(registry.registerRole(BusRole::Core, 0, core));
	CHECK(registry.registerRole(BusRole::StageRemote, 0, stage));
	CHECK(registry.registerRole(BusRole::HeadRemote, 0, head));

	// Each role has its own owner/count. Registering one role must not be
	// visible to, or block, an unrelated role sharing the same Instrument ID.
	CHECK(registry.roleCount(BusRole::Core, 0) == 1);
	CHECK(registry.roleCount(BusRole::StageRemote, 0) == 1);
	CHECK(registry.roleCount(BusRole::HeadRemote, 0) == 1);
	CHECK(registry.roleCount(BusRole::ProgramRemote, 0) == 0);

	// A second StageRemote on the same slot is a duplicate; it must not
	// affect HeadRemote's independent count or ownership.
	uint32_t secondStage = registry.makeToken();
	CHECK_FALSE(registry.registerRole(BusRole::StageRemote, 0, secondStage));
	CHECK(registry.roleCount(BusRole::StageRemote, 0) == 2);
	CHECK(registry.roleCount(BusRole::HeadRemote, 0) == 1);

	registry.unregisterRole(BusRole::Core, 0, core);
	registry.unregisterRole(BusRole::StageRemote, 0, stage);
	registry.unregisterRole(BusRole::StageRemote, 0, secondStage);
	registry.unregisterRole(BusRole::HeadRemote, 0, head);
	CHECK(registry.roleCount(BusRole::Core, 0) == 0);
	CHECK(registry.roleCount(BusRole::StageRemote, 0) == 0);
	CHECK(registry.roleCount(BusRole::HeadRemote, 0) == 0);
}
