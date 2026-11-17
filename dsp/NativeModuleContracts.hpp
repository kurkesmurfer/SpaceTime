#pragma once

// Stable IDs shared by the original VCV modules and their MetaModule
// implementations. Program deliberately uses two momentary parameters for
// every spring lever so both directions are independently bindable on either
// platform. Program and Head use the post-2.0.5 dual-action contracts; older
// development mappings are not compatible with them.

namespace spacetime {

struct NativeProgramContract {
	static constexpr const char* slug = "Program";
	enum ParamId {
		STAGE_SCROLL_DOWN_PARAM,
		STAGE_SCROLL_UP_PARAM,
		CLEAR_PARAM,
		QUANTIZE_DOWN_PARAM,
		QUANTIZE_UP_PARAM,
		SLOPE_DOWN_PARAM,
		SLOPE_UP_PARAM,
		RANGE_DOWN_PARAM,
		RANGE_UP_PARAM,
		VSOURCE_DOWN_PARAM,
		VSOURCE_UP_PARAM,
		LTD_PARAMS,
		STOP_DOWN_PARAM = LTD_PARAMS + 5,
		STOP_UP_PARAM,
		SUSTAIN_DOWN_PARAM,
		SUSTAIN_UP_PARAM,
		ENABLE_DOWN_PARAM,
		ENABLE_UP_PARAM,
		FIRST_DOWN_PARAM,
		FIRST_UP_PARAM,
		LAST_DOWN_PARAM,
		LAST_UP_PARAM,
		TRANGE_PARAMS,
		TSOURCE_DOWN_PARAM = TRANGE_PARAMS + 4,
		TSOURCE_UP_PARAM,
		PULSE1_DOWN_PARAM,
		PULSE1_UP_PARAM,
		PULSE2_DOWN_PARAM,
		PULSE2_UP_PARAM,
		PRESET_PARAMS,
		LOAD_PARAM = PRESET_PARAMS + 12,
		SAVE_PARAM,
		KEY_PARAM,
		SCALE_PARAM,
		PULSE_RETRIG_PARAM,
		INSTRUMENT_PARAM,
		LTD_SELECTOR_PARAM,
		TRANGE_SELECTOR_PARAM,
		INSTRUMENT_DOWN_PARAM,
		INSTRUMENT_UP_PARAM,
		PULSE_TIME_PARAM,
		NATIVE_PARAMS_LEN
	};
	enum InputId { EXT_INPUTS, INPUTS_LEN = EXT_INPUTS + 4 };
	enum OutputId { POLY_OUTPUT, OUTPUTS_LEN };
};

struct NativeStage4Contract {
	static constexpr const char* slug = "Stage4";
	enum ParamId {
		VOLTAGE_PARAMS,
		TIME_PARAMS = VOLTAGE_PARAMS + 4,
		INSTRUMENT_PARAM = TIME_PARAMS + 4,
		BANK_PARAM,
		FOCUS_PARAM,
		PARAMS_LEN
	};
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
};

struct NativeHeadContract {
	static constexpr const char* slug = "Head";
	enum ParamId {
		START_PARAM,
		STOP_PARAM,
		ADVANCE_PARAM,
		RESET_PARAM,
		ADDRESS_PARAM,
		ADDR_SOURCE_PARAM,
		ADDR_CONTINUOUS_PARAM,
		ADDR_STROBE_PARAM,
		DIRECTION_PARAM,
		CLK_SOURCE_PARAM,
		CLK_DIV_PARAM,
		TIMECV_PARAM,
		LOOP_PARAM,
		DISPLAY_PARAM,
		INSTRUMENT_PARAM,
		HEAD_PARAM,
		TIME_MULT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		START_INPUT, STOP_INPUT, ADVANCE_INPUT, STROBE_INPUT,
		ADDRESS_INPUT, CLK_INPUT, TIMECV_INPUT, RESET_INPUT, INPUTS_LEN
	};
	enum OutputId {
		CV_OUTPUT, TIME_OUTPUT, REF_OUTPUT, ALL_OUTPUT,
		PULSE1_OUTPUT, PULSE2_OUTPUT, EOC_OUTPUT, OUTPUTS_LEN
	};
};

} // namespace spacetime
