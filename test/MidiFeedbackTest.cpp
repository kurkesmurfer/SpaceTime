#include "doctest.h"
#include "MidiFeedback.hpp"

#include <vector>

using namespace spacetime;

struct FeedbackMessage {
	uint8_t status, channel, cc, value;
};

struct TestFeedbackSink : MidiFeedbackSink {
	std::vector<FeedbackMessage> sent;

	void send(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) override {
		FeedbackMessage message = {status, channel, data1, data2};
		sent.push_back(message);
	}
};

static bool hasFeedback(const TestFeedbackSink& sink, int channel, int cc, int value) {
	for (size_t i = 0; i < sink.sent.size(); i++) {
		const FeedbackMessage& message = sink.sent[i];
		if (message.channel == channel && message.cc == cc && message.value == value)
			return true;
	}
	return false;
}

static MidiFeedbackState populatedFeedbackState() {
	MidiFeedbackState state;
	state.table.count = 12;
	state.table.voltage[0] = 10.f;
	state.table.time[0] = 0.5f;
	state.program.selectedStage = 0;
	state.program.scaleKey.key = 7;
	state.program.scaleKey.scale = 1;
	state.program.pulseRetrig = false;
	state.program.bulkArmed = true;
	state.table.program[0].setQuantize(true);
	state.table.program[0].setSlew(2);
	state.table.program[0].setRange(RANGE_LIMITED);
	state.table.program[0].setLimitedOctave(4);
	state.table.program[0].setVoltageSource(true);
	state.table.program[0].setStop(true);
	state.table.program[0].setPulse2(true);

	HeadFeedbackState& head = state.head[0];
	head.present = true;
	head.runState = RUN_RUNNING;
	head.currentStage = 11;
	head.display = true;
	head.address = 5.f;
	head.addressExternal = true;
	head.addressMode = 2;
	head.direction = 3;
	head.clockSource = 2;
	head.clockDivIndex = 6;
	head.timeCvAmount = 1.f;
	head.loopMode = 2;
	head.followMidiTransport = true;
	return state;
}

TEST_CASE("feedback protocol decodes fixed-channel snapshot requests") {
	MidiFeedbackCore core;
	CHECK_FALSE(core.handleMessage(0xB8, 0, 0));
	CHECK(core.handleMessage(0xB9, 3, 127));
	CHECK(core.handleMessage(0xB9, 1, 0));

	MidiFeedbackState state;
	TestFeedbackSink sink;
	core.process(state, 0.f, false, sink);
	REQUIRE(sink.sent.size() == 1);
	CHECK(sink.sent[0].status == 0xB);
	CHECK(sink.sent[0].channel == kFeedbackProtocolChannel);
	CHECK(sink.sent[0].cc == 3);
	CHECK(sink.sent[0].value == kFeedbackProtocolVersion);
}

TEST_CASE("HEAD snapshot is framed and uses exact semantic values") {
	MidiFeedbackCore core;
	MidiFeedbackState state = populatedFeedbackState();
	TestFeedbackSink sink;
	CHECK(core.handleMessage(0xB9, 0, 0));
	core.process(state, 0.f, false, sink);

	REQUIRE(sink.sent.size() == 17);
	CHECK(sink.sent.front().channel == kFeedbackProtocolChannel);
	CHECK(sink.sent.front().cc == 118);
	CHECK(sink.sent.front().value == 0);
	CHECK(sink.sent.back().cc == 119);
	CHECK(sink.sent.back().value == 0);
	CHECK(hasFeedback(sink, 0, 1, 127));
	CHECK(hasFeedback(sink, 0, 2, 0));
	CHECK(hasFeedback(sink, 0, 5, 64));
	CHECK(hasFeedback(sink, 0, 7, 2));
	CHECK(hasFeedback(sink, 0, 9, 2));
	CHECK(hasFeedback(sink, 0, 10, 6));
	CHECK(hasFeedback(sink, 0, 11, 127));
	CHECK(hasFeedback(sink, 0, 15, 11));
	CHECK(hasFeedback(sink, 0, 16, 127));
	CHECK_FALSE(hasFeedback(sink, 0, 0, 127));
	CHECK_FALSE(hasFeedback(sink, 0, 3, 0));
}

TEST_CASE("HEAD ALL snapshot reports all eight channels including absence") {
	MidiFeedbackCore core;
	MidiFeedbackState state = populatedFeedbackState();
	TestFeedbackSink sink;
	CHECK(core.handleMessage(0xB9, 0, 8));
	core.process(state, 0.f, false, sink);

	CHECK(sink.sent.front().cc == 118);
	CHECK(sink.sent.front().value == FEEDBACK_AREA_HEAD_ALL);
	CHECK(sink.sent.back().cc == 119);
	CHECK(sink.sent.back().value == FEEDBACK_AREA_HEAD_ALL);
	CHECK(hasFeedback(sink, 0, 16, 127));
	for (int head = 1; head < kMaxHeads; head++)
		CHECK(hasFeedback(sink, head, 16, 0));
}

TEST_CASE("HEAD deltas group run state and acknowledge advance and reset") {
	MidiFeedbackCore core;
	MidiFeedbackState state = populatedFeedbackState();
	TestFeedbackSink sink;
	core.process(state, 0.f, false, sink);
	CHECK(sink.sent.empty());

	state.head[0].runState = RUN_HOLDING;
	state.head[0].currentStage = 12;
	state.head[0].advanceSeq++;
	state.head[0].resetSeq++;
	core.process(state, 0.f, false, sink);
	CHECK(hasFeedback(sink, 0, 1, 0));
	CHECK(hasFeedback(sink, 0, 2, 0));
	CHECK(hasFeedback(sink, 0, 14, 127));
	CHECK(hasFeedback(sink, 0, 15, 12));
	CHECK(hasFeedback(sink, 0, 3, 127));
	CHECK(hasFeedback(sink, 0, 3, 0));
	CHECK(hasFeedback(sink, 0, 4, 127));
	CHECK(hasFeedback(sink, 0, 4, 0));

	sink.sent.clear();
	state.head[0].present = false;
	core.process(state, 0.f, false, sink);
	REQUIRE(sink.sent.size() == 1);
	CHECK(hasFeedback(sink, 0, 16, 0));
}

TEST_CASE("PROGRAM snapshot reports globals and selected-stage fields") {
	MidiFeedbackCore core;
	MidiFeedbackState state = populatedFeedbackState();
	TestFeedbackSink sink;
	CHECK(core.handleMessage(0xB9, 1, 127));
	core.process(state, 0.f, false, sink);

	REQUIRE(sink.sent.size() == 21);
	CHECK(sink.sent.front().cc == 118);
	CHECK(sink.sent.front().value == FEEDBACK_AREA_PROGRAM);
	CHECK(sink.sent.back().cc == 119);
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 1, 7));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 2, 1));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 3, 0));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 4, 127));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 16, 127));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 17, 2));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 18, 2));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 19, 4));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 20, 127));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 21, 127));
	CHECK(hasFeedback(sink, kFeedbackProgramChannel, 29, 127));
}

TEST_CASE("PROGRAM selection delta refreshes the complete selected-stage bank") {
	MidiFeedbackCore core;
	MidiFeedbackState state = populatedFeedbackState();
	TestFeedbackSink sink;
	core.process(state, 0.f, false, sink);

	state.program.selectedStage = 1;
	core.process(state, 0.f, false, sink);
	REQUIRE(sink.sent.size() == 15);
	CHECK(sink.sent[0].cc == 0);
	CHECK(sink.sent[0].value == 1);
	for (int cc = 16; cc <= 29; cc++)
		CHECK(sink.sent[cc - 15].cc == cc);
}

TEST_CASE("stage page snapshot emits voltage then time and clears absent stages") {
	MidiFeedbackCore core;
	MidiFeedbackState state = populatedFeedbackState();
	TestFeedbackSink sink;
	CHECK(core.handleMessage(0xB9, 2, 1));
	core.process(state, 0.f, false, sink);

	REQUIRE(sink.sent.size() == 18);
	CHECK(sink.sent.front().value == FEEDBACK_AREA_STAGE_PAGE_1 + 1);
	for (int i = 0; i < 8; i++) {
		CHECK(sink.sent[1 + i].cc == 8 + i);
		CHECK(sink.sent[9 + i].cc == 72 + i);
	}
	CHECK(sink.sent[1 + 4].value == 0);
	CHECK(sink.sent[9 + 4].value == 0);
}

TEST_CASE("live stage deltas are opt-in coalesced and rate-limited") {
	MidiFeedbackCore core;
	MidiFeedbackState state = populatedFeedbackState();
	TestFeedbackSink sink;
	core.process(state, 0.f, false, sink);

	state.table.voltage[0] = 1.f;
	core.process(state, 0.003f, false, sink);
	CHECK(sink.sent.empty());

	state.table.voltage[0] = 2.f;
	state.table.time[0] = 1.f;
	core.process(state, 0.002f, true, sink);
	CHECK(sink.sent.empty());
	state.table.voltage[0] = 3.f;
	core.process(state, 0.001f, true, sink);
	REQUIRE(sink.sent.size() == 1);
	CHECK(sink.sent[0].channel == kFeedbackStageChannel);
	CHECK(sink.sent[0].cc == 0);
	CHECK(sink.sent[0].value == 38);

	sink.sent.clear();
	core.process(state, 0.003f, true, sink);
	REQUIRE(sink.sent.size() == 1);
	CHECK(sink.sent[0].cc == 64);
	CHECK(sink.sent[0].value == 127);
}
