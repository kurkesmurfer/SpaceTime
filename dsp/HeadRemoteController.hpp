#pragma once

// Shared single-head MIDI-to-signals controller. SpaceTimeEngine uses eight
// instances for its fallback heads; each portable MMHead uses one instance
// when it claims that head. This keeps the control translation, timers and
// HeadDSP ownership in one implementation.
//
// Deliberately takes plain parameters, not MetaModuleHeadMidiSnapshot
// (MetaModuleRemoteBus.hpp) -- this class knows nothing about the
// MetaModule bus, only about "a head's CC seq/value rows and the shared
// transport counters," so it stays reusable if a future transport ever
// looks different. metamodule/src/HeadRemote.cpp unpacks the snapshot into
// this call.
//
// Also deliberately does not carry MidiCore's headEventSeq[h] fast-path
// skip ("nothing changed for this head at all, skip the 14-CC scan") --
// the bus payload this is fed from doesn't carry that field, and 14
// uint32_t comparisons per control tick is not a cost worth a second bus
// field to avoid.

#include "HeadDSP.hpp"
#include "Chain.hpp"  // kMidiHeadControls, Globals, ScaleKey

namespace spacetime {

class HeadRemoteController {
public:
	struct MidiEventCounts {
		uint32_t midiClock;
		uint32_t virtualClock;

		MidiEventCounts() : midiClock(0), virtualClock(0) {}
	};

	HeadRemoteController() { dsp_.reset(1); }

	void reset(uint32_t seed) {
		dsp_.reset(seed);
		config_ = HeadConfig();
		runtime_ = Runtime();
	}

	HeadConfig& config() { return config_; }
	const HeadConfig& config() const { return config_; }

	int clockSource() const { return runtime_.clockSource; }
	void setClockSource(int source) { runtime_.clockSource = clampInt(source, 0, 3); }
	bool followsMidiTransport() const { return runtime_.followMidiTransport; }
	void setFollowsMidiTransport(bool follow) { runtime_.followMidiTransport = follow; }
	bool isRunning() const { return dsp_.isRunning(); }

	// Mirrors SpaceTimeEngine::applyMidiToHeads' per-head loop body plus
	// applyHeadCc, verbatim, for exactly one head. `ccSeq`/`ccValue` are
	// this head's row from the MIDI-forwarding bus; the four transport seqs
	// are the shared/global ones (same fields MetaModuleHeadMidiSnapshot
	// carries).
	MidiEventCounts applyMidi(const uint32_t (&ccSeq)[kMidiHeadControls],
	               const float (&ccValue)[kMidiHeadControls],
	               uint32_t midiClockSeq, uint32_t midiStartSeq,
	               uint32_t midiStopSeq, uint32_t midiContinueSeq) {
		return applyMidiImpl(ccSeq, ccValue, midiClockSeq, midiStartSeq,
			midiStopSeq, midiContinueSeq, false, 0);
	}

	// SpaceTimeEngine receives MidiCore's aggregate per-head sequence and uses
	// it to avoid scanning all CC lanes at audio rate when nothing changed.
	MidiEventCounts applyMidiWithHeadEventSeq(
	               const uint32_t (&ccSeq)[kMidiHeadControls],
	               const float (&ccValue)[kMidiHeadControls],
	               uint32_t midiClockSeq, uint32_t midiStartSeq,
	               uint32_t midiStopSeq, uint32_t midiContinueSeq,
	               uint32_t headEventSeq) {
		return applyMidiImpl(ccSeq, ccValue, midiClockSeq, midiStartSeq,
			midiStopSeq, midiContinueSeq, true, headEventSeq);
	}

	// Merges MIDI/CC-driven timers with locally-supplied signals and ticks the
	// single HeadDSP owned by this controller.
	void tick(const StageTable& table, const ExtInputs& ext, const Globals& g,
	          const ScaleKey& sk, const HeadSignals& local, float dt, HeadOut& out) {
		runtime_.idleTime += dt;
		bool refreshIdle = runtime_.idleTime >= 1.f / 3000.f;
		bool localSignalActive = local.start >= kGateThreshold || local.stop >= kGateThreshold ||
			local.advance >= kGateThreshold || local.strobe >= kGateThreshold ||
			local.extClock >= kGateThreshold || local.reset;
		bool audioRate = dsp_.isRunning() || config_.continuous ||
			dsp_.hasTransientOutput() || runtime_.forceTick || localSignalActive;
		if (!audioRate && !refreshIdle)
			return;

		HeadSignals signals = local;
		signals.start = maxFloat(signals.start, pulseGate(runtime_.startTimer, dt));
		signals.stop = maxFloat(signals.stop, pulseGate(runtime_.stopTimer, dt));
		signals.advance = maxFloat(signals.advance, pulseGate(runtime_.advanceTimer, dt));
		signals.strobe = maxFloat(signals.strobe, pulseGate(runtime_.strobeTimer, dt));
		if (runtime_.clockSource == 2)
			signals.extClock = pulseGate(runtime_.midiClockTimer, dt);
		else if (runtime_.clockSource == 3)
			signals.extClock = pulseGate(runtime_.virtualClockTimer, dt);
		else if (runtime_.clockSource == 0)
			signals.extClock = 0.f;
		signals.reset = signals.reset || runtime_.resetPending;
		runtime_.resetPending = false;
		runtime_.forceTick = false;
		config_.clkExt = runtime_.clockSource != 0;

		dsp_.tick(table, ext, g, sk, config_, signals, audioRate ? dt : runtime_.idleTime, out);
		if (refreshIdle)
			runtime_.idleTime = 0.f;
	}

private:
	MidiEventCounts applyMidiImpl(const uint32_t (&ccSeq)[kMidiHeadControls],
	               const float (&ccValue)[kMidiHeadControls],
	               uint32_t midiClockSeq, uint32_t midiStartSeq,
	               uint32_t midiStopSeq, uint32_t midiContinueSeq,
	               bool useHeadEventSeq, uint32_t headEventSeq) {
		MidiEventCounts counts;
		bool clockChanged = midiClockSeq != runtime_.lastClockSeq;
		if (clockChanged) {
			uint32_t delta = midiClockSeq - runtime_.lastClockSeq;
			runtime_.lastClockSeq = midiClockSeq;
			runtime_.midiClockTimer = 1e-3f;
			if (runtime_.clockSource == 2)
				counts.midiClock = delta;
		}
		bool startChanged = midiStartSeq != runtime_.lastStartSeq;
		bool stopChanged = midiStopSeq != runtime_.lastStopSeq;
		bool continueChanged = midiContinueSeq != runtime_.lastContinueSeq;
		runtime_.lastStartSeq = midiStartSeq;
		runtime_.lastStopSeq = midiStopSeq;
		runtime_.lastContinueSeq = midiContinueSeq;
		if (runtime_.followMidiTransport && (startChanged || continueChanged))
			runtime_.startTimer = 1e-3f;
		if (runtime_.followMidiTransport && stopChanged)
			runtime_.stopTimer = 1e-3f;
		if ((runtime_.followMidiTransport && (startChanged || continueChanged || stopChanged)) || clockChanged)
			runtime_.forceTick = true;

		if (useHeadEventSeq && headEventSeq == runtime_.lastHeadEventSeq)
			return counts;
		if (useHeadEventSeq)
			runtime_.lastHeadEventSeq = headEventSeq;
		for (int control = 0; control < kMidiHeadControls; control++) {
			uint32_t sequence = ccSeq[control];
			if (sequence == runtime_.lastCcSeq[control])
				continue;
			uint32_t delta = sequence - runtime_.lastCcSeq[control];
			runtime_.lastCcSeq[control] = sequence;
			if (control == 0 && runtime_.clockSource == 3)
				counts.virtualClock += delta;
			applyHeadCc(control, ccValue[control]);
			runtime_.forceTick = true;
		}
		return counts;
	}
	struct Runtime {
		int clockSource;
		bool followMidiTransport;
		bool resetPending;
		bool forceTick;
		float midiClockTimer;
		float virtualClockTimer;
		float startTimer;
		float stopTimer;
		float advanceTimer;
		float strobeTimer;
		float idleTime;
		uint32_t lastClockSeq;
		uint32_t lastStartSeq;
		uint32_t lastStopSeq;
		uint32_t lastContinueSeq;
		uint32_t lastHeadEventSeq;
		uint32_t lastCcSeq[kMidiHeadControls];

		Runtime()
			: clockSource(0), followMidiTransport(false), resetPending(false), forceTick(false),
			  midiClockTimer(0.f), virtualClockTimer(0.f), startTimer(0.f), stopTimer(0.f),
			  advanceTimer(0.f), strobeTimer(0.f), idleTime(0.f),
			  lastClockSeq(0), lastStartSeq(0), lastStopSeq(0), lastContinueSeq(0),
			  lastHeadEventSeq(0) {
			for (int c = 0; c < kMidiHeadControls; c++)
				lastCcSeq[c] = 0;
		}
	};

	void applyHeadCc(int cc, float value) {
		switch (cc) {
			case 0: runtime_.virtualClockTimer = 1e-3f; break;
			case 1: runtime_.startTimer = 1e-3f; break;
			case 2: runtime_.stopTimer = 1e-3f; break;
			case 3: runtime_.advanceTimer = 1e-3f; break;
			case 4: runtime_.resetPending = true; break;
			case 5: config_.addressKnob = value < 0.f ? 0.f : (value > 10.f ? 10.f : value); break;
			case 6: config_.addrExt = value >= 0.5f; break;
			case 7: {
				int mode = clampInt((int)std::round(value), 0, 2);
				if (mode == 0)
					runtime_.strobeTimer = 1e-3f;
				else
					config_.continuous = mode == 2;
				break;
			}
			case 8: config_.direction = (uint8_t)clampInt((int)std::round(value), 0, 4); break;
			case 9: runtime_.clockSource = clampInt((int)std::round(value), 0, 3); break;
			case 10: config_.clkDivIndex = (uint8_t)clampInt((int)std::round(value), 0, 8); break;
			case 11: config_.timeCvAmount = value < -1.f ? -1.f : (value > 1.f ? 1.f : value); break;
			case 12: config_.loopMode = (uint8_t)clampInt((int)std::round(value), 0, 2); break;
			default: break;
		}
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

	HeadConfig config_;
	Runtime runtime_;
	HeadDSP dsp_;
};

} // namespace spacetime
