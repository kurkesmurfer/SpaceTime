#pragma once
// SpaceTime — preset button row modal behaviour (WP3).
// Rack-free: no Rack SDK includes permitted in this file.
//
// Hardware behaviour (248t manual): press Load then a button 1-12 to recall;
// press Save then 1-12 to store; press Key then 1-12 to select key C..B;
// press Scale then 10/11/12 to select Major/Minor/Chromatic.
// Pressing the armed mode button again cancels; pressing another mode button
// re-arms to that mode. Slot presses while idle do nothing. In Scale mode an
// invalid slot (1-9) is ignored and the mode stays armed.
// (Hardware auto-tune/ART is dropped in VCV.)

#include <cstdint>

namespace spacetime {

enum class PresetMode : uint8_t {
	Idle = 0,
	Load,
	Save,
	Key,
	Scale
};

enum ScaleId { SCALE_MAJOR = 0, SCALE_MINOR = 1, SCALE_CHROMATIC = 2 };

struct PresetAction {
	enum Type : uint8_t {
		None = 0,
		Load,      // index = slot 0..11
		Save,      // index = slot 0..11
		SetKey,    // index = key 0..11 (C..B)
		SetScale   // index = ScaleId 0..2
	};
	Type type;
	uint8_t index;

	PresetAction() : type(None), index(0) {}
	PresetAction(Type t, uint8_t i) : type(t), index(i) {}
};

class PresetRowLogic {
public:
	PresetRowLogic() : mode_(PresetMode::Idle) {}

	PresetMode mode() const { return mode_; }

	// Mode button press (Load/Save/Key/Scale). Same mode again cancels,
	// a different mode re-arms. Passing Idle is a no-op.
	void onModePress(PresetMode m) {
		if (m == PresetMode::Idle)
			return;
		mode_ = (mode_ == m) ? PresetMode::Idle : m;
	}

	void cancel() { mode_ = PresetMode::Idle; }

	// Slot button press, slot 0..11 (panel buttons 1-12). Returns the
	// resulting action; disarms on success.
	PresetAction onSlotPress(uint8_t slot) {
		if (slot > 11)
			return PresetAction();
		switch (mode_) {
			case PresetMode::Load:
				mode_ = PresetMode::Idle;
				return PresetAction(PresetAction::Load, slot);
			case PresetMode::Save:
				mode_ = PresetMode::Idle;
				return PresetAction(PresetAction::Save, slot);
			case PresetMode::Key:
				mode_ = PresetMode::Idle;
				return PresetAction(PresetAction::SetKey, slot);
			case PresetMode::Scale:
				// Hardware: Major(10), Minor(11), Chromatic(12) = slots 9..11.
				if (slot < 9)
					return PresetAction();  // ignored, stays armed
				mode_ = PresetMode::Idle;
				return PresetAction(PresetAction::SetScale, (uint8_t)(slot - 9));
			default:
				return PresetAction();
		}
	}

private:
	PresetMode mode_;
};

} // namespace spacetime
