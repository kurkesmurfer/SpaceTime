#include <rack.hpp>
#include <metamodule/VCVTextDisplay.hpp>
#include "SpaceTimeEngine.hpp"
#include "TimingBus.hpp"

#include <algorithm>
#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

namespace {

struct SpaceTimeCore : Module {
	enum ParamId {
		STAGE_PARAM,
		PRESET_PARAM,
		CONTROL_CHANNEL_PARAM,
		SLIDER_CHANNEL_PARAM,
		SAVE_PARAM,
		LOAD_PARAM,
		CLEAR_PARAM,
		PULSE_RETRIG_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(EXT_INPUTS, 4),
		INPUTS_LEN
	};
	enum OutputId {
		SELECTED_V_OUTPUT,
		SELECTED_TIME_OUTPUT,
		HEAD1_CV_OUTPUT,
		HEAD1_ALL_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		MIDI_IN_LIGHT,
		MIDI_CLOCK_LIGHT,
		MIDI_OUT_LIGHT,
		HEAD1_RUN_LIGHT,
		STATUS_DISPLAY,
		LIGHTS_LEN
	};

	spacetime::SpaceTimeEngine engine;
	midi::InputQueue midiInput;
	midi::Output midiOutput;
	dsp::SchmittTrigger saveTrigger;
	dsp::SchmittTrigger loadTrigger;
	dsp::SchmittTrigger clearTrigger;
	dsp::ClockDivider controlDivider;
	float midiLight = 0.f;
	float clockLight = 0.f;
	float outLight = 0.f;
	uint32_t busToken = timingBusRegistry.makeToken();
	int instrumentId = 0;
	bool ownsTimingBus = false;

	struct RackMidiSink : spacetime::MidiOutputSink {
		SpaceTimeCore* owner;
		explicit RackMidiSink(SpaceTimeCore* module) : owner(module) {}
		void send(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) override {
			midi::Message message;
			message.setStatus(status);
			message.setChannel(channel & 0xf);
			message.setNote(data1);
			message.setValue(data2);
			owner->midiOutput.sendMessage(message);
			owner->outLight = 1.f;
		}
	};

	SpaceTimeCore() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(STAGE_PARAM, 0.f, 63.f, 0.f, "Selected stage", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(PRESET_PARAM, 0.f, 11.f, 0.f, "Preset slot", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(CONTROL_CHANNEL_PARAM, 0.f, 15.f, 15.f, "Program MIDI channel", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(SLIDER_CHANNEL_PARAM, 0.f, 15.f, 14.f, "Stage slider MIDI channel", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configButton(SAVE_PARAM, "Save selected preset");
		configButton(LOAD_PARAM, "Load selected preset");
		configButton(CLEAR_PARAM, "Clear all stage program words");
		configSwitch(PULSE_RETRIG_PARAM, 0.f, 1.f, 1.f, "Pulse retrigger",
			{"Continuous", "Hardware-compatible notch"});
		for (int i = 0; i < 4; i++)
			configInput(EXT_INPUTS + i, string::f("External %c", 'A' + i));
		configOutput(SELECTED_V_OUTPUT, "Selected stage voltage");
		configOutput(SELECTED_TIME_OUTPUT, "Selected stage time slider");
		configOutput(HEAD1_CV_OUTPUT, "Head 1 CV");
		configOutput(HEAD1_ALL_OUTPUT, "Head 1 all pulse");
		configLight(MIDI_IN_LIGHT, "MIDI input activity");
		configLight(MIDI_CLOCK_LIGHT, "MIDI clock activity");
		configLight(MIDI_OUT_LIGHT, "MIDI output activity");
		configLight(HEAD1_RUN_LIGHT, "Head 1 running");
		midiInput.channel = -1;
		midiOutput.channel = -1;
		controlDivider.setDivision(16);
		ownsTimingBus = timingBusRegistry.registerCore(instrumentId, busToken);
	}

	~SpaceTimeCore() override {
		timingBusRegistry.unregisterCore(instrumentId, busToken);
	}

	void process(const ProcessArgs& args) override {
		midi::Message message;
		while (midiInput.tryPop(&message, args.frame)) {
			if (message.bytes.empty())
				continue;
			uint8_t status = message.bytes[0];
			uint8_t data1 = message.bytes.size() > 1 ? message.bytes[1] : 0;
			uint8_t data2 = message.bytes.size() > 2 ? message.bytes[2] : 0;
			engine.handleMidi(status, data1, data2);
			midiLight = 1.f;
			if (status == 0xF8)
				clockLight = 1.f;
			if (engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SELECT_PREV ||
				engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SELECT_NEXT)
				params[STAGE_PARAM].setValue((float)engine.program().selectedStage());
			if (engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SET_PULSE_RETRIG)
				params[PULSE_RETRIG_PARAM].setValue(engine.globals().pulseRetrig ? 1.f : 0.f);
		}

		bool controlTick = controlDivider.process();
		if (controlTick) {
			int controlChannel = clamp((int)std::round(params[CONTROL_CHANNEL_PARAM].getValue()), 0, 15);
			int sliderChannel = clamp((int)std::round(params[SLIDER_CHANNEL_PARAM].getValue()), 0, 15);
			if (sliderChannel == controlChannel) {
				sliderChannel = controlChannel == 15 ? 14 : 15;
				params[SLIDER_CHANNEL_PARAM].setValue((float)sliderChannel);
			}
			engine.midi().controlChannel = controlChannel;
			engine.midi().sliderChannel = sliderChannel;
			engine.program().setSelected(clamp((int)std::round(params[STAGE_PARAM].getValue()), 0, 63));
			engine.globals().pulseRetrig = params[PULSE_RETRIG_PARAM].getValue() > 0.5f;
			for (int i = 0; i < 4; i++)
				engine.setExternal(i, inputs[EXT_INPUTS + i].getVoltage(), inputs[EXT_INPUTS + i].isConnected());

			int preset = clamp((int)std::round(params[PRESET_PARAM].getValue()), 0, 11);
			if (saveTrigger.process(params[SAVE_PARAM].getValue()))
				engine.savePreset(preset);
			if (loadTrigger.process(params[LOAD_PARAM].getValue()))
				engine.loadPreset(preset);
			if (clearTrigger.process(params[CLEAR_PARAM].getValue())) {
				spacetime::EditOp operations[spacetime::kMaxStages];
				int count = engine.program().emitClear(engine.table(), operations, spacetime::kMaxStages);
				for (int i = 0; i < count; i++)
					engine.applyEdit(operations[i]);
			}
			engine.processControl(args.sampleTime * controlDivider.getDivision());
		}

		engine.processHeads(args.sampleTime);
		if (controlTick)
			publishTiming();
		RackMidiSink sink(this);
		engine.processMidiOutput(args.sampleTime, sink);

		int selected = clamp(engine.program().selectedStage(), 0, spacetime::kMaxStages - 1);
		outputs[SELECTED_V_OUTPUT].setVoltage(engine.table().voltage[selected]);
		outputs[SELECTED_TIME_OUTPUT].setVoltage(engine.table().time[selected] * 10.f);
		outputs[HEAD1_CV_OUTPUT].setVoltage(engine.headOut(0).cv);
		outputs[HEAD1_ALL_OUTPUT].setVoltage(engine.headOut(0).allPulse ? 10.f : 0.f);
		midiLight = std::fmax(0.f, midiLight - args.sampleTime * 8.f);
		clockLight = std::fmax(0.f, clockLight - args.sampleTime * 10.f);
		outLight = std::fmax(0.f, outLight - args.sampleTime * 4.f);
		lights[MIDI_IN_LIGHT].setBrightnessSmooth(midiLight, args.sampleTime);
		lights[MIDI_CLOCK_LIGHT].setBrightnessSmooth(clockLight, args.sampleTime);
		lights[MIDI_OUT_LIGHT].setBrightnessSmooth(outLight, args.sampleTime);
		lights[HEAD1_RUN_LIGHT].setBrightnessSmooth(
			engine.headOut(0).runState == spacetime::RUN_RUNNING ? 1.f : 0.f, args.sampleTime);
	}

	size_t get_display_text(int lightId, std::span<char> text) override {
		if (lightId != STATUS_DISPLAY || text.empty())
			return 0;
		int selected = clamp(engine.program().selectedStage(), 0, spacetime::kMaxStages - 1);
		char runStates[spacetime::kMaxHeads + 1];
		for (int h = 0; h < spacetime::kMaxHeads; h++)
			runStates[h] = engine.headOut(h).runState == spacetime::RUN_RUNNING ? 'R' :
				(engine.headOut(h).runState == spacetime::RUN_HOLDING ? 'H' : '-');
		runStates[spacetime::kMaxHeads] = '\0';
		const char* midiKind = engine.midi().lastStatus < 0 ? "---" :
			(engine.midi().lastChannel < 0 ? "RT " :
				(((engine.midi().lastStatus >> 4) & 0xf) == 0xc ? "PC " : "CC "));
		char buffer[128];
		int length = std::snprintf(buffer, sizeof(buffer),
			"STAGE %02d V %.2f T %.3f\nID %c P%02d S%02d K%02d SC%d\nRUN 1-8 %s\nMIDI CH%02d %s%03d V%03d %s",
			selected + 1, engine.table().voltage[selected], engine.table().time[selected],
			(char)('A' + instrumentId),
			engine.midi().controlChannel + 1, engine.midi().sliderChannel + 1,
			engine.program().scaleKey().key, engine.program().scaleKey().scale,
			runStates,
			engine.midi().lastChannel < 0 ? 0 : engine.midi().lastChannel + 1,
			midiKind,
			engine.midi().lastNumber < 0 ? 0 : engine.midi().lastNumber,
			engine.midi().lastValue < 0 ? 0 : engine.midi().lastValue,
			spacetime::MidiCore::routeName(engine.midi().lastRoute));
		if (length < 0)
			return 0;
		size_t copyLength = std::min(text.size(), (size_t)length);
		std::copy(buffer, buffer + copyLength, text.begin());
		return copyLength;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "key", json_integer(engine.program().scaleKey().key));
		json_object_set_new(root, "scale", json_integer(engine.program().scaleKey().scale));
		json_object_set_new(root, "slewFrac1", json_real(engine.globals().slewFrac1));
		json_object_set_new(root, "slewFrac2", json_real(engine.globals().slewFrac2));
		json_object_set_new(root, "slopeLaw", json_integer(engine.globals().slopeLaw));
		json_object_set_new(root, "addressScale", json_integer(engine.globals().addressScale));
		json_object_set_new(root, "instrumentId", json_integer(instrumentId));
		json_object_set_new(root, "moveStageSliders", json_boolean(engine.midi().moveStageSliders));
		json_object_set_new(root, "midiInput", midiInput.toJson());
		json_object_set_new(root, "midiOutput", midiOutput.toJson());

		json_t* voltage = json_array();
		json_t* time = json_array();
		json_t* words = json_array();
		for (int stage = 0; stage < spacetime::kMaxStages; stage++) {
			json_array_append_new(voltage, json_real(engine.table().voltage[stage]));
			json_array_append_new(time, json_real(engine.table().time[stage]));
			json_array_append_new(words, json_integer(engine.table().program[stage].bits));
		}
		json_object_set_new(root, "voltage", voltage);
		json_object_set_new(root, "time", time);
		json_object_set_new(root, "program", words);

		json_t* presets = json_array();
		for (int slot = 0; slot < spacetime::kPresetSlots; slot++) {
			const spacetime::PresetSlot& source = engine.program().slot(slot);
			if (!source.used)
				continue;
			json_t* preset = json_object();
			json_object_set_new(preset, "slot", json_integer(slot));
			json_object_set_new(preset, "key", json_integer(source.scaleKey.key));
			json_object_set_new(preset, "scale", json_integer(source.scaleKey.scale));
			json_t* pv = json_array();
			json_t* pt = json_array();
			json_t* pw = json_array();
			for (int stage = 0; stage < spacetime::kMaxStages; stage++) {
				json_array_append_new(pv, json_real(source.table.voltage[stage]));
				json_array_append_new(pt, json_real(source.table.time[stage]));
				json_array_append_new(pw, json_integer(source.table.program[stage].bits));
			}
			json_object_set_new(preset, "voltage", pv);
			json_object_set_new(preset, "time", pt);
			json_object_set_new(preset, "program", pw);
			json_array_append_new(presets, preset);
		}
		json_object_set_new(root, "presets", presets);

		json_t* heads = json_array();
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			const spacetime::HeadConfig& config = engine.headConfig(h);
			json_t* head = json_object();
			json_object_set_new(head, "continuous", json_boolean(config.continuous));
			json_object_set_new(head, "addrExt", json_boolean(config.addrExt));
			json_object_set_new(head, "address", json_real(config.addressKnob));
			json_object_set_new(head, "direction", json_integer(config.direction));
			json_object_set_new(head, "clockSource", json_integer(engine.headClockSource(h)));
			json_object_set_new(head, "clockDivision", json_integer(config.clkDivIndex));
			json_object_set_new(head, "timeAmount", json_real(config.timeCvAmount));
			json_object_set_new(head, "loopMode", json_integer(config.loopMode));
			json_object_set_new(head, "followTransport", json_boolean(engine.headFollowsMidiTransport(h)));
			json_array_append_new(heads, head);
		}
		json_object_set_new(root, "heads", heads);

		json_t* lanes = json_array();
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			const spacetime::MidiOutLaneConfig& config = engine.midi().outLane[h];
			json_t* lane = json_object();
			json_object_set_new(lane, "mode", json_integer(config.mode));
			json_object_set_new(lane, "channel", json_integer(config.channel));
			json_object_set_new(lane, "gate", json_integer(config.gateSource));
			json_object_set_new(lane, "cc", json_integer(config.cc));
			json_array_append_new(lanes, lane);
		}
		json_object_set_new(root, "outLanes", lanes);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* value = nullptr;
		spacetime::ScaleKey scaleKey = engine.program().scaleKey();
		if ((value = json_object_get(root, "key"))) scaleKey.key = (uint8_t)clamp((int)json_integer_value(value), 0, 11);
		if ((value = json_object_get(root, "scale"))) scaleKey.scale = (uint8_t)clamp((int)json_integer_value(value), 0, 2);
		engine.program().setScaleKey(scaleKey);
		if ((value = json_object_get(root, "slewFrac1"))) engine.globals().slewFrac1 = (float)json_number_value(value);
		if ((value = json_object_get(root, "slewFrac2"))) engine.globals().slewFrac2 = (float)json_number_value(value);
		if ((value = json_object_get(root, "slopeLaw"))) engine.globals().slopeLaw = (uint8_t)json_integer_value(value);
		if ((value = json_object_get(root, "addressScale"))) engine.globals().addressScale = (uint8_t)json_integer_value(value);
		if ((value = json_object_get(root, "instrumentId"))) setInstrumentId((int)json_integer_value(value));
		if ((value = json_object_get(root, "moveStageSliders"))) engine.midi().moveStageSliders = json_boolean_value(value);
		if ((value = json_object_get(root, "midiInput"))) midiInput.fromJson(value);
		if ((value = json_object_get(root, "midiOutput"))) midiOutput.fromJson(value);
		loadTableArrays(root, engine.table());

		json_t* presets = json_object_get(root, "presets");
		if (presets) {
			size_t index;
			json_t* preset;
			json_array_foreach(presets, index, preset) {
				json_t* slotValue = json_object_get(preset, "slot");
				if (!slotValue)
					continue;
				int slot = (int)json_integer_value(slotValue);
				if (slot < 0 || slot >= spacetime::kPresetSlots)
					continue;
				spacetime::PresetSlot target;
				target.used = true;
				target.table.count = spacetime::kMaxStages;
				if ((value = json_object_get(preset, "key"))) target.scaleKey.key = (uint8_t)clamp((int)json_integer_value(value), 0, 11);
				if ((value = json_object_get(preset, "scale"))) target.scaleKey.scale = (uint8_t)clamp((int)json_integer_value(value), 0, 2);
				loadTableArrays(preset, target.table);
				engine.program().setSlot(slot, target);
			}
		}

		json_t* heads = json_object_get(root, "heads");
		for (int h = 0; heads && h < spacetime::kMaxHeads; h++) {
			json_t* head = json_array_get(heads, h);
			if (!head)
				continue;
			spacetime::HeadConfig& config = engine.headConfig(h);
			if ((value = json_object_get(head, "continuous"))) config.continuous = json_boolean_value(value);
			if ((value = json_object_get(head, "addrExt"))) config.addrExt = json_boolean_value(value);
			if ((value = json_object_get(head, "address"))) config.addressKnob = clamp((float)json_number_value(value), 0.f, 10.f);
			if ((value = json_object_get(head, "direction"))) config.direction = (uint8_t)clamp((int)json_integer_value(value), 0, 4);
			if ((value = json_object_get(head, "clockSource"))) engine.setHeadClockSource(h, (int)json_integer_value(value));
			if ((value = json_object_get(head, "clockDivision"))) config.clkDivIndex = (uint8_t)clamp((int)json_integer_value(value), 0, 8);
			if ((value = json_object_get(head, "timeAmount"))) config.timeCvAmount = clamp((float)json_number_value(value), -1.f, 1.f);
			if ((value = json_object_get(head, "loopMode"))) config.loopMode = (uint8_t)clamp((int)json_integer_value(value), 0, 2);
			if ((value = json_object_get(head, "followTransport"))) engine.setHeadFollowsMidiTransport(h, json_boolean_value(value));
		}

		json_t* lanes = json_object_get(root, "outLanes");
		for (int h = 0; lanes && h < spacetime::kMaxHeads; h++) {
			json_t* lane = json_array_get(lanes, h);
			if (!lane)
				continue;
			spacetime::MidiOutLaneConfig& config = engine.midi().outLane[h];
			if ((value = json_object_get(lane, "mode"))) config.mode = (uint8_t)clamp((int)json_integer_value(value), 0, 2);
			if ((value = json_object_get(lane, "channel"))) config.channel = (uint8_t)clamp((int)json_integer_value(value), 0, 15);
			if ((value = json_object_get(lane, "gate"))) config.gateSource = (uint8_t)clamp((int)json_integer_value(value), 0, 2);
			if ((value = json_object_get(lane, "cc"))) config.cc = (uint8_t)clamp((int)json_integer_value(value), 0, 127);
		}
	}

	static void loadTableArrays(json_t* root, spacetime::StageTable& table) {
		json_t* voltage = json_object_get(root, "voltage");
		json_t* time = json_object_get(root, "time");
		json_t* words = json_object_get(root, "program");
		table.count = spacetime::kMaxStages;
		for (int stage = 0; stage < spacetime::kMaxStages; stage++) {
			json_t* value;
			if (voltage && (value = json_array_get(voltage, stage)))
				table.voltage[stage] = clamp((float)json_number_value(value), 0.f, 10.f);
			if (time && (value = json_array_get(time, stage)))
				table.time[stage] = clamp((float)json_number_value(value), 0.f, 1.f);
			if (words && (value = json_array_get(words, stage))) {
				uint32_t bits = (uint32_t)json_integer_value(value);
				if (bits < (1u << 19))
					table.program[stage].bits = bits;
			}
		}
	}

	void setInstrumentId(int next) {
		next = clamp(next, 0, 3);
		if (next == instrumentId)
			return;
		timingBusRegistry.unregisterCore(instrumentId, busToken);
		instrumentId = next;
		ownsTimingBus = timingBusRegistry.registerCore(instrumentId, busToken);
	}

	void publishTiming() {
		spacetime::MetaModuleTimingBus& bus = timingBusRegistry.bus(instrumentId);
		if (!ownsTimingBus)
			ownsTimingBus = timingBusRegistry.tryClaimCore(instrumentId, busToken);
		if (!ownsTimingBus || bus.coreCount.load(std::memory_order_acquire) != 1)
			return;
		spacetime::TimingSnapshot snapshot;
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			snapshot.sourceEvents[h] = engine.sourceClockEvents(h);
			snapshot.stageEntries[h] = engine.stageEntries(h);
			snapshot.runState[h] = engine.headOut(h).runState;
			snapshot.stage[h] = engine.headOut(h).currentStage;
			snapshot.clockSource[h] = (uint8_t)engine.headClockSource(h);
			snapshot.clockDivision[h] = engine.headConfig(h).clkDivIndex;
		}
		bus.telemetry.publish(snapshot);
	}
};

struct SpaceTimeCoreWidget : ModuleWidget {
	SpaceTimeCoreWidget(SpaceTimeCore* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Core.svg")));
		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(4.f, 12.f)));
		display->box.size = mm2px(Vec(73.f, 27.f));
		display->firstLightId = SpaceTimeCore::STATUS_DISPLAY;
		display->font = "Default_10";
		display->color = Colors565::White;
		addChild(display);

		const float x[4] = {11.f, 31.f, 51.f, 71.f};
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x[0], 50.f)), module, SpaceTimeCore::STAGE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x[1], 50.f)), module, SpaceTimeCore::PRESET_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x[2], 50.f)), module, SpaceTimeCore::CONTROL_CHANNEL_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x[3], 50.f)), module, SpaceTimeCore::SLIDER_CHANNEL_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(x[0], 72.f)), module, SpaceTimeCore::SAVE_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(x[1], 72.f)), module, SpaceTimeCore::LOAD_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(x[2], 72.f)), module, SpaceTimeCore::CLEAR_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(x[3], 72.f)), module, SpaceTimeCore::PULSE_RETRIG_PARAM));
		for (int i = 0; i < 4; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x[i], 94.f)), module, SpaceTimeCore::EXT_INPUTS + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x[0], 116.f)), module, SpaceTimeCore::SELECTED_V_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x[1], 116.f)), module, SpaceTimeCore::SELECTED_TIME_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x[2], 116.f)), module, SpaceTimeCore::HEAD1_CV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x[3], 116.f)), module, SpaceTimeCore::HEAD1_ALL_OUTPUT));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(59.f, 6.f)), module, SpaceTimeCore::MIDI_IN_LIGHT));
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(64.f, 6.f)), module, SpaceTimeCore::MIDI_CLOCK_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(69.f, 6.f)), module, SpaceTimeCore::MIDI_OUT_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(74.f, 6.f)), module, SpaceTimeCore::HEAD1_RUN_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		SpaceTimeCore* module = getModule<SpaceTimeCore>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		static const char* instrumentLabels[] = {"A", "B", "C", "D"};
		menu->addChild(createSubmenuItem("Instrument ID", [=]() {
			return instrumentLabels[module->instrumentId];
		}, [=](Menu* ids) {
			for (int id = 0; id < 4; id++)
				ids->addChild(createCheckMenuItem(instrumentLabels[id], "",
					[=]() { return module->instrumentId == id; },
					[=]() { module->setInstrumentId(id); }));
		}));
		menu->addChild(createBoolPtrMenuItem("Move stage controls with CC", "", &module->engine.midi().moveStageSliders));
		menu->addChild(createMenuLabel("MIDI output per head"));
		static const char* modes[] = {"Off", "Notes", "CC 7-bit"};
		static const char* gates[] = {"Pulse 1", "Pulse 2", "ALL"};
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			menu->addChild(createSubmenuItem(string::f("Head %d", h + 1),
				[=]() { return modes[module->engine.midi().outLane[h].mode]; },
				[=](Menu* sub) {
					for (int mode = 0; mode < 3; mode++)
						sub->addChild(createCheckMenuItem(modes[mode], "",
							[=]() { return module->engine.midi().outLane[h].mode == mode; },
							[=]() { module->engine.midi().outLane[h].mode = (uint8_t)mode; }));
					sub->addChild(createSubmenuItem("Channel", [=]() {
						return string::f("%d", module->engine.midi().outLane[h].channel + 1);
					}, [=](Menu* channels) {
						for (int channel = 0; channel < 16; channel++)
							channels->addChild(createCheckMenuItem(string::f("%d", channel + 1), "",
								[=]() { return module->engine.midi().outLane[h].channel == channel; },
								[=]() { module->engine.midi().outLane[h].channel = (uint8_t)channel; }));
					}));
					sub->addChild(createSubmenuItem("Note gate source", [=]() {
						return gates[module->engine.midi().outLane[h].gateSource];
					}, [=](Menu* sources) {
						for (int source = 0; source < 3; source++)
							sources->addChild(createCheckMenuItem(gates[source], "",
								[=]() { return module->engine.midi().outLane[h].gateSource == source; },
								[=]() { module->engine.midi().outLane[h].gateSource = (uint8_t)source; }));
					}));
					sub->addChild(createSubmenuItem("CC number", [=]() {
						return string::f("%d", module->engine.midi().outLane[h].cc);
					}, [=](Menu* numbers) {
						for (int cc = 0; cc < 128; cc++)
							numbers->addChild(createCheckMenuItem(string::f("%d", cc), "",
								[=]() { return module->engine.midi().outLane[h].cc == cc; },
								[=]() { module->engine.midi().outLane[h].cc = (uint8_t)cc; }));
					}));
					sub->addChild(createCheckMenuItem("Follow MIDI transport", "",
						[=]() { return module->engine.headFollowsMidiTransport(h); },
						[=]() { module->engine.setHeadFollowsMidiTransport(h,
							!module->engine.headFollowsMidiTransport(h)); }));
				}));
		}
	}
};

} // namespace

Model* modelSpaceTimeCore = createModel<SpaceTimeCore, SpaceTimeCoreWidget>("Core");
