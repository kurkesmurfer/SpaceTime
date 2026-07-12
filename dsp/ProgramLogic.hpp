#pragma once
// SpaceTime — program-section logic (WP6).
// Rack-free: no Rack SDK includes permitted in this file.
//
// The programming section EMITS EDIT-OPS ONLY — it never touches block state
// directly (blocks own their data, spec rev 4). All emission functions write
// into a caller-provided buffer; the anchor pushes them into the rightward
// expander message (pushOp, Chain.hpp).
//
// Modifier gesture semantics (hardware panel, manual p.3):
//   Quantize      up = quantize,   down = continuous
//   Slew          up = add a slew level (0->1->2), down = reduce
//   Range         up = Full,       down = Half
//   Limited bank  press octave i   -> Range = Limited + octave i (radio)
//   Voltage src   up = External,   down = Internal
//   Stop/Sustain/Enable/First/Last/Pulse1/Pulse2:
//                 up = add to stage, down = remove
//   Time range    press i          -> range i (radio)
//   Time src      up = External,   down = Internal
//
// Bulk edit (hardware): while the stage-select scroll is held, any
// programming gesture applies to ALL stages.
//
// Presets: 12 slots, each the full stage table + key/scale. Loading emits
// ops for every stage (Voltage, Time, ProgramWord — 3 x count ops), queued
// and drained over ticks to respect the per-tick op budget. The slider
// takeover rule (saved value active until the physical slider crosses it)
// is provided as the SliderTakeover state machine, instantiated per slider
// by the STAGE4 modules (WP7).

#include <cstdint>
#include <cmath>
#include "StageTable.hpp"
#include "Chain.hpp"
#include "PresetRow.hpp"

namespace spacetime {

// Hold-to-autoscroll timing (~16 stages in 2.4 s after the initial delay).
static const float kScrollDelay = 0.5f;    // hold before autoscroll starts
static const float kScrollPeriod = 0.15f;  // per-stage repeat while held

static const int kPresetSlots = 12;
static const int kLoadOpsMax = 3 * kMaxStages;  // voltage + time + word per stage

// ---- Slider takeover (preset recall) -----------------------------------------
// After a preset load the saved value stays active until the physical slider
// crosses it; then the physical slider takes over (hardware behaviour).
struct SliderTakeover {
	float stored;
	int side;      // physical side of stored at engage: -1 below, +1 above
	bool active;

	SliderTakeover() : stored(0.f), side(0), active(false) {}

	void engage(float storedValue, float physical) {
		stored = storedValue;
		side = physical < storedValue ? -1 : (physical > storedValue ? 1 : 0);
		active = (side != 0);  // equal -> physical already matches
	}

	void release() { active = false; }

	// Effective value for the current physical slider position.
	float value(float physical) {
		if (!active)
			return physical;
		int nowSide = physical < stored ? -1 : (physical > stored ? 1 : 0);
		if (nowSide != side) {  // crossed (or reached) the stored value
			active = false;
			return physical;
		}
		return stored;
	}
};

// ---- Preset slot ----------------------------------------------------------------
struct PresetSlot {
	StageTable table;
	ScaleKey scaleKey;
	bool used;

	PresetSlot() : used(false) {}
};

// ---- Program-section logic --------------------------------------------------------
class ProgramLogic {
public:
	ProgramLogic()
		: selected_(0), selectOverride_(-1), bulkOnce_(false), scrollDir_(0),
		  holdTimer_(0.f), repeatTimer_(0.f), pendingCount_(0), pendingHead_(0) {}

	// ---- Stage-select scroll -------------------------------------------------
	// dir: -1 (left), 0 (center), +1 (right) from the spring switch.
	void tickScroll(int dir, float dt, int stageCount) {
		if (stageCount <= 0) {
			selected_ = 0;
			scrollDir_ = 0;
			return;
		}
		if (selected_ >= stageCount)
			selected_ = stageCount - 1;

		if (dir != 0 && scrollDir_ == 0) {
			step(dir, stageCount);  // immediate step on press
			holdTimer_ = 0.f;
			repeatTimer_ = 0.f;
		}
		else if (dir != 0) {
			holdTimer_ += dt;
			if (holdTimer_ >= kScrollDelay) {
				repeatTimer_ += dt;
				while (repeatTimer_ >= kScrollPeriod) {
					repeatTimer_ -= kScrollPeriod;
					step(dir, stageCount);
				}
			}
		}
		scrollDir_ = dir;
	}

	// Effective edit/display target: the FG Display override (hardware:
	// per-head "display" switch) wins over the scroll selection.
	int selectedStage() const { return selectOverride_ >= 0 ? selectOverride_ : selected_; }
	void setSelected(int s) { selected_ = s < 0 ? 0 : s; }
	void selectRelative(int dir, int stageCount) {
		if (stageCount <= 0)
			return;
		if (selected_ >= stageCount)
			selected_ = stageCount - 1;
		step(dir, stageCount);
	}
	// -1 = none; else the displayed head's current stage (updated per tick).
	void setSelectOverride(int s) { selectOverride_ = s; }
	int selectOverride() const { return selectOverride_; }

	// Bulk-edit window: any gesture while the scroll switch is held applies
	// to all stages (hardware).
	bool scrollActive() const { return scrollDir_ != 0; }
	// VCV single-pointer alternative (hold+press needs two hands): arm bulk
	// for exactly the next gesture (context menu).
	void armBulkOnce() { bulkOnce_ = true; }
	void disarmBulkOnce() { bulkOnce_ = false; }
	bool bulkArmed() const { return bulkOnce_; }

	// ---- Modifier gestures -> edit ops ----------------------------------------
	// dir: +1 = switch pressed up (add), -1 = pressed down (remove).
	// Returns the number of ops written (skips no-ops to save budget).
	int emitModifier(Field f, int dir, const StageTable& t, EditOp* out, int maxOps) {
		int n = 0;
		forEachTarget(t, [&](int s) {
			const ProgramWord& w = t.program[s];
			uint32_t cur, next;
			switch (f) {
				case Field::Slew:
					cur = w.slew();
					next = clampU(cur, dir, 2);
					break;
				case Field::Range:
					cur = w.range();
					next = (dir > 0) ? RANGE_FULL : RANGE_HALF;
					break;
				case Field::Quantize:      cur = w.quantize(); next = dir > 0; break;
				case Field::VoltageSource: cur = w.voltageSource(); next = dir > 0; break;
				case Field::Stop:          cur = w.stop(); next = dir > 0; break;
				case Field::Sustain:       cur = w.sustain(); next = dir > 0; break;
				case Field::Enable:        cur = w.enable(); next = dir > 0; break;
				case Field::First:         cur = w.first(); next = dir > 0; break;
				case Field::Last:          cur = w.last(); next = dir > 0; break;
				case Field::Pulse1:        cur = w.pulse1(); next = dir > 0; break;
				case Field::Pulse2:        cur = w.pulse2(); next = dir > 0; break;
				case Field::TimeSource:    cur = w.timeSource(); next = dir > 0; break;
				default:                   return;  // not a gesture field
			}
			if (next != cur && n < maxOps)
				out[n++] = EditOp((uint8_t)s, f, (float)next);
		});
		return n;
	}

	// Limited-range bank: press octave button i (0..4) -> Range = Limited +
	// octave offset, radio behaviour.
	int emitLimited(int octaveIdx, const StageTable& t, EditOp* out, int maxOps) {
		if (octaveIdx < 0 || octaveIdx > 4)
			return 0;
		int n = 0;
		forEachTarget(t, [&](int s) {
			if (t.program[s].range() != RANGE_LIMITED && n < maxOps)
				out[n++] = EditOp((uint8_t)s, Field::Range, (float)RANGE_LIMITED);
			if (t.program[s].limitedOctave() != (uint8_t)octaveIdx && n < maxOps)
				out[n++] = EditOp((uint8_t)s, Field::LimitedOctave, (float)octaveIdx);
		});
		return n;
	}

	// Time-range buttons: radio select range i (0..3).
	int emitTimeRange(int idx, const StageTable& t, EditOp* out, int maxOps) {
		if (idx < 0 || idx > 3)
			return 0;
		int n = 0;
		forEachTarget(t, [&](int s) {
			if (t.program[s].timeRange() != (uint8_t)idx && n < maxOps)
				out[n++] = EditOp((uint8_t)s, Field::TimeRange, (float)idx);
		});
		return n;
	}

	// Clear: every stage back to the default program word. Sliders stay at
	// their physical position (hardware).
	int emitClear(const StageTable& t, EditOp* out, int maxOps) {
		int n = 0;
		for (int s = 0; s < t.count && n < maxOps; s++)
			out[n++] = EditOp((uint8_t)s, Field::ClearWord, 0.f);
		return n;
	}

	// ---- Presets ------------------------------------------------------------------
	void savePreset(int slot, const StageTable& t, const ScaleKey& sk) {
		if (slot < 0 || slot >= kPresetSlots)
			return;
		slots_[slot].table = t;
		slots_[slot].scaleKey = sk;
		slots_[slot].used = true;
	}

	bool slotUsed(int slot) const {
		return slot >= 0 && slot < kPresetSlots && slots_[slot].used;
	}

	// Queue the ops restoring a preset onto the CURRENT chain (only stages
	// that exist now: min(saved count, current count)). Drained over ticks.
	bool loadPreset(int slot, const StageTable& current) {
		if (!slotUsed(slot))
			return false;
		const PresetSlot& p = slots_[slot];
		scaleKey_ = p.scaleKey;
		int count = p.table.count < current.count ? p.table.count : current.count;
		pendingCount_ = 0;
		pendingHead_ = 0;
		for (int s = 0; s < count; s++) {
			pending_[pendingCount_++] = EditOp((uint8_t)s, Field::Voltage, p.table.voltage[s]);
			pending_[pendingCount_++] = EditOp((uint8_t)s, Field::Time, p.table.time[s]);
			pending_[pendingCount_++] = EditOp((uint8_t)s, Field::ProgramWord,
				(float)p.table.program[s].bits);
		}
		return true;
	}

	// Emit up to maxOps queued preset ops (call once per tick until 0).
	int drainPendingOps(EditOp* out, int maxOps) {
		int n = 0;
		while (pendingHead_ < pendingCount_ && n < maxOps)
			out[n++] = pending_[pendingHead_++];
		if (pendingHead_ >= pendingCount_) {
			pendingHead_ = 0;
			pendingCount_ = 0;
		}
		return n;
	}

	int pendingOps() const { return pendingCount_ - pendingHead_; }

	// ---- Key / scale plumbing (to the quantizer via the anchor broadcast) -----
	const ScaleKey& scaleKey() const { return scaleKey_; }

	void setScaleKey(const ScaleKey& sk) { scaleKey_ = sk; }

	// Persistence access (WP7: PROGRAM's dataToJson/FromJson).
	const PresetSlot& slot(int i) const { return slots_[clampSlot(i)]; }
	void setSlot(int i, const PresetSlot& s) { slots_[clampSlot(i)] = s; }

	// Wire the preset row's modal actions (PresetRowLogic, WP3).
	void handlePresetAction(const PresetAction& a, const StageTable& current) {
		switch (a.type) {
			case PresetAction::Load:     loadPreset(a.index, current); break;
			case PresetAction::Save:     savePreset(a.index, current, scaleKey_); break;
			case PresetAction::SetKey:   scaleKey_.key = a.index; break;
			case PresetAction::SetScale: scaleKey_.scale = a.index; break;
			default: break;
		}
	}

private:
	int selected_;
	int selectOverride_;
	bool bulkOnce_;
	int scrollDir_;
	float holdTimer_, repeatTimer_;
	ScaleKey scaleKey_;
	PresetSlot slots_[kPresetSlots];
	EditOp pending_[kLoadOpsMax];
	int pendingCount_, pendingHead_;

	static int clampSlot(int i) {
		return i < 0 ? 0 : (i >= kPresetSlots ? kPresetSlots - 1 : i);
	}

	static uint32_t clampU(uint32_t cur, int dir, uint32_t maxV) {
		if (dir > 0)
			return cur >= maxV ? maxV : cur + 1;
		return cur == 0 ? 0 : cur - 1;
	}

	void step(int dir, int stageCount) {
		selected_ += dir;
		if (selected_ < 0)
			selected_ = stageCount - 1;  // circular
		if (selected_ >= stageCount)
			selected_ = 0;
	}

	// Gesture targets: all stages while scrolling or one-shot armed (bulk
	// edit), else the selected/displayed stage.
	template <typename F>
	void forEachTarget(const StageTable& t, F f) {
		if (t.count == 0)
			return;
		if (scrollActive() || bulkOnce_) {
			for (int s = 0; s < t.count; s++)
				f(s);
			bulkOnce_ = false;  // consumed by this gesture
		}
		else {
			int s = selectedStage();
			if (s >= t.count)
				s = t.count - 1;
			f(s);
		}
	}
};

} // namespace spacetime
