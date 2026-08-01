#include "plugin.hpp"
#include "paneltheme.hpp"
#include "ChainAdapter.hpp"
#include "MidiCore.hpp"
#include "MidiFeedback.hpp"
#include <app/MidiDisplay.hpp>

// ============================================================================
// MIDI — platform adapter around dsp/MidiCore.hpp.
// Rack owns device queues, persistence, LEDs and menus; the shared core owns
// all decoding, routing and outgoing note/CC behavior used by VCV/MetaModule.
// ============================================================================

namespace LayoutM {
constexpr float CX = 10.16f;
constexpr float ACT_Y0 = 25.0f;
constexpr float ACT_PITCH = 9.0f;
constexpr float OUT_Y = 116.0f;
} // namespace LayoutM

struct Midi : Module {
	enum ParamId {
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		IN_LIGHT,
		CLK_LIGHT,
		OUT_LIGHT,
		LIGHTS_LEN
	};

	midi::InputQueue midiInput;
	midi::Output midiOutput;
	spacetime::MidiCore core;
	spacetime::MidiFeedbackState feedbackState;

	spacetime::MessagePort<spacetime::AnchorToHeadsMsg> rightPort;
	spacetime::MessagePort<spacetime::HeadsToAnchorMsg> leftPort;

	float inLight = 0.f, clkLight = 0.f, outLight = 0.f;

	Midi() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configLight(IN_LIGHT, "MIDI input activity");
		configLight(CLK_LIGHT, "MIDI clock activity");
		configLight(OUT_LIGHT, "MIDI output activity");
		midiInput.channel = -1;  // channel routing is SpaceTime-specific
		midiOutput.channel = -1; // SpaceTime routes per-head output channels
		rightPort.attach(rightExpander);
		leftPort.attach(leftExpander);
	}

	void handleMessage(const midi::Message& msg) {
		if (msg.bytes.empty())
			return;
		uint8_t status = msg.bytes[0];
		uint8_t data1 = msg.bytes.size() > 1 ? msg.bytes[1] : 0;
		uint8_t data2 = msg.bytes.size() > 2 ? msg.bytes[2] : 0;
		inLight = 1.f;
		if (status == 0xF8)
			clkLight = 1.f;
		core.handleMessage(status, data1, data2);
	}

	void sendMidi(uint8_t status, uint8_t channel, uint8_t a, uint8_t b) {
		midi::Message msg;
		msg.setStatus(status);
		msg.setChannel(channel & 0xf);
		msg.setNote(a);
		msg.setValue(b);
		midiOutput.sendMessage(msg);
		outLight = 1.f;
	}

	struct RackMidiSink : spacetime::MidiOutputSink {
		Midi* module;
		explicit RackMidiSink(Midi* module) : module(module) {}
		void send(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) override {
			module->sendMidi(status, channel, data1, data2);
		}
	};

	void process(const ProcessArgs& args) override {
		midiInput.channel = -1;  // SpaceTime routes channels internally
		midiOutput.channel = -1;
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame))
			handleMessage(msg);

		bool rightIsProgram = spacetime::modelIs(rightExpander.module, modelProgram, modelGlueRight);
		bool leftIsHead = spacetime::modelIs(leftExpander.module, modelHead, modelGlueLeft);

		const spacetime::AnchorToHeadsMsg* fromProgram = rightPort.consume(rightExpander);
		bool broadcastValid = rightIsProgram && fromProgram->valid;
		if (leftIsHead) {
			spacetime::AnchorToHeadsMsg* toHead =
				spacetime::leftNeighborProducer<spacetime::AnchorToHeadsMsg>(
					this, modelHead, modelGlueLeft);
			if (toHead) {
				if (broadcastValid) {
					*toHead = *fromProgram;
					core.injectMidi(*toHead);
				}
				else {
					toHead->valid = false;
				}
				spacetime::flipLeftNeighbor(this);
			}
		}

		if (rightIsProgram) {
			spacetime::HeadsToAnchorMsg* toProgram =
				spacetime::rightNeighborProducer<spacetime::HeadsToAnchorMsg>(
					this, modelProgram, modelGlueRight);
			if (toProgram) {
				const spacetime::HeadsToAnchorMsg* fromHead = leftPort.consume(leftExpander);
				bool statusValid = leftIsHead && fromHead->valid;
				spacetime::collectHeadFeedbackState(statusValid ? fromHead : NULL,
					feedbackState);
				if (statusValid) {
					RackMidiSink sink(this);
					core.processOutput(*fromHead, args.sampleTime, sink);
				}
				if (statusValid) {
					*toProgram = *fromHead;
				}
				else {
					*toProgram = spacetime::HeadsToAnchorMsg();
					toProgram->valid = true;
				}
				core.appendProgramEvents(*toProgram);
				spacetime::flipRightNeighbor(this);
			}
		}

		inLight = std::fmax(0.f, inLight - args.sampleTime * 8.f);
		clkLight = std::fmax(0.f, clkLight - args.sampleTime * 10.f);
		outLight = std::fmax(0.f, outLight - args.sampleTime * 4.f);
		lights[IN_LIGHT].setBrightnessSmooth(inLight, args.sampleTime);
		lights[CLK_LIGHT].setBrightnessSmooth(clkLight, args.sampleTime);
		lights[OUT_LIGHT].setBrightnessSmooth(outLight, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "controlChannel", json_integer(core.controlChannel));
		json_object_set_new(root, "sliderChannel", json_integer(core.sliderChannel));
		json_object_set_new(root, "moveStageSliders", json_boolean(core.moveStageSliders));
		json_object_set_new(root, "midiInput", midiInput.toJson());
		json_object_set_new(root, "midiOutput", midiOutput.toJson());
		json_t* lanes = json_array();
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			json_t* lane = json_object();
			json_object_set_new(lane, "mode", json_integer(core.outLane[h].mode));
			json_object_set_new(lane, "channel", json_integer(core.outLane[h].channel));
			json_object_set_new(lane, "gate", json_integer(core.outLane[h].gateSource));
			json_object_set_new(lane, "cc", json_integer(core.outLane[h].cc));
			json_array_append_new(lanes, lane);
		}
		json_object_set_new(root, "outLanes", lanes);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "controlChannel")))
			core.controlChannel = clamp((int)json_integer_value(j), 0, 15);
		if ((j = json_object_get(root, "sliderChannel")))
			core.sliderChannel = clamp((int)json_integer_value(j), 0, 15);
		if (core.sliderChannel == core.controlChannel)
			core.sliderChannel = core.controlChannel == 15 ? 14 : 15;
		if ((j = json_object_get(root, "moveStageSliders")))
			core.moveStageSliders = json_boolean_value(j);
		if ((j = json_object_get(root, "midiInput")))
			midiInput.fromJson(j);
		if ((j = json_object_get(root, "midiOutput")))
			midiOutput.fromJson(j);
		if ((j = json_object_get(root, "outLanes"))) {
			for (int h = 0; h < spacetime::kMaxHeads; h++) {
				json_t* lane = json_array_get(j, h);
				if (!lane)
					continue;
				json_t* k;
				if ((k = json_object_get(lane, "mode")))
					core.outLane[h].mode = (uint8_t)clamp((int)json_integer_value(k), 0, 2);
				if ((k = json_object_get(lane, "channel")))
					core.outLane[h].channel = (uint8_t)clamp((int)json_integer_value(k), 0, 15);
				if ((k = json_object_get(lane, "gate")))
					core.outLane[h].gateSource = (uint8_t)clamp((int)json_integer_value(k), 0, 2);
				if ((k = json_object_get(lane, "cc")))
					core.outLane[h].cc = (uint8_t)clamp((int)json_integer_value(k), 0, 127);
			}
		}
		midiInput.channel = -1;
		midiOutput.channel = -1;
	}
};

#ifndef METAMODULE
struct MidiReadout : Widget {
	Midi* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		std::string program = module ? string::f("P%02d", module->core.controlChannel + 1) : "P16";
		std::string sliders = module ? string::f("S%02d", module->core.sliderChannel + 1) : "S15";
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 9.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xc1, 0x3c, 0x36));
		nvgText(args.vg, box.size.x / 2.f, box.size.y * 0.32f, program.c_str(), NULL);
		nvgText(args.vg, box.size.x / 2.f, box.size.y * 0.72f, sliders.c_str(), NULL);
	}
};
#endif

struct MidiWidget : ModuleWidget {
	MidiWidget(Midi* module) {
		using namespace LayoutM;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Midi.svg")));

#ifndef METAMODULE
		spacetime::addTitle(this, CX, 5.6f, "Midi");
		spacetime::addSubtitle(this, CX, 10.2f, "CONTROL");

		spacetime::addSectionHeading(this, CX, 16.8f, "ACTIVITY");
		spacetime::addMicroLabel(this, CX + 5.1f, ACT_Y0 + 1.0f, "IN");
		spacetime::addMicroLabel(this, CX + 5.1f, ACT_Y0 + ACT_PITCH + 1.0f, "CLK");
		spacetime::addMicroLabel(this, CX + 5.1f, ACT_Y0 + 2.f * ACT_PITCH + 1.0f, "OUT");

		spacetime::addSectionHeading(this, CX, 55.2f, "PROGRAM");
		spacetime::addSectionHeading(this, CX, 78.2f, "HEADS");
		spacetime::addMicroLabel(this, CX, 101.0f, "MIDI IN");
		spacetime::addMicroLabel(this, CX, OUT_Y + 4.6f, "MIDI OUT");

		auto* ch = new MidiReadout;
		ch->module = module;
		ch->box.pos = mm2px(Vec(2.7f, 59.0f));
		ch->box.size = mm2px(Vec(14.9f, 12.0f));
		addChild(ch);
		spacetime::addMicroLabel(this, CX, 87.5f, "CH 1-8");
#endif

		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(CX - 3.7f, ACT_Y0)), module, Midi::IN_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(CX - 3.7f, ACT_Y0 + ACT_PITCH)), module, Midi::CLK_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(CX - 3.7f, ACT_Y0 + 2.f * ACT_PITCH)), module, Midi::OUT_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Midi* module = getModule<Midi>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI input"));
		if (!module) {
			menu->addChild(createMenuLabel("Settings appear when module exists"));
			return;
		}

		app::appendMidiMenu(menu, &module->midiInput);
		menu->addChild(createMenuLabel("Input channel is forced to All"));
		menu->addChild(createMenuLabel("Channels shown as MIDI 1-16"));

		std::vector<std::string> chLabels;
		chLabels.reserve(16);
		for (int i = 1; i <= 16; i++)
			chLabels.push_back(string::f("%d", i));
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("PROGRAM controls channel",
			chLabels,
			[=]() { return module->core.controlChannel; },
			[=](int i) {
				if (i != module->core.sliderChannel)
					module->core.controlChannel = i;
			}));
		menu->addChild(createIndexSubmenuItem("Stage sliders channel",
			chLabels,
			[=]() { return module->core.sliderChannel; },
			[=](int i) {
				if (i != module->core.controlChannel)
					module->core.sliderChannel = i;
			}));
		menu->addChild(createBoolPtrMenuItem("Move stage sliders with CC", "",
			&module->core.moveStageSliders));

		menu->addChild(createMenuLabel("HEAD channels fixed: 1-8"));
		menu->addChild(createMenuLabel("HEAD ALL channel fixed: 9 (CC 0-12)"));
		menu->addChild(createMenuLabel("PROGRAM and stage channels must differ"));
		menu->addChild(createMenuLabel("CC maps fixed; no base offset"));
		if (module->core.lastStatus >= 0) {
			std::string last = module->core.lastChannel >= 0 ?
				string::f("Last: ch %d, %s %d, value %d -> %s",
					module->core.lastChannel + 1,
					((module->core.lastStatus >> 4) == 0xC) ? "PC" : "CC",
					module->core.lastNumber,
					module->core.lastValue,
					spacetime::MidiCore::routeName(module->core.lastRoute)) :
				string::f("Last: status 0x%02x -> %s",
					module->core.lastStatus,
					spacetime::MidiCore::routeName(module->core.lastRoute));
			menu->addChild(createMenuLabel(last));
			menu->addChild(createMenuLabel(string::f("PROGRAM event seq: %u",
				module->core.programEventSeq)));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI output"));
		app::appendMidiMenu(menu, &module->midiOutput);

		std::vector<std::string> modeLabels = {"Off", "Notes", "CC 7-bit"};
		std::vector<std::string> gateLabels = {"Pulse 1", "Pulse 2", "ALL"};
		std::vector<std::string> ccLabels;
		ccLabels.reserve(128);
		for (int i = 0; i < 128; i++)
			ccLabels.push_back(string::f("%d", i));

		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			int hh = h;
			menu->addChild(createSubmenuItem(string::f("HEAD %d output", h + 1),
				modeLabels[module->core.outLane[h].mode],
				[=](Menu* sub) {
					sub->addChild(createIndexSubmenuItem("Mode",
						modeLabels,
						[=]() { return (size_t)module->core.outLane[hh].mode; },
						[=](size_t i) { module->core.outLane[hh].mode = (uint8_t)i; }));
					sub->addChild(createIndexSubmenuItem("Channel",
						chLabels,
						[=]() { return (size_t)module->core.outLane[hh].channel; },
						[=](size_t i) { module->core.outLane[hh].channel = (uint8_t)i; }));
					sub->addChild(createIndexSubmenuItem("Note gate source",
						gateLabels,
						[=]() { return (size_t)module->core.outLane[hh].gateSource; },
						[=](size_t i) { module->core.outLane[hh].gateSource = (uint8_t)i; }));
					sub->addChild(createIndexSubmenuItem("CC number",
						ccLabels,
						[=]() { return (size_t)module->core.outLane[hh].cc; },
						[=](size_t i) { module->core.outLane[hh].cc = (uint8_t)i; }));
					sub->addChild(createMenuLabel("Notes require quantized stages"));
				}));
		}
		menu->addChild(createMenuLabel(string::f("Output notes/CCs: %u/%u",
			module->core.outNoteCount,
			module->core.outCcCount)));
	}
};

Model* modelMidi = createModel<Midi, MidiWidget>("Midi");
