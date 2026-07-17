#include "doctest.h"
#include "MetaModuleTimingBus.hpp"

using namespace spacetime;

TEST_CASE("MetaModule timing bus transfers a coherent eight-head snapshot") {
	TimingTelemetry telemetry;
	TimingSnapshot published;
	for (int h = 0; h < kMaxHeads; h++) {
		published.sourceEvents[h] = 100 + h;
		published.stageEntries[h] = 200 + h;
		published.runState[h] = h % 3;
		published.stage[h] = h * 4;
		published.clockSource[h] = h % 4;
		published.clockDivision[h] = h;
	}
	telemetry.publish(published);

	TimingSnapshot received;
	CHECK(telemetry.read(received));
	CHECK(telemetry.heartbeat() == 1);
	for (int h = 0; h < kMaxHeads; h++) {
		CHECK(received.sourceEvents[h] == published.sourceEvents[h]);
		CHECK(received.stageEntries[h] == published.stageEntries[h]);
		CHECK(received.runState[h] == published.runState[h]);
		CHECK(received.stage[h] == published.stage[h]);
		CHECK(received.clockSource[h] == published.clockSource[h]);
		CHECK(received.clockDivision[h] == published.clockDivision[h]);
	}
}

TEST_CASE("MetaModule timing bus detects duplicate cores and allows recovery") {
	MetaModuleTimingBusRegistry registry;
	uint32_t first = registry.makeToken();
	uint32_t second = registry.makeToken();
	CHECK(registry.registerCore(0, first));
	CHECK_FALSE(registry.registerCore(0, second));
	CHECK(registry.bus(0).coreCount.load() == 2);
	registry.unregisterCore(0, first);
	CHECK(registry.tryClaimCore(0, second));
	CHECK(registry.bus(0).coreOwner.load() == second);
	registry.unregisterCore(0, second);
	CHECK(registry.bus(0).coreCount.load() == 0);
}

TEST_CASE("MetaModule timing buses isolate instruments and count monitors") {
	MetaModuleTimingBusRegistry registry;
	uint32_t a = registry.makeToken();
	uint32_t b = registry.makeToken();
	CHECK(registry.registerCore(0, a));
	CHECK(registry.registerCore(1, b));
	registry.registerMonitor(0);
	registry.registerMonitor(0);
	CHECK(registry.bus(0).monitorCount.load() == 2);

	TimingSnapshot snapshotA;
	TimingSnapshot snapshotB;
	snapshotA.sourceEvents[0] = 11;
	snapshotB.sourceEvents[0] = 22;
	registry.bus(0).telemetry.publish(snapshotA);
	registry.bus(1).telemetry.publish(snapshotB);
	TimingSnapshot readA;
	TimingSnapshot readB;
	CHECK(registry.bus(0).telemetry.read(readA));
	CHECK(registry.bus(1).telemetry.read(readB));
	CHECK(readA.sourceEvents[0] == 11);
	CHECK(readB.sourceEvents[0] == 22);
	registry.unregisterMonitor(0);
	registry.unregisterMonitor(0);
	CHECK(registry.bus(0).monitorCount.load() == 0);
}
