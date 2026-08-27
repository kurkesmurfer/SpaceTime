#pragma once

// Platform-neutral owner for the fused SpaceTime instrument. UI adapters feed
// MIDI and direct program actions into this class; no expander transport is
// involved here.

#include "HeadRemoteController.hpp"
#include "MMModuleContracts.hpp"
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
			sourceClockEvents_[h] = 0;
			stageEntries_[h] = 0;
			headController_[h].reset((uint32_t)(h + 1));
			headSignals_[h] = HeadSignals();
			headOut_[h] = HeadOut();
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

	void setStageBanks(int banks) {
		table_.count = stageCountFromBanks(banks);
		if (program_.selectedStage() >= table_.count)
			program_.setSelected(table_.count - 1);
	}
	int stageBanks() const { return stageBanksFromCount(table_.count); }
	int stageCount() const { return table_.count; }

	HeadConfig& headConfig(int head) { return headController_[clampHead(head)].config(); }
	const HeadConfig& headConfig(int head) const { return headController_[clampHead(head)].config(); }
	HeadSignals& headSignals(int head) { return headSignals_[clampHead(head)]; }
	const HeadSignals& headSignals(int head) const { return headSignals_[clampHead(head)]; }
	const HeadOut& headOut(int head) const { return headOut_[clampHead(head)]; }
	void setExternalHeadOut(int head, const HeadOut& out) {
		headOut_[clampHead(head)] = out;
	}
	uint32_t sourceClockEvents(int head) const { return sourceClockEvents_[clampHead(head)]; }
	uint32_t stageEntries(int head) const { return stageEntries_[clampHead(head)]; }
	int headClockSource(int head) const { return headController_[clampHead(head)].clockSource(); }
	void setHeadClockSource(int head, int source) {
		headController_[clampHead(head)].setClockSource(source);
	}
	bool headFollowsMidiTransport(int head) const {
		return headController_[clampHead(head)].followsMidiTransport();
	}
	void setHeadFollowsMidiTransport(int head, bool follow) {
		headController_[clampHead(head)].setFollowsMidiTransport(follow);
	}

	void setExternal(int index, float voltage, bool connected) {
		if (index < 0 || index >= 4)
			return;
		ext_.v[index] = voltage;
		ext_.connected[index] = connected;
	}

	// EB8: a HeadRemote's HeadDSP::tick() needs the same ExtInputs a fused
	// engine already has, so Core has to be able to publish what it just
	// received via setExternal(). No setter existed before because nothing
	// needed to read it back until now.
	const ExtInputs& ext() const { return ext_; }

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

	void processHeads(float dt, uint8_t externalHeadMask = 0) {
		applyMidiToHeads();
		for (int h = 0; h < kMaxHeads; h++) {
			if ((externalHeadMask & (uint8_t)(1u << h)) != 0)
				continue;
			uint8_t previousStage = headOut_[h].currentStage;
			headController_[h].tick(table_, ext_, globals_, program_.scaleKey(),
				headSignals_[h], dt, headOut_[h]);
			if (headOut_[h].currentStage != previousStage)
				stageEntries_[h]++;
		}
	}

	void processMidiOutput(float dt, MidiOutputSink& sink) {
		if (!midi_.outputRequiresService())
			return;
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
	HeadRemoteController headController_[kMaxHeads];
	HeadSignals headSignals_[kMaxHeads];
	HeadOut headOut_[kMaxHeads];
	uint32_t lastMidiEventSeq_ = 0;
	uint32_t appliedMidiEvents_ = 0;
	uint8_t lastAppliedMidiType_ = MIDI_PROG_NONE;
	uint8_t lastAppliedMidiIndex_ = 0;
	uint8_t lastAppliedMidiValue_ = 0;
	uint32_t sourceClockEvents_[kMaxHeads] = {};
	uint32_t stageEntries_[kMaxHeads] = {};

	static int clampHead(int head) {
		return head < 0 ? 0 : (head >= kMaxHeads ? kMaxHeads - 1 : head);
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
			HeadRemoteController::MidiEventCounts counts =
				headController_[h].applyMidiWithHeadEventSeq(
					midi_.headCcSeq[h], midi_.headCcValue[h], midi_.midiClockSeq,
					midi_.midiStartSeq, midi_.midiStopSeq, midi_.midiContinueSeq,
					midi_.headEventSeq[h]);
			sourceClockEvents_[h] += counts.midiClock + counts.virtualClock;
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
				if (event.index < table_.count)
					apply(table_, EditOp(event.index, Field::Voltage, event.fvalue, event.flags));
				else if (event.index >= kMaxStages &&
					(event.index - kMaxStages) < table_.count)
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
