#include "plugin.hpp"
#include "paneltheme.hpp"
#include "spacetime_widgets.hpp"
#include "PresetRow.hpp"

// ============================================================================
// WidgetTest — WP3 dev-only module exercising each custom widget.
// Not intended for patches; hide from the browser at release (WP8).
//
// Manual checklist (Rack):
//   1. Spring switches: press upper/lower half -> UP/DOWN light while held,
//      returns to center on release; rapid clicking leaves no stuck states;
//      no history (undo) events are created by presses.
//   2. LED cluster: edit LED blinks, dots cycle all 8 head colours.
//   3. Limited bank: radio behaviour — exactly one of five lit, presses move it.
//   4. Preset row: LOAD/SAVE/KEY/SCALE arms (mode LED on; same button cancels,
//      other mode re-arms); slot press fires action (LAST ACTION light flashes
//      green, the slot's own LED flashes); SCALE accepts only 10/11/12.
// ============================================================================

struct WidgetTest : Module {
	enum ParamId {
		SPRING_A_PARAM,
		SPRING_B_PARAM,
		ENUMS(LTD_PARAMS, 5),
		ENUMS(SLOT_PARAMS, 12),
		LOAD_PARAM,
		SAVE_PARAM,
		KEY_PARAM,
		SCALE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		A_UP_LIGHT,
		A_DOWN_LIGHT,
		B_UP_LIGHT,
		B_DOWN_LIGHT,
		EDIT_LIGHT,
		ENUMS(HEAD_LIGHTS, 8 * 3),
		ENUMS(LTD_LIGHTS, 5),
		ENUMS(SLOT_LIGHTS, 12),
		ENUMS(MODE_LIGHTS, 4),   // Load, Save, Key, Scale armed
		ENUMS(ACT_LIGHTS, 4),    // last action: Load, Save, SetKey, SetScale
		LIGHTS_LEN
	};

	spacetime::PresetRowLogic presetLogic;
	dsp::BooleanTrigger ltdTriggers[5];
	dsp::BooleanTrigger slotTriggers[12];
	dsp::BooleanTrigger modeTriggers[4];
	dsp::ClockDivider lightDivider;

	int ltdSelected = 2;
	float clusterPhase = 0.f;
	int clusterStep = 0;
	float actTimer[4] = {};
	int flashSlot = -1;
	float flashTimer = 0.f;

	WidgetTest() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		spacetime::configSpring3(this, SPRING_A_PARAM, "Stage scroll (spring test)",
			"Scroll left", "Scroll right");
		spacetime::configSpring3(this, SPRING_B_PARAM, "Modifier (spring test)",
			"Remove", "Add");
		for (int i = 0; i < 5; i++)
			configButton(LTD_PARAMS + i, string::f("Limited octave %+d", i - 2));
		for (int i = 0; i < 12; i++)
			configButton(SLOT_PARAMS + i, string::f("Preset slot %d", i + 1));
		configButton(LOAD_PARAM, "Load (modal)");
		configButton(SAVE_PARAM, "Save (modal)");
		configButton(KEY_PARAM, "Key (modal)");
		configButton(SCALE_PARAM, "Scale (modal; then 10/11/12)");

		lightDivider.setDivision(16);
	}

	void process(const ProcessArgs& args) override {
		if (!lightDivider.process())
			return;
		float dt = args.sampleTime * lightDivider.getDivision();

		// 1. Spring switches — show live position.
		float a = params[SPRING_A_PARAM].getValue();
		float b = params[SPRING_B_PARAM].getValue();
		lights[A_UP_LIGHT].setBrightnessSmooth(a > 1.5f ? 1.f : 0.f, dt);
		lights[A_DOWN_LIGHT].setBrightnessSmooth(a < 0.5f ? 1.f : 0.f, dt);
		lights[B_UP_LIGHT].setBrightnessSmooth(b > 1.5f ? 1.f : 0.f, dt);
		lights[B_DOWN_LIGHT].setBrightnessSmooth(b < 0.5f ? 1.f : 0.f, dt);

		// 2. LED cluster — edit blink + head colour cycle.
		clusterPhase += dt;
		if (clusterPhase >= 0.4f) {
			clusterPhase -= 0.4f;
			clusterStep++;
		}
		lights[EDIT_LIGHT].setBrightnessSmooth((clusterStep % 2) ? 1.f : 0.f, dt);
		for (int h = 0; h < 8; h++)
			spacetime::setHeadDot(this, HEAD_LIGHTS + h * 3, h,
				(h == clusterStep % 8) ? 1.f : 0.f, dt);

		// 3. Limited bank — radio.
		for (int i = 0; i < 5; i++) {
			if (ltdTriggers[i].process(params[LTD_PARAMS + i].getValue() > 0.5f))
				ltdSelected = i;
		}
		for (int i = 0; i < 5; i++)
			lights[LTD_LIGHTS + i].setBrightnessSmooth(i == ltdSelected ? 1.f : 0.f, dt);

		// 4. Preset row — modal logic.
		static const spacetime::PresetMode modes[4] = {
			spacetime::PresetMode::Load, spacetime::PresetMode::Save,
			spacetime::PresetMode::Key, spacetime::PresetMode::Scale};
		for (int i = 0; i < 4; i++) {
			if (modeTriggers[i].process(params[LOAD_PARAM + i].getValue() > 0.5f))
				presetLogic.onModePress(modes[i]);
		}
		for (int i = 0; i < 12; i++) {
			if (slotTriggers[i].process(params[SLOT_PARAMS + i].getValue() > 0.5f)) {
				spacetime::PresetAction act = presetLogic.onSlotPress((uint8_t)i);
				if (act.type != spacetime::PresetAction::None) {
					actTimer[act.type - 1] = 1.f;
					flashSlot = act.index;
					flashTimer = 0.6f;
				}
			}
		}
		for (int i = 0; i < 4; i++)
			lights[MODE_LIGHTS + i].setBrightnessSmooth(
				presetLogic.mode() == modes[i] ? 1.f : 0.f, dt);
		for (int i = 0; i < 4; i++) {
			actTimer[i] = std::max(0.f, actTimer[i] - dt);
			lights[ACT_LIGHTS + i].setBrightness(actTimer[i]);
		}
		flashTimer = std::max(0.f, flashTimer - dt);
		for (int i = 0; i < 12; i++)
			lights[SLOT_LIGHTS + i].setBrightness(
				(flashTimer > 0.f && i == flashSlot) ? 1.f : 0.f);
	}
};

struct WidgetTestWidget : ModuleWidget {
	WidgetTestWidget(WidgetTest* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/WidgetTest.svg")));

#ifndef METAMODULE
		spacetime::addTitle(this, 40.64f, 5.6f, "WidgetTest");
		spacetime::addSubtitle(this, 40.64f, 10.2f, "WP3 WIDGET EXERCISER");

		spacetime::addSectionHeading(this, 40.64f, 16.5f, "SPRING-RETURN 3-POS");
		spacetime::addCvLabel(this, 15.f, 35.5f, "SCROLL");
		spacetime::addCvLabel(this, 35.f, 35.5f, "MODIFIER");
		spacetime::addMicroLabel(this, 22.f, 21.5f, "UP");
		spacetime::addMicroLabel(this, 22.f, 30.5f, "DN");
		spacetime::addMicroLabel(this, 42.f, 21.5f, "UP");
		spacetime::addMicroLabel(this, 42.f, 30.5f, "DN");
		spacetime::addCvLabel(this, 62.f, 35.5f, "CLUSTER");

		spacetime::addSectionHeading(this, 40.64f, 42.f, "LIMITED BANK (RADIO)");
		{
			static const char* ltd[5] = {"-2", "-1", "0", "+1", "+2"};
			for (int i = 0; i < 5; i++)
				spacetime::addCvLabel(this, 16.f + 12.f * i, 53.8f, ltd[i]);
		}

		spacetime::addSectionHeading(this, 40.64f, 60.f, "PRESET ROW (MODAL)");
		for (int i = 0; i < 12; i++)
			spacetime::addMicroLabel(this, 6.5f + 6.2f * i, 71.f, string::f("%d", i + 1));
		{
			static const char* mode[4] = {"LOAD", "SAVE", "KEY", "SCALE"};
			for (int i = 0; i < 4; i++)
				spacetime::addCvLabel(this, 16.f + 14.f * i, 84.3f, mode[i]);
		}

		spacetime::addSectionHeading(this, 40.64f, 91.f, "LAST ACTION");
		{
			static const char* act[4] = {"LOAD", "SAVE", "KEY", "SCALE"};
			for (int i = 0; i < 4; i++)
				spacetime::addMicroLabel(this, 16.f + 14.f * i, 100.3f, act[i]);
		}
#endif

		// Spring switches + position lights
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(15.f, 26.f)), module, WidgetTest::SPRING_A_PARAM));
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(35.f, 26.f)), module, WidgetTest::SPRING_B_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(19.f, 21.5f)), module, WidgetTest::A_UP_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(19.f, 30.5f)), module, WidgetTest::A_DOWN_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(39.f, 21.5f)), module, WidgetTest::B_UP_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(39.f, 30.5f)), module, WidgetTest::B_DOWN_LIGHT));

		// LED cluster
		spacetime::addStageLedCluster(this, module, 62.f, 27.5f,
			WidgetTest::EDIT_LIGHT, WidgetTest::HEAD_LIGHTS);

		// Limited bank
		spacetime::addLimitedBank(this, module, 16.f, 48.5f, 12.f,
			WidgetTest::LTD_PARAMS, WidgetTest::LTD_LIGHTS);

		// Preset row + mode buttons
		spacetime::addPresetRow(this, module, 6.5f, 66.5f, 6.2f,
			WidgetTest::SLOT_PARAMS, WidgetTest::SLOT_LIGHTS);
		for (int i = 0; i < 4; i++)
			spacetime::addPresetModeButton(this, module, 16.f + 14.f * i, 79.f,
				WidgetTest::LOAD_PARAM + i, WidgetTest::MODE_LIGHTS + i);

		// Action lights
		for (int i = 0; i < 4; i++)
			addChild(createLightCentered<MediumLight<GreenLight>>(
				mm2px(Vec(16.f + 14.f * i, 96.f)), module, WidgetTest::ACT_LIGHTS + i));
	}
};

Model* modelWidgetTest = createModel<WidgetTest, WidgetTestWidget>("WidgetTest");
