#pragma once
// Platform-neutral SpaceTime MIDI router and output engine.
// VCV Rack and MetaModule adapters provide device I/O, UI and persistence.

#include <cmath>
#include <cstdint>
#include "Chain.hpp"

namespace spacetime {

enum MidiOutMode {
	MIDI_OUT_OFF = 0,
	MIDI_OUT_NOTES,
	MIDI_OUT_CC7
};

enum MidiGateSource {
	MIDI_GATE_P1 = 0,
	MIDI_GATE_P2,
	MIDI_GATE_ALL
};

enum MidiRoute {
	MIDI_ROUTE_NONE = 0,
	MIDI_ROUTE_IGNORED,
	MIDI_ROUTE_CLOCK,
	MIDI_ROUTE_START,
	MIDI_ROUTE_CONTINUE,
	MIDI_ROUTE_STOP,
	MIDI_ROUTE_PROGRAM,
	MIDI_ROUTE_STAGE,
	MIDI_ROUTE_HEAD,
	MIDI_ROUTE_PROGRAM_HEAD,
	MIDI_ROUTE_STAGE_HEAD
};

struct MidiOutLaneConfig {
	uint8_t mode;
	uint8_t channel;
	uint8_t gateSource;
	uint8_t cc;

	MidiOutLaneConfig()
		: mode(MIDI_OUT_OFF), channel(0), gateSource(MIDI_GATE_ALL), cc(20) {}
};

struct MidiOutputSink {
	virtual ~MidiOutputSink() {}
	virtual void send(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) = 0;
};

class MidiCore {
public:
	int controlChannel;
	int sliderChannel;
	bool moveStageSliders;

	uint32_t midiClockSeq, midiStartSeq, midiStopSeq, midiContinueSeq;
	uint32_t headCcSeq[kMaxHeads][kMidiHeadControls];
	float headCcValue[kMaxHeads][kMidiHeadControls];

	MidiProgramEvent programEvents[kMaxMidiProgramEvents];
	uint8_t programEventCount;
	uint32_t programEventSeq;

	int lastStatus, lastChannel, lastNumber, lastValue;
	uint8_t lastRoute;

	MidiOutLaneConfig outLane[kMaxHeads];
	uint32_t outNoteCount, outCcCount;

	MidiCore()
		: controlChannel(15), sliderChannel(14), moveStageSliders(true),
		  midiClockSeq(0), midiStartSeq(0), midiStopSeq(0), midiContinueSeq(0),
		  programEventCount(0), programEventSeq(0),
		  lastStatus(-1), lastChannel(-1), lastNumber(-1), lastValue(-1),
		  lastRoute(MIDI_ROUTE_NONE), outNoteCount(0), outCcCount(0) {
		for (int h = 0; h < kMaxHeads; h++) {
			outLane[h].channel = (uint8_t)h;
			outLane[h].cc = (uint8_t)(20 + h);
			prevGate_[h] = false;
			noteActive_[h] = false;
			activeNote_[h] = 0;
			noteTimer_[h] = 0.f;
			ccTimer_[h] = 0.f;
			lastCcValue_[h] = -1;
			for (int c = 0; c < kMidiHeadControls; c++) {
				headCcSeq[h][c] = 0;
				headCcValue[h][c] = 0.f;
			}
		}
	}

	static int scaleIndex(uint8_t value, int maxIndex) {
		int i = (int)std::floor(((float)value / 127.f) * (float)(maxIndex + 1));
		if (i < 0) i = 0;
		if (i > maxIndex) i = maxIndex;
		return i;
	}

	static const char* routeName(uint8_t route) {
		switch (route) {
			case MIDI_ROUTE_IGNORED: return "ignored";
			case MIDI_ROUTE_CLOCK: return "clock";
			case MIDI_ROUTE_START: return "start";
			case MIDI_ROUTE_CONTINUE: return "continue";
			case MIDI_ROUTE_STOP: return "stop";
			case MIDI_ROUTE_PROGRAM: return "program";
			case MIDI_ROUTE_STAGE: return "stage";
			case MIDI_ROUTE_HEAD: return "head";
			case MIDI_ROUTE_PROGRAM_HEAD: return "program+head";
			case MIDI_ROUTE_STAGE_HEAD: return "stage+head";
			default: return "none";
		}
	}

	void handleMessage(uint8_t statusByte, uint8_t data1 = 0, uint8_t data2 = 0) {
		lastStatus = statusByte;
		lastChannel = -1;
		lastNumber = -1;
		lastValue = -1;
		lastRoute = MIDI_ROUTE_IGNORED;

		if (statusByte == 0xF8) {
			midiClockSeq++;
			lastRoute = MIDI_ROUTE_CLOCK;
			return;
		}
		if (statusByte == 0xFA) {
			midiStartSeq++;
			lastRoute = MIDI_ROUTE_START;
			return;
		}
		if (statusByte == 0xFB) {
			midiContinueSeq++;
			lastRoute = MIDI_ROUTE_CONTINUE;
			return;
		}
		if (statusByte == 0xFC) {
			midiStopSeq++;
			lastRoute = MIDI_ROUTE_STOP;
			return;
		}

		uint8_t status = (statusByte >> 4) & 0xF;
		uint8_t channel = statusByte & 0xF;
		data1 &= 0x7F;
		data2 &= 0x7F;
		lastChannel = channel;
		if (status == 0xB) {
			lastNumber = data1;
			lastValue = data2;
			bool toProgram = channel == controlChannel;
			bool toStage = channel == sliderChannel;
			bool toHead = channel < kMaxHeads;
			if (toProgram)
				handleProgramCc(data1, data2);
			if (toStage)
				handleSliderCc(data1, data2);
			if (toHead)
				handleHeadCc(channel, data1, data2);
			lastRoute = toProgram && toHead ? MIDI_ROUTE_PROGRAM_HEAD :
				(toStage && toHead ? MIDI_ROUTE_STAGE_HEAD :
				 (toProgram ? MIDI_ROUTE_PROGRAM :
				  (toStage ? MIDI_ROUTE_STAGE : (toHead ? MIDI_ROUTE_HEAD : MIDI_ROUTE_IGNORED))));
		}
		else if (status == 0xC && channel == controlChannel) {
			lastNumber = data1;
			int slot = -1;
			if (data1 >= 1 && data1 <= 12)
				slot = data1 - 1;
			else if (data1 == 0)
				slot = 0;
			if (slot >= 0) {
				pushProgramEvent(MIDI_PROG_PRESET_LOAD, (uint8_t)slot, data1);
				lastRoute = MIDI_ROUTE_PROGRAM;
			}
		}
	}

	void injectMidi(AnchorToHeadsMsg& msg) const {
		msg.midiClockSeq = midiClockSeq;
		msg.midiStartSeq = midiStartSeq;
		msg.midiStopSeq = midiStopSeq;
		msg.midiContinueSeq = midiContinueSeq;
		for (int h = 0; h < kMaxHeads; h++) {
			for (int c = 0; c < kMidiHeadControls; c++) {
				msg.headCcSeq[h][c] = headCcSeq[h][c];
				msg.headCcValue[h][c] = headCcValue[h][c];
			}
		}
	}

	void appendProgramEvents(HeadsToAnchorMsg& msg) const {
		msg.midiEventCount = programEventCount;
		msg.midiEventSeq = programEventSeq;
		for (int i = 0; i < programEventCount && i < kMaxMidiProgramEvents; i++)
			msg.midiEvents[i] = programEvents[i];
	}

	void processOutput(const HeadsToAnchorMsg& status, float dt, MidiOutputSink& sink) {
		bool present[kMaxHeads] = {};
		for (int h = 0; h < kMaxHeads; h++) {
			ccTimer_[h] += dt;
			if (noteActive_[h]) {
				noteTimer_[h] -= dt;
				if (noteTimer_[h] <= 0.f)
					sendNoteOff(h, sink);
			}
		}

		for (int i = 0; i < status.headCount && i < kMaxHeads; i++) {
			const HeadStatus& st = status.status[i];
			int h = st.headId;
			if (h < 0 || h >= kMaxHeads)
				continue;
			present[h] = true;
			MidiOutLaneConfig& lane = outLane[h];

			if (lane.mode == MIDI_OUT_NOTES) {
				bool gate = gateFor(st, lane.gateSource);
				bool rising = gate && !prevGate_[h];
				bool falling = !gate && prevGate_[h];
				prevGate_[h] = gate;
				if (rising && st.quantized) {
					sendNoteOff(h, sink);
					activeNote_[h] = cvToNote(st.cv);
					sink.send(0x9, lane.channel, activeNote_[h], 100);
					noteActive_[h] = true;
					noteTimer_[h] = lane.gateSource == MIDI_GATE_ALL ? 0.03f : 30.f;
					outNoteCount++;
				}
				if (falling && lane.gateSource != MIDI_GATE_ALL)
					sendNoteOff(h, sink);
				if (st.runState == RUN_STOPPED)
					sendNoteOff(h, sink);
			}
			else {
				prevGate_[h] = gateFor(st, lane.gateSource);
				sendNoteOff(h, sink);
			}

			if (lane.mode == MIDI_OUT_CC7) {
				int value = cvToCc(st.cv);
				if (value != lastCcValue_[h] && ccTimer_[h] >= 0.003f) {
					sink.send(0xB, lane.channel, lane.cc, (uint8_t)value);
					lastCcValue_[h] = value;
					ccTimer_[h] = 0.f;
					outCcCount++;
				}
			}
		}

		for (int h = 0; h < kMaxHeads; h++) {
			if (!present[h]) {
				prevGate_[h] = false;
				sendNoteOff(h, sink);
			}
		}
	}

private:
	bool prevGate_[kMaxHeads];
	bool noteActive_[kMaxHeads];
	uint8_t activeNote_[kMaxHeads];
	float noteTimer_[kMaxHeads];
	float ccTimer_[kMaxHeads];
	int lastCcValue_[kMaxHeads];

	void pushProgramEvent(uint8_t type, uint8_t index, uint8_t value,
	                      float fvalue = 0.f, uint8_t flags = 0) {
		if (programEventCount >= kMaxMidiProgramEvents) {
			for (int i = 1; i < kMaxMidiProgramEvents; i++)
				programEvents[i - 1] = programEvents[i];
			programEventCount = kMaxMidiProgramEvents - 1;
		}
		MidiProgramEvent& event = programEvents[programEventCount++];
		event.seq = ++programEventSeq;
		event.type = type;
		event.index = index;
		event.value = value;
		event.fvalue = fvalue;
		event.flags = flags;
	}

	void handleSliderCc(uint8_t cc, uint8_t value) {
		float scaled = cc < kMaxStages ? ((float)value / 127.f) * 10.f :
			(float)value / 127.f;
		pushProgramEvent(MIDI_PROG_SLIDER, cc, value, scaled,
			moveStageSliders ? EDIT_OP_MOVE_SLIDER : EDIT_OP_NONE);
	}

	void handleProgramCc(uint8_t cc, uint8_t value) {
		if (cc == 64 && value >= 64)
			pushProgramEvent(MIDI_PROG_SELECT_PREV, 0, value);
		else if (cc == 65 && value >= 64)
			pushProgramEvent(MIDI_PROG_SELECT_NEXT, 0, value);
		else if (cc == 66 && value >= 64)
			pushProgramEvent(MIDI_PROG_CLEAR, 0, value);
		else if (cc == 67 && value >= 64)
			pushProgramEvent(MIDI_PROG_BULK_ARM, 0, value);
		else if ((cc >= 68 && cc <= 71) || (cc >= 77 && cc <= 81) ||
		         cc == 86 || cc == 87 || cc == 88)
			pushProgramEvent(MIDI_PROG_GESTURE, cc, value);
		else if (cc >= 72 && cc <= 76)
			pushProgramEvent(MIDI_PROG_LIMITED, cc - 72, value);
		else if (cc >= 82 && cc <= 85)
			pushProgramEvent(MIDI_PROG_TIME_RANGE, cc - 82, value);
		else if (cc == 89)
			pushProgramEvent(MIDI_PROG_SET_KEY, (uint8_t)scaleIndex(value, 11), value);
		else if (cc == 90)
			pushProgramEvent(MIDI_PROG_SET_SCALE, (uint8_t)scaleIndex(value, 2), value);
		else if (cc == 91)
			pushProgramEvent(MIDI_PROG_SET_PULSE_RETRIG, value >= 64 ? 1 : 0, value);
	}

	void handleHeadCc(uint8_t channel, uint8_t cc, uint8_t value) {
		if (channel >= kMaxHeads || cc >= kMidiHeadControls)
			return;
		float scaled = 0.f;
		switch (cc) {
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 13:
				if (value < 64)
					return;
				scaled = 1.f;
				break;
			case 5: scaled = ((float)value / 127.f) * 10.f; break;
			case 6: scaled = value >= 64 ? 1.f : 0.f; break;
			case 7: scaled = (float)scaleIndex(value, 2); break;
			case 8: scaled = (float)scaleIndex(value, 4); break;
			case 9: scaled = (float)scaleIndex(value, 3); break;
			case 10: scaled = (float)scaleIndex(value, 8); break;
			case 11: scaled = ((float)value / 127.f) * 2.f - 1.f; break;
			case 12: scaled = (float)scaleIndex(value, 2); break;
		}
		headCcValue[channel][cc] = scaled;
		headCcSeq[channel][cc]++;
	}

	static uint8_t cvToNote(float cv) {
		int note = (int)std::round(cv * 12.f) + 60;
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		return (uint8_t)note;
	}

	static uint8_t cvToCc(float cv) {
		if (cv < 0.f) cv = 0.f;
		if (cv > 10.f) cv = 10.f;
		int value = (int)std::round(cv / 10.f * 127.f);
		if (value < 0) value = 0;
		if (value > 127) value = 127;
		return (uint8_t)value;
	}

	static bool gateFor(const HeadStatus& status, int source) {
		if (source == MIDI_GATE_P1)
			return status.pulse1 != 0;
		if (source == MIDI_GATE_P2)
			return status.pulse2 != 0;
		return status.allPulse != 0;
	}

	void sendNoteOff(int head, MidiOutputSink& sink) {
		if (!noteActive_[head])
			return;
		sink.send(0x8, outLane[head].channel, activeNote_[head], 0);
		noteActive_[head] = false;
	}
};

} // namespace spacetime
