#include "doctest.h"
#include "MetaModuleRemoteBus.hpp"
#include "MetaModuleTimingBus.hpp"
#include "MidiCore.hpp"
#include "SpaceTimeEngine.hpp"

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
	registry.publishBank(0, 3, segment, 14, 7);

	BlockSegment read;
	uint8_t focus = 0;
	uint32_t focusSequence = 0;
	CHECK(registry.readBank(0, 3, read, &focus, &focusSequence));
	CHECK(focus == 14);
	CHECK(focusSequence == 7);
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

// ---- EB8: head relocation -- Core publishes the full table, a HeadRemote
// runs its own HeadDSP and publishes its output back. See
// MetaModuleRemoteBus.hpp's class comments for why the table registry has no
// ownership CAS of its own (composes with MetaModuleTimingBusRegistry's
// coreCount instead) and why head output shares HeadConfig's claimed slot.

TEST_CASE("Stage table publish/read round-trips all 64 stages through the flat field array") {
	MetaModuleStageTableRegistry registry;
	StageTable table;
	table.count = 40;
	for (int i = 0; i < kMaxStages; i++) {
		table.voltage[i] = (float)i * 0.1f;
		table.time[i] = (float)(i % 10) * 0.05f;
	}
	table.program[0].setQuantize(true);
	table.program[63].setPulse1(true);
	registry.publishTable(2, table);

	StageTable read;
	CHECK(registry.readTable(2, /*coreValid=*/true, read));
	CHECK(read.count == table.count);
	for (int i = 0; i < kMaxStages; i++) {
		CHECK(read.voltage[i] == doctest::Approx(table.voltage[i]));
		CHECK(read.time[i] == doctest::Approx(table.time[i]));
	}
	CHECK(read.program[0].quantize());
	CHECK(read.program[63].pulse1());
}

TEST_CASE("Stage table read is gated on caller-supplied core validity, not just publish state") {
	MetaModuleStageTableRegistry registry;
	StageTable table;
	table.count = 8;
	table.voltage[0] = 5.f;
	registry.publishTable(0, table);

	// Published, but the caller's own MetaModuleTimingBusRegistry check says
	// there's no valid sole Core right now (e.g. zero or duplicate Cores) --
	// this registry has no way to know that itself by design, so it must
	// trust the caller.
	StageTable read;
	read.voltage[0] = 9.f;  // pre-dirty
	CHECK_FALSE(registry.readTable(0, /*coreValid=*/false, read));
	StageTable defaults;
	CHECK(read.voltage[0] == doctest::Approx(defaults.voltage[0]));
	CHECK(read.count == defaults.count);

	CHECK(registry.readTable(0, /*coreValid=*/true, read));
	CHECK(read.voltage[0] == doctest::Approx(5.f));
}

TEST_CASE("Stage table read fails on zero heartbeat even when core is valid") {
	MetaModuleStageTableRegistry registry;
	CHECK(registry.tableHeartbeat(1) == 0);
	StageTable read;
	CHECK_FALSE(registry.readTable(1, /*coreValid=*/true, read));
}

TEST_CASE("Core broadcast context and selected stage round-trip independently of the table") {
	MetaModuleStageTableRegistry registry;
	ExtInputs ext;
	ext.v[2] = 3.3f;
	ext.connected[2] = true;
	Globals globals;
	globals.slewFrac1 = 0.4f;
	globals.slopeLaw = 1;
	globals.pulseRetrig = false;
	ScaleKey scaleKey;
	scaleKey.key = 7;
	scaleKey.scale = 1;
	registry.publishContext(0, ext, globals, scaleKey, 37);

	ExtInputs readExt;
	Globals readGlobals;
	ScaleKey readScaleKey;
	uint8_t selectedStage = 0;
	CHECK(registry.readContext(0, /*coreValid=*/true, readExt, readGlobals, readScaleKey,
		&selectedStage));
	CHECK(readExt.v[2] == doctest::Approx(3.3f));
	CHECK(readExt.connected[2]);
	CHECK_FALSE(readExt.connected[0]);
	CHECK(readGlobals.slewFrac1 == doctest::Approx(0.4f));
	CHECK(readGlobals.slopeLaw == 1);
	CHECK_FALSE(readGlobals.pulseRetrig);
	CHECK(readScaleKey.key == 7);
	CHECK(readScaleKey.scale == 1);
	CHECK(selectedStage == 37);

	// The table channel is untouched -- these are genuinely independent
	// snapshots on the same registry, not one payload split in two calls.
	CHECK(registry.tableHeartbeat(0) == 0);
	StageTable table;
	CHECK_FALSE(registry.readTable(0, /*coreValid=*/true, table));
}

TEST_CASE("Core broadcast context read is gated on core validity and heartbeat, same as the table") {
	MetaModuleStageTableRegistry registry;
	ExtInputs ext;
	Globals globals;
	ScaleKey scaleKey;
	CHECK_FALSE(registry.readContext(4, /*coreValid=*/true, ext, globals, scaleKey));  // never published

	registry.publishContext(4, ext, globals, scaleKey, 0);
	CHECK_FALSE(registry.readContext(4, /*coreValid=*/false, ext, globals, scaleKey));
	CHECK(registry.readContext(4, /*coreValid=*/true, ext, globals, scaleKey));
	CHECK(registry.contextHeartbeat(4) == 1);
}

TEST_CASE("Head output publish/read round-trips and does not disturb the config half of the same slot") {
	MetaModuleHeadRegistry registry;
	uint32_t token = registry.makeToken();
	CHECK(registry.registerHead(0, 5, token));

	HeadConfig config;
	config.direction = 2;
	registry.publishHead(0, 5, config);

	HeadOut out;
	out.cv = 7.25f;
	out.timeOut = 3.5f;
	out.ref = 1.5f;
	out.pulse1 = true;
	out.pulse2 = false;
	out.allPulse = true;
	out.eoc = true;
	out.currentStage = 12;
	out.phase = 0.75f;
	out.runState = RUN_RUNNING;
	registry.publishHeadOutput(0, 5, out);

	HeadOut readOut;
	CHECK(registry.readHeadOutput(0, 5, readOut));
	CHECK(readOut.cv == doctest::Approx(out.cv));
	CHECK(readOut.timeOut == doctest::Approx(out.timeOut));
	CHECK(readOut.ref == doctest::Approx(out.ref));
	CHECK(readOut.pulse1 == out.pulse1);
	CHECK(readOut.pulse2 == out.pulse2);
	CHECK(readOut.allPulse == out.allPulse);
	CHECK(readOut.eoc == out.eoc);
	CHECK(readOut.currentStage == out.currentStage);
	CHECK(readOut.phase == doctest::Approx(out.phase));
	CHECK(readOut.runState == out.runState);

	// Config, published earlier on the same claimed slot, is untouched.
	HeadConfig readConfig;
	CHECK(registry.readHead(0, 5, readConfig));
	CHECK(readConfig.direction == config.direction);
}

TEST_CASE("An unclaimed head's output reads as HeadOut defaults") {
	MetaModuleHeadRegistry registry;
	HeadOut out;
	out.cv = 9.f;  // pre-dirty
	CHECK_FALSE(registry.readHeadOutput(0, 6, out));
	HeadOut defaults;
	CHECK(out.cv == doctest::Approx(defaults.cv));
	CHECK(out.runState == defaults.runState);
}

TEST_CASE("Duplicate heads' output reports DUP and neither is trusted on read") {
	MetaModuleHeadRegistry registry;
	uint32_t first = registry.makeToken();
	uint32_t second = registry.makeToken();
	CHECK(registry.registerHead(0, 7, first));
	CHECK_FALSE(registry.registerHead(0, 7, second));

	HeadOut out;
	out.cv = 4.f;
	registry.publishHeadOutput(0, 7, out);

	HeadOut read;
	CHECK_FALSE(registry.readHeadOutput(0, 7, read));
	CHECK(read.cv == doctest::Approx(0.f));

	registry.unregisterHead(0, 7, second);
	CHECK(registry.readHeadOutput(0, 7, read));
	CHECK(read.cv == doctest::Approx(4.f));
}

// The actual hypothesis EB8 stands or falls on: a HeadDSP driven entirely by
// registry-sourced (table published by "Core", config published by the
// HeadRemote itself) inputs behaves identically, tick for tick, to the same
// HeadDSP driven directly by the same table/config objects -- i.e. relocating
// a head out of Core costs nothing in behavior, only in bus overhead. Runs a
// real start-and-advance sequence, not a static snapshot, so slew/phase/
// stage-entry state (all of which is internal to HeadDSP, not part of the
// published/read payloads) has real work to disagree over if the round trip
// ever lost precision.
TEST_CASE("A head computed from registry-sourced table and config matches direct computation") {
	StageTable table;
	table.count = 8;
	for (int i = 0; i < table.count; i++) {
		table.voltage[i] = (float)(i + 1) * 1.25f;
		table.time[i] = 0.02f;  // short intervals so the sequence advances fast
	}
	table.program[0].setPulse1(true);

	HeadConfig cfg;
	cfg.continuous = false;
	cfg.direction = 0;  // forward
	cfg.clkExt = true;  // listen to in.extClock, not internal per-stage timing
	cfg.clkDivIndex = 4;  // x1
	cfg.loopMode = 1;  // LOOP_FIRST_LAST

	ExtInputs ext;
	Globals globals;
	ScaleKey scaleKey;

	// Round-trip both inputs through the EB8 registries exactly as a real
	// Core (table) and HeadRemote (config, published about itself) would.
	MetaModuleStageTableRegistry tableRegistry;
	tableRegistry.publishTable(0, table);
	StageTable registryTable;
	CHECK(tableRegistry.readTable(0, /*coreValid=*/true, registryTable));

	MetaModuleHeadRegistry headRegistry;
	uint32_t token = headRegistry.makeToken();
	CHECK(headRegistry.registerHead(0, 0, token));
	headRegistry.publishHead(0, 0, cfg);
	HeadConfig registryConfig;
	CHECK(headRegistry.readHead(0, 0, registryConfig));

	HeadDSP direct;
	HeadDSP viaRegistry;
	HeadOut directOut, registryOut;

	auto step = [&](float startGate, float clockGate) {
		HeadSignals in;
		in.start = startGate;
		in.extClock = clockGate;
		direct.tick(table, ext, globals, scaleKey, cfg, in, 1.f / 48000.f, directOut);
		viaRegistry.tick(registryTable, ext, globals, scaleKey, registryConfig, in,
			1.f / 48000.f, registryOut);
	};

	step(10.f, 0.f);   // start
	for (int tick = 0; tick < 6; tick++) {
		step(0.f, 10.f);   // clock edge
		step(0.f, 0.f);    // release, matches the edge() rising-edge discipline
		CHECK(directOut.currentStage == registryOut.currentStage);
		CHECK(directOut.cv == doctest::Approx(registryOut.cv));
		CHECK(directOut.runState == registryOut.runState);
	}

	CHECK(directOut.cv == doctest::Approx(registryOut.cv));
	CHECK(directOut.timeOut == doctest::Approx(registryOut.timeOut));
	CHECK(directOut.ref == doctest::Approx(registryOut.ref));
	CHECK(directOut.pulse1 == registryOut.pulse1);
	CHECK(directOut.pulse2 == registryOut.pulse2);
	CHECK(directOut.allPulse == registryOut.allPulse);
	CHECK(directOut.currentStage == registryOut.currentStage);
	CHECK(directOut.phase == doctest::Approx(registryOut.phase));
	CHECK(directOut.runState == registryOut.runState);

	// Close the loop: the HeadRemote publishes what it computed, and
	// whatever reads it back (Core, for detected-count/ack purposes, or a
	// future monitor) sees exactly that, matching the direct computation too.
	headRegistry.publishHeadOutput(0, 0, registryOut);
	HeadOut ack;
	CHECK(headRegistry.readHeadOutput(0, 0, ack));
	CHECK(ack.cv == doctest::Approx(directOut.cv));
	CHECK(ack.currentStage == directOut.currentStage);
	CHECK(headRegistry.headOutputHeartbeat(0, 0) == 1);
}

// ---- EB8 extension: per-head MIDI CC forwarding (Core -> HeadRemote) ------
// Mirrors VCV's Midi.cpp -> AnchorToHeadsMsg -> Head.cpp path (headCcSeq/
// headCcValue, addressed by headId) over the MetaModule bus instead of an
// expander pointer. See MetaModuleHeadMidiRegistry's header comment for why
// this deliberately carries only the MIDI subset, not the whole
// AnchorToHeadsMsg (the table already has its own channel).

TEST_CASE("Head MIDI snapshot publish/read round-trips all heads and CCs") {
	MetaModuleHeadMidiRegistry registry;
	MetaModuleHeadMidiSnapshot snapshot;
	snapshot.midiClockSeq = 5;
	snapshot.midiStartSeq = 2;
	snapshot.midiStopSeq = 1;
	snapshot.midiContinueSeq = 3;
	for (int h = 0; h < kMaxHeads; h++) {
		for (int c = 0; c < kMidiHeadControls; c++) {
			snapshot.headCcSeq[h][c] = (uint32_t)(h * kMidiHeadControls + c + 1);
			snapshot.headCcValue[h][c] = (float)h + (float)c * 0.01f;
		}
	}
	registry.publishMidi(1, snapshot);

	MetaModuleHeadMidiSnapshot read;
	CHECK(registry.readMidi(1, /*coreValid=*/true, read));
	CHECK(read.midiClockSeq == snapshot.midiClockSeq);
	CHECK(read.midiStartSeq == snapshot.midiStartSeq);
	CHECK(read.midiStopSeq == snapshot.midiStopSeq);
	CHECK(read.midiContinueSeq == snapshot.midiContinueSeq);
	for (int h = 0; h < kMaxHeads; h++) {
		for (int c = 0; c < kMidiHeadControls; c++) {
			CHECK(read.headCcSeq[h][c] == snapshot.headCcSeq[h][c]);
			CHECK(read.headCcValue[h][c] == doctest::Approx(snapshot.headCcValue[h][c]));
		}
	}
}

TEST_CASE("Head MIDI snapshot read is gated on caller-supplied core validity and heartbeat") {
	MetaModuleHeadMidiRegistry registry;
	CHECK(registry.midiHeartbeat(2) == 0);
	MetaModuleHeadMidiSnapshot out;
	CHECK_FALSE(registry.readMidi(2, /*coreValid=*/true, out));  // never published

	MetaModuleHeadMidiSnapshot snapshot;
	snapshot.midiClockSeq = 7;
	registry.publishMidi(2, snapshot);
	CHECK(registry.midiHeartbeat(2) == 1);

	MetaModuleHeadMidiSnapshot dirty;
	dirty.midiClockSeq = 99;
	CHECK_FALSE(registry.readMidi(2, /*coreValid=*/false, dirty));
	CHECK(dirty.midiClockSeq == MetaModuleHeadMidiSnapshot().midiClockSeq);  // reset to default

	CHECK(registry.readMidi(2, /*coreValid=*/true, dirty));
	CHECK(dirty.midiClockSeq == 7);
}

// The parity claim this needs to earn: whatever real Core.cpp will one day
// publish (MidiCore's actual state, not a hand-built stand-in) survives the
// bus round trip exactly. Drives a real SpaceTimeEngine through
// engine.handleMidi() -- the same call Core.cpp makes -- so the CC seq/value
// data is genuinely what MidiCore produces, not a guess at its shape.
TEST_CASE("A real MidiCore's per-head CC state survives the bus round trip exactly") {
	SpaceTimeEngine engine;
	// Head 3 (channel 0xB3): address knob (CC 5) and direction (CC 8).
	engine.handleMidi(0xB3, 5, 100);
	engine.handleMidi(0xB3, 8, 2);
	// Head 0 (channel 0xB0): virtual clock select (CC 9) then a Start (CC 1).
	engine.handleMidi(0xB0, 9, 127);
	engine.handleMidi(0xB0, 1, 127);
	// Global transport: MIDI clock edge and Start.
	engine.handleMidi(0xF8);
	engine.handleMidi(0xFA);

	AnchorToHeadsMsg wire;
	engine.midi().injectMidi(wire);

	MetaModuleHeadMidiSnapshot snapshot;
	snapshot.midiClockSeq = wire.midiClockSeq;
	snapshot.midiStartSeq = wire.midiStartSeq;
	snapshot.midiStopSeq = wire.midiStopSeq;
	snapshot.midiContinueSeq = wire.midiContinueSeq;
	for (int h = 0; h < kMaxHeads; h++) {
		for (int c = 0; c < kMidiHeadControls; c++) {
			snapshot.headCcSeq[h][c] = wire.headCcSeq[h][c];
			snapshot.headCcValue[h][c] = wire.headCcValue[h][c];
		}
	}

	MetaModuleHeadMidiRegistry registry;
	registry.publishMidi(3, snapshot);
	MetaModuleHeadMidiSnapshot read;
	CHECK(registry.readMidi(3, /*coreValid=*/true, read));

	CHECK(read.midiClockSeq == engine.midi().midiClockSeq);
	CHECK(read.midiStartSeq == engine.midi().midiStartSeq);
	CHECK(read.midiStopSeq == engine.midi().midiStopSeq);
	CHECK(read.midiContinueSeq == engine.midi().midiContinueSeq);
	CHECK(read.headCcSeq[3][5] == engine.midi().headCcSeq[3][5]);
	CHECK(read.headCcValue[3][5] == doctest::Approx(engine.midi().headCcValue[3][5]));
	CHECK(read.headCcSeq[3][8] == engine.midi().headCcSeq[3][8]);
	CHECK(read.headCcValue[3][8] == doctest::Approx(engine.midi().headCcValue[3][8]));
	CHECK(read.headCcSeq[0][9] == engine.midi().headCcSeq[0][9]);
	CHECK(read.headCcSeq[0][1] == engine.midi().headCcSeq[0][1]);
	CHECK(read.midiClockSeq > 0);
	CHECK(read.midiStartSeq > 0);
}

// ---- MIDI activity/channel status (Core -> MidiMonitor) --------------------

TEST_CASE("MetaModuleMidiStatusRegistry round-trips a snapshot losslessly") {
	MetaModuleMidiStatusRegistry registry;
	MetaModuleMidiStatusSnapshot snapshot;
	snapshot.inSeq = 42;
	snapshot.clkSeq = 7;
	snapshot.outSeq = 3;
	snapshot.controlChannel = 16 - 1;
	snapshot.sliderChannel = 15 - 1;
	snapshot.lastStatus = 0xB4;
	snapshot.lastChannel = 4;
	snapshot.lastNumber = 9;
	snapshot.lastValue = 127;
	snapshot.lastRoute = MIDI_ROUTE_HEAD;

	registry.publish(1, snapshot);
	MetaModuleMidiStatusSnapshot read;
	CHECK(registry.read(1, /*coreValid=*/true, read));
	CHECK(read.inSeq == 42);
	CHECK(read.clkSeq == 7);
	CHECK(read.outSeq == 3);
	CHECK(read.controlChannel == 15);
	CHECK(read.sliderChannel == 14);
	CHECK(read.lastStatus == 0xB4);
	CHECK(read.lastChannel == 4);
	CHECK(read.lastNumber == 9);
	CHECK(read.lastValue == 127);
	CHECK(read.lastRoute == (uint8_t)MIDI_ROUTE_HEAD);
}

TEST_CASE("MetaModuleMidiStatusRegistry preserves -1 sentinels (no MIDI seen yet)") {
	MetaModuleMidiStatusRegistry registry;
	MetaModuleMidiStatusSnapshot snapshot;  // defaults: lastStatus/Channel/Number/Value = -1
	registry.publish(2, snapshot);
	MetaModuleMidiStatusSnapshot read;
	CHECK(registry.read(2, /*coreValid=*/true, read));
	CHECK(read.lastStatus == -1);
	CHECK(read.lastChannel == -1);
	CHECK(read.lastNumber == -1);
	CHECK(read.lastValue == -1);
}

TEST_CASE("MetaModuleMidiStatusRegistry gates on coreValid and heartbeat, same as the other EB8 buses") {
	MetaModuleMidiStatusRegistry registry;
	MetaModuleMidiStatusSnapshot never;
	CHECK_FALSE(registry.read(3, /*coreValid=*/true, never));  // never published

	MetaModuleMidiStatusSnapshot snapshot;
	snapshot.inSeq = 5;
	registry.publish(3, snapshot);
	CHECK_FALSE(registry.read(3, /*coreValid=*/false, never));  // no valid Core

	MetaModuleMidiStatusSnapshot read;
	CHECK(registry.read(3, /*coreValid=*/true, read));
	CHECK(read.inSeq == 5);
}

// Parity claim: a real MidiCore's own last-event/channel state, as Core.cpp
// will read it, survives the bus round trip exactly.
TEST_CASE("A real MidiCore's status fields survive the MetaModuleMidiStatusRegistry round trip") {
	SpaceTimeEngine engine;
	engine.handleMidi(0xB0, 9, 127);  // head 0, CC 9 (clock source)

	MetaModuleMidiStatusSnapshot snapshot;
	snapshot.controlChannel = (uint8_t)engine.midi().controlChannel;
	snapshot.sliderChannel = (uint8_t)engine.midi().sliderChannel;
	snapshot.lastStatus = engine.midi().lastStatus;
	snapshot.lastChannel = engine.midi().lastChannel;
	snapshot.lastNumber = engine.midi().lastNumber;
	snapshot.lastValue = engine.midi().lastValue;
	snapshot.lastRoute = engine.midi().lastRoute;

	MetaModuleMidiStatusRegistry registry;
	registry.publish(0, snapshot);
	MetaModuleMidiStatusSnapshot read;
	CHECK(registry.read(0, /*coreValid=*/true, read));
	CHECK(read.lastStatus == engine.midi().lastStatus);
	CHECK(read.lastChannel == engine.midi().lastChannel);
	CHECK(read.lastNumber == engine.midi().lastNumber);
	CHECK(read.lastValue == engine.midi().lastValue);
	CHECK(read.lastRoute == engine.midi().lastRoute);
	CHECK(read.lastRoute == (uint8_t)MIDI_ROUTE_HEAD);
}
