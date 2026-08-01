#include "doctest.h"
#include "MidiCore.hpp"

#include <vector>
#include <string>

using namespace spacetime;

struct SentMidi {
	uint8_t status, channel, data1, data2;
};

struct TestMidiSink : MidiOutputSink {
	std::vector<SentMidi> sent;

	void send(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) override {
		SentMidi message = {status, channel, data1, data2};
		sent.push_back(message);
	}
};

TEST_CASE("MIDI core routes PROGRAM CCs and new global controls") {
	MidiCore core;
	core.handleMessage(0xBE, 0, 127);
	core.handleMessage(0xBF, 68, 127);
	core.handleMessage(0xBF, 89, 127);
	core.handleMessage(0xBF, 90, 64);
	core.handleMessage(0xBF, 91, 0);

	REQUIRE(core.programEventCount == 5);
	CHECK(core.programEvents[0].type == MIDI_PROG_SLIDER);
	CHECK(core.programEvents[0].index == 0);
	CHECK(core.programEvents[0].fvalue == doctest::Approx(10.f));
	CHECK((core.programEvents[0].flags & EDIT_OP_MOVE_SLIDER) != 0);
	CHECK(core.programEvents[1].type == MIDI_PROG_GESTURE);
	CHECK(core.programEvents[2].type == MIDI_PROG_SET_KEY);
	CHECK(core.programEvents[2].index == 11);
	CHECK(core.programEvents[3].type == MIDI_PROG_SET_SCALE);
	CHECK(core.programEvents[3].index == 1);
	CHECK(core.programEvents[4].type == MIDI_PROG_SET_PULSE_RETRIG);
	CHECK(core.programEvents[4].index == 0);
	CHECK(core.lastRoute == MIDI_ROUTE_PROGRAM);
}

TEST_CASE("MIDI stage slider visibility option controls edit flags") {
	MidiCore core;
	core.moveStageSliders = false;
	core.handleMessage(0xBE, 0, 64);
	REQUIRE(core.programEventCount == 1);
	CHECK(core.programEvents[0].flags == EDIT_OP_NONE);

	core.moveStageSliders = true;
	core.handleMessage(0xBE, 64, 64);
	REQUIRE(core.programEventCount == 2);
	CHECK((core.programEvents[1].flags & EDIT_OP_MOVE_SLIDER) != 0);
}

TEST_CASE("MIDI slider channel spans all 64 voltage and time stages") {
	MidiCore core;
	CHECK(core.sliderChannel == 14);
	CHECK(core.controlChannel == 15);
	core.handleMessage(0xBE, 0, 127);
	core.handleMessage(0xBE, 63, 64);
	core.handleMessage(0xBE, 64, 32);
	core.handleMessage(0xBE, 127, 127);

	REQUIRE(core.programEventCount == 4);
	CHECK(core.programEvents[0].index == 0);
	CHECK(core.programEvents[0].fvalue == doctest::Approx(10.f));
	CHECK(core.programEvents[1].index == 63);
	CHECK(core.programEvents[1].fvalue == doctest::Approx(64.f / 127.f * 10.f));
	CHECK(core.programEvents[2].index == 64);
	CHECK(core.programEvents[2].fvalue == doctest::Approx(32.f / 127.f));
	CHECK(core.programEvents[3].index == 127);
	CHECK(core.programEvents[3].fvalue == doctest::Approx(1.f));
	CHECK(core.lastRoute == MIDI_ROUTE_STAGE);
}

TEST_CASE("MIDI PROGRAM and slider channels keep overlapping CCs separate") {
	MidiCore core;
	core.handleMessage(0xBE, 68, 127);
	core.handleMessage(0xBF, 68, 127);
	REQUIRE(core.programEventCount == 2);
	CHECK(core.programEvents[0].type == MIDI_PROG_SLIDER);
	CHECK(core.programEvents[0].index == 68);
	CHECK(core.programEvents[1].type == MIDI_PROG_GESTURE);
}

TEST_CASE("MIDI core routes per-head controls and realtime counters") {
	MidiCore core;
	core.handleMessage(0xB1, 2, 127);
	core.handleMessage(0xB1, 8, 95);
	core.handleMessage(0xF8);
	core.handleMessage(0xFA);
	core.handleMessage(0xFB);
	core.handleMessage(0xFC);

	AnchorToHeadsMsg message;
	core.injectMidi(message);
	CHECK(message.headCcSeq[1][2] == 1);
	CHECK(message.headCcValue[1][2] == doctest::Approx(1.f));
	CHECK(message.headCcValue[1][8] == doctest::Approx(3.f));
	CHECK(message.midiClockSeq == 1);
	CHECK(message.midiStartSeq == 1);
	CHECK(message.midiContinueSeq == 1);
	CHECK(message.midiStopSeq == 1);
	CHECK(core.lastRoute == MIDI_ROUTE_STOP);
}

TEST_CASE("MIDI channel 9 applies CC 0-12 to every head but excludes Display") {
	MidiCore core;
	core.handleMessage(0xB8, 9, 127);
	for (int head = 0; head < kMaxHeads; head++) {
		CHECK(core.headCcSeq[head][9] == 1);
		CHECK(core.headCcValue[head][9] == doctest::Approx(3.f));
	}
	CHECK(core.headAllCcSeq[9] == 1);
	CHECK(core.headAllCcValue[9] == doctest::Approx(3.f));
	AnchorToHeadsMsg allMessage;
	core.injectMidi(allMessage);
	CHECK(allMessage.headAllCcSeq[9] == 1);
	CHECK(allMessage.headAllCcValue[9] == doctest::Approx(3.f));
	CHECK(core.lastRoute == MIDI_ROUTE_HEAD_ALL);
	CHECK(std::string(MidiCore::routeName(core.lastRoute)) == "head all");

	core.handleMessage(0xB8, 13, 127);
	for (int head = 0; head < kMaxHeads; head++)
		CHECK(core.headCcSeq[head][13] == 0);
	CHECK(core.headAllCcSeq[12] == 0);
	CHECK(core.lastRoute == MIDI_ROUTE_HEAD_ALL);

	core.handleMessage(0xB8, 1, 0);
	for (int head = 0; head < kMaxHeads; head++)
		CHECK(core.headCcSeq[head][1] == 0);
	CHECK(core.headAllCcSeq[1] == 0);
	core.handleMessage(0xB8, 1, 127);
	for (int head = 0; head < kMaxHeads; head++)
		CHECK(core.headCcSeq[head][1] == 1);
	CHECK(core.headAllCcSeq[1] == 1);
}

TEST_CASE("MIDI core preserves command release and Program Change behavior") {
	MidiCore core;
	core.handleMessage(0xB0, 1, 0);
	CHECK(core.headCcSeq[0][1] == 0);
	core.handleMessage(0xB0, 1, 127);
	CHECK(core.headCcSeq[0][1] == 1);
	core.handleMessage(0xB0, 1, 0);
	CHECK(core.headCcSeq[0][1] == 1);

	core.handleMessage(0xCF, 12, 0);
	REQUIRE(core.programEventCount == 1);
	CHECK(core.programEvents[0].type == MIDI_PROG_PRESET_LOAD);
	CHECK(core.programEvents[0].index == 11);
	core.handleMessage(0xCF, 13, 0);
	CHECK(core.programEventCount == 1);
}

TEST_CASE("MIDI key map spans all twelve equal bins") {
	static const uint8_t values[12] = {
		0, 12, 23, 35, 46, 58, 69, 81, 92, 104, 115, 127
	};
	for (int key = 0; key < 12; key++)
		CHECK(MidiCore::scaleIndex(values[key], 11) == key);
}

TEST_CASE("MIDI core emits quantized notes and gate-fall note off") {
	MidiCore core;
	TestMidiSink sink;
	core.outLane[0].mode = MIDI_OUT_NOTES;
	core.outLane[0].gateSource = MIDI_GATE_P1;

	HeadsToAnchorMsg status;
	status.headCount = 1;
	status.status[0].headId = 0;
	status.status[0].runState = RUN_RUNNING;
	status.status[0].quantized = 1;
	status.status[0].cv = 1.f;
	status.status[0].pulse1 = 1;
	core.processOutput(status, 0.01f, sink);

	REQUIRE(sink.sent.size() == 1);
	CHECK(sink.sent[0].status == 0x9);
	CHECK(sink.sent[0].channel == 0);
	CHECK(sink.sent[0].data1 == 72);
	CHECK(sink.sent[0].data2 == 100);

	status.status[0].pulse1 = 0;
	core.processOutput(status, 0.01f, sink);
	REQUIRE(sink.sent.size() == 2);
	CHECK(sink.sent[1].status == 0x8);
	CHECK(sink.sent[1].data1 == 72);
}

TEST_CASE("disabled MIDI output is idle but still clears an active note") {
	MidiCore core;
	TestMidiSink sink;
	CHECK_FALSE(core.outputRequiresService());
	core.outLane[0].mode = MIDI_OUT_NOTES;
	core.outLane[0].gateSource = MIDI_GATE_P1;
	CHECK(core.outputRequiresService());

	HeadsToAnchorMsg status;
	status.headCount = 1;
	status.status[0].headId = 0;
	status.status[0].runState = RUN_RUNNING;
	status.status[0].quantized = 1;
	status.status[0].pulse1 = 1;
	core.processOutput(status, 0.001f, sink);
	REQUIRE(sink.sent.size() == 1);

	core.outLane[0].mode = MIDI_OUT_OFF;
	CHECK(core.outputRequiresService());
	core.processOutput(status, 0.001f, sink);
	REQUIRE(sink.sent.size() == 2);
	CHECK(sink.sent[1].status == 0x8);
	CHECK_FALSE(core.outputRequiresService());
}

TEST_CASE("MIDI core emits throttled 7-bit CC from head CV") {
	MidiCore core;
	TestMidiSink sink;
	core.outLane[0].mode = MIDI_OUT_CC7;
	core.outLane[0].channel = 4;
	core.outLane[0].cc = 23;

	HeadsToAnchorMsg status;
	status.headCount = 1;
	status.status[0].headId = 0;
	status.status[0].cv = 10.f;
	core.processOutput(status, 0.003f, sink);

	REQUIRE(sink.sent.size() == 1);
	CHECK(sink.sent[0].status == 0xB);
	CHECK(sink.sent[0].channel == 4);
	CHECK(sink.sent[0].data1 == 23);
	CHECK(sink.sent[0].data2 == 127);

	status.status[0].cv = 0.f;
	core.processOutput(status, 0.001f, sink);
	CHECK(sink.sent.size() == 1);
	core.processOutput(status, 0.002f, sink);
	REQUIRE(sink.sent.size() == 2);
	CHECK(sink.sent[1].data2 == 0);
}

TEST_CASE("MIDI core suppresses unquantized notes and times ALL notes") {
	MidiCore core;
	TestMidiSink sink;
	core.outLane[0].mode = MIDI_OUT_NOTES;
	core.outLane[0].gateSource = MIDI_GATE_ALL;

	HeadsToAnchorMsg status;
	status.headCount = 1;
	status.status[0].headId = 0;
	status.status[0].runState = RUN_RUNNING;
	status.status[0].allPulse = 1;
	status.status[0].cv = 0.f;
	status.status[0].quantized = 0;
	core.processOutput(status, 0.001f, sink);
	CHECK(sink.sent.empty());

	status.status[0].allPulse = 0;
	core.processOutput(status, 0.001f, sink);
	status.status[0].allPulse = 1;
	status.status[0].quantized = 1;
	core.processOutput(status, 0.001f, sink);
	REQUIRE(sink.sent.size() == 1);
	CHECK(sink.sent[0].status == 0x9);

	status.status[0].allPulse = 0;
	core.processOutput(status, 0.028f, sink);
	CHECK(sink.sent.size() == 1);
	core.processOutput(status, 0.002f, sink);
	REQUIRE(sink.sent.size() == 2);
	CHECK(sink.sent[1].status == 0x8);
}
