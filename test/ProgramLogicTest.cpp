// SpaceTime WP6 — program-section logic tests.
#include "doctest.h"
#include "ProgramLogic.hpp"

using namespace spacetime;

static StageTable makeTable(int count) {
	StageTable t;
	t.count = (uint8_t)count;
	for (int i = 0; i < count; i++) {
		t.voltage[i] = (float)i;
		t.time[i] = 0.1f * i;
	}
	return t;
}

// ---------------------------------------------------------------------------
// New protocol fields (appended in WP6)
// ---------------------------------------------------------------------------
TEST_CASE("ClearWord op resets the program word; value must be 0") {
	ProgramWord w;
	w.setStop(true);
	w.setSlew(2);
	CHECK(setProgramField(w, Field::ClearWord, 0));
	CHECK(w.bits == kClearWord);
	CHECK_FALSE(setProgramField(w, Field::ClearWord, 1));
	uint32_t out;
	CHECK_FALSE(getProgramField(w, Field::ClearWord, out));  // write-only
}

TEST_CASE("ProgramWord op sets raw bits; reserved bits rejected") {
	ProgramWord w;
	uint32_t v = kClearWord | 1u | (1u << 9);  // quantize + stop
	CHECK(setProgramField(w, Field::ProgramWord, v));
	CHECK(w.bits == v);
	CHECK(w.quantize());
	CHECK(w.stop());
	CHECK_FALSE(setProgramField(w, Field::ProgramWord, 1u << 19));
	uint32_t out;
	CHECK(getProgramField(w, Field::ProgramWord, out));
	CHECK(out == v);
	// float payload survives exactly through an EditOp (19 bits < 2^24)
	StageTable t;
	t.count = 1;
	CHECK(apply(t, EditOp(0, Field::ProgramWord, (float)0x7FFFF)));
	CHECK(t.program[0].bits == 0x7FFFFu);
}

// ---------------------------------------------------------------------------
// Scroll model
// ---------------------------------------------------------------------------
TEST_CASE("scroll steps once per press and wraps in both directions") {
	ProgramLogic p;
	const float dt = 0.01f;
	// Right: press-release 5 times on a 4-stage chain -> wraps to 1.
	for (int k = 0; k < 5; k++) {
		p.tickScroll(+1, dt, 4);
		p.tickScroll(0, dt, 4);
	}
	CHECK(p.selectedStage() == 1);
	// Left from 1: two presses wrap to 3.
	for (int k = 0; k < 2; k++) {
		p.tickScroll(-1, dt, 4);
		p.tickScroll(0, dt, 4);
	}
	CHECK(p.selectedStage() == 3);
}

TEST_CASE("hold-to-autoscroll: delay, then one stage per period") {
	ProgramLogic p;
	const float dt = 0.01f;
	// Hold right for 2.0 s on a 32-stage chain:
	// 1 immediate + floor((2.0 - 0.5) / 0.15) = 1 + 10 = 11 steps.
	int ticks = (int)(2.0f / dt);
	for (int k = 0; k < ticks; k++)
		p.tickScroll(+1, dt, 32);
	// 1 immediate + ~10 repeats (float accumulation may drop the last one).
	CHECK(p.selectedStage() >= 10);
	CHECK(p.selectedStage() <= 11);
	CHECK(p.scrollActive());
	p.tickScroll(0, dt, 32);
	CHECK_FALSE(p.scrollActive());

	// ~16 stages within 2-3 s of autoscroll (plan timing).
	ProgramLogic q;
	int steps16Ticks = (int)((kScrollDelay + 16 * kScrollPeriod) / dt);
	for (int k = 0; k < steps16Ticks; k++)
		q.tickScroll(+1, dt, 32);
	CHECK(q.selectedStage() >= 15);
	CHECK(q.selectedStage() <= 17);
	float total = kScrollDelay + 16 * kScrollPeriod;
	CHECK(total >= 2.f);
	CHECK(total <= 3.5f);
}

TEST_CASE("selection clamps when the chain shrinks") {
	ProgramLogic p;
	for (int k = 0; k < 7; k++) {
		p.tickScroll(+1, 0.01f, 8);
		p.tickScroll(0, 0.01f, 8);
	}
	CHECK(p.selectedStage() == 7);
	p.tickScroll(0, 0.01f, 4);  // two blocks removed
	CHECK(p.selectedStage() == 3);
}

// ---------------------------------------------------------------------------
// Gesture emission
// ---------------------------------------------------------------------------
TEST_CASE("modifier gestures emit one op for the selected stage") {
	ProgramLogic p;
	StageTable t = makeTable(8);
	EditOp ops[kMaxOpsPerTick];

	// Select stage 2.
	for (int k = 0; k < 2; k++) {
		p.tickScroll(+1, 0.01f, 8);
		p.tickScroll(0, 0.01f, 8);
	}
	REQUIRE(p.selectedStage() == 2);

	int n = p.emitModifier(Field::Quantize, +1, t, ops, kMaxOpsPerTick);
	REQUIRE(n == 1);
	CHECK(ops[0].stageIndex == 2);
	CHECK(ops[0].field == Field::Quantize);
	CHECK(ops[0].value == 1.f);
	CHECK(apply(t, ops[0]));
	CHECK(t.program[2].quantize());

	// Removing when already default is a no-op (budget preserved).
	n = p.emitModifier(Field::Stop, -1, t, ops, kMaxOpsPerTick);
	CHECK(n == 0);
}

TEST_CASE("slew gesture steps up and down with clamping") {
	ProgramLogic p;
	StageTable t = makeTable(4);
	EditOp ops[8];

	// Up twice: 0 -> 1 -> 2; third up is a no-op.
	for (int k = 0; k < 2; k++) {
		int n = p.emitModifier(Field::Slew, +1, t, ops, 8);
		REQUIRE(n == 1);
		REQUIRE(apply(t, ops[0]));
	}
	CHECK(t.program[0].slew() == SLEW_2);
	CHECK(p.emitModifier(Field::Slew, +1, t, ops, 8) == 0);
	// Down: 2 -> 1.
	int n = p.emitModifier(Field::Slew, -1, t, ops, 8);
	REQUIRE(n == 1);
	CHECK(ops[0].value == 1.f);
}

TEST_CASE("range gesture: up = Full, down = Half; limited bank engages Limited") {
	ProgramLogic p;
	StageTable t = makeTable(4);
	EditOp ops[8];

	int n = p.emitModifier(Field::Range, -1, t, ops, 8);
	REQUIRE(n == 1);
	REQUIRE(apply(t, ops[0]));
	CHECK(t.program[0].range() == RANGE_HALF);

	// Limited bank: octave +1 (index 3) -> two ops (range + octave).
	n = p.emitLimited(3, t, ops, 8);
	REQUIRE(n == 2);
	for (int i = 0; i < n; i++)
		REQUIRE(apply(t, ops[i]));
	CHECK(t.program[0].range() == RANGE_LIMITED);
	CHECK(t.program[0].limitedOctave() == 3);
	// Same octave again: only... nothing changes.
	CHECK(p.emitLimited(3, t, ops, 8) == 0);
	// Back to Full via the range switch.
	n = p.emitModifier(Field::Range, +1, t, ops, 8);
	REQUIRE(n == 1);
	REQUIRE(apply(t, ops[0]));
	CHECK(t.program[0].range() == RANGE_FULL);
	CHECK(p.emitLimited(7, t, ops, 8) == 0);  // invalid octave index
}

TEST_CASE("time range buttons are radio selects") {
	ProgramLogic p;
	StageTable t = makeTable(4);
	EditOp ops[8];
	int n = p.emitTimeRange(0, t, ops, 8);
	REQUIRE(n == 1);
	REQUIRE(apply(t, ops[0]));
	CHECK(t.program[0].timeRange() == 0);
	CHECK(p.emitTimeRange(0, t, ops, 8) == 0);  // already selected
	CHECK(p.emitTimeRange(5, t, ops, 8) == 0);  // invalid
}

TEST_CASE("bulk edit while scrolling emits ops for all stages") {
	ProgramLogic p;
	StageTable t = makeTable(8);
	EditOp ops[kMaxOpsPerTick];

	p.tickScroll(+1, 0.01f, 8);  // held -> bulk window
	REQUIRE(p.scrollActive());
	int n = p.emitModifier(Field::Pulse1, +1, t, ops, kMaxOpsPerTick);
	CHECK(n == 8);
	for (int i = 0; i < n; i++) {
		CHECK(ops[i].stageIndex == i);
		REQUIRE(apply(t, ops[i]));
	}
	for (int s = 0; s < 8; s++)
		CHECK(t.program[s].pulse1());

	// Bulk limited bank: 2 ops per stage, still within the tick budget.
	n = p.emitLimited(0, t, ops, kMaxOpsPerTick);
	CHECK(n == 16);
}

TEST_CASE("select override (FG Display) redirects gestures and display") {
	ProgramLogic p;
	StageTable t = makeTable(16);
	EditOp ops[8];

	p.setSelected(3);
	CHECK(p.selectedStage() == 3);
	p.setSelectOverride(11);  // a head's Display took over
	CHECK(p.selectedStage() == 11);
	int n = p.emitModifier(Field::Pulse1, +1, t, ops, 8);
	REQUIRE(n == 1);
	CHECK(ops[0].stageIndex == 11);  // edit lands on the displayed stage
	p.setSelectOverride(-1);
	CHECK(p.selectedStage() == 3);   // back to the scroll selection
}

TEST_CASE("one-shot bulk arms exactly one gesture") {
	ProgramLogic p;
	StageTable t = makeTable(8);
	EditOp ops[kMaxOpsPerTick];

	CHECK_FALSE(p.bulkArmed());
	p.armBulkOnce();
	CHECK(p.bulkArmed());
	int n = p.emitModifier(Field::Pulse2, +1, t, ops, kMaxOpsPerTick);
	CHECK(n == 8);                   // applied to all stages
	CHECK_FALSE(p.bulkArmed());      // consumed
	n = p.emitModifier(Field::Stop, +1, t, ops, kMaxOpsPerTick);
	CHECK(n == 1);                   // next gesture is single-stage again
	p.armBulkOnce();
	p.disarmBulkOnce();
	CHECK_FALSE(p.bulkArmed());
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------
TEST_CASE("clear emits ClearWord for every stage; sliders untouched") {
	ProgramLogic p;
	StageTable t = makeTable(8);
	for (int s = 0; s < 8; s++) {
		t.program[s].setStop(true);
		t.program[s].setQuantize(true);
		t.program[s].setTimeRange(0);
	}
	EditOp ops[kMaxOpsPerTick];
	int n = p.emitClear(t, ops, kMaxOpsPerTick);
	REQUIRE(n == 8);
	for (int i = 0; i < n; i++)
		REQUIRE(apply(t, ops[i]));
	for (int s = 0; s < 8; s++) {
		CHECK(t.program[s].bits == kClearWord);   // exact default table
		CHECK(t.voltage[s] == (float)s);          // sliders stay physical
		CHECK(t.time[s] == doctest::Approx(0.1f * s));
	}
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
TEST_CASE("preset save/load round-trip through the op stream") {
	ProgramLogic p;
	StageTable a = makeTable(8);
	for (int s = 0; s < 8; s++) {
		a.voltage[s] = 10.f - s;
		a.time[s] = 0.05f * s;
		a.program[s].setPulse2(true);
		a.program[s].setSlew(1);
	}
	ScaleKey sk;
	sk.key = 7;
	sk.scale = SCALE_MINOR;
	p.savePreset(4, a, sk);
	CHECK(p.slotUsed(4));
	CHECK_FALSE(p.slotUsed(5));

	// Mutate the live table, then recall.
	StageTable b = makeTable(8);
	REQUIRE(p.loadPreset(4, b));
	CHECK(p.scaleKey().key == 7);
	CHECK(p.scaleKey().scale == SCALE_MINOR);
	CHECK(p.pendingOps() == 3 * 8);

	// Drain respecting the per-tick budget, apply to the live table.
	EditOp ops[kMaxOpsPerTick];
	int total = 0, rounds = 0;
	while (p.pendingOps() > 0 && rounds++ < 10) {
		int n = p.drainPendingOps(ops, kMaxOpsPerTick);
		CHECK(n <= kMaxOpsPerTick);
		for (int i = 0; i < n; i++)
			REQUIRE(apply(b, ops[i]));
		total += n;
	}
	CHECK(total == 24);
	for (int s = 0; s < 8; s++) {
		CHECK(b.voltage[s] == a.voltage[s]);
		CHECK(b.time[s] == a.time[s]);
		CHECK(b.program[s].bits == a.program[s].bits);
	}
}

TEST_CASE("loading onto a shorter chain restores only existing stages") {
	ProgramLogic p;
	StageTable a = makeTable(32);
	p.savePreset(0, a, ScaleKey());
	StageTable b = makeTable(8);  // chain shrank to 2 blocks
	REQUIRE(p.loadPreset(0, b));
	CHECK(p.pendingOps() == 3 * 8);
	// A 32-stage load takes two drains at the 64-op budget.
	StageTable c = makeTable(32);
	StageTable saved = makeTable(32);
	for (int s = 0; s < 32; s++)
		saved.voltage[s] = 9.f;
	p.savePreset(1, saved, ScaleKey());
	REQUIRE(p.loadPreset(1, c));
	EditOp ops[kMaxOpsPerTick];
	CHECK(p.drainPendingOps(ops, kMaxOpsPerTick) == kMaxOpsPerTick);
	CHECK(p.pendingOps() == 96 - kMaxOpsPerTick);
	CHECK(p.drainPendingOps(ops, kMaxOpsPerTick) == 96 - kMaxOpsPerTick);
	CHECK(p.pendingOps() == 0);
}

TEST_CASE("empty slot load fails; preset actions are wired") {
	ProgramLogic p;
	StageTable t = makeTable(4);
	CHECK_FALSE(p.loadPreset(3, t));
	CHECK_FALSE(p.loadPreset(-1, t));
	CHECK_FALSE(p.loadPreset(12, t));

	p.handlePresetAction(PresetAction(PresetAction::SetKey, 11), t);
	CHECK(p.scaleKey().key == 11);
	p.handlePresetAction(PresetAction(PresetAction::SetScale, SCALE_MAJOR), t);
	CHECK(p.scaleKey().scale == SCALE_MAJOR);
	p.handlePresetAction(PresetAction(PresetAction::Save, 2), t);
	CHECK(p.slotUsed(2));
	p.handlePresetAction(PresetAction(PresetAction::Load, 2), t);
	CHECK(p.pendingOps() == 3 * 4);
}

// ---------------------------------------------------------------------------
// Slider takeover
// ---------------------------------------------------------------------------
TEST_CASE("slider takeover: stored value active until the slider crosses it") {
	SliderTakeover st;
	st.engage(5.f, 2.f);  // physical below stored
	CHECK(st.active);
	CHECK(st.value(2.f) == 5.f);
	CHECK(st.value(4.9f) == 5.f);   // approaching, same side
	CHECK(st.value(5.1f) == 5.1f);  // crossed -> physical takes over
	CHECK_FALSE(st.active);
	CHECK(st.value(2.f) == 2.f);    // stays physical

	// From above, capture on reaching exactly the stored value.
	st.engage(5.f, 8.f);
	CHECK(st.value(6.f) == 5.f);
	CHECK(st.value(5.f) == 5.f);    // reached: released, physical == stored
	CHECK_FALSE(st.active);

	// Physical equal at engage: immediately physical.
	st.engage(5.f, 5.f);
	CHECK_FALSE(st.active);
	CHECK(st.value(4.f) == 4.f);
}
