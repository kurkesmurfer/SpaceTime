#include "plugin.hpp"
#include "MMBus.hpp"
#include "ChainAdapter.hpp"
#include "paneltheme.hpp"
#include "NativeModuleContracts.hpp"
#include "spacetime_widgets.hpp"

#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

struct StageTime : Module {
	enum ParamId { INSTRUMENT_PARAM, FREE_PARAM, SYNC_PARAM, INSTRUMENT_DOWN_PARAM, INSTRUMENT_UP_PARAM, PARAMS_LEN };
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { FREE_LIGHT, SYNC_LIGHT, LIGHTS_LEN };
	dsp::BooleanTrigger freeTrigger, syncTrigger, instrumentDownTrigger, instrumentUpTrigger;
	int instrumentId = 0;
	uint8_t selectedStage = 0;
	bool sync = false;
	bool modePending = false;
	uint8_t pendingStage = 0;
	bool pendingSync = false;

	StageTime() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INSTRUMENT_PARAM, 0.f, 15.f, 0.f, "Instrument ID", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		spacetime::configDualSpring3(this, SYNC_PARAM, FREE_PARAM, "Stage timing", "Sync", "Free");
		spacetime::configDualSpring3(this, INSTRUMENT_DOWN_PARAM, INSTRUMENT_UP_PARAM,
			"Instrument ID", "Previous", "Next");
	}

	void process(const ProcessArgs&) override {
		// Follow an adjacent PROGRAM's ID so the sidecar stays on the same
		// instrument; the bottom spring selector remains available when standalone.
		engine::Module* linkedProgram = spacetime::chainNeighbor(this, true);
		if (!linkedProgram || linkedProgram->model != modelProgram)
			linkedProgram = spacetime::chainNeighbor(this, false);
		if (instrumentDownTrigger.process(params[INSTRUMENT_DOWN_PARAM].getValue() > 0.5f)) {
			int id = linkedProgram && linkedProgram->model == modelProgram
				? spacetime::clampInstrumentId((int)std::round(linkedProgram->params[spacetime::NativeProgramContract::INSTRUMENT_PARAM].getValue()))
				: spacetime::clampInstrumentId((int)std::round(params[INSTRUMENT_PARAM].getValue()));
			id = (id + spacetime::kInstrumentIdCount - 1) % spacetime::kInstrumentIdCount;
			params[INSTRUMENT_PARAM].setValue((float)id);
			if (linkedProgram && linkedProgram->model == modelProgram)
				linkedProgram->params[spacetime::NativeProgramContract::INSTRUMENT_PARAM].setValue((float)id);
		}
		if (instrumentUpTrigger.process(params[INSTRUMENT_UP_PARAM].getValue() > 0.5f)) {
			int id = linkedProgram && linkedProgram->model == modelProgram
				? spacetime::clampInstrumentId((int)std::round(linkedProgram->params[spacetime::NativeProgramContract::INSTRUMENT_PARAM].getValue()))
				: spacetime::clampInstrumentId((int)std::round(params[INSTRUMENT_PARAM].getValue()));
			id = (id + 1) % spacetime::kInstrumentIdCount;
			params[INSTRUMENT_PARAM].setValue((float)id);
			if (linkedProgram && linkedProgram->model == modelProgram)
				linkedProgram->params[spacetime::NativeProgramContract::INSTRUMENT_PARAM].setValue((float)id);
		}
		if (linkedProgram && linkedProgram->model == modelProgram) {
			int linkedId = spacetime::clampInstrumentId((int)std::round(
				linkedProgram->params[spacetime::NativeProgramContract::INSTRUMENT_PARAM].getValue()));
			if (linkedId != instrumentId)
				params[INSTRUMENT_PARAM].setValue((float)linkedId);
		}
		instrumentId = spacetime::clampInstrumentId((int)std::round(params[INSTRUMENT_PARAM].getValue()));
		uint32_t sequence = 0;
		uint8_t stage = 0;
		bool currentSync = false;
		if (mmStageTimeRegistry.readSelection(instrumentId, sequence, stage, currentSync)) {
			selectedStage = stage;
			if (modePending && stage == pendingStage && currentSync == pendingSync)
				modePending = false;
			if (!modePending || stage != pendingStage)
				sync = currentSync;
			else
				sync = pendingSync;
		}
		if (freeTrigger.process(params[FREE_PARAM].getValue() > 0.5f)) {
			sync = false;
			modePending = true;
			pendingStage = selectedStage;
			pendingSync = false;
			mmStageTimeRegistry.publishMode(instrumentId, selectedStage, false);
		}
		if (syncTrigger.process(params[SYNC_PARAM].getValue() > 0.5f)) {
			sync = true;
			modePending = true;
			pendingStage = selectedStage;
			pendingSync = true;
			mmStageTimeRegistry.publishMode(instrumentId, selectedStage, true);
		}
		lights[FREE_LIGHT].setBrightness(sync ? 0.f : 1.f);
		lights[SYNC_LIGHT].setBrightness(sync ? 1.f : 0.f);
	}
};

struct StageTimeInstrumentDisplay : Widget {
	StageTime* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !module) return;
		auto font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font) return;
		char text[2] = {spacetime::instrumentIdHex(module->instrumentId), '\0'};
		nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 17.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xc1, 0x3c, 0x36));
		nvgText(args.vg, box.size.x / 2, box.size.y / 2, text, nullptr);
	}
};

struct StageTimeSelectedStageDisplay : Widget {
	StageTime* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !module) return;
		auto font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font) return;
		char text[4];
		snprintf(text, sizeof(text), "%u", (unsigned)module->selectedStage + 1);
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 14.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xc1, 0x3c, 0x36));
		nvgText(args.vg, box.size.x / 2, box.size.y / 2, text, nullptr);
	}
};

struct StageTimeWidget : ModuleWidget {
	StageTimeWidget(StageTime* module) {
		setModule(module);
		setPanel(spacetime::createThemedPanel(
			asset::plugin(pluginInstance, "res/StageTime-light.svg"),
			asset::plugin(pluginInstance, "res/StageTime.svg")));
		spacetime::addHeaderLockup(this, 50.8f, "Stage Time", 24.5f, 0.92f);
		spacetime::addHeaderMark(this, 50.8f, 24.5f, 0.92f);
		spacetime::addSubtitle(this, 25.4f, 10.2f, "STEP TIMING");
		// Match the switch direction to the panel: FREE is the upper position,
		// SYNC is the lower position.
		spacetime::addDualSpring3(this, module, mm2px(Vec(26.8f, 25.1f)), StageTime::SYNC_PARAM, StageTime::FREE_PARAM);
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(34.3f, 22.1f)), module, StageTime::FREE_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(34.3f, 27.8f)), module, StageTime::SYNC_LIGHT));
		auto* selectedStageDisplay = createWidget<StageTimeSelectedStageDisplay>(mm2px(Vec(7.1f, 21.1f)));
		selectedStageDisplay->box.size = mm2px(Vec(8.f, 8.f));
		selectedStageDisplay->module = module;
		addChild(selectedStageDisplay);
		auto* instrumentDisplay = createWidget<StageTimeInstrumentDisplay>(mm2px(Vec(4.74f, 110.5f)));
		instrumentDisplay->box.size = mm2px(Vec(8.f, 8.f)); instrumentDisplay->module = module; addChild(instrumentDisplay);
		spacetime::addDualSpring3(this, module, mm2px(Vec(18.2f, 114.8f)), StageTime::INSTRUMENT_DOWN_PARAM, StageTime::INSTRUMENT_UP_PARAM);
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator);
		spacetime::appendPanelThemeMenu(menu);
	}
};

Model* modelStageTime = createModel<StageTime, StageTimeWidget>("StageTime");
