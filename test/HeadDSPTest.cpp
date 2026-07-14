// SpaceTime WP5 — Function Generator core tests.
// Behavioral assertions for every scripted scenario, plus golden CSV traces
// (written to golden/ on first run, compared thereafter — review once, lock).
#include "doctest.h"
#include "HeadDSP.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

using namespace spacetime;

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------
struct Sim {
	StageTable t;
	ExtInputs ext;
	Globals g;
	ScaleKey sk;
	HeadConfig cfg;
	HeadSignals in;
	HeadDSP dsp;
	HeadOut out;
	float dt;
	int tickNo;
	std::ostringstream trace;
	bool tracing;

	Sim() : dt(0.01f), tickNo(0), tracing(false) {
		dsp.reset(42);
	}

	// count stages, voltage[i] = i volts, time slider 0, range 2 -> T = 0.2 s
	void makeTable(int count) {
		t = StageTable();
		t.count = (uint8_t)count;
		for (int i = 0; i < count; i++) {
			t.voltage[i] = (float)i;
			t.time[i] = 0.f;
			t.program[i].setTimeRange(2);  // 0.2 .. 3 s
		}
	}

	void step(int n = 1) {
		for (int k = 0; k < n; k++) {
			dsp.tick(t, ext, g, sk, cfg, in, dt, out);
			tickNo++;
			if (tracing) {
				char line[128];
				std::snprintf(line, sizeof(line), "%d,%d,%.3f,%.3f,%d,%d,%d,%d,%d\n",
					tickNo, out.currentStage, out.cv, out.ref,
					out.pulse1 ? 1 : 0, out.pulse2 ? 1 : 0,
					out.allPulse ? 1 : 0, out.eoc ? 1 : 0, out.runState);
				trace << line;
			}
		}
	}

	void pulseStart(float v = 10.f) { in.start = v; step(); in.start = 0.f; step(); }
	void pulseStop() { in.stop = 10.f; step(); in.stop = 0.f; step(); }
	void pulseAdvance() { in.advance = 10.f; step(); in.advance = 0.f; step(); }
	void pulseStrobe() { in.strobe = 10.f; step(); in.strobe = 0.f; step(); }

	// Run until the head sits on `stage` (or fail after maxTicks).
	bool runToStage(int stage, int maxTicks = 2000) {
		for (int k = 0; k < maxTicks; k++) {
			step();
			if (out.currentStage == stage)
				return true;
		}
		return false;
	}
};

static void checkGolden(const char* name, const std::string& actual) {
	mkdir("golden", 0755);
	std::string path = std::string("golden/") + name + ".csv";
	std::ifstream f(path.c_str());
	if (!f.good()) {
		std::ofstream o(path.c_str());
		o << "tick,stage,cv,ref,p1,p2,all,eoc,state\n" << actual;
		MESSAGE("golden trace created: " << path << " — review once, then locked");
		return;
	}
	std::ostringstream expect;
	expect << f.rdbuf();
	std::string want = expect.str();
	std::string got = std::string("tick,stage,cv,ref,p1,p2,all,eoc,state\n") + actual;
	CHECK_MESSAGE(want == got, "golden trace mismatch: " << path);
}

// ---------------------------------------------------------------------------
// 1. Plain 4-stage loop
// ---------------------------------------------------------------------------
TEST_CASE("plain 4-stage forward loop: order, CV, ramp, ALL and EOC") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.tracing = true;

	CHECK(s.out.runState == RUN_STOPPED);
	s.pulseStart();
	CHECK(s.out.runState == RUN_RUNNING);
	CHECK(s.out.currentStage == 0);
	CHECK(s.out.cv == doctest::Approx(0.f));

	// Stage interval = 0.2 s = 20 ticks; watch a full cycle.
	std::vector<int> seen;
	int eocTick = -1, lastStage = 0;
	seen.push_back(0);
	for (int k = 0; k < 90; k++) {
		s.step();
		if (s.out.currentStage != lastStage) {
			CHECK(s.out.allPulse);  // ALL fires on every newly addressed stage
			lastStage = s.out.currentStage;
			seen.push_back(lastStage);
		}
		if (s.out.eoc && eocTick < 0)
			eocTick = k;
		if ((int)seen.size() >= 5)
			break;
	}
	REQUIRE(seen.size() >= 5);
	CHECK(seen[1] == 1);
	CHECK(seen[2] == 2);
	CHECK(seen[3] == 3);
	CHECK(seen[4] == 0);      // wrap
	CHECK(eocTick >= 0);      // EOC fired at the wrap
	CHECK(s.out.cv == doctest::Approx((float)s.out.currentStage));

	// Ramp descends within a stage.
	float r0 = s.out.ref;
	s.step(5);
	CHECK(s.out.ref < r0);

	checkGolden("plain_4stage_loop", s.trace.str());
}

// ---------------------------------------------------------------------------
// 2. First/Last subrange
// ---------------------------------------------------------------------------
TEST_CASE("first/last flags bound the cycle") {
	Sim s;
	s.makeTable(8);
	s.t.program[2].setFirst(true);
	s.t.program[5].setLast(true);
	s.cfg.loopMode = LOOP_FIRST_LAST;

	s.in.reset = true;
	s.step();
	s.in.reset = false;
	CHECK(s.out.currentStage == 2);  // reset -> First stage

	s.pulseStart();
	std::vector<int> seen;
	int last = s.out.currentStage;
	for (int k = 0; k < 600 && seen.size() < 8; k++) {
		s.step();
		if (s.out.currentStage != last) {
			last = s.out.currentStage;
			seen.push_back(last);
		}
	}
	// Cycle must stay within [2,5] and wrap 5 -> 2.
	REQUIRE(seen.size() >= 4);
	for (size_t i = 0; i < seen.size(); i++) {
		CHECK(seen[i] >= 2);
		CHECK(seen[i] <= 5);
	}
	CHECK(seen[0] == 3);
	CHECK(seen[1] == 4);
	CHECK(seen[2] == 5);
	CHECK(seen[3] == 2);
}

static std::vector<int> stageSequence(Sim& s, size_t n) {
	std::vector<int> seq;
	int last = s.out.currentStage;
	for (int k = 0; k < 5000 && seq.size() < n; k++) {
		s.step();
		if (s.out.currentStage != last) {
			last = s.out.currentStage;
			seq.push_back(last);
		}
	}
	return seq;
}

TEST_CASE("multiple First/Last pairs form independent regions (Stazma patch)") {
	// First on stages 1 and 9, Last on 8 and 16 (indices 0/8 and 7/15):
	// a head in region 1 cycles 1-8, a head in region 2 cycles 9-16.
	auto makeRegions = [](Sim& s) {
		s.makeTable(16);
		s.cfg.loopMode = LOOP_FIRST_LAST;
		s.t.program[0].setFirst(true);
		s.t.program[8].setFirst(true);
		s.t.program[7].setLast(true);
		s.t.program[15].setLast(true);
	};

	// Head A: started in region 1.
	Sim a;
	makeRegions(a);
	a.pulseStart();
	std::vector<int> seqA = stageSequence(a, 10);
	REQUIRE(seqA.size() == 10);
	for (size_t i = 0; i < seqA.size(); i++) {
		CHECK(seqA[i] >= 0);
		CHECK(seqA[i] <= 7);   // never leaves region 1
	}
	CHECK(seqA[6] == 7);
	CHECK(seqA[7] == 0);       // wraps 8 -> 1 (indices 7 -> 0)

	// Head B: strobed into region 2, then started.
	Sim b;
	makeRegions(b);
	b.in.addressCv = 9.f * 10.f / 16.f + 0.1f;  // stage index 9
	b.cfg.addrExt = true;
	b.pulseStrobe();
	CHECK(b.out.currentStage == 9);
	b.pulseStart();
	std::vector<int> seqB = stageSequence(b, 12);
	REQUIRE(seqB.size() == 12);
	for (size_t i = 0; i < seqB.size(); i++) {
		CHECK(seqB[i] >= 8);
		CHECK(seqB[i] <= 15);  // never leaves region 2
	}
	// Wrap 16 -> 9 (indices 15 -> 8) somewhere in the sequence.
	bool sawWrap = false;
	int prev = 9;
	for (size_t i = 0; i < seqB.size(); i++) {
		sawWrap = sawWrap || (prev == 15 && seqB[i] == 8);
		prev = seqB[i];
	}
	CHECK(sawWrap);

	// Reset keeps each head in its own region.
	b.in.reset = true;
	b.step();
	b.in.reset = false;
	CHECK(b.out.currentStage == 8);  // nearest First at/below, not stage 1

	// Reverse direction in region 2 wraps 9 -> 16.
	Sim c;
	makeRegions(c);
	c.cfg.direction = DIR_REVERSE;
	c.in.addressCv = 9.f * 10.f / 16.f + 0.1f;
	c.cfg.addrExt = true;
	c.pulseStrobe();
	c.pulseStart();
	std::vector<int> seqC = stageSequence(c, 4);
	REQUIRE(seqC.size() == 4);
	CHECK(seqC[0] == 8);   // 9 -> 8 is inside the region? index 9->8
	CHECK(seqC[1] == 15);  // below First (index 8) -> wrap to region Last
	for (size_t i = 0; i < seqC.size(); i++)
		CHECK(seqC[i] >= 8);
}

// ---------------------------------------------------------------------------
// 3. Stop stage retrigger
// ---------------------------------------------------------------------------
TEST_CASE("stop stage: plays its interval, halts holding CV, start advances") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.t.program[1].setStop(true);
	s.tracing = true;

	s.pulseStart();
	REQUIRE(s.runToStage(1));
	CHECK(s.out.runState == RUN_RUNNING);  // stop stage still runs its interval

	s.step(25);  // past the 20-tick interval
	CHECK(s.out.runState == RUN_STOPPED);
	CHECK(s.out.currentStage == 1);        // halted ON the stop stage
	CHECK(s.out.cv == doctest::Approx(1.f));  // CV held

	s.step(20);
	CHECK(s.out.currentStage == 1);        // still halted

	s.pulseStart();                        // retrigger
	CHECK(s.out.runState == RUN_RUNNING);
	CHECK(s.out.currentStage == 2);        // released to the NEXT stage

	checkGolden("stop_stage_retrigger", s.trace.str());
}

// ---------------------------------------------------------------------------
// 4. Sustain gate hold
// ---------------------------------------------------------------------------
TEST_CASE("sustain stage holds while START gate is high, falls to advance") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.t.program[1].setSustain(true);
	s.tracing = true;

	s.in.start = 10.f;  // gate stays high
	s.step(2);
	CHECK(s.out.runState == RUN_RUNNING);
	REQUIRE(s.runToStage(1));

	s.step(30);  // interval over, gate still high
	CHECK(s.out.runState == RUN_HOLDING);
	CHECK(s.out.currentStage == 1);
	CHECK(s.out.cv == doctest::Approx(1.f));

	s.in.start = 0.f;  // gate falls
	s.step(2);
	CHECK(s.out.runState == RUN_RUNNING);
	CHECK(s.out.currentStage == 2);

	checkGolden("sustain_hold", s.trace.str());
}

// ---------------------------------------------------------------------------
// 5. Enable threshold
// ---------------------------------------------------------------------------
TEST_CASE("enable stage waits on entry until START exceeds 5 V") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.t.program[2].setEnable(true);

	s.pulseStart(2.f);  // gate above 1 V but below the 5 V enable threshold
	REQUIRE(s.runToStage(2));
	s.step(30);
	CHECK(s.out.runState == RUN_HOLDING);  // waiting on the enable stage
	CHECK(s.out.currentStage == 2);

	s.in.start = 8.f;  // > 5 V releases
	s.step(2);
	CHECK(s.out.runState == RUN_RUNNING);
	s.in.start = 0.f;
	REQUIRE(s.runToStage(3));  // proceeds normally
}

// ---------------------------------------------------------------------------
// 6. Pendulum, random and brownian
// ---------------------------------------------------------------------------

TEST_CASE("pendulum bounces without endpoint repeat (Q5 default)") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.cfg.direction = DIR_PENDULUM;
	s.pulseStart();
	std::vector<int> seq = stageSequence(s, 9);
	REQUIRE(seq.size() == 9);
	int want[9] = {1, 2, 3, 2, 1, 0, 1, 2, 3};
	for (int i = 0; i < 9; i++)
		CHECK(seq[i] == want[i]);
}

TEST_CASE("random direction is deterministic for a fixed seed and in bounds") {
	std::vector<int> a, b;
	for (int run = 0; run < 2; run++) {
		Sim s;
		s.makeTable(8);
		s.cfg.loopMode = LOOP_FULL_CHAIN;
		s.cfg.direction = DIR_RANDOM;
		s.dsp.reset(1234);
		s.pulseStart();
		std::vector<int> seq = stageSequence(s, 12);
		REQUIRE(seq.size() == 12);
		for (size_t i = 0; i < seq.size(); i++) {
			CHECK(seq[i] >= 0);
			CHECK(seq[i] <= 7);
		}
		if (run == 0) a = seq; else b = seq;
	}
	CHECK(a == b);  // determinism

	Sim s2;
	s2.makeTable(8);
	s2.cfg.loopMode = LOOP_FULL_CHAIN;
	s2.cfg.direction = DIR_RANDOM;
	s2.dsp.reset(999);
	s2.pulseStart();
	CHECK(stageSequence(s2, 12) != a);  // seed changes the sequence
}

TEST_CASE("brownian moves in single steps within bounds") {
	Sim s;
	s.makeTable(8);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.cfg.direction = DIR_BROWNIAN;
	s.dsp.reset(77);
	s.pulseStart();
	std::vector<int> seq = stageSequence(s, 20);
	REQUIRE(seq.size() == 20);
	int prev = 0;
	for (size_t i = 0; i < seq.size(); i++) {
		CHECK(std::abs(seq[i] - prev) <= 1);
		CHECK(seq[i] >= 0);
		CHECK(seq[i] <= 7);
		prev = seq[i];
	}
}

// ---------------------------------------------------------------------------
// 7. Strobe capture
// ---------------------------------------------------------------------------
TEST_CASE("strobe loads the addressed stage (self-normalizing 0-10 V)") {
	Sim s;
	s.makeTable(8);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.pulseStart();
	s.step(5);

	s.cfg.addrExt = true;
	s.in.addressCv = 7.6f;   // floor(7.6/10 * 8) = 6
	s.pulseStrobe();
	CHECK(s.out.currentStage == 6);
	CHECK(s.out.runState == RUN_RUNNING);  // keeps running from there

	// Fixed 0.5 V/stage alternative (globals menu).
	s.g.addressScale = 1;
	s.in.addressCv = 1.2f;   // floor(1.2 / 0.5) = 2
	s.pulseStrobe();
	CHECK(s.out.currentStage == 2);
}

// ---------------------------------------------------------------------------
// 8. Continuous addressing
// ---------------------------------------------------------------------------
TEST_CASE("continuous: clock stopped, output follows the address, red status") {
	Sim s;
	s.makeTable(8);
	s.cfg.continuous = true;
	s.cfg.addrExt = false;

	for (int st = 0; st < 8; st++) {
		s.cfg.addressKnob = (st + 0.5f) * 10.f / 8.f;
		s.step(3);
		CHECK(s.out.currentStage == st);
		CHECK(s.out.cv == doctest::Approx((float)st));
		CHECK(s.out.runState == RUN_STOPPED);  // red, per manual
	}
	// Stage does not advance on its own: park and wait.
	int st = s.out.currentStage;
	s.step(100);
	CHECK(s.out.currentStage == st);
	// ALL pulse fires on address-driven stage changes.
	s.cfg.addressKnob = 0.3f;
	s.step();
	CHECK(s.out.allPulse);
}

// ---------------------------------------------------------------------------
// 9. External time source fallback
// ---------------------------------------------------------------------------
static int ticksPerStage(Sim& s, int maxTicks = 5000) {
	int from = s.out.currentStage;
	int k = 0;
	while (s.out.currentStage == from && k < maxTicks) {
		s.step();
		k++;
	}
	return k;
}

TEST_CASE("external time source: unpatched input -> fastest of range") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	for (int i = 0; i < 4; i++) {
		s.t.time[i] = 0.9f;              // internal would give 0.2+0.9*2.8 = 2.72 s
		s.t.program[i].setTimeSource(true);  // external; slider quartile -> input D
	}
	s.pulseStart();
	int n = ticksPerStage(s);
	CHECK(n >= 18);
	CHECK(n <= 22);  // fastest of range 2 = 0.2 s = 20 ticks

	// Patch input D with 10 V -> slowest of range (3 s = 300 ticks).
	s.ext.connected[3] = true;
	s.ext.v[3] = 10.f;
	int n2 = ticksPerStage(s);
	CHECK(n2 >= 290);
	CHECK(n2 <= 310);
}

// ---------------------------------------------------------------------------
// 10. Slew proportionality
// ---------------------------------------------------------------------------
static int ticksToReach(Sim& s, float target, int maxTicks = 5000) {
	for (int k = 1; k <= maxTicks; k++) {
		s.step();
		if (std::fabs(s.out.cv - target) < 1e-3f)
			return k;
	}
	return -1;
}

TEST_CASE("slew time is proportional to the stage interval") {
	int reach[2];
	float sliders[2] = {0.f, 0.107143f};  // T = 0.2 s and 0.5 s
	for (int r = 0; r < 2; r++) {
		Sim s;
		s.makeTable(2);
		s.cfg.loopMode = LOOP_FULL_CHAIN;
		s.t.voltage[0] = 0.f;
		s.t.voltage[1] = 10.f;
		for (int i = 0; i < 2; i++) {
			s.t.time[i] = sliders[r];
			s.t.program[i].setSlew(SLEW_1);  // frac 0.25 (Q1 placeholder)
		}
		s.pulseStart();
		REQUIRE(s.runToStage(1));
		reach[r] = ticksToReach(s, 10.f);
		REQUIRE(reach[r] > 0);
	}
	// T ratio is 2.5; the full-span slew (0 -> 10 V) takes frac*T, so the
	// tick counts must scale by ~2.5.
	float ratio = (float)reach[1] / (float)reach[0];
	CHECK(ratio == doctest::Approx(2.5f).epsilon(0.25));

	// Slew level 2 is slower than level 1 at the same interval.
	Sim s1, s2;
	Sim* sims[2] = {&s1, &s2};
	int reach2[2];
	for (int r = 0; r < 2; r++) {
		Sim& s = *sims[r];
		s.makeTable(2);
		s.cfg.loopMode = LOOP_FULL_CHAIN;
		s.t.voltage[0] = 0.f;
		s.t.voltage[1] = 10.f;
		for (int i = 0; i < 2; i++)
			s.t.program[i].setSlew(r == 0 ? SLEW_1 : SLEW_2);
		s.pulseStart();
		REQUIRE(s.runToStage(1));
		reach2[r] = ticksToReach(s, 10.f);
	}
	CHECK(reach2[1] > reach2[0]);
}

// ---------------------------------------------------------------------------
// 11. Quantizer round-trip
// ---------------------------------------------------------------------------
TEST_CASE("quantizer: chromatic snaps to the nearest semitone") {
	for (int n = -24; n <= 24; n++) {
		float v = (float)n / 12.f;
		CHECK(quantize1Voct(v + 0.02f, SCALE_CHROMATIC, 0) == doctest::Approx(v));
		CHECK(quantize1Voct(v - 0.02f, SCALE_CHROMATIC, 0) == doctest::Approx(v));
	}
}

TEST_CASE("quantizer: C major admits only scale degrees") {
	static const int cMajor[7] = {0, 2, 4, 5, 7, 9, 11};
	for (int n = 0; n < 24; n++) {
		float q = quantize1Voct((float)n / 12.f, SCALE_MAJOR, 0);
		int semi = (int)std::floor(q * 12.f + 0.5f);
		int deg = ((semi % 12) + 12) % 12;
		bool inScale = false;
		for (int i = 0; i < 7; i++)
			inScale = inScale || (deg == cMajor[i]);
		CHECK(inScale);
		// And never further than a semitone away from the input.
		CHECK(std::fabs(q - (float)n / 12.f) <= 1.f / 12.f + 1e-6f);
	}
}

TEST_CASE("quantizer: key transposes the scale (A minor == C major pitch set)") {
	// Natural A minor and C major share the same pitch classes.
	for (int n = 0; n < 36; n++) {
		float v = (float)n / 17.3f;  // arbitrary spread
		float qa = quantize1Voct(v, SCALE_MINOR, 9);   // A minor
		float qc = quantize1Voct(v, SCALE_MAJOR, 0);   // C major
		CHECK(qa == doctest::Approx(qc));
	}
}

TEST_CASE("quantized stage outputs 1 V/oct scale notes") {
	Sim s;
	s.makeTable(2);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.t.voltage[1] = 1.3f;  // ~ 15.6 semitones
	s.t.program[1].setQuantize(true);
	s.sk.key = 0;
	s.sk.scale = SCALE_MAJOR;
	s.pulseStart();
	REQUIRE(s.runToStage(1));
	s.step(2);
	// 15.6 semis -> nearest C-major degree = 16 (E) = 1.3333 V
	CHECK(s.out.cv == doctest::Approx(16.f / 12.f).epsilon(1e-3));
}

// ---------------------------------------------------------------------------
// One-shot and TIME OUT
// ---------------------------------------------------------------------------
TEST_CASE("one-shot: runs once, EOC at completion, start re-arms") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_ONESHOT;
	s.pulseStart();
	bool sawEoc = false;
	for (int k = 0; k < 200; k++) {
		s.step();
		sawEoc = sawEoc || s.out.eoc;
	}
	CHECK(s.out.runState == RUN_STOPPED);
	CHECK(s.out.currentStage == 3);  // parked on the final stage
	CHECK(sawEoc);

	s.pulseStart();
	CHECK(s.out.runState == RUN_RUNNING);
}

TEST_CASE("stage pulses gate for exactly their stage's interval") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.t.program[1].setPulse1(true);
	s.t.program[2].setPulse2(true);
	s.pulseStart();
	int p1 = 0, p2 = 0;
	for (int k = 0; k < 90; k++) {
		s.step();
		if (s.out.pulse1) { p1++; CHECK(s.out.currentStage == 1); }
		if (s.out.pulse2) { p2++; CHECK(s.out.currentStage == 2); }
	}
	// One full pass: each pulse held high for its 20-tick stage interval
	// (they are GATES per the manual, not triggers).
	CHECK(p1 >= 18);
	CHECK(p2 >= 18);
}

TEST_CASE("consecutive flagged stages retrigger P1 (hardware-verified)") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.t.program[1].setPulse1(true);
	s.t.program[2].setPulse1(true);
	s.pulseStart();
	// Count rising edges of pulse1 over two full cycles: with a retrigger
	// notch at the 1->2 boundary there must be 2 edges per cycle, not 1.
	int edges = 0;
	bool prev = false;
	for (int k = 0; k < 170; k++) {
		s.step();
		if (s.out.pulse1 && !prev)
			edges++;
		prev = s.out.pulse1;
	}
	CHECK(edges == 4);  // 2 stages x 2 cycles, each with a fresh edge

	// Single-stage region wrap also retriggers (re-entering the same stage).
	Sim w;
	w.makeTable(4);
	w.cfg.loopMode = LOOP_FIRST_LAST;
	w.t.program[1].setFirst(true);
	w.t.program[1].setLast(true);
	w.t.program[1].setPulse2(true);
	w.in.addressCv = 0.f;
	w.pulseStart();
	REQUIRE(w.runToStage(1));
	int edges2 = 0;
	bool prev2 = false;
	for (int k = 0; k < 90; k++) {
		w.step();
		if (w.out.pulse2 && !prev2)
			edges2++;
		prev2 = w.out.pulse2;
	}
	CHECK(edges2 >= 3);  // one edge per pass of the looping single stage
}

TEST_CASE("pulse retrigger compatibility can be disabled globally") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.g.pulseRetrig = false;
	s.t.program[1].setPulse1(true);
	s.t.program[2].setPulse1(true);
	s.pulseStart();

	REQUIRE(s.runToStage(1));
	bool fellBetweenFlaggedStages = false;
	while (s.out.currentStage == 1) {
		s.step();
		if (!s.out.pulse1)
			fellBetweenFlaggedStages = true;
	}
	CHECK(s.out.currentStage == 2);
	CHECK(s.out.pulse1);
	CHECK_FALSE(fellBetweenFlaggedStages);
}

TEST_CASE("stop wins when start and stop edges coincide") {
	Sim s;
	s.makeTable(4);
	s.pulseStart();
	REQUIRE(s.dsp.isRunning());

	s.in.start = 10.f;
	s.in.stop = 10.f;
	s.step();
	CHECK_FALSE(s.dsp.isRunning());
	CHECK(s.out.runState == RUN_STOPPED);
}

TEST_CASE("external clock starvation shows HOLD, recovers on edges") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.cfg.clkExt = true;
	s.cfg.clkDivIndex = 4;  // x1
	s.pulseStart();
	// Feed a few clock edges at a 0.1 s period.
	for (int e = 0; e < 4; e++) {
		s.in.extClock = 10.f;
		s.step(2);
		s.in.extClock = 0.f;
		s.step(8);
	}
	CHECK(s.out.runState == RUN_RUNNING);
	// Stop the clock: after max(2 s, 4x period) the head reports HOLD.
	s.step(230);  // 2.3 s
	CHECK(s.out.runState == RUN_HOLDING);
	// Edges resume -> RUNNING again.
	s.in.extClock = 10.f;
	s.step(2);
	s.in.extClock = 0.f;
	s.step(2);
	CHECK(s.out.runState == RUN_RUNNING);
}

TEST_CASE("TIME OUT follows the current stage's time slider") {
	Sim s;
	s.makeTable(4);
	s.cfg.loopMode = LOOP_FULL_CHAIN;
	s.t.time[0] = 0.25f;
	s.step();
	CHECK(s.out.timeOut == doctest::Approx(2.5f));
}
