#pragma once

// Stable, target-neutral IDs for the portable SpaceTime authoring family.
// VCV Rack and MetaModule adapters must use these exact values. Once a module
// is released, IDs are append-only so exported MetaModule mappings remain
// valid across plugin updates.

namespace spacetime {

static const int kMinStageBanks = 1;
static const int kMaxStageBanks = 16;
static const int kStagesPerBank = 4;

inline int clampStageBanks(int banks) {
	return banks < kMinStageBanks ? kMinStageBanks :
		(banks > kMaxStageBanks ? kMaxStageBanks : banks);
}

inline int stageCountFromBanks(int banks) {
	return clampStageBanks(banks) * kStagesPerBank;
}

inline int stageBanksFromCount(int stages) {
	if (stages <= kStagesPerBank)
		return kMinStageBanks;
	return clampStageBanks((stages + kStagesPerBank - 1) / kStagesPerBank);
}

struct MMProgramContract {
	static constexpr const char* slug = "MMProgram";

	// Contract history (2026-09-04, option A): the original 3-position
	// STAGE_STEP lever became two momentary stage buttons. STAGE_STEP_UP_PARAM
	// is *appended at the end* so every existing index (PRESET_PARAM..
	// TRANGE_SELECTOR_PARAM) and every DROID/MIDI binding that reads them
	// stays byte-for-byte identical across the plugin update.
	enum ParamId {
		STAGE_PARAM,
		// 0:0 down button (was STAGE_STEP_PARAM at index 1 before this edit;
		// the paramId slot is unchanged, only the semantic narrowed to
		// "previous stage" from "3-way lever").
		STAGE_STEP_DOWN_PARAM,
		PRESET_PARAM,
		CONTROL_CHANNEL_PARAM,
		SLIDER_CHANNEL_PARAM,
		SAVE_PARAM,
		LOAD_PARAM,
		CLEAR_PARAM,
		PULSE_RETRIG_PARAM,
		KEY_PARAM,
		SCALE_PARAM,
		BULK_PARAM,
		// 12 modifier momentary step-buttons (see emitStep for wrap semantics).
		MODIFIER_PARAMS,
		LTD_PARAMS = MODIFIER_PARAMS + 12,
		TRANGE_PARAMS = LTD_PARAMS + 5,
		STAGE_BANKS_PARAM = TRANGE_PARAMS + 4,
		INSTRUMENT_PARAM,
		LTD_SELECTOR_PARAM,
		TRANGE_SELECTOR_PARAM,
		// 1:1 up button (new, appended).
		STAGE_STEP_UP_PARAM,
		PARAMS_LEN
	};

	enum InputId {
		EXT_INPUTS,
		INPUTS_LEN = EXT_INPUTS + 4
	};

	enum OutputId {
		SELECTED_V_OUTPUT,
		SELECTED_TIME_OUTPUT,
		POLY_OUTPUT,
		OUTPUTS_LEN
	};
};

struct MMStage4Contract {
	static constexpr const char* slug = "MMStage4";

	enum ParamId {
		INSTRUMENT_PARAM,
		BANK_PARAM,
		VOLTAGE_PARAMS,
		TIME_PARAMS = VOLTAGE_PARAMS + 4,
		FOCUS_PARAM = TIME_PARAMS + 4,
		PARAMS_LEN
	};

	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
};

struct MMHeadContract {
	static constexpr const char* slug = "MMHead";

	enum ParamId {
		INSTRUMENT_PARAM,
		HEAD_PARAM,
		START_PARAM,
		STOP_PARAM,
		ADVANCE_PARAM,
		RESET_PARAM,
		ADDRESS_PARAM,
		ADDR_SOURCE_PARAM,
		ADDR_MODE_PARAM,
		DIRECTION_PARAM,
		CLK_SOURCE_PARAM,
		CLK_DIV_PARAM,
		TIMECV_PARAM,
		LOOP_PARAM,
		PARAMS_LEN
	};

	enum InputId {
		START_INPUT,
		STOP_INPUT,
		ADVANCE_INPUT,
		STROBE_INPUT,
		ADDRESS_INPUT,
		CLK_INPUT,
		TIMECV_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};

	enum OutputId {
		CV_OUTPUT,
		TIME_OUTPUT,
		REF_OUTPUT,
		ALL_OUTPUT,
		PULSE1_OUTPUT,
		PULSE2_OUTPUT,
		EOC_OUTPUT,
		OUTPUTS_LEN
	};
};

struct MMHeadAllContract {
	static constexpr const char* slug = "MMHeadAll";
};

} // namespace spacetime
