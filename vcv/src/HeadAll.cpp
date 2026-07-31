#include "plugin.hpp"
#include "paneltheme.hpp"
#include "spacetime_widgets.hpp"
#include "ChainAdapter.hpp"
#include "Chain.hpp"

// ============================================================================
// HEAD ALL — common control source at the far-left end of the HEAD chain.
// It contributes no playhead and no signal outputs. Persistent panel changes
// and edge-safe common inputs travel rightward in HeadsToAnchorMsg.
// ============================================================================

namespace LayoutHA {
constexpr float CX = 25.4f;
constexpr float BTN_X0 = 9.4f, BTN_PITCH = 11.f, BTN_Y = 21.5f;
constexpr float STATUS_Y = 30.5f;
constexpr float ADDR_Y = 45.f;
constexpr float ADDR_KNOB_X = 10.f, ADDR_SRC_X = 25.4f, ADDR_MODE_X = 41.f;
constexpr float PLAY_Y = 64.5f;
constexpr float DIR_X = 10.f, CLKSRC_X = 25.4f, CLKDIV_X = 41.f;
constexpr float PLAY2_Y = 78.f;
constexpr float TIMECV_X = 10.f, LOOP_X = 25.4f, LINK_X = 41.f;
constexpr float JACK_X0 = 9.4f, JACK_PITCH = 11.f;
constexpr float IN1_Y = 89.f, IN2_Y = 98.5f;
} // namespace LayoutHA

struct HeadAll : Module {
	enum ParamId {
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
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LINK_LIGHT, LIGHTS_LEN };

	spacetime::HeadAllState state;
	rack::dsp::BooleanTrigger startTrigger, stopTrigger, advanceTrigger;
	rack::dsp::BooleanTrigger resetTrigger, strobeTrigger, clockTrigger;
	rack::dsp::ClockDivider divider;
	float lastPersistent[8];
	bool chainOk = false;

	HeadAll() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(START_PARAM, "Start all Heads");
		configButton(STOP_PARAM, "Stop all Heads");
		configButton(ADVANCE_PARAM, "Advance all Heads");
		configButton(RESET_PARAM, "Reset all Heads to First stage");
		configParam(ADDRESS_PARAM, 0.f, 10.f, 0.f, "Address for all Heads", " V");
		configSwitch(ADDR_SOURCE_PARAM, 0.f, 1.f, 0.f, "Address source for all Heads",
			{"Internal (knob)", "External (common ADDRESS CV or local override)"});
		configSwitch(ADDR_MODE_PARAM, 0.f, 2.f, 1.f,
			"Addressing mode for all Heads",
			{"Strobe (momentary)", "Sequential", "Continuous"});
		configSwitch(DIRECTION_PARAM, 0.f, 4.f, 0.f, "Direction for all Heads",
			{"Forward", "Reverse", "Pendulum", "Random", "Brownian"});
		configSwitch(CLK_SOURCE_PARAM, 0.f, 3.f, 0.f, "Clock source for all Heads",
			{"Internal", "External common CLK or local override", "MIDI clock", "Virtual clock"});
		configSwitch(CLK_DIV_PARAM, 0.f, 8.f, 4.f, "Clock div/mult for all Heads",
			{"/16", "/8", "/4", "/2", "x1", "x2", "x4", "x8", "x16"});
		configParam(TIMECV_PARAM, -1.f, 1.f, 0.f, "Time CV amount for all Heads", "%", 0.f, 100.f);
		configSwitch(LOOP_PARAM, 0.f, 2.f, 1.f, "Loop mode for all Heads",
			{"One-shot", "Cycle First-Last", "Full chain"});

		configInput(START_INPUT, "Common Start (also Sustain/Enable gate)");
		configInput(STOP_INPUT, "Common Stop");
		configInput(ADVANCE_INPUT, "Common Advance");
		configInput(STROBE_INPUT, "Common Strobe");
		configInput(ADDRESS_INPUT, "Common Address CV (normalled to unpatched Head inputs)");
		configInput(CLK_INPUT, "Common external clock (normalled to unpatched Head inputs)");
		configInput(TIMECV_INPUT, "Common Time CV (normalled to unpatched Head inputs)");
		configInput(RESET_INPUT, "Common Reset");
		configLight(LINK_LIGHT, "HEAD ALL chain connection");

		for (int i = 0; i < 8; i++)
			lastPersistent[i] = NAN;
		divider.setDivision(16);
	}

	void emitControl(int cc, float value) {
		if (cc < 0 || cc >= spacetime::kHeadAllControls)
			return;
		state.controlValue[cc] = value;
		state.controlSeq[cc]++;
	}

	void publishPersistentControls() {
		static const int paramIds[8] = {
			ADDRESS_PARAM, ADDR_SOURCE_PARAM, ADDR_MODE_PARAM, DIRECTION_PARAM,
			CLK_SOURCE_PARAM, CLK_DIV_PARAM, TIMECV_PARAM, LOOP_PARAM
		};
		static const int ccs[8] = {5, 6, 7, 8, 9, 10, 11, 12};
		for (int i = 0; i < 8; i++) {
			float value = params[paramIds[i]].getValue();
			if (!std::isfinite(lastPersistent[i]) || value != lastPersistent[i]) {
				lastPersistent[i] = value;
				// Strobe is emitted by the audio-rate edge detector below. Its
				// return to center still publishes Sequential normally.
				if (ccs[i] != 7 || value >= 0.5f)
					emitControl(ccs[i], value);
			}
		}
	}

	void process(const ProcessArgs& args) override {
		float start = std::fmax(inputs[START_INPUT].getVoltage(),
			params[START_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		float stop = std::fmax(inputs[STOP_INPUT].getVoltage(),
			params[STOP_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		float advance = std::fmax(inputs[ADVANCE_INPUT].getVoltage(),
			params[ADVANCE_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		float reset = std::fmax(inputs[RESET_INPUT].getVoltage(),
			params[RESET_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		float strobe = std::fmax(inputs[STROBE_INPUT].getVoltage(),
			params[ADDR_MODE_PARAM].getValue() < 0.5f ? 10.f : 0.f);

		state.startGate = start;
		state.addressCv = inputs[ADDRESS_INPUT].getVoltage();
		state.timeCv = inputs[TIMECV_INPUT].getVoltage();
		state.addressConnected = inputs[ADDRESS_INPUT].isConnected();
		state.timeConnected = inputs[TIMECV_INPUT].isConnected();
		state.valid = true;

		if (startTrigger.process(start >= 1.f)) emitControl(1, 1.f);
		if (stopTrigger.process(stop >= 1.f)) emitControl(2, 1.f);
		if (advanceTrigger.process(advance >= 1.f)) emitControl(3, 1.f);
		if (resetTrigger.process(reset >= 1.f)) emitControl(4, 1.f);
		if (strobeTrigger.process(strobe >= 1.f)) emitControl(7, 0.f);
		if (clockTrigger.process(inputs[CLK_INPUT].getVoltage() >= 1.f))
			state.externalClockSeq++;

		if (!divider.process())
			return;
		float dt = args.sampleTime * divider.getDivision();
		publishPersistentControls();

		chainOk = spacetime::modelIs(rightExpander.module, modelHead) ||
			(spacetime::modelIs(rightExpander.module, modelGlueRight) &&
			 gluePartnerOutward(rightExpander.module));
		spacetime::HeadsToAnchorMsg* out =
			spacetime::rightNeighborProducer<spacetime::HeadsToAnchorMsg>(
				this, modelHead, modelGlueRight);
		if (out) {
			*out = spacetime::HeadsToAnchorMsg();
			out->headAll = state;
			out->valid = true;
			spacetime::flipRightNeighbor(this);
		}
		lights[LINK_LIGHT].setBrightnessSmooth(chainOk ? 1.f : 0.f, dt);
	}
};

struct HeadAllWidget : ModuleWidget {
	explicit HeadAllWidget(HeadAll* module) {
		using namespace LayoutHA;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/HeadAll.svg")));

#ifndef METAMODULE
		spacetime::addTitle(this, CX, 5.6f, "Head All");
		spacetime::addSubtitle(this, CX, 10.2f, "COMMON CONTROL");
		spacetime::addSectionHeading(this, CX, 14.7f, "TRANSPORT");
		static const char* btnNames[4] = {"START", "STOP", "ADV", "RST"};
		for (int i = 0; i < 4; i++)
			spacetime::addCvLabel(this, BTN_X0 + BTN_PITCH * i, 26.5f, btnNames[i]);
		spacetime::addMicroLabel(this, LINK_X, 33.8f, "LINK");

		spacetime::addSectionHeading(this, CX, 36.7f, "ADDRESS");
		spacetime::addCvLabel(this, ADDR_KNOB_X, 53.f, "ADDRESS");
		spacetime::addMicroLabel(this, ADDR_SRC_X, 40.f, "EXT");
		spacetime::addMicroLabel(this, ADDR_SRC_X, 50.6f, "INT");
		spacetime::addMicroLabel(this, ADDR_MODE_X + 5.f, 41.f, "CONT");
		spacetime::addMicroLabel(this, ADDR_MODE_X + 5.f, 45.f, "SEQ");
		spacetime::addMicroLabel(this, ADDR_MODE_X + 5.f, 49.f, "STRB");

		spacetime::addSectionHeading(this, CX, 56.2f, "PLAYBACK");
		spacetime::addCvLabel(this, DIR_X, 71.8f, "DIRECTION");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 58.6f, "VCLK");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 62.0f, "MIDI");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 65.4f, "CV");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 68.8f, "INT");
		spacetime::addCvLabel(this, CLKDIV_X, 71.8f, "DIV / MULT");
		spacetime::addCvLabel(this, TIMECV_X, 83.8f, "TIME CV");
		spacetime::addMicroLabel(this, LOOP_X + 5.6f, 74.f, "ALL");
		spacetime::addMicroLabel(this, LOOP_X + 5.6f, 78.f, "F-L");
		spacetime::addMicroLabel(this, LOOP_X + 5.6f, 82.f, "1-SHOT");
		spacetime::addMicroLabel(this, LINK_X, 83.8f, "ALL");

		static const char* in1[4] = {"START", "STOP", "ADV", "STRB"};
		static const char* in2[4] = {"ADDR", "CLK", "TIME", "RST"};
		for (int i = 0; i < 4; i++) {
			spacetime::addIoLabel(this, JACK_X0 + JACK_PITCH * i, IN1_Y + 4.6f, in1[i]);
			spacetime::addIoLabel(this, JACK_X0 + JACK_PITCH * i, IN2_Y + 4.6f, in2[i]);
		}
		spacetime::addSectionHeading(this, CX, 109.f, "COMMON HEAD BUS");
		addChild(new spacetime::CornerMark(50.8f, 128.5f, 0.70f, 3.21f, 4.f));
#endif

		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0, BTN_Y)), module, HeadAll::START_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0 + BTN_PITCH, BTN_Y)), module, HeadAll::STOP_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0 + 2 * BTN_PITCH, BTN_Y)), module, HeadAll::ADVANCE_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0 + 3 * BTN_PITCH, BTN_Y)), module, HeadAll::RESET_PARAM));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(LINK_X, STATUS_Y)), module, HeadAll::LINK_LIGHT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(ADDR_KNOB_X, ADDR_Y)), module, HeadAll::ADDRESS_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(ADDR_SRC_X, ADDR_Y)), module, HeadAll::ADDR_SOURCE_PARAM));
		addParam(createParamCentered<spacetime::LatchSpringSwitch3>(mm2px(Vec(ADDR_MODE_X, ADDR_Y)), module, HeadAll::ADDR_MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(DIR_X, PLAY_Y)), module, HeadAll::DIRECTION_PARAM));
		addParam(createParamCentered<spacetime::Switch4>(mm2px(Vec(CLKSRC_X, PLAY_Y)), module, HeadAll::CLK_SOURCE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(CLKDIV_X, PLAY_Y)), module, HeadAll::CLK_DIV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(TIMECV_X, PLAY2_Y)), module, HeadAll::TIMECV_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(LOOP_X, PLAY2_Y)), module, HeadAll::LOOP_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0, IN1_Y)), module, HeadAll::START_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + JACK_PITCH, IN1_Y)), module, HeadAll::STOP_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 2 * JACK_PITCH, IN1_Y)), module, HeadAll::ADVANCE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 3 * JACK_PITCH, IN1_Y)), module, HeadAll::STROBE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0, IN2_Y)), module, HeadAll::ADDRESS_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + JACK_PITCH, IN2_Y)), module, HeadAll::CLK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 2 * JACK_PITCH, IN2_Y)), module, HeadAll::TIMECV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 3 * JACK_PITCH, IN2_Y)), module, HeadAll::RESET_INPUT));
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Placement: left of the furthest HEAD"));
		menu->addChild(createMenuLabel("MIDI: channel 9, CC 0-12"));
		menu->addChild(createMenuLabel("Display CC 13 is intentionally excluded"));
	}
};

Model* modelHeadAll = createModel<HeadAll, HeadAllWidget>("HeadAll");
