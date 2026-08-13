// SpaceTime -- HeadRemoteController tests (EB8: head relocation).
// Two things to prove: (1) standalone behavior is sane on its own, and
// (2) it produces byte-identical results to SpaceTimeEngine's own internal
// per-head logic for the same MIDI stream -- the parity claim this class
// exists to earn, since it is a deliberate duplication of already-shipped
// logic (see HeadRemoteController.hpp's header comment for why).
#include "doctest.h"
#include "HeadRemoteController.hpp"
#include "SpaceTimeEngine.hpp"

using namespace spacetime;

TEST_CASE("HeadRemoteController runs a start-and-advance sequence from MIDI CC alone") {
	HeadRemoteController controller;
	StageTable table;
	table.count = 4;
	for (int i = 0; i < table.count; i++) {
		table.voltage[i] = (float)(i + 1) * 2.f;
		table.time[i] = 0.02f;
	}
	ExtInputs ext;
	Globals globals;
	ScaleKey scaleKey;
	HeadSignals noLocalSignals;

	// CC 9 = 3 (virtual clock source), CC 1 (Start).
	uint32_t ccSeq[kMidiHeadControls] = {};
	float ccValue[kMidiHeadControls] = {};
	ccSeq[9] = 1; ccValue[9] = 3.f;
	ccSeq[1] = 1; ccValue[1] = 127.f;
	controller.applyMidi(ccSeq, ccValue, 0, 0, 0, 0);
	CHECK(controller.clockSource() == 3);

	HeadOut out;
	controller.tick(table, ext, globals, scaleKey, noLocalSignals, 1.f / 48000.f, out);
	CHECK(out.runState == RUN_RUNNING);

	// One virtual clock CC per stage advance (CC 0, distinct sequence
	// numbers so applyMidi treats each as a new event). Each CC's simulated
	// pulse (pulseGate, ~1 ms) must fully decay -- a real falling edge --
	// before the next CC arrives, or the next trigger never sees a rising
	// edge; same requirement the real MIDI-driven path has, not a test
	// artifact.
	uint32_t stage = 0;
	for (int tick = 1; tick <= 3; tick++) {
		ccSeq[0] = (uint32_t)tick;
		ccValue[0] = 127.f;
		controller.applyMidi(ccSeq, ccValue, 0, 0, 0, 0);
		for (int settle = 0; settle < 60; settle++)
			controller.tick(table, ext, globals, scaleKey, noLocalSignals, 1.f / 48000.f, out);
		CHECK(out.currentStage == (uint8_t)tick);
		stage = out.currentStage;
	}
	CHECK(stage == 3);
	CHECK(out.cv == doctest::Approx(table.voltage[3]));
}

TEST_CASE("HeadRemoteController local jack signals merge with MIDI-driven ones") {
	HeadRemoteController controller;
	StageTable table;
	table.count = 4;
	table.time[0] = 0.02f;
	ExtInputs ext;
	Globals globals;
	ScaleKey scaleKey;

	// clockSource left at default (0, internal) -- external clock is a real
	// local jack signal, not MIDI-driven, so drive it directly.
	HeadSignals local;
	local.start = 10.f;
	HeadOut out;
	controller.tick(table, ext, globals, scaleKey, local, 1.f / 48000.f, out);
	CHECK(out.runState == RUN_RUNNING);
}

TEST_CASE("HeadRemoteController matches SpaceTimeEngine's own per-head behavior exactly") {
	SpaceTimeEngine engine;
	engine.table().count = 6;
	for (int i = 0; i < engine.table().count; i++) {
		engine.table().voltage[i] = (float)(i + 1) * 1.5f;
		engine.table().time[i] = 0.02f;
	}

	// Head 4: virtual clock (CC 9=3), address knob (CC 5), direction (CC 8),
	// then Start (CC 1) and three virtual clock ticks (CC 0).
	engine.handleMidi(0xB4, 9, 3);
	engine.handleMidi(0xB4, 5, 80);
	engine.handleMidi(0xB4, 8, 0);
	engine.handleMidi(0xB4, 1, 127);
	engine.handleMidi(0xF8);   // global MIDI clock edge (not used by this head, exercises the seq path)
	engine.handleMidi(0xFA);   // global MIDI Start (this head doesn't follow transport, no effect)

	HeadRemoteController controller;
	auto syncMidi = [&]() {
		AnchorToHeadsMsg wire;
		engine.midi().injectMidi(wire);
		uint32_t ccSeq[kMidiHeadControls];
		float ccValue[kMidiHeadControls];
		for (int c = 0; c < kMidiHeadControls; c++) {
			ccSeq[c] = wire.headCcSeq[4][c];
			ccValue[c] = wire.headCcValue[4][c];
		}
		controller.applyMidi(ccSeq, ccValue, wire.midiClockSeq, wire.midiStartSeq,
			wire.midiStopSeq, wire.midiContinueSeq);
	};
	syncMidi();
	CHECK(controller.clockSource() == engine.headClockSource(4));

	ExtInputs ext;
	HeadOut engineOut, controllerOut;
	HeadSignals noLocalSignals;
	float dt = 1.f / 48000.f;

	// engine.headConfig(h) only actually gets the pending CC applied inside
	// processHeads() (applyMidiToHeads() is called from there, not from
	// handleMidi() directly) -- so the config comparison has to happen
	// after the engine's own first processHeads(), not before, or it's
	// comparing the controller's already-applied state against the
	// engine's still-pending one.
	engine.processHeads(dt);
	controller.tick(engine.table(), ext, engine.globals(), engine.program().scaleKey(),
		noLocalSignals, dt, controllerOut);
	CHECK(controller.config().addressKnob == doctest::Approx(engine.headConfig(4).addressKnob));
	CHECK(controller.config().direction == engine.headConfig(4).direction);
	engineOut = engine.headOut(4);
	CHECK(engineOut.runState == controllerOut.runState);
	CHECK(engineOut.cv == doctest::Approx(controllerOut.cv));

	for (int tick = 1; tick <= 4; tick++) {
		engine.handleMidi(0xB4, 0, 127);  // virtual clock tick, head 4
		syncMidi();
		engine.processHeads(dt);
		controller.tick(engine.table(), ext, engine.globals(), engine.program().scaleKey(),
			noLocalSignals, dt, controllerOut);
		engineOut = engine.headOut(4);
		CHECK(engineOut.currentStage == controllerOut.currentStage);
		CHECK(engineOut.cv == doctest::Approx(controllerOut.cv));
		CHECK(engineOut.runState == controllerOut.runState);
		CHECK(engineOut.phase == doctest::Approx(controllerOut.phase));

		// Settle both past the short stage interval identically before the
		// next clock tick, same as the EB8 HeadDSP parity test's cadence.
		for (int settle = 0; settle < 50; settle++) {
			engine.processHeads(dt);
			controller.tick(engine.table(), ext, engine.globals(), engine.program().scaleKey(),
				noLocalSignals, dt, controllerOut);
		}
	}

	engineOut = engine.headOut(4);
	CHECK(engineOut.cv == doctest::Approx(controllerOut.cv));
	CHECK(engineOut.timeOut == doctest::Approx(controllerOut.timeOut));
	CHECK(engineOut.pulse1 == controllerOut.pulse1);
	CHECK(engineOut.allPulse == controllerOut.allPulse);
	CHECK(engineOut.currentStage == controllerOut.currentStage);
}

TEST_CASE("HeadRemoteController follow-MIDI-transport matches SpaceTimeEngine for global Start/Stop") {
	SpaceTimeEngine engine;
	engine.table().count = 4;
	engine.table().time[0] = 0.02f;
	engine.setHeadFollowsMidiTransport(2, true);
	engine.setHeadClockSource(2, 3);  // virtual, so it actually runs once started

	HeadRemoteController controller;
	controller.setFollowsMidiTransport(true);
	controller.setClockSource(3);

	engine.handleMidi(0xFA);  // global MIDI Start
	AnchorToHeadsMsg wire;
	engine.midi().injectMidi(wire);
	uint32_t ccSeq[kMidiHeadControls] = {};
	float ccValue[kMidiHeadControls] = {};
	controller.applyMidi(ccSeq, ccValue, wire.midiClockSeq, wire.midiStartSeq,
		wire.midiStopSeq, wire.midiContinueSeq);

	ExtInputs ext;
	Globals globals;
	ScaleKey scaleKey;
	HeadSignals noLocalSignals;
	float dt = 1.f / 48000.f;
	engine.processHeads(dt);
	HeadOut controllerOut;
	controller.tick(engine.table(), ext, globals, scaleKey, noLocalSignals, dt, controllerOut);

	CHECK(engine.headOut(2).runState == RUN_RUNNING);
	CHECK(controllerOut.runState == RUN_RUNNING);
}
