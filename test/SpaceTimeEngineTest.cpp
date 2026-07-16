#include "doctest.h"
#include "SpaceTimeEngine.hpp"

using namespace spacetime;

TEST_CASE("SpaceTime engine owns the full instrument dimensions") {
	SpaceTimeEngine engine;
	CHECK(engine.table().count == 64);
	for (int h = 0; h < kMaxHeads; h++) {
		CHECK(engine.midi().outLane[h].channel == h);
		CHECK(engine.headOut(h).currentStage == 0);
	}
}

TEST_CASE("SpaceTime engine applies all 64 voltage and time slider CCs") {
	SpaceTimeEngine engine;
	engine.handleMidi(0xBE, 0, 127);
	engine.handleMidi(0xBE, 63, 64);
	engine.handleMidi(0xBE, 64, 32);
	engine.handleMidi(0xBE, 127, 127);
	CHECK(engine.table().voltage[0] == doctest::Approx(10.f));
	CHECK(engine.table().voltage[63] == doctest::Approx(64.f / 127.f * 10.f));
	CHECK(engine.table().time[0] == doctest::Approx(32.f / 127.f));
	CHECK(engine.table().time[63] == doctest::Approx(1.f));
}

TEST_CASE("SpaceTime engine keeps Program MIDI channel filtering") {
	SpaceTimeEngine engine;
	engine.handleMidi(0xBF, 65, 127);
	CHECK(engine.program().selectedStage() == 1);
	engine.handleMidi(0xBD, 65, 127);
	CHECK(engine.program().selectedStage() == 1);
	engine.handleMidi(0xBF, 64, 127);
	CHECK(engine.program().selectedStage() == 0);
}

TEST_CASE("SpaceTime engine applies Program gestures and globals") {
	SpaceTimeEngine engine;
	engine.handleMidi(0xBF, 68, 127);
	CHECK(engine.table().program[0].quantize());
	engine.handleMidi(0xBF, 69, 127);
	engine.handleMidi(0xBF, 69, 127);
	CHECK(engine.table().program[0].slew() == SLEW_2);
	engine.handleMidi(0xBF, 72, 127);
	CHECK(engine.table().program[0].range() == RANGE_LIMITED);
	CHECK(engine.table().program[0].limitedOctave() == 0);
	engine.handleMidi(0xBF, 85, 127);
	CHECK(engine.table().program[0].timeRange() == 3);
	engine.handleMidi(0xBF, 89, 127);
	engine.handleMidi(0xBF, 90, 64);
	engine.handleMidi(0xBF, 91, 0);
	CHECK(engine.program().scaleKey().key == 11);
	CHECK(engine.program().scaleKey().scale == 1);
	CHECK_FALSE(engine.globals().pulseRetrig);
}

TEST_CASE("SpaceTime engine bulk arm applies the next Program gesture to 64 stages") {
	SpaceTimeEngine engine;
	engine.handleMidi(0xBF, 67, 127);
	engine.handleMidi(0xBF, 87, 127);
	for (int stage = 0; stage < kMaxStages; stage++)
		CHECK(engine.table().program[stage].pulse1());
	CHECK_FALSE(engine.program().bulkArmed());
}

TEST_CASE("SpaceTime engine presets restore table and musical state through MIDI") {
	SpaceTimeEngine engine;
	engine.table().voltage[0] = 7.5f;
	engine.table().time[63] = 0.125f;
	ScaleKey saved;
	saved.key = 9;
	saved.scale = SCALE_MAJOR;
	engine.program().setScaleKey(saved);
	engine.savePreset(3);
	engine.table().voltage[0] = 1.f;
	engine.table().time[63] = 0.9f;
	engine.program().setScaleKey(ScaleKey());
	engine.handleMidi(0xCF, 4, 0);
	engine.processControl(1.f / 3000.f);
	engine.processControl(1.f / 3000.f);
	CHECK(engine.table().voltage[0] == doctest::Approx(7.5f));
	CHECK(engine.table().time[63] == doctest::Approx(0.125f));
	CHECK(engine.program().scaleKey().key == 9);
	CHECK(engine.program().scaleKey().scale == SCALE_MAJOR);
}

TEST_CASE("SpaceTime engine preserves realtime and all eight head MIDI lanes") {
	SpaceTimeEngine engine;
	engine.handleMidi(0xF8);
	engine.handleMidi(0xFA);
	engine.handleMidi(0xFB);
	engine.handleMidi(0xFC);
	CHECK(engine.midi().midiClockSeq == 1);
	CHECK(engine.midi().midiStartSeq == 1);
	CHECK(engine.midi().midiContinueSeq == 1);
	CHECK(engine.midi().midiStopSeq == 1);
	for (int head = 0; head < kMaxHeads; head++) {
		engine.handleMidi((uint8_t)(0xB0 | head), 9, 127);
		CHECK(engine.midi().headCcSeq[head][9] == 1);
		CHECK(engine.midi().headCcValue[head][9] == doctest::Approx(3.f));
	}
}

TEST_CASE("SpaceTime engine runs all eight HeadDSP instances from one table") {
	SpaceTimeEngine engine;
	engine.table().voltage[0] = 4.f;
	for (int head = 0; head < kMaxHeads; head++)
		engine.headConfig(head).continuous = true;
	engine.processHeads(1.f / 48000.f);
	for (int head = 0; head < kMaxHeads; head++)
		CHECK(engine.headOut(head).cv == doctest::Approx(4.f));
}

TEST_CASE("SpaceTime engine applies per-head MIDI transport and virtual clock") {
	SpaceTimeEngine engine;
	engine.table().program[0].setPulse1(true);
	engine.handleMidi(0xB0, 9, 127);
	engine.handleMidi(0xB0, 1, 127);
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headClockSource(0) == 3);
	CHECK(engine.headOut(0).runState == RUN_RUNNING);
	engine.handleMidi(0xB0, 0, 127);
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headOut(0).currentStage == 1);
	engine.handleMidi(0xB0, 2, 127);
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headOut(0).runState == RUN_STOPPED);
}

TEST_CASE("SpaceTime engine follows global MIDI transport only when enabled") {
	SpaceTimeEngine engine;
	engine.handleMidi(0xFA);
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headOut(0).runState == RUN_STOPPED);
	engine.setHeadFollowsMidiTransport(0, true);
	engine.handleMidi(0xFA);
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headOut(0).runState == RUN_RUNNING);
	engine.handleMidi(0xFC);
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headOut(0).runState == RUN_STOPPED);
}
