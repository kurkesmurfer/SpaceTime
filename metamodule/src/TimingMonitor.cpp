#include <rack.hpp>
#include <metamodule/VCVTextDisplay.hpp>

#include "Chain.hpp"
#include "TimingBus.hpp"

#include <algorithm>
#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

namespace {

enum class MonitorLink { Waiting, Linked, Duplicate };

struct SpaceTimeTimingMonitor : Module {
	enum ParamId {
		INSTRUMENT_PARAM,
		HEAD_PARAM,
		RESET_PARAM,
		PARAMS_LEN
	};
	enum InputId { INPUTS_LEN };
	enum OutputId {
		CLOCK_OUTPUT,
		STEP_OUTPUT,
		DIFF_OUTPUT,
		STATUS_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(HEAD_LIGHTS, 16),
		STATUS_DISPLAY,
		LIGHTS_LEN
	};

	dsp::ClockDivider controlDivider;
	dsp::SchmittTrigger resetTrigger;
	spacetime::TimingSnapshot snapshot;
	uint32_t baseSource[spacetime::kMaxHeads] = {};
	uint32_t baseStep[spacetime::kMaxHeads] = {};
	uint32_t lastSource[spacetime::kMaxHeads] = {};
	uint32_t lastStep[spacetime::kMaxHeads] = {};
	bool errorLatched[spacetime::kMaxHeads] = {};
	bool wasValid[spacetime::kMaxHeads] = {};
	int monitorId = 0;
	int selectedHead = 0;
	int previousSelectedHead = 0;
	uint32_t lastHeartbeat = 0;
	float staleTime = 1.f;
	float clockPulse = 0.f;
	float stepPulse = 0.f;
	MonitorLink link = MonitorLink::Waiting;

	SpaceTimeTimingMonitor() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INSTRUMENT_PARAM, 0.f, 3.f, 0.f, "Instrument ID", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(HEAD_PARAM, 0.f, 7.f, 0.f, "Selected head", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configButton(RESET_PARAM, "Reset timing baseline");
		configOutput(CLOCK_OUTPUT, "Selected head source clock event");
		configOutput(STEP_OUTPUT, "Selected head stage entry");
		configOutput(DIFF_OUTPUT, "Selected head step minus clock count");
		configOutput(STATUS_OUTPUT, "Link status");
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			configLight(HEAD_LIGHTS + 2 * h, string::f("Head %d timing match", h + 1));
			configLight(HEAD_LIGHTS + 2 * h + 1, string::f("Head %d timing error", h + 1));
		}
		controlDivider.setDivision(16);
		timingBusRegistry.registerMonitor(monitorId);
	}

	~SpaceTimeTimingMonitor() override {
		timingBusRegistry.unregisterMonitor(monitorId);
	}

	void process(const ProcessArgs& args) override {
		clockPulse = std::fmax(0.f, clockPulse - args.sampleTime);
		stepPulse = std::fmax(0.f, stepPulse - args.sampleTime);
		if (controlDivider.process())
			processControl(args.sampleTime * controlDivider.getDivision());

		outputs[CLOCK_OUTPUT].setVoltage(clockPulse > 0.f ? 10.f : 0.f);
		outputs[STEP_OUTPUT].setVoltage(stepPulse > 0.f ? 10.f : 0.f);
		outputs[DIFF_OUTPUT].setVoltage(clamp((float)difference(selectedHead), -10.f, 10.f));
		outputs[STATUS_OUTPUT].setVoltage(link == MonitorLink::Linked ? 5.f :
			link == MonitorLink::Duplicate ? -5.f : 0.f);
	}

	void processControl(float dt) {
		int nextId = clamp((int)std::round(params[INSTRUMENT_PARAM].getValue()), 0, 3);
		if (nextId != monitorId)
			setMonitorId(nextId);
		selectedHead = clamp((int)std::round(params[HEAD_PARAM].getValue()), 0, 7);

		spacetime::MetaModuleTimingBus& bus = timingBusRegistry.bus(monitorId);
		uint32_t coreCount = bus.coreCount.load(std::memory_order_acquire);
		uint32_t heartbeat = bus.telemetry.heartbeat();
		if (heartbeat != lastHeartbeat) {
			lastHeartbeat = heartbeat;
			staleTime = 0.f;
		} else {
			staleTime += dt;
		}
		MonitorLink previousLink = link;
		link = coreCount > 1 ? MonitorLink::Duplicate :
			(coreCount == 1 && staleTime < 0.25f ? MonitorLink::Linked : MonitorLink::Waiting);
		if (link == MonitorLink::Linked)
			bus.telemetry.read(snapshot);
		if (link == MonitorLink::Linked && previousLink != MonitorLink::Linked)
			resetBaseline();

		if (resetTrigger.process(params[RESET_PARAM].getValue()))
			resetBaseline();

		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			bool valid = validationActive(h);
			if (valid && !wasValid[h]) {
				baseSource[h] = snapshot.sourceEvents[h];
				baseStep[h] = snapshot.stageEntries[h];
				errorLatched[h] = false;
			}
			wasValid[h] = valid;
			if (valid && relativeSource(h) > 0 && difference(h) != 0)
				errorLatched[h] = true;
			float green = valid && relativeSource(h) > 0 && !errorLatched[h] ? 1.f : 0.f;
			float red = errorLatched[h] ? 1.f : 0.f;
			lights[HEAD_LIGHTS + 2 * h].setBrightness(green);
			lights[HEAD_LIGHTS + 2 * h + 1].setBrightness(red);
		}

		if (selectedHead != previousSelectedHead) {
			lastSource[selectedHead] = snapshot.sourceEvents[selectedHead];
			lastStep[selectedHead] = snapshot.stageEntries[selectedHead];
			previousSelectedHead = selectedHead;
		}
		if (snapshot.sourceEvents[selectedHead] != lastSource[selectedHead]) {
			lastSource[selectedHead] = snapshot.sourceEvents[selectedHead];
			clockPulse = 1e-3f;
		}
		if (snapshot.stageEntries[selectedHead] != lastStep[selectedHead]) {
			lastStep[selectedHead] = snapshot.stageEntries[selectedHead];
			stepPulse = 1e-3f;
		}
	}

	size_t get_display_text(int lightId, std::span<char> text) override {
		if (lightId != STATUS_DISPLAY || text.empty())
			return 0;
		const char* linkName = link == MonitorLink::Linked ? "LINK" :
			(link == MonitorLink::Duplicate ? "DUP" : "WAIT");
		const char* sourceNames[] = {"INTERNAL", "EXT CV", "MIDI", "VIRTUAL"};
		const char* divisionNames[] = {"/16", "/8", "/4", "/2", "x1", "x2", "x4", "x8", "x16"};
		int source = clamp((int)snapshot.clockSource[selectedHead], 0, 3);
		int division = clamp((int)snapshot.clockDivision[selectedHead], 0, 8);
		const char* result = errorLatched[selectedHead] ? "ERROR" :
			(validationActive(selectedHead) && relativeSource(selectedHead) > 0 ? "PASS" : "READY");
		char buffer[128];
		int length = std::snprintf(buffer, sizeof(buffer),
			"%c %s H%d S%02d\n%s %s %s\nCLK %08lu\nSTP %08lu\nD%+ld %s",
			(char)('A' + monitorId), linkName, selectedHead + 1,
			(int)snapshot.stage[selectedHead] + 1,
			sourceNames[source], divisionNames[division],
			snapshot.runState[selectedHead] == spacetime::RUN_STOPPED ? "STOP" :
				(snapshot.runState[selectedHead] == spacetime::RUN_HOLDING ? "HOLD" : "RUN"),
			(unsigned long)relativeSource(selectedHead),
			(unsigned long)relativeStep(selectedHead),
			(long)difference(selectedHead), result);
		if (length < 0)
			return 0;
		size_t copyLength = std::min(text.size(), (size_t)length);
		std::copy(buffer, buffer + copyLength, text.begin());
		return copyLength;
	}

	void setMonitorId(int next) {
		timingBusRegistry.unregisterMonitor(monitorId);
		monitorId = clamp(next, 0, 3);
		timingBusRegistry.registerMonitor(monitorId);
		lastHeartbeat = 0;
		staleTime = 1.f;
		link = MonitorLink::Waiting;
		resetBaseline();
	}

	void resetBaseline() {
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			baseSource[h] = snapshot.sourceEvents[h];
			baseStep[h] = snapshot.stageEntries[h];
			lastSource[h] = snapshot.sourceEvents[h];
			lastStep[h] = snapshot.stageEntries[h];
			errorLatched[h] = false;
			wasValid[h] = validationActive(h);
		}
		clockPulse = 0.f;
		stepPulse = 0.f;
	}

	bool validationActive(int head) const {
		return link == MonitorLink::Linked &&
			snapshot.runState[head] != spacetime::RUN_STOPPED &&
			(snapshot.clockSource[head] == 2 || snapshot.clockSource[head] == 3) &&
			snapshot.clockDivision[head] == 4;
	}

	uint32_t relativeSource(int head) const {
		return snapshot.sourceEvents[head] - baseSource[head];
	}

	uint32_t relativeStep(int head) const {
		return snapshot.stageEntries[head] - baseStep[head];
	}

	int32_t difference(int head) const {
		return (int32_t)(relativeStep(head) - relativeSource(head));
	}
};

struct SpaceTimeTimingMonitorWidget : ModuleWidget {
	SpaceTimeTimingMonitorWidget(SpaceTimeTimingMonitor* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TimingMonitor.svg")));

		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(4.f, 12.f)));
		display->box.size = mm2px(Vec(53.f, 33.f));
		display->firstLightId = SpaceTimeTimingMonitor::STATUS_DISPLAY;
		display->font = "Default_12";
		display->color = Colors565::White;
		addChild(display);

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(10.f, 57.f)), module,
			SpaceTimeTimingMonitor::INSTRUMENT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(30.5f, 57.f)), module,
			SpaceTimeTimingMonitor::HEAD_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(51.f, 57.f)), module,
			SpaceTimeTimingMonitor::RESET_PARAM));

		const float lightX[4] = {9.f, 23.f, 38.f, 52.f};
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			float y = h < 4 ? 76.f : 88.f;
			addChild(createLightCentered<MediumLight<GreenRedLight>>(mm2px(Vec(lightX[h % 4], y)),
				module, SpaceTimeTimingMonitor::HEAD_LIGHTS + 2 * h));
		}

		const float outputX[4] = {9.f, 23.f, 38.f, 52.f};
		for (int output = 0; output < 4; output++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(outputX[output], 113.f)), module,
				SpaceTimeTimingMonitor::CLOCK_OUTPUT + output));
	}
};

} // namespace

Model* modelSpaceTimeTimingMonitor =
	createModel<SpaceTimeTimingMonitor, SpaceTimeTimingMonitorWidget>("TimingMonitor");
