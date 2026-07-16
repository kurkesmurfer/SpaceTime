#pragma once

// Platform-neutral owner for the fused SpaceTime instrument. UI adapters feed
// MIDI and direct program actions into this class; no expander transport is
// involved here.

#include "HeadDSP.hpp"
#include "MidiCore.hpp"
#include "ProgramLogic.hpp"

namespace spacetime {

class SpaceTimeEngine {
public:
	SpaceTimeEngine() {
		reset();
	}

	void reset() {
		table_ = StageTable();
		table_.count = kMaxStages;
		program_ = ProgramLogic();
		midi_ = MidiCore();
		globals_ = Globals();
		lastMidiEventSeq_ = 0;
		lastAppliedMidiType_ = MIDI_PROG_NONE;
		lastAppliedMidiIndex_ = 0;
		lastAppliedMidiValue_ = 0;
		appliedMidiEvents_ = 0;
		for (int h = 0; h < kMaxHeads; h++) {
			headDsp_[h].reset((uint32_t)(h + 1));
			headConfig_[h] = HeadConfig();
			headSignals_[h] = HeadSignals();
			headOut_[h] = HeadOut();
			headRuntime_[h] = HeadRuntime();
		}
	}

	StageTable& table() { return table_; }
	const StageTable& table() const { return table_; }
	ProgramLogic& program() { return program_; }
	const ProgramLogic& program() const { return program_; }
	MidiCore& midi() { return midi_; }
	const MidiCore& midi() const { return midi_; }
	Globals& globals() { return globals_; }
	const Globals& globals() const { return globals_; }

	HeadConfig& headConfig(int head) { return headConfig_[clampHead(head)]; }
	const HeadConfig& headConfig(int head) const { return headConfig_[clampHead(head)]; }
	HeadSignals& headSignals(int head) { return headSignals_[clampHead(head)]; }
	const HeadSignals& headSignals(int head) const { return headSignals_[clampHead(head)]; }
	const HeadOut& headOut(int head) const { return headOut_[clampHead(head)]; }
	int headClockSource(int head) const { return headRuntime_[clampHead(head)].clockSource; }
	void setHeadClockSource(int head, int source) {
		headRuntime_[clampHead(head)].clockSource = clampInt(source, 0, 3);
	}
	bool headFollowsMidiTransport(int head) const {
		return headRuntime_[clampHead(head)].followMidiTransport;
	}
	void setHeadFollowsMidiTransport(int head, bool follow) {
		headRuntime_[clampHead(head)].followMidiTransport = follow;
	}

	void setExternal(int index, float voltage, bool connected) {
		if (index < 0 || index >= 4)
			return;
		ext_.v[index] = voltage;
		ext_.connected[index] = connected;
	}

	void handleMidi(uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0) {
		midi_.handleMessage(status, data1, data2);
		applyMidiProgramEvents();
	}

	// Control-rate maintenance. Preset restores retain the existing bounded
	// edit-op path even though the fused engine owns the table directly.
	void processControl(float dt) {
		(void)dt;
		applyMidiProgramEvents();
		EditOp ops[kMaxOpsPerTick];
		int count = program_.drainPendingOps(ops, kMaxOpsPerTick);
		applyOps(ops, count);
	}

	void processHeads(float dt) {
		applyMidiToHeads();
		for (int h = 0; h < kMaxHeads; h++) {
			HeadRuntime& runtime = headRuntime_[h];
			HeadSignals signals = headSignals_[h];
			signals.start = maxFloat(signals.start, pulseGate(runtime.startTimer, dt));
			signals.stop = maxFloat(signals.stop, pulseGate(runtime.stopTimer, dt));
			signals.advance = maxFloat(signals.advance, pulseGate(runtime.advanceTimer, dt));
			signals.strobe = maxFloat(signals.strobe, pulseGate(runtime.strobeTimer, dt));
			if (runtime.clockSource == 2)
				signals.extClock = pulseGate(runtime.midiClockTimer, dt);
			else if (runtime.clockSource == 3)
				signals.extClock = pulseGate(runtime.virtualClockTimer, dt);
			else if (runtime.clockSource == 0)
				signals.extClock = 0.f;
			signals.reset = signals.reset || runtime.resetPending;
			runtime.resetPending = false;
			headConfig_[h].clkExt = runtime.clockSource != 0;
			headDsp_[h].tick(table_, ext_, globals_, program_.scaleKey(),
				headConfig_[h], signals, dt, headOut_[h]);
		}
	}

	void processMidiOutput(float dt, MidiOutputSink& sink) {
		HeadsToAnchorMsg status;
		status.headCount = kMaxHeads;
		status.valid = true;
		for (int h = 0; h < kMaxHeads; h++) {
			HeadStatus& target = status.status[h];
			const HeadOut& source = headOut_[h];
			target.headId = (uint8_t)h;
			target.currentStage = source.currentStage;
			target.runState = source.runState;
			target.pulse1 = source.pulse1 ? 1 : 0;
			target.pulse2 = source.pulse2 ? 1 : 0;
			target.allPulse = source.allPulse ? 1 : 0;
			target.quantized = source.currentStage < table_.count &&
				table_.program[source.currentStage].quantize() ? 1 : 0;
			target.phase = source.phase;
			target.cv = source.cv;
		}
		midi_.processOutput(status, dt, sink);
	}

	bool applyEdit(const EditOp& op) {
		return apply(table_, op);
	}

	void selectRelative(int direction) {
		program_.selectRelative(direction, table_.count);
	}

	void savePreset(int slot) {
		program_.savePreset(slot, table_, program_.scaleKey());
	}

	bool loadPreset(int slot) {
		return program_.loadPreset(slot, table_);
	}

	uint32_t appliedMidiEvents() const { return appliedMidiEvents_; }
	uint8_t lastAppliedMidiType() const { return lastAppliedMidiType_; }
	uint8_t lastAppliedMidiIndex() const { return lastAppliedMidiIndex_; }
	uint8_t lastAppliedMidiValue() const { return lastAppliedMidiValue_; }

private:
	StageTable table_;
	ProgramLogic program_;
	MidiCore midi_;
	Globals globals_;
	ExtInputs ext_;
	HeadDSP headDsp_[kMaxHeads];
	HeadConfig headConfig_[kMaxHeads];
	HeadSignals headSignals_[kMaxHeads];
	HeadOut headOut_[kMaxHeads];
	struct HeadRuntime {
		int clockSource;
		bool followMidiTransport;
		bool resetPending;
		float midiClockTimer;
		float virtualClockTimer;
		float startTimer;
		float stopTimer;
		float advanceTimer;
		float strobeTimer;
		uint32_t lastClockSeq;
		uint32_t lastStartSeq;
		uint32_t lastStopSeq;
		uint32_t lastContinueSeq;
		uint32_t lastCcSeq[kMidiHeadControls];

		HeadRuntime()
			: clockSource(0), followMidiTransport(false), resetPending(false),
			  midiClockTimer(0.f), virtualClockTimer(0.f), startTimer(0.f),
			  stopTimer(0.f), advanceTimer(0.f), strobeTimer(0.f),
			  lastClockSeq(0), lastStartSeq(0), lastStopSeq(0), lastContinueSeq(0) {
			for (int control = 0; control < kMidiHeadControls; control++)
				lastCcSeq[control] = 0;
		}
	};
	HeadRuntime headRuntime_[kMaxHeads];
	uint32_t lastMidiEventSeq_ = 0;
	uint32_t appliedMidiEvents_ = 0;
	uint8_t lastAppliedMidiType_ = MIDI_PROG_NONE;
	uint8_t lastAppliedMidiIndex_ = 0;
	uint8_t lastAppliedMidiValue_ = 0;

	static int clampHead(int head) {
		return head < 0 ? 0 : (head >= kMaxHeads ? kMaxHeads - 1 : head);
	}

	static int clampInt(int value, int low, int high) {
		return value < low ? low : (value > high ? high : value);
	}

	static float maxFloat(float a, float b) {
		return a > b ? a : b;
	}

	static float pulseGate(float& timer, float dt) {
		float value = timer > 0.f ? 10.f : 0.f;
		timer = timer > dt ? timer - dt : 0.f;
		return value;
	}

	static Field midiGestureField(int cc) {
		switch (cc) {
			case 68: return Field::Quantize;
			case 69: return Field::Slew;
			case 70: return Field::Range;
			case 71: return Field::VoltageSource;
			case 77: return Field::Stop;
			case 78: return Field::Sustain;
			case 79: return Field::Enable;
			case 80: return Field::First;
			case 81: return Field::Last;
			case 86: return Field::TimeSource;
			case 87: return Field::Pulse1;
			case 88: return Field::Pulse2;
			default: return Field::Count_;
		}
	}

	void applyOps(const EditOp* ops, int count) {
		for (int i = 0; i < count; i++)
			apply(table_, ops[i]);
	}

	void applyMidiToHeads() {
		for (int h = 0; h < kMaxHeads; h++) {
			HeadRuntime& runtime = headRuntime_[h];
			if (midi_.midiClockSeq != runtime.lastClockSeq) {
				runtime.lastClockSeq = midi_.midiClockSeq;
				runtime.midiClockTimer = 1e-3f;
			}
			bool startChanged = midi_.midiStartSeq != runtime.lastStartSeq;
			bool stopChanged = midi_.midiStopSeq != runtime.lastStopSeq;
			bool continueChanged = midi_.midiContinueSeq != runtime.lastContinueSeq;
			runtime.lastStartSeq = midi_.midiStartSeq;
			runtime.lastStopSeq = midi_.midiStopSeq;
			runtime.lastContinueSeq = midi_.midiContinueSeq;
			if (runtime.followMidiTransport && (startChanged || continueChanged))
				runtime.startTimer = 1e-3f;
			if (runtime.followMidiTransport && stopChanged)
				runtime.stopTimer = 1e-3f;
			for (int control = 0; control < kMidiHeadControls; control++) {
				uint32_t sequence = midi_.headCcSeq[h][control];
				if (sequence == runtime.lastCcSeq[control])
					continue;
				runtime.lastCcSeq[control] = sequence;
				applyHeadCc(h, control, midi_.headCcValue[h][control]);
			}
		}
	}

	void applyHeadCc(int head, int cc, float value) {
		HeadConfig& config = headConfig_[head];
		HeadRuntime& runtime = headRuntime_[head];
		switch (cc) {
			case 0: runtime.virtualClockTimer = 1e-3f; break;
			case 1: runtime.startTimer = 1e-3f; break;
			case 2: runtime.stopTimer = 1e-3f; break;
			case 3: runtime.advanceTimer = 1e-3f; break;
			case 4: runtime.resetPending = true; break;
			case 5: config.addressKnob = value < 0.f ? 0.f : (value > 10.f ? 10.f : value); break;
			case 6: config.addrExt = value >= 0.5f; break;
			case 7: {
				int mode = clampInt((int)std::round(value), 0, 2);
				if (mode == 0)
					runtime.strobeTimer = 1e-3f;
				else
					config.continuous = mode == 2;
				break;
			}
			case 8: config.direction = (uint8_t)clampInt((int)std::round(value), 0, 4); break;
			case 9: runtime.clockSource = clampInt((int)std::round(value), 0, 3); break;
			case 10: config.clkDivIndex = (uint8_t)clampInt((int)std::round(value), 0, 8); break;
			case 11: config.timeCvAmount = value < -1.f ? -1.f : (value > 1.f ? 1.f : value); break;
			case 12: config.loopMode = (uint8_t)clampInt((int)std::round(value), 0, 2); break;
			default: break;
		}
	}

	void applyMidiProgramEvents() {
		for (int i = 0; i < midi_.programEventCount && i < kMaxMidiProgramEvents; i++) {
			const MidiProgramEvent& event = midi_.programEvents[i];
			if (event.seq <= lastMidiEventSeq_)
				continue;
			lastMidiEventSeq_ = event.seq;
			lastAppliedMidiType_ = event.type;
			lastAppliedMidiIndex_ = event.index;
			lastAppliedMidiValue_ = event.value;
			appliedMidiEvents_++;
			applyMidiProgramEvent(event);
		}
	}

	void applyMidiProgramEvent(const MidiProgramEvent& event) {
		EditOp ops[kMaxOpsPerTick];
		int count = 0;
		switch (event.type) {
			case MIDI_PROG_SELECT_PREV:
				program_.selectRelative(-1, table_.count);
				break;
			case MIDI_PROG_SELECT_NEXT:
				program_.selectRelative(1, table_.count);
				break;
			case MIDI_PROG_BULK_ARM:
				program_.armBulkOnce();
				break;
			case MIDI_PROG_CLEAR:
				count = program_.emitClear(table_, ops, kMaxOpsPerTick);
				applyOps(ops, count);
				break;
			case MIDI_PROG_PRESET_LOAD:
				program_.loadPreset(event.index, table_);
				break;
			case MIDI_PROG_SET_KEY: {
				ScaleKey scaleKey = program_.scaleKey();
				scaleKey.key = event.index < 12 ? event.index : 11;
				program_.setScaleKey(scaleKey);
				break;
			}
			case MIDI_PROG_SET_SCALE: {
				ScaleKey scaleKey = program_.scaleKey();
				scaleKey.scale = event.index < 3 ? event.index : 2;
				program_.setScaleKey(scaleKey);
				break;
			}
			case MIDI_PROG_SET_PULSE_RETRIG:
				globals_.pulseRetrig = event.index != 0;
				break;
			case MIDI_PROG_SLIDER:
				if (event.index < kMaxStages)
					apply(table_, EditOp(event.index, Field::Voltage, event.fvalue, event.flags));
				else if (event.index < 2 * kMaxStages)
					apply(table_, EditOp((uint8_t)(event.index - kMaxStages),
						Field::Time, event.fvalue, event.flags));
				break;
			case MIDI_PROG_GESTURE: {
				Field field = midiGestureField(event.index);
				if (field != Field::Count_) {
					count = program_.emitModifier(field, event.value >= 64 ? 1 : -1,
						table_, ops, kMaxOpsPerTick);
					applyOps(ops, count);
				}
				break;
			}
			case MIDI_PROG_LIMITED:
				if (event.value >= 64) {
					count = program_.emitLimited(event.index, table_, ops, kMaxOpsPerTick);
					applyOps(ops, count);
				}
				break;
			case MIDI_PROG_TIME_RANGE:
				if (event.value >= 64) {
					count = program_.emitTimeRange(event.index, table_, ops, kMaxOpsPerTick);
					applyOps(ops, count);
				}
				break;
			default:
				break;
		}
	}
};

} // namespace spacetime
