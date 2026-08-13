#pragma once

// Standalone, single-head MIDI-to-signals controller for a MetaModule
// HeadRemote (head relocation, METAMODULE_EXPANDER_BUS_PLAN.md EB8).
//
// This is the same translation SpaceTimeEngine already does internally, per
// head, inside applyMidiToHeads()/applyHeadCc()/processHeads() -- factored
// out rather than reused in place, deliberately: SpaceTimeEngine is shipped,
// hardware-tested (Core.cpp's fused path, Singularity's VCV twin) code, and
// this is a brand new, unverified module. Refactoring SpaceTimeEngine to
// share this class internally is a legitimate future cleanup once
// HeadRemote itself is proven -- not done here, same reasoning as deferring
// the MetaModuleStageBankRegistry/MetaModuleHeadRegistry template
// generalization until a second real consumer existed. The duplication is
// real and small (one switch statement, one signal-building block); a host
// test (HeadRemoteControllerTest.cpp) proves this produces byte-identical
// HeadConfig/signal behavior to SpaceTimeEngine's own for the same CC
// stream, so the two can't silently drift without a test failing.
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
	void applyMidi(const uint32_t (&ccSeq)[kMidiHeadControls],
	               const float (&ccValue)[kMidiHeadControls],
	               uint32_t midiClockSeq, uint32_t midiStartSeq,
	               uint32_t midiStopSeq, uint32_t midiContinueSeq) {
		bool clockChanged = midiClockSeq != runtime_.lastClockSeq;
		if (clockChanged) {
			runtime_.lastClockSeq = midiClockSeq;
			runtime_.midiClockTimer = 1e-3f;
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
		if (runtime_.followMidiTransport && (startChanged || continueChanged || stopChanged || clockChanged))
			runtime_.forceTick = true;

		for (int control = 0; control < kMidiHeadControls; control++) {
			uint32_t sequence = ccSeq[control];
			if (sequence == runtime_.lastCcSeq[control])
				continue;
			runtime_.lastCcSeq[control] = sequence;
			applyHeadCc(control, ccValue[control]);
			runtime_.forceTick = true;
		}
	}

	// Merges MIDI/CC-driven timers with locally-supplied signals (real
	// panel jacks, if this HeadRemote exposes any) and ticks HeadDSP.
	// `local` may leave any field at 0.f/false for signals this module has
	// no jack for -- exactly as Head.cpp's own jack-vs-MIDI merge already
	// works (both write into the same signal, whichever is higher/newer
	// wins for a gate). Preserves SpaceTimeEngine::processHeads' idle-head
	// refresh gating (skip full audio-rate ticks for a stopped, non-
	// continuous head with no pending event, refresh at ~3 kHz instead) --
	// the same optimization the Core hardware test measured at ~3.5 points
	// of CPU per active head, still worth keeping per HeadRemote instance.
	void tick(const StageTable& table, const ExtInputs& ext, const Globals& g,
	          const ScaleKey& sk, const HeadSignals& local, float dt, HeadOut& out) {
		runtime_.idleTime += dt;
		bool refreshIdle = runtime_.idleTime >= 1.f / 3000.f;
		// SpaceTimeEngine's own audioRate condition, plus one HeadRemote-
		// specific addition: Core and Singularity have no real per-head
		// start/stop/advance/strobe/reset/clock jacks (MIDI is their only
		// input for those), so this case never arose there. HeadRemote, by
		// design, does have real local jacks -- without this, a cold
		// controller's very first tick with only a local jack signal active
		// (no MIDI forceTick yet, not already running, not continuous)
		// would be silently skipped by the idle-refresh gate and miss the
		// edge entirely. Caught by
		// "HeadRemoteController local jack signals merge with MIDI-driven
		// ones" before this shipped anywhere.
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
		uint32_t lastCcSeq[kMidiHeadControls];

		Runtime()
			: clockSource(0), followMidiTransport(false), resetPending(false), forceTick(false),
			  midiClockTimer(0.f), virtualClockTimer(0.f), startTimer(0.f), stopTimer(0.f),
			  advanceTimer(0.f), strobeTimer(0.f), idleTime(0.f),
			  lastClockSeq(0), lastStartSeq(0), lastStopSeq(0), lastContinueSeq(0) {
			for (int c = 0; c < kMidiHeadControls; c++)
				lastCcSeq[c] = 0;
		}
	};

	// Same switch as SpaceTimeEngine::applyHeadCc, verbatim.
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

	// Verbatim copy of SpaceTimeEngine's own pulseGate: reads the current
	// gate level and decays the timer by dt in the same call, called
	// exactly once per timer per tick() -- matching processHeads' own
	// single-call-per-timer discipline so decay never runs twice for one
	// tick.
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
