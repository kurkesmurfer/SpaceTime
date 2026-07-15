// SpaceTime WP2 — StageTable unit tests.
#include "doctest.h"
#include "StageTable.hpp"

#include <cmath>
#include <limits>

using namespace spacetime;

// ---------------------------------------------------------------------------
// Layout freeze
// ---------------------------------------------------------------------------
TEST_CASE("program word layout is frozen (version 1)") {
	CHECK(kProgramLayoutVersion == 1);
	// Clear word: limited octave = 2 at bits 5-7, time range = 3 at bits 16-17.
	CHECK(kClearWord == 0x30040u);
	CHECK(sizeof(ProgramWord) == sizeof(uint32_t));
	CHECK(kMaxStages == 64);
	CHECK(kStagesPerBlock == 4);
	CHECK(kMaxBlocks == 16);
	CHECK(kMaxHeads == 8);
}

// ---------------------------------------------------------------------------
// Clear-state conformance (manual: pulses off, Continuous, Full Range,
// Internal sources, mode flags off, time range 2-30 s)
// ---------------------------------------------------------------------------
TEST_CASE("default program word matches the hardware Clear state") {
	ProgramWord w;
	CHECK(w.bits == kClearWord);
	CHECK(w.quantize() == false);            // Continuous
	CHECK(w.slew() == SLEW_STEPPED);         // stepped
	CHECK(w.range() == RANGE_FULL);          // Full Range
	CHECK(w.limitedOctave() == 2);           // offset 0
	CHECK(limitedOctaveOffset(w.limitedOctave()) == 0);
	CHECK(w.voltageSource() == SOURCE_INTERNAL);
	CHECK(w.stop() == false);
	CHECK(w.sustain() == false);
	CHECK(w.enable() == false);
	CHECK(w.first() == false);
	CHECK(w.last() == false);
	CHECK(w.pulse1() == false);              // pulses off
	CHECK(w.pulse2() == false);
	CHECK(w.timeRange() == kDefaultTimeRange);
	CHECK(kTimeRangeMin[w.timeRange()] == doctest::Approx(2.f));   // 2-30 s
	CHECK(kTimeRangeMax[w.timeRange()] == doctest::Approx(30.f));
	CHECK(w.timeSource() == SOURCE_INTERNAL);
}

// ---------------------------------------------------------------------------
// Bitfield round-trip, every field, every boundary value, field isolation
// ---------------------------------------------------------------------------
struct FieldCase {
	Field field;
	uint32_t maxValue;  // valid values are 0..maxValue
};
static const FieldCase fieldCases[] = {
	{Field::Quantize, 1},  {Field::Slew, 2},          {Field::Range, 2},
	{Field::LimitedOctave, 4}, {Field::VoltageSource, 1}, {Field::Stop, 1},
	{Field::Sustain, 1},   {Field::Enable, 1},        {Field::First, 1},
	{Field::Last, 1},      {Field::Pulse1, 1},        {Field::Pulse2, 1},
	{Field::TimeRange, 3}, {Field::TimeSource, 1},
};

TEST_CASE("every program field round-trips every valid value") {
	for (const FieldCase& fc : fieldCases) {
		for (uint32_t v = 0; v <= fc.maxValue; v++) {
			ProgramWord w;
			CHECK(setProgramField(w, fc.field, v));
			uint32_t got = 999;
			CHECK(getProgramField(w, fc.field, got));
			CHECK(got == v);
		}
	}
}

TEST_CASE("setting one field leaves all other fields unchanged") {
	for (const FieldCase& fc : fieldCases) {
		ProgramWord w;  // start from Clear
		REQUIRE(setProgramField(w, fc.field, fc.maxValue));
		for (const FieldCase& other : fieldCases) {
			if (other.field == fc.field)
				continue;
			ProgramWord def;
			uint32_t got = 999, defVal = 999;
			REQUIRE(getProgramField(w, other.field, got));
			REQUIRE(getProgramField(def, other.field, defVal));
			CHECK(got == defVal);
		}
	}
}

TEST_CASE("setting a field back to a saturated word restores the original") {
	// Build a word with every field at max, then walk each field down to 0
	// and back, checking full-word equality.
	ProgramWord full;
	for (const FieldCase& fc : fieldCases)
		REQUIRE(setProgramField(full, fc.field, fc.maxValue));
	for (const FieldCase& fc : fieldCases) {
		ProgramWord w = full;
		REQUIRE(setProgramField(w, fc.field, 0));
		REQUIRE(setProgramField(w, fc.field, fc.maxValue));
		CHECK(w.bits == full.bits);
	}
}

TEST_CASE("out-of-range field values are rejected and leave the word unmodified") {
	for (const FieldCase& fc : fieldCases) {
		ProgramWord w;
		uint32_t before = w.bits;
		CHECK_FALSE(setProgramField(w, fc.field, fc.maxValue + 1));
		CHECK(w.bits == before);
		// Large values must not be accepted via uint8 truncation either.
		CHECK_FALSE(setProgramField(w, fc.field, 256));
		CHECK_FALSE(setProgramField(w, fc.field, 0x10000u));
		CHECK(w.bits == before);
	}
	// Unknown field id
	ProgramWord w;
	uint32_t out;
	CHECK_FALSE(setProgramField(w, Field::Count_, 0));
	CHECK_FALSE(getProgramField(w, Field::Count_, out));
	// Voltage/Time are not program fields
	CHECK_FALSE(setProgramField(w, Field::Voltage, 0));
	CHECK_FALSE(setProgramField(w, Field::Time, 0));
}

TEST_CASE("reserved bits stay zero for any combination of valid fields") {
	ProgramWord w;
	for (const FieldCase& fc : fieldCases)
		REQUIRE(setProgramField(w, fc.field, fc.maxValue));
	CHECK((w.bits >> 19) == 0u);
}

TEST_CASE("limitedOctaveOffset maps 0..4 to -2..+2") {
	CHECK(limitedOctaveOffset(0) == -2);
	CHECK(limitedOctaveOffset(1) == -1);
	CHECK(limitedOctaveOffset(2) == 0);
	CHECK(limitedOctaveOffset(3) == 1);
	CHECK(limitedOctaveOffset(4) == 2);
}

// ---------------------------------------------------------------------------
// Stage table defaults and Clear
// ---------------------------------------------------------------------------
TEST_CASE("fresh stage table: count 0, sliders at defaults, program at Clear") {
	StageTable t;
	CHECK(t.count == 0);
	for (int i = 0; i < kMaxStages; i++) {
		CHECK(t.voltage[i] == 0.f);
		CHECK(t.time[i] == 0.5f);
		CHECK(t.program[i].bits == kClearWord);
	}
}

TEST_CASE("clearProgram resets program words but not sliders") {
	StageTable t;
	t.count = 8;
	t.voltage[3] = 7.5f;
	t.time[3] = 0.9f;
	t.program[3].setStop(true);
	t.program[3].setQuantize(true);
	clearProgram(t);
	CHECK(t.program[3].bits == kClearWord);
	CHECK(t.voltage[3] == 7.5f);  // sliders stay at physical position
	CHECK(t.time[3] == 0.9f);
	CHECK(t.count == 8);
}

// ---------------------------------------------------------------------------
// Edit ops
// ---------------------------------------------------------------------------
TEST_CASE("edit op: program field application") {
	StageTable t;
	t.count = 8;
	CHECK(apply(t, EditOp(5, Field::Pulse1, 1.f)));
	CHECK(t.program[5].pulse1());
	CHECK(t.program[4].pulse1() == false);
	CHECK(t.program[6].pulse1() == false);
	CHECK(apply(t, EditOp(5, Field::Slew, 2.f)));
	CHECK(t.program[5].slew() == SLEW_2);
	CHECK(apply(t, EditOp(5, Field::TimeRange, 0.f)));
	CHECK(t.program[5].timeRange() == 0);
}

TEST_CASE("edit op: slider fields with boundary values") {
	StageTable t;
	t.count = 4;
	CHECK(apply(t, EditOp(0, Field::Voltage, 0.f)));
	CHECK(apply(t, EditOp(1, Field::Voltage, 10.f)));
	CHECK(apply(t, EditOp(2, Field::Time, 0.f)));
	CHECK(apply(t, EditOp(3, Field::Time, 1.f)));
	CHECK(t.voltage[1] == 10.f);
	CHECK(t.time[3] == 1.f);
	CHECK(apply(t, EditOp(0, Field::Voltage, 3.14f)));
	CHECK(t.voltage[0] == doctest::Approx(3.14f));
}

TEST_CASE("edit op: out-of-range stage index is rejected") {
	StageTable t;
	t.count = 4;
	CHECK_FALSE(apply(t, EditOp(4, Field::Pulse1, 1.f)));   // == count
	CHECK_FALSE(apply(t, EditOp(63, Field::Pulse1, 1.f)));  // < kMaxStages but >= count
	t.count = 0;
	CHECK_FALSE(apply(t, EditOp(0, Field::Pulse1, 1.f)));   // empty table
}

TEST_CASE("edit op: out-of-range and malformed values are rejected, table unmodified") {
	StageTable t;
	t.count = 4;
	StageTable before = t;

	CHECK_FALSE(apply(t, EditOp(0, Field::Voltage, -0.001f)));
	CHECK_FALSE(apply(t, EditOp(0, Field::Voltage, 10.001f)));
	CHECK_FALSE(apply(t, EditOp(0, Field::Time, -0.001f)));
	CHECK_FALSE(apply(t, EditOp(0, Field::Time, 1.001f)));
	CHECK_FALSE(apply(t, EditOp(0, Field::Slew, 3.f)));
	CHECK_FALSE(apply(t, EditOp(0, Field::Quantize, 2.f)));
	CHECK_FALSE(apply(t, EditOp(0, Field::Quantize, 0.5f)));   // non-integral
	CHECK_FALSE(apply(t, EditOp(0, Field::Quantize, -1.f)));   // negative
	CHECK_FALSE(apply(t, EditOp(0, Field::Voltage,
		std::numeric_limits<float>::quiet_NaN())));
	CHECK_FALSE(apply(t, EditOp(0, Field::Voltage,
		std::numeric_limits<float>::infinity())));
	CHECK_FALSE(apply(t, EditOp(0, Field::Time,
		-std::numeric_limits<float>::infinity())));

	for (int i = 0; i < kMaxStages; i++) {
		CHECK(t.voltage[i] == before.voltage[i]);
		CHECK(t.time[i] == before.time[i]);
		CHECK(t.program[i].bits == before.program[i].bits);
	}
}

// ---------------------------------------------------------------------------
// Concatenation
// ---------------------------------------------------------------------------
static BlockSegment makeBlock(int b) {
	BlockSegment seg;
	for (int i = 0; i < kStagesPerBlock; i++) {
		seg.voltage[i] = (float)(b * 10 + i);
		seg.time[i] = (float)(b * kStagesPerBlock + i) / 100.f;
		seg.program[i] = ProgramWord();
		seg.program[i].setTimeRange((uint8_t)(b % 4));
		seg.program[i].setPulse1((b + i) % 2);
	}
	return seg;
}

TEST_CASE("concatenation of 1..16 blocks preserves order and contents") {
	for (int n = 1; n <= kMaxBlocks; n++) {
		BlockSegment blocks[kMaxBlocks];
		for (int b = 0; b < n; b++)
			blocks[b] = makeBlock(b);
		StageTable t;
		int count = concatenate(blocks, n, t);
		CHECK(count == n * kStagesPerBlock);
		CHECK(t.count == count);
		for (int b = 0; b < n; b++) {
			for (int i = 0; i < kStagesPerBlock; i++) {
				int s = b * kStagesPerBlock + i;
				CHECK(t.voltage[s] == blocks[b].voltage[i]);
				CHECK(t.time[s] == blocks[b].time[i]);
				CHECK(t.program[s].bits == blocks[b].program[i].bits);
			}
		}
	}
}

TEST_CASE("concatenation edge cases: 0 blocks, clamped block count") {
	StageTable t;
	CHECK(concatenate(nullptr, 0, t) == 0);
	CHECK(t.count == 0);
	CHECK(concatenate(nullptr, -3, t) == 0);

	BlockSegment blocks[kMaxBlocks];
	for (int b = 0; b < kMaxBlocks; b++)
		blocks[b] = makeBlock(b);
	// Requesting more than kMaxBlocks is clamped to 16.
	CHECK(concatenate(blocks, kMaxBlocks + 4, t) == kMaxStages);
	CHECK(t.count == kMaxStages);
}

TEST_CASE("concatenation models live block reorder (blocks own their data)") {
	BlockSegment a = makeBlock(1), b = makeBlock(2);
	BlockSegment order1[2] = {a, b};
	BlockSegment order2[2] = {b, a};
	StageTable t1, t2;
	concatenate(order1, 2, t1);
	concatenate(order2, 2, t2);
	// Swapping blocks swaps the 4-stage groups wholesale.
	for (int i = 0; i < kStagesPerBlock; i++) {
		CHECK(t1.voltage[i] == t2.voltage[kStagesPerBlock + i]);
		CHECK(t1.voltage[kStagesPerBlock + i] == t2.voltage[i]);
	}
}
