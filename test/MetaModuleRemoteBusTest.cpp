#include "doctest.h"
#include "MetaModuleRemoteBus.hpp"
#include "MetaModuleTimingBus.hpp"

using namespace spacetime;

TEST_CASE("Stage bank publish/read round-trips through the flat field array") {
	MetaModuleStageBankRegistry registry;
	uint32_t token = registry.makeToken();
	CHECK(registry.registerBank(0, 3, token));

	BlockSegment segment;
	for (int i = 0; i < kStagesPerBlock; i++) {
		segment.voltage[i] = 1.25f * (float)(i + 1);
		segment.time[i] = 0.1f * (float)(i + 1);
		segment.program[i].setQuantize(true);
		segment.program[i].setPulse1(i % 2 == 0);
	}
	registry.publishBank(0, 3, segment);

	BlockSegment read;
	CHECK(registry.readBank(0, 3, read));
	for (int i = 0; i < kStagesPerBlock; i++) {
		CHECK(read.voltage[i] == doctest::Approx(segment.voltage[i]));
		CHECK(read.time[i] == doctest::Approx(segment.time[i]));
		CHECK(read.program[i].quantize() == segment.program[i].quantize());
		CHECK(read.program[i].pulse1() == segment.program[i].pulse1());
	}
}

TEST_CASE("An unclaimed stage bank reads as BlockSegment defaults, not stale data") {
	MetaModuleStageBankRegistry registry;
	BlockSegment out;
	out.voltage[0] = 9.f;  // pre-dirty the output to prove it gets overwritten

	CHECK_FALSE(registry.readBank(0, 5, out));
	BlockSegment defaults;
	for (int i = 0; i < kStagesPerBlock; i++) {
		CHECK(out.voltage[i] == doctest::Approx(defaults.voltage[i]));
		CHECK(out.time[i] == doctest::Approx(defaults.time[i]));
		CHECK(out.program[i].bits == defaults.program[i].bits);
	}
}

TEST_CASE("Duplicate stage banks report DUP and neither is trusted on read") {
	MetaModuleStageBankRegistry registry;
	uint32_t first = registry.makeToken();
	uint32_t second = registry.makeToken();
	CHECK(registry.registerBank(0, 7, first));
	CHECK_FALSE(registry.registerBank(0, 7, second));
	CHECK(registry.bankLinkCount(0, 7) == 2);

	// The first owner publishes real data, but with two registrants present
	// the bank must not be trusted -- matches the discard behavior already
	// proven for duplicate Cores/Remotes at the ownership layer.
	BlockSegment segment;
	segment.voltage[0] = 5.f;
	registry.publishBank(0, 7, segment);

	BlockSegment out;
	CHECK_FALSE(registry.readBank(0, 7, out));
	CHECK(out.voltage[0] == doctest::Approx(0.f));  // default, not the published 5V

	// Removing the loser recovers a normal single-owner link.
	registry.unregisterBank(0, 7, second);
	CHECK(registry.bankLinkCount(0, 7) == 1);
	CHECK(registry.readBank(0, 7, out));
	CHECK(out.voltage[0] == doctest::Approx(5.f));

	registry.unregisterBank(0, 7, first);
	CHECK(registry.bankLinkCount(0, 7) == 0);
}

TEST_CASE("readAllBanks concatenates claimed banks and defaults the rest") {
	MetaModuleStageBankRegistry registry;
	uint32_t tokenBank0 = registry.makeToken();
	uint32_t tokenBank2 = registry.makeToken();
	CHECK(registry.registerBank(0, 0, tokenBank0));
	CHECK(registry.registerBank(0, 2, tokenBank2));
	// Bank 1 is left unclaimed on purpose: a gap in the middle of the chain.

	BlockSegment bank0;
	bank0.voltage[0] = 3.f;
	registry.publishBank(0, 0, bank0);

	BlockSegment bank2;
	bank2.voltage[0] = 7.f;
	registry.publishBank(0, 2, bank2);

	StageTable table;
	int count = registry.readAllBanks(0, table);
	CHECK(count == (int)(MetaModuleStageBankRegistry::kBankCount * kStagesPerBlock));
	CHECK(table.voltage[0] == doctest::Approx(3.f));                       // bank 0, stage 0
	CHECK(table.voltage[kStagesPerBlock] == doctest::Approx(0.f));         // bank 1 (gap): default
	CHECK(table.voltage[kStagesPerBlock * 2] == doctest::Approx(7.f));     // bank 2, stage 0
}

TEST_CASE("Stage bank slots are isolated per Instrument ID") {
	MetaModuleStageBankRegistry registry;
	uint32_t tokenA = registry.makeToken();
	uint32_t tokenB = registry.makeToken();
	CHECK(registry.registerBank(0, 0, tokenA));
	CHECK(registry.registerBank(1, 0, tokenB));  // same bank index, different instrument

	BlockSegment segA;
	segA.voltage[0] = 1.f;
	BlockSegment segB;
	segB.voltage[0] = 2.f;
	registry.publishBank(0, 0, segA);
	registry.publishBank(1, 0, segB);

	BlockSegment outA;
	BlockSegment outB;
	CHECK(registry.readBank(0, 0, outA));
	CHECK(registry.readBank(1, 0, outB));
	CHECK(outA.voltage[0] == doctest::Approx(1.f));
	CHECK(outB.voltage[0] == doctest::Approx(2.f));
}

TEST_CASE("Head config publish/read round-trips through the flat field array") {
	MetaModuleHeadRegistry registry;
	uint32_t token = registry.makeToken();
	CHECK(registry.registerHead(0, 2, token));

	HeadConfig config;
	config.continuous = true;
	config.addrExt = true;
	config.addressKnob = 6.5f;
	config.direction = 3;
	config.clkExt = true;
	config.clkDivIndex = 7;
	config.timeCvAmount = -0.5f;
	config.loopMode = 1;
	registry.publishHead(0, 2, config);

	HeadConfig read;
	CHECK(registry.readHead(0, 2, read));
	CHECK(read.continuous == config.continuous);
	CHECK(read.addrExt == config.addrExt);
	CHECK(read.addressKnob == doctest::Approx(config.addressKnob));
	CHECK(read.direction == config.direction);
	CHECK(read.clkExt == config.clkExt);
	CHECK(read.clkDivIndex == config.clkDivIndex);
	CHECK(read.timeCvAmount == doctest::Approx(config.timeCvAmount));
	CHECK(read.loopMode == config.loopMode);
}

TEST_CASE("An unclaimed head reads as HeadConfig defaults") {
	MetaModuleHeadRegistry registry;
	HeadConfig out;
	out.addressKnob = 9.f;  // pre-dirty to prove it gets overwritten

	CHECK_FALSE(registry.readHead(0, 4, out));
	HeadConfig defaults;
	CHECK(out.continuous == defaults.continuous);
	CHECK(out.addressKnob == doctest::Approx(defaults.addressKnob));
	CHECK(out.direction == defaults.direction);
	CHECK(out.clkDivIndex == defaults.clkDivIndex);
	CHECK(out.loopMode == defaults.loopMode);
}

TEST_CASE("Duplicate heads report DUP and neither is trusted on read") {
	MetaModuleHeadRegistry registry;
	uint32_t first = registry.makeToken();
	uint32_t second = registry.makeToken();
	CHECK(registry.registerHead(0, 1, first));
	CHECK_FALSE(registry.registerHead(0, 1, second));
	CHECK(registry.headLinkCount(0, 1) == 2);

	HeadConfig config;
	config.addressKnob = 4.f;
	registry.publishHead(0, 1, config);

	HeadConfig out;
	CHECK_FALSE(registry.readHead(0, 1, out));
	CHECK(out.addressKnob == doctest::Approx(0.f));

	registry.unregisterHead(0, 1, second);
	CHECK(registry.headLinkCount(0, 1) == 1);
	CHECK(registry.readHead(0, 1, out));
	CHECK(out.addressKnob == doctest::Approx(4.f));
}

// EB4: this is the intended end-to-end sequence a real StageRemote/HeadRemote
// follows -- resolve the Instrument ID via MetaModuleTimingBusRegistry's
// auto-bind query (EB4), then register/publish through the EB3 registry
// using that resolved id. Neither registry knows about the other; a future
// module's own code is what composes them, exactly as shown here.
TEST_CASE("Auto-bind resolves the sole Core's instrument, then binds a bank there") {
	MetaModuleTimingBusRegistry coreRegistry;
	MetaModuleStageBankRegistry stageRegistry;

	// No Core yet: a fresh StageRemote has nothing to auto-bind to and must
	// fall back to explicit Instrument ID selection rather than guessing.
	unsigned instrumentId = 0;
	CHECK_FALSE(coreRegistry.findSoleCore(instrumentId));

	// Core appears on instrument B (index 1).
	uint32_t coreToken = coreRegistry.makeToken();
	CHECK(coreRegistry.registerCore(1, coreToken));

	// StageRemote auto-binds: resolve, then register on the resolved id.
	CHECK(coreRegistry.findSoleCore(instrumentId));
	CHECK(instrumentId == 1);
	uint32_t bankToken = stageRegistry.makeToken();
	CHECK(stageRegistry.registerBank(instrumentId, 0, bankToken));

	BlockSegment segment;
	segment.voltage[0] = 8.f;
	stageRegistry.publishBank(instrumentId, 0, segment);

	BlockSegment out;
	CHECK(stageRegistry.readBank(1, 0, out));
	CHECK(out.voltage[0] == doctest::Approx(8.f));

	// A second Core appears on another instrument: auto-bind now declines
	// for any *new* Remote, though the already-bound StageRemote above is
	// unaffected -- EB4 only governs how a fresh Remote picks its instrument
	// at bind time, not what happens to Remotes already bound.
	uint32_t secondCore = coreRegistry.makeToken();
	CHECK(coreRegistry.registerCore(3, secondCore));
	CHECK_FALSE(coreRegistry.findSoleCore(instrumentId));
	CHECK(stageRegistry.readBank(1, 0, out));
	CHECK(out.voltage[0] == doctest::Approx(8.f));
}

// EB5: a bank/head that is validly registered but has never actually been
// published falls through the owner/count checks (both look "claimed") and
// would otherwise read ExpanderSnapshot's own pre-publish all-zero-bits
// default -- which disagrees with BlockSegment()'s/HeadConfig()'s real
// defaults (time 0.5 not 0; clkDivIndex 4 not 0; program bits kClearWord not
// 0). This is also the closest host-testable stand-in for stale leftover
// memory from an unclean plugin unload/reload (EB7): both cases look
// structurally claimed without ever having received real data.
TEST_CASE("A claimed-but-never-published bank reads as BlockSegment defaults, not raw zero bits") {
	MetaModuleStageBankRegistry registry;
	uint32_t token = registry.makeToken();
	CHECK(registry.registerBank(0, 6, token));  // registered; publishBank deliberately never called

	BlockSegment out;
	out.voltage[0] = 9.f;  // pre-dirty to prove it gets overwritten
	CHECK_FALSE(registry.readBank(0, 6, out));

	BlockSegment defaults;
	for (int i = 0; i < kStagesPerBlock; i++) {
		CHECK(out.voltage[i] == doctest::Approx(defaults.voltage[i]));
		CHECK(out.time[i] == doctest::Approx(defaults.time[i]));      // 0.5, not 0
		CHECK(out.program[i].bits == defaults.program[i].bits);       // kClearWord, not 0
	}
}

TEST_CASE("A claimed-but-never-published head reads as HeadConfig defaults, not raw zero bits") {
	MetaModuleHeadRegistry registry;
	uint32_t token = registry.makeToken();
	CHECK(registry.registerHead(0, 5, token));  // registered; publishHead deliberately never called

	HeadConfig out;
	out.clkDivIndex = 9;  // pre-dirty to prove it gets overwritten
	CHECK_FALSE(registry.readHead(0, 5, out));

	HeadConfig defaults;
	CHECK(out.direction == defaults.direction);
	CHECK(out.clkDivIndex == defaults.clkDivIndex);  // 4 (x1), not 0 (/16)
	CHECK(out.loopMode == defaults.loopMode);         // LOOP_FIRST_LAST (1), not 0
}

TEST_CASE("Bank and head heartbeats advance only on publish, for a caller's own freshness check") {
	MetaModuleStageBankRegistry stageRegistry;
	uint32_t stageToken = stageRegistry.makeToken();
	CHECK(stageRegistry.registerBank(0, 1, stageToken));
	CHECK(stageRegistry.bankHeartbeat(0, 1) == 0);  // registered, not yet published

	BlockSegment segment;
	stageRegistry.publishBank(0, 1, segment);
	CHECK(stageRegistry.bankHeartbeat(0, 1) == 1);
	stageRegistry.publishBank(0, 1, segment);
	CHECK(stageRegistry.bankHeartbeat(0, 1) == 2);
	// Reading does not itself advance the heartbeat -- only publishing does.
	BlockSegment out;
	stageRegistry.readBank(0, 1, out);
	CHECK(stageRegistry.bankHeartbeat(0, 1) == 2);

	MetaModuleHeadRegistry headRegistry;
	uint32_t headToken = headRegistry.makeToken();
	CHECK(headRegistry.registerHead(0, 3, headToken));
	CHECK(headRegistry.headHeartbeat(0, 3) == 0);
	HeadConfig config;
	headRegistry.publishHead(0, 3, config);
	CHECK(headRegistry.headHeartbeat(0, 3) == 1);
}
