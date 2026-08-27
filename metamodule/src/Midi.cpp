#include <rack.hpp>
#include <metamodule/VCVTextDisplay.hpp>

#include "TimingBus.hpp"
#include "RemoteBus.hpp"
#include "MidiCore.hpp"

#include <algorithm>
#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

namespace {

// Renamed from MidiMonitor/slug "MidiMonitor" (2026-08-09, Peet): the
// metamodule parallel MUST have the same slug as its VCV role-equivalent --
// "that the mechanics are all different is irrelevant, as that does not
// show up in the yml." So this is now slug "Midi", matching VCV's real
// Midi.cpp, deliberately reversing the collision-avoidance naming this file
// started with. Capability is unchanged: a read-only monitor, no MIDI jacks
// of its own, bound to an Instrument ID like TimingMonitor.cpp, reading
// MetaModuleMidiStatusRegistry (dsp/MetaModuleRemoteBus.hpp) for whatever
// Program (formerly Core) already tracks -- VCV's real Midi.cpp owns device
// queues, DROID feedback, and MIDI-out lanes; this module owns none of
// that. Same LINK/WAIT/DUP convention as TimingMonitor.cpp and the same
// STATUS_OUTPUT +5/0/-5 V idiom.
struct SpaceTimeMidi : Module {
	enum ParamId {
		INSTRUMENT_PARAM,
		PARAMS_LEN
	};
	enum InputId { INPUTS_LEN };
	enum OutputId {
		STATUS_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		IN_LIGHT,
		CLK_LIGHT,
		OUT_LIGHT,
		STATUS_DISPLAY,
		LIGHTS_LEN
	};

	enum class MonitorLink { Waiting, Linked, Duplicate };

	dsp::ClockDivider controlDivider;
	spacetime::MetaModuleMidiStatusSnapshot snapshot;
	int instrumentId = 0;
	uint32_t lastHeartbeat = 0;
	float staleTime = 1.f;
	MonitorLink link = MonitorLink::Waiting;

	uint32_t lastInSeq = 0, lastClkSeq = 0, lastOutSeq = 0;
	float inPulse = 0.f, clkPulse = 0.f, outPulse = 0.f;

	SpaceTimeMidi() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INSTRUMENT_PARAM, 0.f, 3.f, 0.f, "Instrument ID", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configOutput(STATUS_OUTPUT, "Link status");
		configLight(IN_LIGHT, "MIDI input activity");
		configLight(CLK_LIGHT, "MIDI clock activity");
		configLight(OUT_LIGHT, "MIDI output activity");
		controlDivider.setDivision(16);
		timingBusRegistry.registerMonitor(instrumentId);
	}

	~SpaceTimeMidi() override {
		timingBusRegistry.unregisterMonitor(instrumentId);
	}

	void process(const ProcessArgs& args) override {
		if (controlDivider.process())
			processControl(args.sampleTime * controlDivider.getDivision());

		inPulse = std::fmax(0.f, inPulse - args.sampleTime * 8.f);
		clkPulse = std::fmax(0.f, clkPulse - args.sampleTime * 10.f);
		outPulse = std::fmax(0.f, outPulse - args.sampleTime * 4.f);
		lights[IN_LIGHT].setBrightnessSmooth(inPulse, args.sampleTime);
		lights[CLK_LIGHT].setBrightnessSmooth(clkPulse, args.sampleTime);
		lights[OUT_LIGHT].setBrightnessSmooth(outPulse, args.sampleTime);
		outputs[STATUS_OUTPUT].setVoltage(link == MonitorLink::Linked ? 5.f :
			link == MonitorLink::Duplicate ? -5.f : 0.f);
	}

	void processControl(float dt) {
		int nextId = clamp((int)std::round(params[INSTRUMENT_PARAM].getValue()), 0, 3);
		if (nextId != instrumentId)
			setInstrumentId(nextId);

		spacetime::MetaModuleTimingBus& bus = timingBusRegistry.bus(instrumentId);
		uint32_t coreCount = bus.coreCount.load(std::memory_order_acquire);
		uint32_t heartbeat = midiStatusRegistry.heartbeat(instrumentId);
		if (heartbeat != lastHeartbeat) {
			lastHeartbeat = heartbeat;
			staleTime = 0.f;
		} else {
			staleTime += dt;
		}
		link = coreCount > 1 ? MonitorLink::Duplicate :
			(coreCount == 1 && staleTime < 0.25f ? MonitorLink::Linked : MonitorLink::Waiting);

		bool coreValid = link != MonitorLink::Waiting;
		midiStatusRegistry.read(instrumentId, coreValid, snapshot);

		if (snapshot.inSeq != lastInSeq) {
			lastInSeq = snapshot.inSeq;
			inPulse = 1.f;
		}
		if (snapshot.clkSeq != lastClkSeq) {
			lastClkSeq = snapshot.clkSeq;
			clkPulse = 1.f;
		}
		if (snapshot.outSeq != lastOutSeq) {
			lastOutSeq = snapshot.outSeq;
			outPulse = 1.f;
		}
	}

	void setInstrumentId(int next) {
		timingBusRegistry.unregisterMonitor(instrumentId);
		instrumentId = clamp(next, 0, 3);
		timingBusRegistry.registerMonitor(instrumentId);
		lastHeartbeat = 0;
		staleTime = 1.f;
		link = MonitorLink::Waiting;
		snapshot = spacetime::MetaModuleMidiStatusSnapshot();
		lastInSeq = lastClkSeq = lastOutSeq = 0;
	}

	// Same "MIDI CH%02d %s%03d V%03d %s" line Program.cpp's own display
	// already uses -- this is the same underlying MidiCore state, now read
	// back over the bus instead of computed locally.
	size_t get_display_text(int lightId, std::span<char> text) override {
		if (lightId != STATUS_DISPLAY || text.empty())
			return 0;
		const char* linkName = link == MonitorLink::Linked ? "LINK" :
			(link == MonitorLink::Duplicate ? "DUP" : "WAIT");
		const char* midiKind = snapshot.lastStatus < 0 ? "---" :
			(snapshot.lastChannel < 0 ? "RT " :
				(((snapshot.lastStatus >> 4) & 0xf) == 0xc ? "PC " : "CC "));
		char buffer[128];
		int length = std::snprintf(buffer, sizeof(buffer),
			"ID %c %s\nPROGRAM CH%02d\nSLIDER CH%02d\nMIDI CH%02d %s%03d V%03d %s",
			(char)('A' + instrumentId), linkName,
			snapshot.controlChannel + 1,
			snapshot.sliderChannel + 1,
			(int)(snapshot.lastChannel < 0 ? 0 : snapshot.lastChannel + 1),
			midiKind,
			(int)(snapshot.lastNumber < 0 ? 0 : snapshot.lastNumber),
			(int)(snapshot.lastValue < 0 ? 0 : snapshot.lastValue),
			spacetime::MidiCore::routeName(snapshot.lastRoute));
		if (length < 0)
			return 0;
		size_t copyLength = std::min(text.size(), (size_t)length);
		std::copy(buffer, buffer + copyLength, text.begin());
		return copyLength;
	}
};

struct SpaceTimeMidiWidget : ModuleWidget {
	SpaceTimeMidiWidget(SpaceTimeMidi* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Midi.svg")));

		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(4.f, 12.f)));
		display->box.size = mm2px(Vec(53.f, 33.f));
		display->firstLightId = SpaceTimeMidi::STATUS_DISPLAY;
		display->font = "Default_12";
		display->color = Colors565::White;
		addChild(display);

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(20.f, 57.f)), module,
			SpaceTimeMidi::INSTRUMENT_PARAM));

		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(45.f, 50.f)), module, SpaceTimeMidi::IN_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(45.f, 58.f)), module, SpaceTimeMidi::CLK_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(45.f, 66.f)), module, SpaceTimeMidi::OUT_LIGHT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(30.5f, 90.f)), module, SpaceTimeMidi::STATUS_OUTPUT));
	}
};

} // namespace

Model* modelSpaceTimeMidi = createModel<SpaceTimeMidi, SpaceTimeMidiWidget>("Midi");
