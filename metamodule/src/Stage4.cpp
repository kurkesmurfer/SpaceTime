#include <rack.hpp>
#include <metamodule/VCVTextDisplay.hpp>

#include "TimingBus.hpp"
#include "RemoteBus.hpp"
#include "Chain.hpp"

#include <algorithm>
#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

namespace {

// New module (2026-08-09, Peet): "a stage does not have much data...
// Basically the metamodule variant of Program contains all stages as well,
// so Stage4 only visualises the stages and does not own the data." Unlike
// HeadRemote (which runs its own head DSP -- a head does real per-sample
// work), a stage is just two sliders and some flags read/written by
// Program, so this module owns nothing: it publishes its own 4 stages'
// voltage/time sliders to Program over MetaModuleStageBankRegistry (EB3,
// already built, unused until now) and reads Program's table back purely
// for its own display. Slug MUST match VCV's real Stage4 ("The metamodule
// parallel MUST have the same slug... mechanics are all different is
// irrelevant, as that does not show up in the yml") -- VCV's Stage4 owns
// its program words locally and is chained by panel adjacency; this module
// owns nothing and binds by Instrument ID + bank index instead, same
// platform-forced divergence as Head/HeadRemote's relationship.
//
// No local mirror/shadow state is needed here the way HeadRemote needs one:
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
		PARAMS_LEN
	};
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId {
		LINK_LIGHT,
		STATUS_DISPLAY,
		LIGHTS_LEN
	};

	enum class CoreLink { Waiting, Linked, Duplicate };

	dsp::ClockDivider controlDivider;
	spacetime::StageTable table;
	spacetime::ExtInputs ext;
	spacetime::Globals globals;
	spacetime::ScaleKey scaleKey;

	uint32_t busToken = stageBankRegistry.makeToken();
	int instrumentId = 0;
	int bankIndex = 0;
	bool ownsBank = false;

	uint32_t lastTableHeartbeat = 0;
	float staleTime = 1.f;
	CoreLink coreLink = CoreLink::Waiting;
	bool bankConflict = false;

	SpaceTimeStage4() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INSTRUMENT_PARAM, 0.f, 3.f, 0.f, "Instrument ID", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(BANK_PARAM, 0.f, 15.f, 0.f, "Bank index", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		for (int s = 0; s < 4; s++) {
			configParam(VOLTAGE_PARAMS + s, 0.f, 10.f, 0.f, string::f("Stage %d voltage", s + 1), " V");
			configParam(TIME_PARAMS + s, 0.f, 1.f, 0.5f, string::f("Stage %d time (within range)", s + 1));
		}
		configLight(LINK_LIGHT, "Linked to Program, no conflicts");
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
	}

	void processControl(float dt) {
		int nextInstrument = clamp((int)std::round(params[INSTRUMENT_PARAM].getValue()), 0, 3);
		int nextBank = clamp((int)std::round(params[BANK_PARAM].getValue()), 0, 15);
		if (nextInstrument != instrumentId || nextBank != bankIndex)
			setBinding(nextInstrument, nextBank);

		bool coreValid = timingBusRegistry.bus(instrumentId).coreCount.load(std::memory_order_acquire) == 1;
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

		// Read-only reference: Program's own table for these 4 stages, for
		// this module's display only -- never republished, never arbitrated
		// (see the struct-level comment for why Program is sole authority).
		stageTableRegistry.readTable(instrumentId, coreValid, table);
		stageTableRegistry.readContext(instrumentId, coreValid, ext, globals, scaleKey);

		spacetime::BlockSegment segment;
		for (int s = 0; s < 4; s++) {
			segment.voltage[s] = params[VOLTAGE_PARAMS + s].getValue();
			segment.time[s] = params[TIME_PARAMS + s].getValue();
			int stage = bankIndex * spacetime::kStagesPerBlock + s;
			segment.program[s] = stage < table.count ? table.program[stage] : spacetime::ProgramWord();
		}
		if (!ownsBank)
			ownsBank = stageBankRegistry.tryClaimBank(instrumentId, bankIndex, busToken);
		if (ownsBank && stageBankRegistry.bankLinkCount(instrumentId, bankIndex) == 1)
			stageBankRegistry.publishBank(instrumentId, bankIndex, segment);
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
	}

	size_t get_display_text(int lightId, std::span<char> text) override {
		if (lightId != STATUS_DISPLAY || text.empty())
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
};

struct SpaceTimeStage4Widget : ModuleWidget {
	SpaceTimeStage4Widget(SpaceTimeStage4* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Stage4.svg")));

		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(4.f, 12.f)));
		display->box.size = mm2px(Vec(53.f, 20.f));
		display->firstLightId = SpaceTimeStage4::STATUS_DISPLAY;
		display->font = "Default_10";
		display->color = Colors565::White;
		addChild(display);

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(15.f, 42.f)), module, SpaceTimeStage4::INSTRUMENT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(45.f, 42.f)), module, SpaceTimeStage4::BANK_PARAM));

		// VCVSlider is about 20 mm tall in the SDK, so both rows fit comfortably
		// on a standard 128.5 mm panel. The former 180 mm panel treated the row
		// pitch as the widget height and produced an invalid MetaModule asset.
		static const float colX[4] = {10.f, 23.6f, 37.2f, 50.8f};
		for (int s = 0; s < 4; s++) {
			addParam(createParamCentered<VCVSlider>(mm2px(Vec(colX[s], 66.f)), module, SpaceTimeStage4::VOLTAGE_PARAMS + s));
			addParam(createParamCentered<VCVSlider>(mm2px(Vec(colX[s], 103.f)), module, SpaceTimeStage4::TIME_PARAMS + s));
		}

		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(56.f, 6.f)), module, SpaceTimeStage4::LINK_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		SpaceTimeStage4* module = getModule<SpaceTimeStage4>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		static const char* instrumentLabels[] = {"A", "B", "C", "D"};
		menu->addChild(createSubmenuItem("Instrument ID", [=]() {
			return std::string(instrumentLabels[module->instrumentId]) +
				(module->bankConflict ? " (duplicate bank!)" : "");
		}, [=](Menu* ids) {
			for (int id = 0; id < 4; id++)
				ids->addChild(createCheckMenuItem(instrumentLabels[id], "",
					[=]() { return module->instrumentId == id; },
					[=]() {
						module->params[SpaceTimeStage4::INSTRUMENT_PARAM].setValue((float)id);
						module->setBinding(id, module->bankIndex);
					}));
		}));
		menu->addChild(createSubmenuItem("Bank index", [=]() {
			return string::f("%d (stages %d-%d)", module->bankIndex + 1,
				module->bankIndex * 4 + 1, module->bankIndex * 4 + 4);
		}, [=](Menu* banks) {
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

Model* modelSpaceTimeStage4 = createModel<SpaceTimeStage4, SpaceTimeStage4Widget>("Stage4");
