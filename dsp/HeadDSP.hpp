#pragma once
// SpaceTime — Function Generator core (WP5).
// Rack-free: no Rack SDK includes permitted in this file.
//
// Pure, tick-driven state machine: everything is a function of
// (state, inputs, dt). Sample-rate-agnostic, no allocation, deterministic
// (seeded xorshift RNG for random/brownian directions).
//
// Behaviour switches for open hardware questions live in kBehaviour below —
// single definition point, hardware observations slot in without rework:
//   Q1  slew level fractions      -> Globals.slewFrac1/2 (Chain.hpp)
//   Q2  continuous-mode interpolation between adjacent stages
//   Q3  limited-range octave offsets (limitedOctaveOffset, StageTable.hpp)
//   Q5  pendulum endpoint repeat
//
// Documented interpretation of the manual (hardware-checkable):
//   * Stop stage: plays its full interval, then halts holding the CV
//     (red status). A start pulse advances to the next stage and resumes.
//   * Sustain stage: at interval end, holds (yellow) while the START gate
//     is high (>= 1 V); falling gate advances.
//   * Enable stage: on entry, waits (yellow) until START exceeds 5 V.
//   * External time source: the stage's TIME slider selects input A-D by
//     quartile (same mechanism as the voltage slider; Q4 assumption);
//     unpatched input -> fastest value of the selected range.
//   * Slew is rate-limited: the CV traverses the full 10 V span in
//     slewFrac * interval seconds, so slew time is proportional to the
//     stage interval (manual).
//   * EOC fires on loop wrap (fwd: last->first, rev: first->last), on the
//     pendulum bounce at either end, at one-shot completion and at a stop-
//     stage halt. Random/brownian have no cycle: no EOC.
//   * Continuous addressing: internal clock stopped, stage follows the
//     address directly (red status, per manual); REF holds at 10 V, ALL
//     pulses fire on stage changes, stage pulse gates stay active.

#include <cstdint>
#include <cmath>
#include "StageTable.hpp"
#include "Chain.hpp"
#include "PresetRow.hpp"  // ScaleId

namespace spacetime {

// ---- Behaviour switches (open questions Q2/Q5) -------------------------------
struct Behaviour {
	bool continuousInterpolates;   // Q2: fractional address interpolates CV
	bool pendulumRepeatsEndpoints; // Q5: play end stages twice on reversal
};
static const Behaviour kBehaviour = {false, false};

// ---- Public value enums --------------------------------------------------------
enum Direction {
	DIR_FORWARD = 0,
	DIR_REVERSE = 1,
	DIR_PENDULUM = 2,
	DIR_RANDOM = 3,
	DIR_BROWNIAN = 4
};
enum LoopMode {
	LOOP_ONESHOT = 0,
	LOOP_FIRST_LAST = 1,
	LOOP_FULL_CHAIN = 2
};

static const float kGateThreshold = 1.f;     // start/stop/advance/strobe gates
static const float kEnableThreshold = 5.f;   // enable: START > 5 V (manual)
static const float kTrigDuration = 1e-3f;    // ALL/EOC trigger length, seconds
static const float kMinInterval = 1e-4f;     // clamp for interval time
// Hardware-verified (Peet, 2026-07-11): P1/P2 RETRIGGER per stage — between
// two consecutive flagged stages the gate drops for a short notch so every
// stage produces a fresh edge. Notch length is hardware-observable.
static const float kPulseRetrig = 1e-3f;
// VCV extension: a head on EXT clock with no incoming edges is starving, not
// fine — show HOLD (yellow) after this long without an edge (or 4x the last
// measured period, whichever is longer) instead of a misleading green.
static const float kClockStarve = 2.f;

// ---- Quantizer ------------------------------------------------------------------
// 1 V/oct semitone snap; scale degrees relative to key. ScaleId from PresetRow.hpp.
inline float quantize1Voct(float v, uint8_t scale, uint8_t key) {
	static const uint16_t masks[3] = {
		0x0AB5,  // major: 0 2 4 5 7 9 11
		0x05AD,  // natural minor: 0 2 3 5 7 8 10
		0x0FFF   // chromatic
	};
	uint16_t mask = masks[scale <= 2 ? scale : 2];
	float semis = v * 12.f;
	int base = (int)std::floor(semis + 0.5f);
	for (int d = 0; d <= 12; d++) {
		for (int sgn = 0; sgn < 2; sgn++) {
			int k = base + (sgn ? -d : d);
			int deg = ((k - (int)key) % 12 + 12) % 12;
			if (mask & (1u << deg))
				return (float)k / 12.f;
			if (d == 0)
				break;  // +0 == -0
		}
	}
	return v;  // unreachable: chromatic always matches
}

// ---- Per-tick inputs -------------------------------------------------------------
struct ExtInputs {
	float v[4];
	bool connected[4];

	ExtInputs() {
		for (int i = 0; i < 4; i++) {
			v[i] = 0.f;
			connected[i] = false;
		}
	}
};

struct HeadConfig {          // panel controls
	bool continuous;         // Cont/Seq latch (up = continuous)
	bool addrExt;            // Int/Ext address latch
	float addressKnob;       // 0..10 V
	uint8_t direction;       // Direction
	bool clkExt;             // clock source
	uint8_t clkDivIndex;     // 0..8 -> /16 /8 /4 /2 x1 x2 x4 x8 x16 (4 = x1)
	float timeCvAmount;      // -1..1 attenuverter
	uint8_t loopMode;        // LoopMode

	HeadConfig()
		: continuous(false), addrExt(false), addressKnob(0.f),
		  direction(DIR_FORWARD), clkExt(false), clkDivIndex(4),
		  timeCvAmount(0.f), loopMode(LOOP_FIRST_LAST) {}
};

struct HeadSignals {         // jack/button inputs, volts
	float start;             // gate/pulse; also sustain gate and enable level
	float stop;
	float advance;
	float strobe;
	float addressCv;         // 0..10 V
	float extClock;
	float timeCv;            // scales all stage times (exponential)
	bool reset;              // reset button/event

	HeadSignals()
		: start(0.f), stop(0.f), advance(0.f), strobe(0.f), addressCv(0.f),
		  extClock(0.f), timeCv(0.f), reset(false) {}
};

struct HeadOut {             // per-tick outputs
	float cv;                // stage voltage after modifiers (1 V/oct if quantized)
	float timeOut;           // current stage's time slider, 0..10 V
	float ref;               // downward ramp 10..0 V over the interval
	bool pulse1, pulse2;     // stage gates
	bool allPulse;           // trigger on every newly addressed stage
	bool eoc;                // trigger at cycle end / stop
	uint8_t currentStage;    // global stage index
	float phase;             // 0..1 within the interval
	uint8_t runState;        // RunState (Chain.hpp): green/yellow/red

	HeadOut()
		: cv(0.f), timeOut(0.f), ref(10.f), pulse1(false), pulse2(false),
		  allPulse(false), eoc(false), currentStage(0), phase(0.f),
		  runState(RUN_STOPPED) {}
};

// ---- The Function Generator core ---------------------------------------------------
class HeadDSP {
public:
	HeadDSP() { reset(1); }

	// Full reset (patch load / seed change). Deterministic for a given seed.
	void reset(uint32_t seed) {
		rng_ = seed ? seed : 1;
		stage_ = 0;
		phase_ = 0.f;
		running_ = false;
		holding_ = false;
		enableWait_ = false;
		stoppedOnStop_ = false;
		pendulumFwd_ = true;
		cvNow_ = 0.f;
		prevStart_ = prevStop_ = prevAdvance_ = prevStrobe_ = prevClock_ = false;
		clockPeriod_ = 0.f;
		clockTimer_ = 0.f;
		subTimer_ = 0.f;
		edgeCount_ = 0;
		allTimer_ = eocTimer_ = 0.f;
		retrig1_ = retrig2_ = 0.f;
		prevContStage_ = -1;
	}

	// One control tick of dt seconds.
	void tick(const StageTable& table, const ExtInputs& ext, const Globals& g,
	          const ScaleKey& sk, const HeadConfig& cfg, const HeadSignals& in,
	          float dt, HeadOut& out) {
		out = HeadOut();
		allTimer_ = std::fmax(0.f, allTimer_ - dt);
		eocTimer_ = std::fmax(0.f, eocTimer_ - dt);
		retrig1_ = std::fmax(0.f, retrig1_ - dt);
		retrig2_ = std::fmax(0.f, retrig2_ - dt);

		if (table.count == 0) {
			out.runState = RUN_STOPPED;
			out.ref = 10.f;
			return;
		}

		// Edges (always processed so gate history stays correct)
		bool startEdge = edge(in.start >= kGateThreshold, prevStart_);
		bool stopEdge = edge(in.stop >= kGateThreshold, prevStop_);
		bool advEdge = edge(in.advance >= kGateThreshold, prevAdvance_);
		bool strobeEdge = edge(in.strobe >= kGateThreshold, prevStrobe_);
		bool clockEdge = edge(in.extClock >= kGateThreshold, prevClock_);

		if (stage_ >= table.count) stage_ = table.count - 1;
		if (stage_ < 0) stage_ = 0;

		float addr = cfg.addrExt ? in.addressCv : cfg.addressKnob;

		// ---- Continuous addressing: clock stopped, output follows address.
		if (cfg.continuous) {
			int s = addressToStage(addr, table.count, g);
			if (s != prevContStage_) {
				fireAll();
				pulseRetrig(table, stage_, s);
				prevContStage_ = s;
			}
			stage_ = s;
			phase_ = 0.f;
			running_ = false;
			holding_ = false;
			stoppedOnStop_ = false;
			// Q2 switch: no inter-stage interpolation until verified.
			cvNow_ = targetVoltage(table, ext, sk, stage_);
			emit(table, out, RUN_STOPPED, /*pulsesActive=*/true);
			out.ref = 10.f;
			return;
		}
		prevContStage_ = -1;

		// ---- Events
		if (in.reset)
			enterStage(table, resetStage(table, cfg.loopMode, stage_));
		if (strobeEdge)
			enterStage(table, addressToStage(addr, table.count, g));
		if (stopEdge) {
			running_ = false;
			holding_ = false;
			enableWait_ = false;
			stoppedOnStop_ = false;
		}
		if (startEdge) {
			if (stoppedOnStop_) {
				advance(table, cfg.loopMode, cfg.direction);  // release the stop stage
				stoppedOnStop_ = false;
			}
			running_ = true;
			holding_ = false;
			enableWait_ = false;
			phase_ = 0.f;
			checkEnable(table, in);
		}
		if (advEdge) {
			advance(table, cfg.loopMode, cfg.direction);
			stoppedOnStop_ = false;
			if (running_)
				checkEnable(table, in);
		}

		// ---- Interval progression
		float T = intervalTime(table, ext, cfg, in, stage_);

		if (running_ && holding_) {
			const ProgramWord& w = table.program[stage_];
			if (enableWait_) {
				// Enable wait (stage entry): released by START > 5 V.
				if (in.start > kEnableThreshold) {
					holding_ = false;
					enableWait_ = false;
				}
			}
			else if (w.sustain() && in.start < kGateThreshold) {
				// Sustain hold (interval end): released by the falling gate.
				holding_ = false;
				advance(table, cfg.loopMode, cfg.direction);
				checkEnable(table, in);
			}
		}
		else if (running_) {
			if (cfg.clkExt) {
				progressExtClock(table, in, cfg, clockEdge, dt);
			}
			else {
				phase_ += dt / T;
				int guard = 0;
				while (phase_ >= 1.f && running_ && !holding_ && guard++ <= kMaxStages)
					endOfStage(table, in, cfg.direction, cfg.loopMode);
			}
		}

		// ---- CV: rate-limited slew toward the current target.
		float target = targetVoltage(table, ext, sk, stage_);
		float slewT = slewTime(table.program[stage_], g, T);
		if (slewT <= 0.f) {
			cvNow_ = target;
		}
		else {
			float step = (dt / slewT) * 10.f;  // 10 V span in slewT seconds
			float d = target - cvNow_;
			if (std::fabs(d) <= step)
				cvNow_ = target;
			else
				cvNow_ += (d > 0.f ? step : -step);
		}

		uint8_t state = running_ ? (holding_ ? RUN_HOLDING : RUN_RUNNING)
		                         : RUN_STOPPED;
		// External-clock starvation: running but no edges arriving -> HOLD.
		if (state == RUN_RUNNING && cfg.clkExt) {
			float starve = std::fmax(kClockStarve, 4.f * clockPeriod_);
			if (clockTimer_ > starve)
				state = RUN_HOLDING;
		}
		emit(table, out, state, running_ || stoppedOnStop_);
	}

	int currentStage() const { return stage_; }
	bool isRunning() const { return running_; }

private:
	// ---- state
	uint32_t rng_;
	int stage_;
	float phase_;
	bool running_, holding_, enableWait_, stoppedOnStop_, pendulumFwd_;
	float cvNow_;
	bool prevStart_, prevStop_, prevAdvance_, prevStrobe_, prevClock_;
	float clockPeriod_, clockTimer_, subTimer_;
	int edgeCount_;
	float allTimer_, eocTimer_;
	float retrig1_, retrig2_;
	int prevContStage_;

	static bool edge(bool now, bool& prev) {
		bool e = now && !prev;
		prev = now;
		return e;
	}

	uint32_t xorshift() {
		rng_ ^= rng_ << 13;
		rng_ ^= rng_ >> 17;
		rng_ ^= rng_ << 5;
		return rng_;
	}

	void fireAll() { allTimer_ = kTrigDuration; }
	void fireEoc() { eocTimer_ = kTrigDuration; }

	// REGIONAL bounds (hardware, cf. multiple First/Last pairs): the cycle is
	// bounded by the First at/below and the Last at/above the head's CURRENT
	// stage. Two heads parked in different regions loop independently
	// (e.g. First on 1 and 9, Last on 8 and 16 -> regions 1-8 and 9-16).
	static void loopBounds(const StageTable& t, uint8_t loopMode, int stage,
	                       int& lo, int& hi) {
		lo = 0;
		hi = t.count - 1;
		if (loopMode == LOOP_FULL_CHAIN)
			return;
		for (int i = stage; i >= 0; i--)
			if (t.program[i].first()) { lo = i; break; }
		for (int i = stage; i < t.count; i++)
			if (t.program[i].last()) { hi = i; break; }
	}

	// Reset target: nearest First at/below the current stage (stay in this
	// head's region); else the first First anywhere (manual); else stage 1.
	static int resetStage(const StageTable& t, uint8_t loopMode, int stage) {
		if (loopMode != LOOP_FULL_CHAIN) {
			for (int i = stage; i >= 0; i--)
				if (t.program[i].first()) return i;
			for (int i = 0; i < t.count; i++)
				if (t.program[i].first()) return i;
		}
		return 0;
	}

	static int addressToStage(float addr, int count, const Globals& g) {
		if (addr < 0.f) addr = 0.f;
		int s;
		if (g.addressScale == 1)
			s = (int)(addr / 0.5f);                 // fixed 0.5 V per stage
		else
			s = (int)(addr / 10.f * (float)count);  // self-normalizing 0-10 V
		if (s > count - 1) s = count - 1;
		if (s < 0) s = 0;
		return s;
	}

	// Retrigger notch: when leaving a flagged stage for another flagged stage
	// (incl. re-entering the same stage on a wrap), drop the gate briefly so
	// each stage produces a fresh edge (hardware-verified behaviour).
	void pulseRetrig(const StageTable& t, int oldStage, int newStage) {
		if (oldStage < 0 || oldStage >= t.count || newStage < 0 || newStage >= t.count)
			return;
		if (t.program[oldStage].pulse1() && t.program[newStage].pulse1())
			retrig1_ = kPulseRetrig;
		if (t.program[oldStage].pulse2() && t.program[newStage].pulse2())
			retrig2_ = kPulseRetrig;
	}

	void enterStage(const StageTable& t, int s) {
		if (s < 0) s = 0;
		if (s > t.count - 1) s = t.count - 1;
		pulseRetrig(t, stage_, s);
		stage_ = s;
		phase_ = 0.f;
		holding_ = false;
		enableWait_ = false;
		fireAll();
	}

	// One step in the given direction within the CURRENT region; handles
	// wraps, bounces, EOC.
	void advance(const StageTable& t, uint8_t loopMode, uint8_t dir) {
		int lo, hi;
		loopBounds(t, loopMode, stage_, lo, hi);
		int span = hi - lo + 1;
		int next = stage_;
		switch (dir) {
			case DIR_REVERSE:
				next = stage_ - 1;
				if (next < lo) {
					next = hi;
					fireEoc();
				}
				break;
			case DIR_PENDULUM:
				if (span == 1) {
					next = lo;
					fireEoc();
					break;
				}
				if (pendulumFwd_) {
					next = stage_ + 1;
					if (next >= hi) {
						pendulumFwd_ = false;
						fireEoc();
						// Q5: without endpoint repeat the bounce lands ON the
						// endpoint and reverses from there next step.
						next = kBehaviour.pendulumRepeatsEndpoints ? hi : (next > hi ? hi : next);
					}
				}
				else {
					next = stage_ - 1;
					if (next <= lo) {
						pendulumFwd_ = true;
						fireEoc();
						next = kBehaviour.pendulumRepeatsEndpoints ? lo : (next < lo ? lo : next);
					}
				}
				break;
			case DIR_RANDOM:
				next = lo + (int)(xorshift() % (uint32_t)span);
				break;
			case DIR_BROWNIAN:
				next = stage_ + ((xorshift() & 1u) ? 1 : -1);
				if (next < lo) next = (lo + 1 <= hi) ? lo + 1 : lo;  // reflect
				if (next > hi) next = (hi - 1 >= lo) ? hi - 1 : hi;
				break;
			default:  // DIR_FORWARD
				next = stage_ + 1;
				if (next > hi) {
					next = lo;
					fireEoc();
				}
				break;
		}
		enterStage(t, next);
	}

	void checkEnable(const StageTable& t, const HeadSignals& in) {
		if (t.program[stage_].enable() && in.start <= kEnableThreshold) {
			holding_ = true;
			enableWait_ = true;
			phase_ = 0.f;
		}
	}

	// The current stage's interval has completed (phase_ >= 1).
	void endOfStage(const StageTable& t, const HeadSignals& in,
	                uint8_t dir, uint8_t loopMode) {
		int lo, hi;
		loopBounds(t, loopMode, stage_, lo, hi);
		const ProgramWord& w = t.program[stage_];
		if (w.stop()) {
			// Stop stage respects its interval time, then halts (manual).
			running_ = false;
			stoppedOnStop_ = true;
			phase_ = 1.f;
			fireEoc();
			return;
		}
		if (w.sustain() && in.start >= kGateThreshold) {
			holding_ = true;
			enableWait_ = false;
			phase_ = 1.f;
			return;
		}
		bool atEnd = (dir == DIR_REVERSE) ? (stage_ == lo) : (stage_ == hi);
		if (loopMode == LOOP_ONESHOT && atEnd) {
			running_ = false;
			phase_ = 1.f;
			fireEoc();
			return;
		}
		float over = phase_ - 1.f;
		advance(t, loopMode, dir);
		phase_ = (over > 0.f && over < 1.f) ? over : 0.f;  // carry overshoot
		checkEnable(t, in);
	}

	// External clock: divisions count edges; multiplications subdivide the
	// measured period.
	void progressExtClock(const StageTable& t, const HeadSignals& in,
	                      const HeadConfig& cfg, bool clockEdge, float dt) {
		clockTimer_ += dt;
		subTimer_ += dt;
		int idx = cfg.clkDivIndex <= 8 ? cfg.clkDivIndex : 4;
		if (clockEdge) {
			clockPeriod_ = clockTimer_;
			clockTimer_ = 0.f;
		}
		if (idx <= 4) {
			int divide = 1 << (4 - idx);  // 16,8,4,2,1
			if (clockEdge) {
				if (++edgeCount_ >= divide) {
					edgeCount_ = 0;
					endOfStage(t, in, cfg.direction, cfg.loopMode);
					phase_ = 0.f;
				}
			}
			if (clockPeriod_ > 0.f) {
				float total = (float)divide * clockPeriod_;
				phase_ = std::fmin(1.f,
					((float)edgeCount_ * clockPeriod_ + clockTimer_) / total);
			}
		}
		else {
			int mult = 1 << (idx - 4);  // 2,4,8,16
			if (clockEdge) {
				subTimer_ = 0.f;
				endOfStage(t, in, cfg.direction, cfg.loopMode);
				phase_ = 0.f;
			}
			else if (clockPeriod_ > 0.f) {
				float sub = clockPeriod_ / (float)mult;
				int guard = 0;
				while (subTimer_ >= sub && running_ && !holding_ && guard++ <= mult) {
					subTimer_ -= sub;
					endOfStage(t, in, cfg.direction, cfg.loopMode);
				}
				phase_ = std::fmin(1.f, subTimer_ / sub);
			}
		}
	}

	// Stage interval in seconds (internal time source or external time CV).
	static float intervalTime(const StageTable& t, const ExtInputs& ext,
	                          const HeadConfig& cfg, const HeadSignals& in, int s) {
		const ProgramWord& w = t.program[s];
		int r = w.timeRange();
		float T;
		if (w.timeSource() == SOURCE_EXTERNAL) {
			// TIME slider selects input A-D by quartile (Q4 assumption).
			int sel = (int)(t.time[s] * 4.f);
			if (sel > 3) sel = 3;
			if (!ext.connected[sel]) {
				T = kTimeRangeMin[r];  // no CV -> fastest value of the range
			}
			else {
				float cv = ext.v[sel];
				if (cv < 0.f) cv = 0.f;
				if (cv > 10.f) cv = 10.f;
				T = kTimeRangeMin[r] + cv / 10.f * (kTimeRangeMax[r] - kTimeRangeMin[r]);
			}
		}
		else {
			T = kTimeRangeMin[r] + t.time[s] * (kTimeRangeMax[r] - kTimeRangeMin[r]);
		}
		// Head-level time CV: exponential; +2.5 V at amount 1 halves the time.
		if (cfg.timeCvAmount != 0.f && in.timeCv != 0.f)
			T *= std::exp2(-(in.timeCv * cfg.timeCvAmount) / 2.5f);
		return T < kMinInterval ? kMinInterval : T;
	}

	// Stage voltage after the voltage modifiers (before slew).
	static float targetVoltage(const StageTable& t, const ExtInputs& ext,
	                           const ScaleKey& sk, int s) {
		const ProgramWord& w = t.program[s];
		float v;
		if (w.voltageSource() == SOURCE_EXTERNAL) {
			// The stage's voltage slider selects the active input A-D (quartiles).
			int sel = (int)(t.voltage[s] / 10.f * 4.f);
			if (sel > 3) sel = 3;
			if (sel < 0) sel = 0;
			v = ext.v[sel];
		}
		else {
			v = t.voltage[s];
		}
		switch (w.range()) {
			case RANGE_HALF:
				v *= 0.5f;
				break;
			case RANGE_LIMITED:
				// 2 V span plus octave offset (exact offsets pending Q3).
				v = v * 0.2f + (float)limitedOctaveOffset(w.limitedOctave());
				break;
			default:
				break;
		}
		if (w.quantize())
			v = quantize1Voct(v, sk.scale, sk.key);
		return v;
	}

	static float slewTime(const ProgramWord& w, const Globals& g, float T) {
		switch (w.slew()) {
			case SLEW_1: return g.slewFrac1 * T;
			case SLEW_2: return g.slewFrac2 * T;
			default: return 0.f;
		}
	}

	void emit(const StageTable& t, HeadOut& out, uint8_t state, bool pulsesActive) {
		const ProgramWord& w = t.program[stage_];
		out.cv = cvNow_;
		out.timeOut = t.time[stage_] * 10.f;
		out.ref = 10.f * (1.f - std::fmin(phase_, 1.f));
		out.pulse1 = pulsesActive && w.pulse1() && retrig1_ <= 0.f;
		out.pulse2 = pulsesActive && w.pulse2() && retrig2_ <= 0.f;
		out.allPulse = allTimer_ > 0.f;
		out.eoc = eocTimer_ > 0.f;
		out.currentStage = (uint8_t)stage_;
		out.phase = std::fmin(phase_, 1.f);
		out.runState = state;
	}
};

} // namespace spacetime
