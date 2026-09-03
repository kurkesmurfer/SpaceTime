#include "plugin.hpp"
#include "paneltheme.hpp"

#include "MMBus.hpp"
#include "MMModuleContracts.hpp"
#include "Chain.hpp"
#include "StageAnnotation.hpp"
#include "spacetime_widgets.hpp"

#include <algorithm>
#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

namespace {

// Portable MMStage4 viewer/editor. Program owns the table; this surface binds
// by Instrument ID and bank, publishes slider edits, and reads the authoritative
// table/context for annotations. The MetaModule twin uses the same contract and bus.
//
// No local mirror/shadow state is needed here the way MMHead needs one:
// there is exactly one legitimate writer to these two sliders on this
// module's own side (the physical/virtual knob itself), so there's nothing
// to arbitrate locally. The arbitration against Program's other writer
// (MIDI slider CC) happens on Program's side -- see Program.cpp's
// syncStageBanks().
struct SpaceTimeStage4 : Module {
	enum ParamId {
		INSTRUMENT_PARAM,
		BANK_PARAM,
		ENUMS(VOLTAGE_PARAMS, 4),
		ENUMS(TIME_PARAMS, 4),
		FOCUS_PARAM,
		PARAMS_LEN
	};
	static_assert((int)INSTRUMENT_PARAM == (int)spacetime::MMStage4Contract::INSTRUMENT_PARAM, "MMStage4 ParamId drift");
	static_assert((int)VOLTAGE_PARAMS == (int)spacetime::MMStage4Contract::VOLTAGE_PARAMS, "MMStage4 slider ParamId drift");
	static_assert((int)FOCUS_PARAM == (int)spacetime::MMStage4Contract::FOCUS_PARAM && (int)PARAMS_LEN == (int)spacetime::MMStage4Contract::PARAMS_LEN, "MMStage4 ParamId count drift");
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId {
		LINK_LIGHT,
		STATUS_DISPLAY,
		ENUMS(PITCH_DISPLAYS, 4),
		ENUMS(TIME_DISPLAYS, 4),
		ENUMS(EDIT_LIGHTS, 4),
		ENUMS(HEAD_LIGHTS, 4 * 8 * 3),
		LIGHTS_LEN
	};

	enum class CoreLink { Waiting, Linked, Duplicate };

	dsp::ClockDivider controlDivider;
	spacetime::StageTable table;
	spacetime::ExtInputs ext;
	spacetime::Globals globals;
	spacetime::ScaleKey scaleKey;
	spacetime::TimingSnapshot timing;
	bool timingValid = false;

	uint32_t busToken = stageBankRegistry.makeToken();
	int instrumentId = 0;
	int bankIndex = 0;
	bool ownsBank = false;
	int selectedStage = -1;
	bool reflectionReady = false;
	float lastParamVoltage[4] = {};
	float lastParamTime[4] = {};
	float lastTableVoltage[4] = {};
	float lastTableTime[4] = {};
	int lastFocusParam = 0;
	uint32_t focusSequence = 0;

	uint32_t lastTableHeartbeat = 0;
	float staleTime = 1.f;
	CoreLink coreLink = CoreLink::Waiting;
	bool bankConflict = false;

	SpaceTimeStage4() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INSTRUMENT_PARAM, 0.f, 3.f, 0.f, "Instrument ID", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(BANK_PARAM, 0.f, 15.f, 0.f, "Bank index", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(FOCUS_PARAM, 0.f, 3.f, 0.f, "Focused stage", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		for (int s = 0; s < 4; s++) {
			configParam(VOLTAGE_PARAMS + s, 0.f, 10.f, 0.f, string::f("Stage %d voltage", s + 1), " V");
			configParam(TIME_PARAMS + s, 0.f, 1.f, 0.5f, string::f("Stage %d time (within range)", s + 1));
		}
		configLight(LINK_LIGHT, "Linked to Program, no conflicts");
		for (int s = 0; s < 4; s++) {
			configLight(EDIT_LIGHTS + s, string::f("Stage %d edit select", s + 1));
			for (int h = 0; h < 8; h++)
				configLight(HEAD_LIGHTS + (s * 8 + h) * 3,
					string::f("Stage %d head %d position", s + 1, h + 1));
		}
		controlDivider.setDivision(16);
		ownsBank = stageBankRegistry.registerBank(instrumentId, bankIndex, busToken);
	}

	~SpaceTimeStage4() override {
		stageBankRegistry.unregisterBank(instrumentId, bankIndex, busToken);
	}

	void process(const ProcessArgs& args) override {
		if (controlDivider.process())
			processControl(args.sampleTime * controlDivider.getDivision());
		lights[LINK_LIGHT].setBrightnessSmooth(
			coreLink == CoreLink::Linked && !bankConflict ? 1.f : 0.f, args.sampleTime);
		for (int s = 0; s < 4; s++) {
			int stage = bankIndex * spacetime::kStagesPerBlock + s;
			lights[EDIT_LIGHTS + s].setBrightnessSmooth(
				selectedStage == stage ? 1.f : 0.f, args.sampleTime);
			for (int h = 0; h < spacetime::kMaxHeads; h++) {
				float brightness = timingValid && timing.stage[h] == stage
					? (timing.runState[h] == spacetime::RUN_STOPPED ? 0.35f : 1.f) : 0.f;
				spacetime::setHeadDot(this, HEAD_LIGHTS + (s * 8 + h) * 3,
					h, brightness, args.sampleTime);
			}
		}
	}

	void processControl(float dt) {
		int nextInstrument = clamp((int)std::round(params[INSTRUMENT_PARAM].getValue()), 0, 3);
		int nextBank = clamp((int)std::round(params[BANK_PARAM].getValue()), 0, 15);
		if (nextInstrument != instrumentId || nextBank != bankIndex)
			setBinding(nextInstrument, nextBank);

		bool coreValid = timingBusRegistry.bus(instrumentId).coreCount.load(std::memory_order_acquire) == 1;
		timingValid = coreValid && timingBusRegistry.bus(instrumentId).telemetry.read(timing);
		uint32_t heartbeat = stageTableRegistry.tableHeartbeat(instrumentId);
		if (heartbeat != lastTableHeartbeat) {
			lastTableHeartbeat = heartbeat;
			staleTime = 0.f;
		} else {
			staleTime += dt;
		}
		uint32_t coreCount = timingBusRegistry.bus(instrumentId).coreCount.load(std::memory_order_acquire);
		coreLink = coreCount > 1 ? CoreLink::Duplicate :
			(coreCount == 1 && staleTime < 0.25f ? CoreLink::Linked : CoreLink::Waiting);
		bankConflict = stageBankRegistry.bankLinkCount(instrumentId, bankIndex) > 1;

		bool tableValid = stageTableRegistry.readTable(instrumentId, coreValid, table);
		uint8_t selected = 0;
		bool contextValid = stageTableRegistry.readContext(
			instrumentId, coreValid, ext, globals, scaleKey, &selected);
		int focus = clamp((int)std::round(params[FOCUS_PARAM].getValue()), 0, 3);
		bool focusEdited = reflectionReady && focus != lastFocusParam;

		if (tableValid) {
			for (int s = 0; s < 4; s++) {
				int stage = bankIndex * spacetime::kStagesPerBlock + s;
				float authoritativeV = table.voltage[stage];
				float authoritativeT = table.time[stage];
				float currentV = params[VOLTAGE_PARAMS + s].getValue();
				float currentT = params[TIME_PARAMS + s].getValue();
				bool tableChanged = !reflectionReady ||
					authoritativeV != lastTableVoltage[s] ||
					authoritativeT != lastTableTime[s];
				if (tableChanged) {
					params[VOLTAGE_PARAMS + s].setValue(authoritativeV);
					params[TIME_PARAMS + s].setValue(authoritativeT);
				} else if (currentV != lastParamVoltage[s] || currentT != lastParamTime[s]) {
					focus = s;
					params[FOCUS_PARAM].setValue((float)s);
					focusEdited = true;
				}
				lastTableVoltage[s] = authoritativeV;
				lastTableTime[s] = authoritativeT;
				lastParamVoltage[s] = params[VOLTAGE_PARAMS + s].getValue();
				lastParamTime[s] = params[TIME_PARAMS + s].getValue();
			}
			reflectionReady = true;
		}

		if (focusEdited) {
			selectedStage = bankIndex * spacetime::kStagesPerBlock + focus;
			focusSequence++;
		} else if (contextValid) {
			selectedStage = clamp((int)selected, 0, spacetime::kMaxStages - 1);
			int localStage = selectedStage - bankIndex * spacetime::kStagesPerBlock;
			if (localStage >= 0 && localStage < spacetime::kStagesPerBlock) {
				params[FOCUS_PARAM].setValue((float)localStage);
				focus = localStage;
			}
		} else {
			selectedStage = -1;
		}
		lastFocusParam = focus;

		spacetime::BlockSegment segment;
		for (int s = 0; s < 4; s++) {
			segment.voltage[s] = params[VOLTAGE_PARAMS + s].getValue();
			segment.time[s] = params[TIME_PARAMS + s].getValue();
			int stage = bankIndex * spacetime::kStagesPerBlock + s;
			segment.program[s] = stage < table.count ? table.program[stage] : spacetime::ProgramWord();
		}
		if (!ownsBank)
			ownsBank = stageBankRegistry.tryClaimBank(instrumentId, bankIndex, busToken);
		bool activeBank = bankIndex * spacetime::kStagesPerBlock < table.count;
		if (activeBank && ownsBank && stageBankRegistry.bankLinkCount(instrumentId, bankIndex) == 1)
			stageBankRegistry.publishBank(instrumentId, bankIndex, segment,
				(uint8_t)(bankIndex * spacetime::kStagesPerBlock + focus), focusSequence);
	}

	void setBinding(int nextInstrument, int nextBank) {
		stageBankRegistry.unregisterBank(instrumentId, bankIndex, busToken);
		instrumentId = nextInstrument;
		bankIndex = nextBank;
		ownsBank = stageBankRegistry.registerBank(instrumentId, bankIndex, busToken);
		table = spacetime::StageTable();
		lastTableHeartbeat = 0;
		staleTime = 1.f;
		coreLink = CoreLink::Waiting;
		selectedStage = -1;
		reflectionReady = false;
	}

	#if 0
	size_t get_display_text(int lightId, std::span<char> text) {
		if (text.empty())
			return 0;
		if (lightId >= PITCH_DISPLAYS && lightId < PITCH_DISPLAYS + 4)
			return copyAnnotationText(lightId - PITCH_DISPLAYS, true, text);
		if (lightId >= TIME_DISPLAYS && lightId < TIME_DISPLAYS + 4)
			return copyAnnotationText(lightId - TIME_DISPLAYS, false, text);
		if (lightId != STATUS_DISPLAY)
			return 0;
		const char* linkName = coreLink == CoreLink::Linked ? "LINK" :
			(coreLink == CoreLink::Duplicate ? "DUP" : "WAIT");
		char buffer[128];
		int length = std::snprintf(buffer, sizeof(buffer),
			"ID %c BANK %02d%s\n%s STAGES %d-%d\nV %.2f %.2f %.2f %.2f",
			(char)('A' + instrumentId), bankIndex + 1, bankConflict ? "!" : "",
			linkName, bankIndex * 4 + 1, bankIndex * 4 + 4,
			params[VOLTAGE_PARAMS + 0].getValue(), params[VOLTAGE_PARAMS + 1].getValue(),
			params[VOLTAGE_PARAMS + 2].getValue(), params[VOLTAGE_PARAMS + 3].getValue());
		if (length < 0)
			return 0;
		size_t copyLength = std::min(text.size(), (size_t)length);
		std::copy(buffer, buffer + copyLength, text.begin());
		return copyLength;
	}
	#endif

	#if 0
	size_t copyAnnotationText(int localStage, bool pitch, std::span<char> text) {
		int stage = bankIndex * spacetime::kStagesPerBlock + localStage;
		std::string value = "--";
		if (coreLink == CoreLink::Linked && stage < table.count) {
			spacetime::StageAnnotation annotation = spacetime::makeStageAnnotation(
				table.voltage[stage], table.time[stage], table.program[stage], scaleKey, true);
			if (pitch) {
				value = annotation.pitch == "EXT CV" ? "EXT" : annotation.pitch;
				if (!annotation.cents.empty())
					value += "\n" + annotation.cents;
			}
			else {
				value = annotation.duration == "EXT TIME" ? "EXT" : annotation.duration;
				value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
			}
		}
		size_t copyLength = std::min(text.size(), value.size());
		std::copy(value.begin(), value.begin() + copyLength, text.begin());
		return copyLength;
	}
	#endif
};

struct SpaceTimeStage4Widget : ModuleWidget {
	SpaceTimeStage4Widget(SpaceTimeStage4* module) {
		setModule(module);
		setPanel(spacetime::createThemedPanel(
			asset::plugin(pluginInstance, "res/MMStage4-light.svg"),
			asset::plugin(pluginInstance, "res/MMStage4.svg")));

		spacetime::addHeaderMark(this, 60.96f, 22.84f);
		spacetime::addHeaderLockup(this, 60.96f, "Stage4", 22.84f);
		spacetime::addSubtitle(this, 30.48f, 10.2f, "STAGE BLOCK");
		spacetime::addMMStatusReadout(this, Vec(4.f, 12.f), Vec(53.f, 20.f),
			"ID A BANK 01\nWAIT STAGES 1-4\nV 0.00 0.00 0.00 0.00",
			[module]() {
				if (!module)
					return std::string();
				const char* linkName = module->coreLink == SpaceTimeStage4::CoreLink::Linked ? "LINK" :
					(module->coreLink == SpaceTimeStage4::CoreLink::Duplicate ? "DUP" : "WAIT");
				char text[160];
				bool inBank = module->selectedStage >= module->bankIndex * spacetime::kStagesPerBlock &&
					module->selectedStage < (module->bankIndex + 1) * spacetime::kStagesPerBlock;
				std::snprintf(text, sizeof(text),
					"ID %c BANK %02d%s\n%s %02d-%02d SEL %02d%s\nV %.2f %.2f %.2f %.2f",
					(char)('A' + module->instrumentId), module->bankIndex + 1,
					module->bankConflict ? "!" : "", linkName,
					module->bankIndex * 4 + 1, module->bankIndex * 4 + 4,
					module->selectedStage < 0 ? 0 : module->selectedStage + 1,
					inBank ? "" : " OUT",
					module->params[SpaceTimeStage4::VOLTAGE_PARAMS + 0].getValue(),
					module->params[SpaceTimeStage4::VOLTAGE_PARAMS + 1].getValue(),
					module->params[SpaceTimeStage4::VOLTAGE_PARAMS + 2].getValue(),
					module->params[SpaceTimeStage4::VOLTAGE_PARAMS + 3].getValue());
				return std::string(text);
			});
		spacetime::addKnobLabel(this, 15.f, 35.5f, "INSTRUMENT ID");
		spacetime::addKnobLabel(this, 30.f, 35.5f, "FOCUS");
		spacetime::addKnobLabel(this, 45.f, 35.5f, "BANK");
		spacetime::addMicroLabel(this, 56.f, 35.5f, "LNK");
		spacetime::addSectionHeading(this, 30.48f, 52.2f, "VOLTAGE");
		spacetime::addMicroLabel(this, 30.48f, 77.5f, "EDIT / HEADS");
		spacetime::addSectionHeading(this, 30.48f, 90.8f, "TIME");
		static const float stageX[4] = {10.f, 23.6f, 37.2f, 50.8f};
		for (int i = 0; i < 4; i++)
			spacetime::addKnobLabel(this, stageX[i], 121.f, string::f("%d", i + 1));

		#if 0
		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(4.f, 12.f)));
		display->box.size = mm2px(Vec(53.f, 20.f));
		display->firstLightId = SpaceTimeStage4::STATUS_DISPLAY;
		display->font = "Default_10";
		display->color = Colors565::White;
		addChild(display);
		#endif

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(15.f, 42.f)), module, SpaceTimeStage4::INSTRUMENT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(45.f, 42.f)), module, SpaceTimeStage4::BANK_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(30.f, 42.f)), module, SpaceTimeStage4::FOCUS_PARAM));

		// VCVSlider is about 20 mm tall in the SDK, so both rows fit comfortably
		// on a standard 128.5 mm panel. The former 180 mm panel treated the row
		// pitch as the widget height and produced an invalid MetaModule asset.
		static const float colX[4] = {10.f, 23.6f, 37.2f, 50.8f};
		for (int s = 0; s < 4; s++) {
			addParam(createParamCentered<VCVSlider>(mm2px(Vec(colX[s], 66.f)), module, SpaceTimeStage4::VOLTAGE_PARAMS + s));
			#if 0
			auto pitch = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(colX[s] - 6.2f, 77.f)));
			pitch->box.size = mm2px(Vec(12.4f, 10.f));
			pitch->firstLightId = SpaceTimeStage4::PITCH_DISPLAYS + s;
			pitch->font = "Default_10";
			pitch->color = Colors565::Black;
			addChild(pitch);

			auto time = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(colX[s] - 6.2f, 92.f)));
			time->box.size = mm2px(Vec(12.4f, 6.f));
			time->firstLightId = SpaceTimeStage4::TIME_DISPLAYS + s;
			time->font = "Default_10";
			time->color = Colors565::Black;
			addChild(time);
			#endif

			addParam(createParamCentered<VCVSlider>(mm2px(Vec(colX[s], 107.5f)), module, SpaceTimeStage4::TIME_PARAMS + s));
			spacetime::addStageLedCluster(this, module, colX[s], 82.f,
				SpaceTimeStage4::EDIT_LIGHTS + s,
				SpaceTimeStage4::HEAD_LIGHTS + s * 8 * 3);
		}

		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(56.f, 42.f)), module, SpaceTimeStage4::LINK_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator);
		spacetime::appendPanelThemeMenu(menu);
		SpaceTimeStage4* module = getModule<SpaceTimeStage4>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		static const char* instrumentLabels[] = {"A", "B", "C", "D"};
		menu->addChild(createSubmenuItem("Instrument ID",
			std::string(instrumentLabels[module->instrumentId]) +
				(module->bankConflict ? " (duplicate bank!)" : ""), [=](Menu* ids) {
			for (int id = 0; id < 4; id++)
				ids->addChild(createCheckMenuItem(instrumentLabels[id], "",
					[=]() { return module->instrumentId == id; },
					[=]() {
						module->params[SpaceTimeStage4::INSTRUMENT_PARAM].setValue((float)id);
						module->setBinding(id, module->bankIndex);
					}));
		}));
		menu->addChild(createSubmenuItem("Bank index",
			string::f("%d (stages %d-%d)", module->bankIndex + 1,
				module->bankIndex * 4 + 1, module->bankIndex * 4 + 4), [=](Menu* banks) {
			for (int b = 0; b < 16; b++)
				banks->addChild(createCheckMenuItem(string::f("%d (stages %d-%d)", b + 1, b * 4 + 1, b * 4 + 4), "",
					[=]() { return module->bankIndex == b; },
					[=]() {
						module->params[SpaceTimeStage4::BANK_PARAM].setValue((float)b);
						module->setBinding(module->instrumentId, b);
					}));
		}));
	}
};

} // namespace

Model* modelMMStage4 = createModel<SpaceTimeStage4, SpaceTimeStage4Widget>(spacetime::MMStage4Contract::slug);
