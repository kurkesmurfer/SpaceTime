#pragma once
// SpaceTime — stage table and data model (WP2).
// Rack-free: no Rack SDK includes permitted in this file. <cstdint>/<cmath> only.
//
// This header is the shared contract between the expander protocol (WP4),
// the head DSP (WP5) and the program-section logic (WP6).
// The program-word bit layout and the Field enumeration are FROZEN as of
// layout version 1: fields may be appended, but existing bit positions and
// Field values must never change (they travel over the expander wire and
// live in patch JSON).
//
// Per-stage program word (uint32_t), LSB first:
//   bit  0     quantize        0 = continuous (default), 1 = quantized
//   bits 1-2   slew level      0 = stepped (default), 1 = slew 1, 2 = slew 2
//   bits 3-4   range           0 = full 0-10 V (default), 1 = half 0-5 V, 2 = limited
//   bits 5-7   limited octave  0..4 = octave offset -2..+2 (default 2 = 0)
//   bit  8     voltage source  0 = internal (default), 1 = external A-D
//   bit  9     stop
//   bits 10    sustain
//   bit  11    enable
//   bit  12    cycle first
//   bit  13    cycle last
//   bit  14    pulse 1
//   bit  15    pulse 2
//   bits 16-17 time range      0..3, index into kTimeRange* (default 3 = 2-30 s)
//   bit  18    time source     0 = internal (default), 1 = external A-D CV
//   bits 19-31 reserved, must stay 0
//
// Clear state (hardware manual): pulses off, Continuous, Full Range,
// Internal sources, mode flags off, time range 2-30 s.

#include <cstdint>
#include <cmath>

namespace spacetime {

// ---- Sizes -----------------------------------------------------------------
static const int kStagesPerBlock = 4;
static const int kMaxBlocks = 8;
static const int kMaxStages = kStagesPerBlock * kMaxBlocks;  // 32
static const int kMaxHeads = 8;

static const uint32_t kProgramLayoutVersion = 1;

// ---- Value enumerations ----------------------------------------------------
enum SlewLevel { SLEW_STEPPED = 0, SLEW_1 = 1, SLEW_2 = 2 };
enum RangeMode { RANGE_FULL = 0, RANGE_HALF = 1, RANGE_LIMITED = 2 };
enum SourceMode { SOURCE_INTERNAL = 0, SOURCE_EXTERNAL = 1 };

// Interval time ranges in seconds (manual: four ranges, 2 ms .. ~2 min class;
// default range 2-30 s). Index = time-range field value.
static const float kTimeRangeMin[4] = {0.002f, 0.02f, 0.2f, 2.f};
static const float kTimeRangeMax[4] = {0.03f, 0.3f, 3.f, 30.f};
static const int kDefaultTimeRange = 3;

// Slider value ranges (VCV scaling).
static const float kVoltageMin = 0.f, kVoltageMax = 10.f;
static const float kTimeSliderMin = 0.f, kTimeSliderMax = 1.f;  // normalized within range

// ---- Field ids (edit-op wire format: append only, never renumber) ----------
enum class Field : uint8_t {
	Quantize = 0,
	Slew = 1,
	Range = 2,
	LimitedOctave = 3,
	VoltageSource = 4,
	Stop = 5,
	Sustain = 6,
	Enable = 7,
	First = 8,
	Last = 9,
	Pulse1 = 10,
	Pulse2 = 11,
	TimeRange = 12,
	TimeSource = 13,
	Voltage = 14,  // float slider, 0..10 V
	Time = 15,     // float slider, 0..1 normalized
	// Appended for WP6 (Clear and preset load; append-only rule respected):
	ClearWord = 16,    // reset the stage's program word to kClearWord (value 0)
	ProgramWord = 17,  // set the raw program word (value = bits, < 2^19)
	Count_ = 18
};

// ---- Program word ----------------------------------------------------------
// Default (Clear) word: limited octave = 2 (offset 0), time range = 3 (2-30 s),
// everything else 0.
static const uint32_t kClearWord = (2u << 5) | (3u << 16);  // 0x30040

struct ProgramWord {
	uint32_t bits;

	ProgramWord() : bits(kClearWord) {}
	explicit ProgramWord(uint32_t b) : bits(b) {}

	// -- typed accessors --
	bool quantize() const { return (bits >> 0) & 1u; }
	void setQuantize(bool v) { setBits(0, 1, v ? 1u : 0u); }

	uint8_t slew() const { return (bits >> 1) & 3u; }         // 0..2
	bool setSlew(uint8_t v) { return v <= 2 && setBits(1, 2, v); }

	uint8_t range() const { return (bits >> 3) & 3u; }        // RangeMode
	bool setRange(uint8_t v) { return v <= 2 && setBits(3, 2, v); }

	uint8_t limitedOctave() const { return (bits >> 5) & 7u; }  // 0..4
	bool setLimitedOctave(uint8_t v) { return v <= 4 && setBits(5, 3, v); }

	bool voltageSource() const { return (bits >> 8) & 1u; }   // SourceMode
	void setVoltageSource(bool v) { setBits(8, 1, v ? 1u : 0u); }

	bool stop() const { return (bits >> 9) & 1u; }
	void setStop(bool v) { setBits(9, 1, v ? 1u : 0u); }

	bool sustain() const { return (bits >> 10) & 1u; }
	void setSustain(bool v) { setBits(10, 1, v ? 1u : 0u); }

	bool enable() const { return (bits >> 11) & 1u; }
	void setEnable(bool v) { setBits(11, 1, v ? 1u : 0u); }

	bool first() const { return (bits >> 12) & 1u; }
	void setFirst(bool v) { setBits(12, 1, v ? 1u : 0u); }

	bool last() const { return (bits >> 13) & 1u; }
	void setLast(bool v) { setBits(13, 1, v ? 1u : 0u); }

	bool pulse1() const { return (bits >> 14) & 1u; }
	void setPulse1(bool v) { setBits(14, 1, v ? 1u : 0u); }

	bool pulse2() const { return (bits >> 15) & 1u; }
	void setPulse2(bool v) { setBits(15, 1, v ? 1u : 0u); }

	uint8_t timeRange() const { return (bits >> 16) & 3u; }   // 0..3
	bool setTimeRange(uint8_t v) { return v <= 3 && setBits(16, 2, v); }

	bool timeSource() const { return (bits >> 18) & 1u; }     // SourceMode
	void setTimeSource(bool v) { setBits(18, 1, v ? 1u : 0u); }

private:
	bool setBits(int shift, int width, uint32_t v) {
		uint32_t mask = ((1u << width) - 1u) << shift;
		bits = (bits & ~mask) | ((v << shift) & mask);
		return true;
	}
};

// Limited-range octave offset in octaves (-2..+2) from field value 0..4.
inline int limitedOctaveOffset(uint8_t fieldValue) {
	return (int)fieldValue - 2;
}

// ---- Generic field access (edit-op path) -----------------------------------
// Program fields only (Voltage/Time are table-level floats, see apply()).
// Returns false for unknown field or out-of-range value; word is unmodified
// on failure.
inline bool setProgramField(ProgramWord& w, Field f, uint32_t v) {
	switch (f) {
		case Field::Quantize:      if (v > 1) return false; w.setQuantize(v); return true;
		case Field::Slew:          return w.setSlew((uint8_t)(v > 255 ? 255 : v));
		case Field::Range:         return w.setRange((uint8_t)(v > 255 ? 255 : v));
		case Field::LimitedOctave: return w.setLimitedOctave((uint8_t)(v > 255 ? 255 : v));
		case Field::VoltageSource: if (v > 1) return false; w.setVoltageSource(v); return true;
		case Field::Stop:          if (v > 1) return false; w.setStop(v); return true;
		case Field::Sustain:       if (v > 1) return false; w.setSustain(v); return true;
		case Field::Enable:        if (v > 1) return false; w.setEnable(v); return true;
		case Field::First:         if (v > 1) return false; w.setFirst(v); return true;
		case Field::Last:          if (v > 1) return false; w.setLast(v); return true;
		case Field::Pulse1:        if (v > 1) return false; w.setPulse1(v); return true;
		case Field::Pulse2:        if (v > 1) return false; w.setPulse2(v); return true;
		case Field::TimeRange:     return w.setTimeRange((uint8_t)(v > 255 ? 255 : v));
		case Field::TimeSource:    if (v > 1) return false; w.setTimeSource(v); return true;
		case Field::ClearWord:
			if (v != 0) return false;
			w.bits = kClearWord;
			return true;
		case Field::ProgramWord:
			if (v >= (1u << 19)) return false;  // reserved bits must stay 0
			w.bits = v;
			return true;
		default:                   return false;
	}
}

inline bool getProgramField(const ProgramWord& w, Field f, uint32_t& out) {
	switch (f) {
		case Field::Quantize:      out = w.quantize(); return true;
		case Field::Slew:          out = w.slew(); return true;
		case Field::Range:         out = w.range(); return true;
		case Field::LimitedOctave: out = w.limitedOctave(); return true;
		case Field::VoltageSource: out = w.voltageSource(); return true;
		case Field::Stop:          out = w.stop(); return true;
		case Field::Sustain:       out = w.sustain(); return true;
		case Field::Enable:        out = w.enable(); return true;
		case Field::First:         out = w.first(); return true;
		case Field::Last:          out = w.last(); return true;
		case Field::Pulse1:        out = w.pulse1(); return true;
		case Field::Pulse2:        out = w.pulse2(); return true;
		case Field::TimeRange:     out = w.timeRange(); return true;
		case Field::TimeSource:    out = w.timeSource(); return true;
		case Field::ProgramWord:   out = w.bits; return true;
		default:                   return false;  // ClearWord is write-only
	}
}

// ---- Stage table -----------------------------------------------------------
// Fixed size, trivially copyable: this is also the leftward expander payload
// layout (blocks -> anchor -> heads).
struct StageTable {
	float voltage[kMaxStages];
	float time[kMaxStages];
	ProgramWord program[kMaxStages];
	uint8_t count;  // valid stages, 0..32 (= 4 x block count)

	StageTable() : count(0) {
		for (int i = 0; i < kMaxStages; i++) {
			voltage[i] = 0.f;
			time[i] = 0.5f;
			// program[i] default-constructs to kClearWord
		}
	}
};

// Reset all program words to the Clear state. Slider values are NOT touched:
// hardware Clear leaves sliders at their physical position (blocks own them).
inline void clearProgram(StageTable& t) {
	for (int i = 0; i < kMaxStages; i++)
		t.program[i] = ProgramWord();
}

// ---- Edit ops (anchor -> blocks, rightward expander payload) ---------------
struct EditOp {
	uint8_t stageIndex;  // 0..count-1
	Field field;
	float value;  // program fields: non-negative integral; sliders: V / normalized

	EditOp() : stageIndex(0), field(Field::Quantize), value(0.f) {}
	EditOp(uint8_t s, Field f, float v) : stageIndex(s), field(f), value(v) {}
};

// Apply an edit op. Returns false (table unmodified) if the stage index is
// out of range, the value is non-finite or out of range, or — for program
// fields — not integral.
inline bool apply(StageTable& t, const EditOp& op) {
	if (op.stageIndex >= t.count)
		return false;
	if (!std::isfinite(op.value))
		return false;

	if (op.field == Field::Voltage) {
		if (op.value < kVoltageMin || op.value > kVoltageMax)
			return false;
		t.voltage[op.stageIndex] = op.value;
		return true;
	}
	if (op.field == Field::Time) {
		if (op.value < kTimeSliderMin || op.value > kTimeSliderMax)
			return false;
		t.time[op.stageIndex] = op.value;
		return true;
	}

	if (op.value < 0.f || op.value != std::floor(op.value))
		return false;
	return setProgramField(t.program[op.stageIndex], op.field, (uint32_t)op.value);
}

// ---- Concatenation (per-block segments -> full table) -----------------------
// One STAGE4 block's contribution. Trivially copyable, fixed size.
struct BlockSegment {
	float voltage[kStagesPerBlock];
	float time[kStagesPerBlock];
	ProgramWord program[kStagesPerBlock];

	BlockSegment() {
		for (int i = 0; i < kStagesPerBlock; i++) {
			voltage[i] = 0.f;
			time[i] = 0.5f;
		}
	}
};

// Concatenate blocks left-to-right into a full stage table (stage index runs
// across blocks). blockCount is clamped to 0..kMaxBlocks. Returns the
// resulting stage count.
inline int concatenate(const BlockSegment* blocks, int blockCount, StageTable& out) {
	if (blockCount < 0)
		blockCount = 0;
	if (blockCount > kMaxBlocks)
		blockCount = kMaxBlocks;
	for (int b = 0; b < blockCount; b++) {
		for (int i = 0; i < kStagesPerBlock; i++) {
			int s = b * kStagesPerBlock + i;
			out.voltage[s] = blocks[b].voltage[i];
			out.time[s] = blocks[b].time[i];
			out.program[s] = blocks[b].program[i];
		}
	}
	out.count = (uint8_t)(blockCount * kStagesPerBlock);
	return out.count;
}

} // namespace spacetime
