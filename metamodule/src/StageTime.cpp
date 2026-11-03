#include <rack.hpp>
#include <metamodule/VCVTextDisplay.hpp>
#include <string>

#include "MMModuleContracts.hpp"
#include "RemoteBus.hpp"
#include "InstrumentId.hpp"

#include <algorithm>

using namespace rack;
extern Plugin* pluginInstance;

namespace {
struct SpaceTimeStageTime : Module {
	enum ParamId { INSTRUMENT_PARAM, FREE_PARAM, SYNC_PARAM, PARAMS_LEN };
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { FREE_LIGHT, SYNC_LIGHT, STATUS_DISPLAY, LIGHTS_LEN };
	dsp::SchmittTrigger freeTrigger, syncTrigger;
	int instrumentId = 0;
	uint8_t selectedStage = 0;
	uint32_t selectionSeq = 0;
	uint32_t lastModeSeq = 0;
	bool sync = false;
	bool linked = false;

	SpaceTimeStageTime() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INSTRUMENT_PARAM, 0.f, 15.f, 0.f, "Instrument ID", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configButton(FREE_PARAM, "Set selected stage to free timing");
		configButton(SYNC_PARAM, "Set selected stage to clock division");
	}
	void process(const ProcessArgs&) override {
		instrumentId = spacetime::clampInstrumentId((int)std::round(params[INSTRUMENT_PARAM].getValue()));
		uint32_t sequence = 0;
		uint8_t stage = 0;
		bool currentSync = false;
		if (stageTimeRegistry.readSelection(instrumentId, sequence, stage, currentSync)) {
			selectedStage = stage;
			selectionSeq = sequence;
			sync = currentSync;
		}
		if (freeTrigger.process(params[FREE_PARAM].getValue())) {
			sync = false;
			stageTimeRegistry.publishMode(instrumentId, selectedStage, false);
		}
		if (syncTrigger.process(params[SYNC_PARAM].getValue())) {
			sync = true;
			stageTimeRegistry.publishMode(instrumentId, selectedStage, true);
		}
		lights[FREE_LIGHT].setBrightness(sync ? 0.f : 1.f);
		lights[SYNC_LIGHT].setBrightness(sync ? 1.f : 0.f);
		linked = stageTimeRegistry.readSelection(instrumentId, selectionSeq, selectedStage, sync);
		lights[STATUS_DISPLAY].setBrightness(linked ? 1.f : 0.f);
	}
	size_t get_display_text(int lightId, std::span<char> output) override {
		if (lightId != STATUS_DISPLAY || output.empty()) return 0;
		std::string value = linked ? "STAGE " + std::to_string((int)selectedStage + 1) : "WAITING";
		size_t length = std::min(output.size(), value.size());
		std::copy(value.begin(), value.begin() + length, output.begin());
		return length;
	}
};

struct SpaceTimeStageTimeWidget : ModuleWidget {
	SpaceTimeStageTimeWidget(SpaceTimeStageTime* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/StageTime.svg")));
		#if defined(METAMODULE)
		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(8.5f, 85.5f)));
		display->box.size = mm2px(Vec(33.8f, 23.f));
		display->firstLightId = SpaceTimeStageTime::STATUS_DISPLAY;
		display->font = "Default_10";
		display->color = Colors565::Black;
		addChild(display);
		#endif
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(25.4f, 25.f)), module, SpaceTimeStageTime::INSTRUMENT_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(15.3f, 55.f)), module, SpaceTimeStageTime::FREE_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(35.5f, 55.f)), module, SpaceTimeStageTime::SYNC_PARAM));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(15.3f, 52.3f)), module, SpaceTimeStageTime::FREE_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(35.5f, 52.3f)), module, SpaceTimeStageTime::SYNC_LIGHT));
	}
};
} // namespace

Model* modelSpaceTimeStageTime = createModel<SpaceTimeStageTime, SpaceTimeStageTimeWidget>("StageTime");
