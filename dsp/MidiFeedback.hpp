#pragma once

// Platform-neutral MIDI controller feedback engine.
//
// Adapters translate host state into MidiFeedbackState and provide MIDI I/O.
// This core owns protocol-v1 request decoding, snapshot framing, semantic
// change detection, acknowledgement pulses, and coalesced stage deltas.

#include <cmath>
#include <cstdint>
#include "Chain.hpp"

namespace spacetime {

static const uint8_t kFeedbackProtocolVersion = 1;
static const uint8_t kFeedbackProtocolChannel = 9;  // displayed MIDI channel 10
static const uint8_t kFeedbackStageChannel = 14;    // displayed MIDI channel 15
static const uint8_t kFeedbackProgramChannel = 15;  // displayed MIDI channel 16
static const float kFeedbackStageInterval = 0.003f;

enum FeedbackArea {
	FEEDBACK_AREA_HEAD_ALL = 8,
	FEEDBACK_AREA_PROGRAM = 16,
	FEEDBACK_AREA_STAGE_PAGE_1 = 32
};

struct MidiFeedbackSink {
	virtual ~MidiFeedbackSink() {}
	virtual void send(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) = 0;
};

struct HeadFeedbackState {
	bool present;
	uint8_t runState;
	uint8_t currentStage;
	bool display;
	float address;
	bool addressExternal;
	uint8_t addressMode;
	uint8_t direction;
	uint8_t clockSource;
	uint8_t clockDivIndex;
	float timeCvAmount;
	uint8_t loopMode;
	bool followMidiTransport;
	uint32_t advanceSeq;
	uint32_t resetSeq;

	HeadFeedbackState()
		: present(false), runState(RUN_STOPPED), currentStage(0), display(false),
		  address(0.f), addressExternal(false), addressMode(1), direction(0),
		  clockSource(0), clockDivIndex(4), timeCvAmount(0.f), loopMode(1),
		  followMidiTransport(false), advanceSeq(0), resetSeq(0) {}
};

struct ProgramFeedbackState {
	uint8_t selectedStage;
	ScaleKey scaleKey;
	bool pulseRetrig;
	bool bulkArmed;

	ProgramFeedbackState()
		: selectedStage(0), pulseRetrig(true), bulkArmed(false) {}
};

struct MidiFeedbackState {
	HeadFeedbackState head[kMaxHeads];
	ProgramFeedbackState program;
	StageTable table;
};

class MidiFeedbackCore {
public:
	MidiFeedbackCore() { reset(); }

	void reset() {
		initialized_ = false;
		pendingHeadMask_ = 0;
		pendingAllHeads_ = false;
		pendingProgram_ = false;
		pendingStageMask_ = 0;
		pendingVersion_ = false;
		stageTimer_ = 0.f;
		stageCursor_ = 0;
		for (int h = 0; h < kMaxHeads; h++) {
			lastAdvanceSeq_[h] = 0;
			lastResetSeq_[h] = 0;
			for (int cc = 0; cc < kHeadValueCount; cc++)
				headCache_[h][cc] = 0;
		}
		for (int cc = 0; cc < kProgramValueCount; cc++)
			programCache_[cc] = 0;
		for (int cc = 0; cc < kStageValueCount; cc++) {
			stageCache_[cc] = 0;
			stagePending_[cc] = false;
			stagePendingValue_[cc] = 0;
		}
	}

	// Returns true for protocol-channel messages, including harmless release
	// values. The caller may use this to keep requests out of normal CC routing.
	bool handleMessage(uint8_t statusByte, uint8_t data1, uint8_t data2) {
		if (((statusByte >> 4) & 0xF) != 0xB || (statusByte & 0xF) != kFeedbackProtocolChannel)
			return false;
		data1 &= 0x7F;
		data2 &= 0x7F;
		switch (data1) {
			case 0:
				if (data2 < kMaxHeads)
					pendingHeadMask_ |= (uint8_t)(1u << data2);
				else if (data2 == kMaxHeads)
					pendingAllHeads_ = true;
				break;
			case 1:
				if (data2 >= 64)
					pendingProgram_ = true;
				break;
			case 2:
				if (data2 < 8)
					pendingStageMask_ |= (uint8_t)(1u << data2);
				break;
			case 3:
				if (data2 >= 64)
					pendingVersion_ = true;
				break;
			default:
				break;
		}
		return true;
	}

	void process(const MidiFeedbackState& state, float dt, bool liveStageDeltas,
	             MidiFeedbackSink& sink) {
		if (!initialized_)
			seedCaches(state);

		if (pendingVersion_) {
			sendCc(sink, kFeedbackProtocolChannel, 3, kFeedbackProtocolVersion);
			pendingVersion_ = false;
		}

		uint8_t snappedHeads = 0;
		if (pendingAllHeads_) {
			sendFrame(sink, 118, FEEDBACK_AREA_HEAD_ALL);
			for (int h = 0; h < kMaxHeads; h++) {
				sendHeadSnapshot(state.head[h], h, sink);
				cacheHead(state.head[h], h);
				snappedHeads |= (uint8_t)(1u << h);
			}
			sendFrame(sink, 119, FEEDBACK_AREA_HEAD_ALL);
			pendingAllHeads_ = false;
			pendingHeadMask_ = 0;
		}
		else {
			for (int h = 0; h < kMaxHeads; h++) {
				if (!(pendingHeadMask_ & (1u << h)))
					continue;
				sendFrame(sink, 118, (uint8_t)h);
				sendHeadSnapshot(state.head[h], h, sink);
				sendFrame(sink, 119, (uint8_t)h);
				cacheHead(state.head[h], h);
				snappedHeads |= (uint8_t)(1u << h);
			}
			pendingHeadMask_ = 0;
		}

		bool snappedProgram = pendingProgram_;
		if (pendingProgram_) {
			sendFrame(sink, 118, FEEDBACK_AREA_PROGRAM);
			sendProgramSnapshot(state, sink);
			sendFrame(sink, 119, FEEDBACK_AREA_PROGRAM);
			cacheProgram(state);
			pendingProgram_ = false;
		}

		uint8_t snappedStagePages = pendingStageMask_;
		for (int page = 0; page < 8; page++) {
			if (!(pendingStageMask_ & (1u << page)))
				continue;
			uint8_t area = (uint8_t)(FEEDBACK_AREA_STAGE_PAGE_1 + page);
			sendFrame(sink, 118, area);
			sendStageSnapshot(state, page, sink);
			sendFrame(sink, 119, area);
		}
		pendingStageMask_ = 0;

		for (int h = 0; h < kMaxHeads; h++) {
			if (!(snappedHeads & (1u << h)))
				emitHeadDeltas(state.head[h], h, sink);
		}
		if (!snappedProgram)
			emitProgramDeltas(state, sink);
		emitStageDeltas(state, dt, liveStageDeltas, snappedStagePages, sink);
	}

private:
	static const int kHeadValueCount = 18;
	static const int kProgramValueCount = 30;
	static const int kStageValueCount = 128;

	bool initialized_;
	uint8_t pendingHeadMask_;
	bool pendingAllHeads_;
	bool pendingProgram_;
	uint8_t pendingStageMask_;
	bool pendingVersion_;
	uint8_t headCache_[kMaxHeads][kHeadValueCount];
	uint32_t lastAdvanceSeq_[kMaxHeads];
	uint32_t lastResetSeq_[kMaxHeads];
	uint8_t programCache_[kProgramValueCount];
	uint8_t stageCache_[kStageValueCount];
	bool stagePending_[kStageValueCount];
	uint8_t stagePendingValue_[kStageValueCount];
	float stageTimer_;
	int stageCursor_;

	static uint8_t clampByte(int value) {
		return (uint8_t)(value < 0 ? 0 : (value > 127 ? 127 : value));
	}

	static uint8_t encodeRange(float value, float low, float high) {
		if (value <= low)
			return 0;
		if (value >= high)
			return 127;
		return clampByte((int)std::floor((value - low) / (high - low) * 127.f + 0.5f));
	}

	static void sendCc(MidiFeedbackSink& sink, uint8_t channel, uint8_t cc, uint8_t value) {
		sink.send(0xB, channel, cc, value);
	}

	static void sendFrame(MidiFeedbackSink& sink, uint8_t cc, uint8_t area) {
		sendCc(sink, kFeedbackProtocolChannel, cc, area);
	}

	static void encodeHead(const HeadFeedbackState& head, uint8_t out[kHeadValueCount],
	                       bool valid[kHeadValueCount]) {
		for (int cc = 0; cc < kHeadValueCount; cc++) {
			out[cc] = 0;
			valid[cc] = false;
		}
		valid[16] = true;
		out[16] = head.present ? 127 : 0;
		if (!head.present)
			return;
		valid[1] = valid[2] = valid[5] = valid[6] = valid[7] = true;
		valid[8] = valid[9] = valid[10] = valid[11] = valid[12] = true;
		valid[13] = valid[14] = valid[15] = valid[17] = true;
		out[1] = head.runState == RUN_RUNNING ? 127 : 0;
		out[2] = head.runState == RUN_STOPPED ? 127 : 0;
		out[5] = encodeRange(head.address, 0.f, 10.f);
		out[6] = head.addressExternal ? 127 : 0;
		out[7] = clampByte(head.addressMode > 2 ? 2 : head.addressMode);
		out[8] = clampByte(head.direction > 4 ? 4 : head.direction);
		out[9] = clampByte(head.clockSource > 3 ? 3 : head.clockSource);
		out[10] = clampByte(head.clockDivIndex > 8 ? 8 : head.clockDivIndex);
		out[11] = encodeRange(head.timeCvAmount, -1.f, 1.f);
		out[12] = clampByte(head.loopMode > 2 ? 2 : head.loopMode);
		out[13] = head.display ? 127 : 0;
		out[14] = head.runState == RUN_HOLDING ? 127 : 0;
		out[15] = clampByte(head.currentStage > 63 ? 63 : head.currentStage);
		out[17] = head.followMidiTransport ? 127 : 0;
	}

	static ProgramWord selectedWord(const MidiFeedbackState& state) {
		if (state.table.count == 0)
			return ProgramWord(0);
		int selected = state.program.selectedStage;
		if (selected >= state.table.count)
			selected = state.table.count - 1;
		return state.table.program[selected];
	}

	static void encodeProgram(const MidiFeedbackState& state,
	                          uint8_t out[kProgramValueCount],
	                          bool valid[kProgramValueCount]) {
		for (int cc = 0; cc < kProgramValueCount; cc++) {
			out[cc] = 0;
			valid[cc] = false;
		}
		for (int cc = 0; cc <= 4; cc++)
			valid[cc] = true;
		for (int cc = 16; cc <= 29; cc++)
			valid[cc] = true;
		out[0] = clampByte(state.program.selectedStage > 63 ? 63 : state.program.selectedStage);
		out[1] = clampByte(state.program.scaleKey.key > 11 ? 11 : state.program.scaleKey.key);
		out[2] = clampByte(state.program.scaleKey.scale > 2 ? 2 : state.program.scaleKey.scale);
		out[3] = state.program.pulseRetrig ? 127 : 0;
		out[4] = state.program.bulkArmed ? 127 : 0;
		ProgramWord word = selectedWord(state);
		out[16] = word.quantize() ? 127 : 0;
		out[17] = word.slew();
		out[18] = word.range();
		out[19] = word.limitedOctave();
		out[20] = word.voltageSource() ? 127 : 0;
		out[21] = word.stop() ? 127 : 0;
		out[22] = word.sustain() ? 127 : 0;
		out[23] = word.enable() ? 127 : 0;
		out[24] = word.first() ? 127 : 0;
		out[25] = word.last() ? 127 : 0;
		out[26] = word.timeRange();
		out[27] = word.timeSource() ? 127 : 0;
		out[28] = word.pulse1() ? 127 : 0;
		out[29] = word.pulse2() ? 127 : 0;
	}

	static uint8_t encodeStage(const MidiFeedbackState& state, int cc) {
		int stage = cc < 64 ? cc : cc - 64;
		if (stage < 0 || stage >= state.table.count)
			return 0;
		return cc < 64 ? encodeRange(state.table.voltage[stage], 0.f, 10.f) :
			encodeRange(state.table.time[stage], 0.f, 1.f);
	}

	void seedCaches(const MidiFeedbackState& state) {
		for (int h = 0; h < kMaxHeads; h++)
			cacheHead(state.head[h], h);
		cacheProgram(state);
		for (int cc = 0; cc < kStageValueCount; cc++)
			stageCache_[cc] = encodeStage(state, cc);
		initialized_ = true;
	}

	void cacheHead(const HeadFeedbackState& head, int index) {
		uint8_t values[kHeadValueCount];
		bool valid[kHeadValueCount];
		encodeHead(head, values, valid);
		for (int cc = 0; cc < kHeadValueCount; cc++)
			headCache_[index][cc] = valid[cc] ? values[cc] : 0;
		lastAdvanceSeq_[index] = head.advanceSeq;
		lastResetSeq_[index] = head.resetSeq;
	}

	void cacheProgram(const MidiFeedbackState& state) {
		uint8_t values[kProgramValueCount];
		bool valid[kProgramValueCount];
		encodeProgram(state, values, valid);
		for (int cc = 0; cc < kProgramValueCount; cc++)
			programCache_[cc] = valid[cc] ? values[cc] : 0;
	}

	void sendHeadSnapshot(const HeadFeedbackState& head, int index, MidiFeedbackSink& sink) {
		uint8_t values[kHeadValueCount];
		bool valid[kHeadValueCount];
		encodeHead(head, values, valid);
		for (int cc = 0; cc < kHeadValueCount; cc++) {
			if (valid[cc])
				sendCc(sink, (uint8_t)index, (uint8_t)cc, values[cc]);
		}
	}

	void sendProgramSnapshot(const MidiFeedbackState& state, MidiFeedbackSink& sink) {
		uint8_t values[kProgramValueCount];
		bool valid[kProgramValueCount];
		encodeProgram(state, values, valid);
		for (int cc = 0; cc < kProgramValueCount; cc++) {
			if (valid[cc])
				sendCc(sink, kFeedbackProgramChannel, (uint8_t)cc, values[cc]);
		}
	}

	void sendStageSnapshot(const MidiFeedbackState& state, int page, MidiFeedbackSink& sink) {
		int first = page * 8;
		for (int offset = 0; offset < 8; offset++) {
			int cc = first + offset;
			uint8_t value = encodeStage(state, cc);
			sendCc(sink, kFeedbackStageChannel, (uint8_t)cc, value);
			stageCache_[cc] = value;
			stagePending_[cc] = false;
		}
		for (int offset = 0; offset < 8; offset++) {
			int cc = 64 + first + offset;
			uint8_t value = encodeStage(state, cc);
			sendCc(sink, kFeedbackStageChannel, (uint8_t)cc, value);
			stageCache_[cc] = value;
			stagePending_[cc] = false;
		}
	}

	void emitHeadDeltas(const HeadFeedbackState& head, int index, MidiFeedbackSink& sink) {
		uint8_t values[kHeadValueCount];
		bool valid[kHeadValueCount];
		encodeHead(head, values, valid);
		bool presenceChanged = values[16] != headCache_[index][16];
		if (presenceChanged && !head.present) {
			sendCc(sink, (uint8_t)index, 16, 0);
			cacheHead(head, index);
			return;
		}
		if (presenceChanged && head.present) {
			sendHeadSnapshot(head, index, sink);
			cacheHead(head, index);
			return;
		}
		if (!head.present) {
			cacheHead(head, index);
			return;
		}

		bool runChanged = values[1] != headCache_[index][1] ||
			values[2] != headCache_[index][2] || values[14] != headCache_[index][14];
		if (runChanged) {
			sendCc(sink, (uint8_t)index, 1, values[1]);
			sendCc(sink, (uint8_t)index, 2, values[2]);
			sendCc(sink, (uint8_t)index, 14, values[14]);
			headCache_[index][1] = values[1];
			headCache_[index][2] = values[2];
			headCache_[index][14] = values[14];
		}
		for (int cc = 0; cc < kHeadValueCount; cc++) {
			if (!valid[cc] || cc == 1 || cc == 2 || cc == 14)
				continue;
			if (values[cc] != headCache_[index][cc]) {
				sendCc(sink, (uint8_t)index, (uint8_t)cc, values[cc]);
				headCache_[index][cc] = values[cc];
			}
		}
		if (head.advanceSeq != lastAdvanceSeq_[index]) {
			sendCc(sink, (uint8_t)index, 3, 127);
			sendCc(sink, (uint8_t)index, 3, 0);
			lastAdvanceSeq_[index] = head.advanceSeq;
		}
		if (head.resetSeq != lastResetSeq_[index]) {
			sendCc(sink, (uint8_t)index, 4, 127);
			sendCc(sink, (uint8_t)index, 4, 0);
			lastResetSeq_[index] = head.resetSeq;
		}
	}

	void emitProgramDeltas(const MidiFeedbackState& state, MidiFeedbackSink& sink) {
		uint8_t values[kProgramValueCount];
		bool valid[kProgramValueCount];
		encodeProgram(state, values, valid);
		bool selectionChanged = values[0] != programCache_[0];
		if (selectionChanged) {
			sendCc(sink, kFeedbackProgramChannel, 0, values[0]);
			programCache_[0] = values[0];
			for (int cc = 16; cc <= 29; cc++) {
				sendCc(sink, kFeedbackProgramChannel, (uint8_t)cc, values[cc]);
				programCache_[cc] = values[cc];
			}
		}
		for (int cc = 0; cc < kProgramValueCount; cc++) {
			if (!valid[cc] || cc == 0 || (selectionChanged && cc >= 16))
				continue;
			if (values[cc] != programCache_[cc]) {
				sendCc(sink, kFeedbackProgramChannel, (uint8_t)cc, values[cc]);
				programCache_[cc] = values[cc];
			}
		}
	}

	void emitStageDeltas(const MidiFeedbackState& state, float dt, bool enabled,
	                     uint8_t snappedPages, MidiFeedbackSink& sink) {
		for (int cc = 0; cc < kStageValueCount; cc++) {
			uint8_t value = encodeStage(state, cc);
			int stage = cc < 64 ? cc : cc - 64;
			int page = stage / 8;
			if (snappedPages & (1u << page)) {
				stageCache_[cc] = value;
				stagePending_[cc] = false;
				continue;
			}
			if (value != stageCache_[cc]) {
				stageCache_[cc] = value;
				if (enabled) {
					stagePending_[cc] = true;
					stagePendingValue_[cc] = value;
				}
			}
		}
		if (!enabled) {
			for (int cc = 0; cc < kStageValueCount; cc++)
				stagePending_[cc] = false;
			stageTimer_ = 0.f;
			return;
		}
		stageTimer_ += dt > 0.f ? dt : 0.f;
		if (stageTimer_ < kFeedbackStageInterval)
			return;
		for (int offset = 0; offset < kStageValueCount; offset++) {
			int cc = (stageCursor_ + offset) % kStageValueCount;
			if (!stagePending_[cc])
				continue;
			sendCc(sink, kFeedbackStageChannel, (uint8_t)cc, stagePendingValue_[cc]);
			stagePending_[cc] = false;
			stageCursor_ = (cc + 1) % kStageValueCount;
			stageTimer_ = 0.f;
			return;
		}
		stageTimer_ = kFeedbackStageInterval;
	}
};

} // namespace spacetime
