#include "plugin.hpp"
#include "paneltheme.hpp"
#include "ChainAdapter.hpp"
#include <app/MidiDisplay.hpp>

// ============================================================================
// MIDI — WP8b.
// Incoming controller MIDI: fixed PROGRAM/stage CC map, per-head CC maps on
// channels 1-8, global MIDI clock/transport counters. Outgoing MIDI: per-head
// note or CC lanes derived from the merged head status stream.
// ============================================================================

namespace LayoutM {
constexpr float CX = 10.16f;
constexpr float ACT_Y0 = 25.0f;
constexpr float ACT_PITCH = 9.0f;
constexpr float OUT_Y = 116.0f;
} // namespace LayoutM

struct Midi : Module {
	enum OutMode {
		OUT_OFF = 0,
		OUT_NOTES,
		OUT_CC7
	};
	enum GateSource {
		GATE_P1 = 0,
		GATE_P2,
		GATE_ALL
	};

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
	int controlChannel = 15;  // 0..15 shown as 1..16

	spacetime::MessagePort<spacetime::AnchorToHeadsMsg> rightPort;
	spacetime::MessagePort<spacetime::HeadsToAnchorMsg> leftPort;

	uint32_t midiClockSeq = 0, midiStartSeq = 0, midiStopSeq = 0, midiContinueSeq = 0;
	uint32_t headCcSeq[spacetime::kMaxHeads][spacetime::kMidiHeadControls] = {};
	float headCcValue[spacetime::kMaxHeads][spacetime::kMidiHeadControls] = {};

	spacetime::MidiProgramEvent programEvents[spacetime::kMaxMidiProgramEvents];
	uint8_t programEventCount = 0;
	uint32_t programEventSeq = 0;

	int lastStatus = -1;
	int lastChannel = -1;
	int lastNumber = -1;
	int lastValue = -1;
	std::string lastRoute = "none";

	uint8_t outMode[spacetime::kMaxHeads] = {};
	uint8_t outChannel[spacetime::kMaxHeads] = {};
	uint8_t outGateSource[spacetime::kMaxHeads] = {};
	uint8_t outCc[spacetime::kMaxHeads] = {};
	bool prevGate[spacetime::kMaxHeads] = {};
	bool noteActive[spacetime::kMaxHeads] = {};
	uint8_t activeNote[spacetime::kMaxHeads] = {};
	float noteTimer[spacetime::kMaxHeads] = {};
	float ccTimer[spacetime::kMaxHeads] = {};
	int lastCcValue[spacetime::kMaxHeads];
	uint32_t outNoteCount = 0;
	uint32_t outCcCount = 0;

	float inLight = 0.f, clkLight = 0.f, outLight = 0.f;

	Midi() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configLight(IN_LIGHT, "MIDI input activity");
		configLight(CLK_LIGHT, "MIDI clock activity");
		configLight(OUT_LIGHT, "MIDI output activity");
		midiInput.channel = -1;  // channel routing is SpaceTime-specific
		midiOutput.channel = -1; // SpaceTime routes per-head output channels
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			outChannel[h] = h;
			outGateSource[h] = GATE_ALL;
			outCc[h] = (uint8_t)(20 + h);
			lastCcValue[h] = -1;
		}
		rightPort.attach(rightExpander);
		leftPort.attach(leftExpander);
	}

	void pushProgramEvent(uint8_t type, uint8_t index, uint8_t value, float fvalue = 0.f) {
		if (programEventCount >= spacetime::kMaxMidiProgramEvents) {
			for (int i = 1; i < spacetime::kMaxMidiProgramEvents; i++)
				programEvents[i - 1] = programEvents[i];
			programEventCount = spacetime::kMaxMidiProgramEvents - 1;
		}
		spacetime::MidiProgramEvent& ev = programEvents[programEventCount++];
		ev.seq = ++programEventSeq;
		ev.type = type;
		ev.index = index;
		ev.value = value;
		ev.fvalue = fvalue;
	}

	static int scaleIndex(uint8_t value, int maxIndex) {
		int i = (int)std::floor(((float)value / 127.f) * (float)(maxIndex + 1));
		if (i < 0) i = 0;
		if (i > maxIndex) i = maxIndex;
		return i;
	}

	void handleProgramCc(uint8_t cc, uint8_t value) {
		using namespace spacetime;
		if (cc < 32) {
			pushProgramEvent(MIDI_PROG_SLIDER, cc, value, ((float)value / 127.f) * 10.f);
		}
		else if (cc < 64) {
			pushProgramEvent(MIDI_PROG_SLIDER, cc, value, (float)value / 127.f);
		}
		else if (cc == 64 && value >= 64) {
			pushProgramEvent(MIDI_PROG_SELECT_PREV, 0, value);
		}
		else if (cc == 65 && value >= 64) {
			pushProgramEvent(MIDI_PROG_SELECT_NEXT, 0, value);
		}
		else if (cc == 66 && value >= 64) {
			pushProgramEvent(MIDI_PROG_CLEAR, 0, value);
		}
		else if (cc == 67 && value >= 64) {
			pushProgramEvent(MIDI_PROG_BULK_ARM, 0, value);
		}
		else if ((cc >= 68 && cc <= 71) || (cc >= 77 && cc <= 81) ||
		         cc == 86 || cc == 87 || cc == 88) {
			pushProgramEvent(MIDI_PROG_GESTURE, cc, value);
		}
		else if (cc >= 72 && cc <= 76) {
			pushProgramEvent(MIDI_PROG_LIMITED, cc - 72, value);
		}
		else if (cc >= 82 && cc <= 85) {
			pushProgramEvent(MIDI_PROG_TIME_RANGE, cc - 82, value);
		}
	}

	void handleHeadCc(uint8_t channel, uint8_t cc, uint8_t value) {
		if (channel >= spacetime::kMaxHeads || cc >= spacetime::kMidiHeadControls)
			return;
		int h = channel;
		float v = 0.f;
		switch (cc) {
			case 0:  // virtual clock edge
			case 1:  // start
			case 2:  // stop
			case 3:  // advance
			case 4:  // reset
			case 13: // display
				if (value < 64)
					return;
				v = 1.f;
				break;
			case 5:  // address
				v = ((float)value / 127.f) * 10.f;
				break;
			case 6:  // address source
				v = value >= 64 ? 1.f : 0.f;
				break;
			case 7:  // address mode
				v = (float)scaleIndex(value, 2);
				break;
			case 8:  // direction
				v = (float)scaleIndex(value, 4);
				break;
			case 9:  // clock source: internal / jack / MIDI clock / virtual
				v = (float)scaleIndex(value, 3);
				break;
			case 10: // div/mult
				v = (float)scaleIndex(value, 8);
				break;
			case 11: // time CV amount
				v = ((float)value / 127.f) * 2.f - 1.f;
				break;
			case 12: // loop mode
				v = (float)scaleIndex(value, 2);
				break;
		}
		headCcValue[h][cc] = v;
		headCcSeq[h][cc]++;
	}

	void handleMessage(const midi::Message& msg) {
		if (msg.bytes.empty())
			return;
		uint8_t b0 = msg.bytes[0];
		inLight = 1.f;
		lastStatus = b0;
		lastChannel = -1;
		lastNumber = -1;
		lastValue = -1;
		lastRoute = "ignored";

		// System realtime: no channel.
		if (b0 == 0xF8) {
			midiClockSeq++;
			clkLight = 1.f;
			lastRoute = "clock";
			return;
		}
		if (b0 == 0xFA) {
			midiStartSeq++;
			lastRoute = "start";
			return;
		}
		if (b0 == 0xFB) {
			midiContinueSeq++;
			lastRoute = "continue";
			return;
		}
		if (b0 == 0xFC) {
			midiStopSeq++;
			lastRoute = "stop";
			return;
		}

		uint8_t status = msg.getStatus();
		uint8_t ch = msg.getChannel();
		lastChannel = ch;
		if (status == 0xB) {
			uint8_t cc = msg.getNote();
			uint8_t value = msg.getValue();
			lastNumber = cc;
			lastValue = value;
			bool toProgram = ch == controlChannel;
			bool toHead = ch < spacetime::kMaxHeads;
			if (toProgram)
				handleProgramCc(cc, value);
			if (toHead)
				handleHeadCc(ch, cc, value);
			lastRoute = toProgram && toHead ? "program+head" :
				(toProgram ? "program" : (toHead ? "head" : "ignored"));
		}
		else if (status == 0xC && ch == controlChannel) {
			int raw = msg.getNote();
			lastNumber = raw;
			lastValue = -1;
			int slot = -1;
			if (raw >= 1 && raw <= 12)
				slot = raw - 1;
			else if (raw == 0)
				slot = 0;
			if (slot >= 0) {
				pushProgramEvent(spacetime::MIDI_PROG_PRESET_LOAD, (uint8_t)slot, (uint8_t)raw);
				lastRoute = "program";
			}
		}
	}

	void injectMidi(spacetime::AnchorToHeadsMsg& msg) {
		msg.midiClockSeq = midiClockSeq;
		msg.midiStartSeq = midiStartSeq;
		msg.midiStopSeq = midiStopSeq;
		msg.midiContinueSeq = midiContinueSeq;
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			for (int c = 0; c < spacetime::kMidiHeadControls; c++) {
				msg.headCcSeq[h][c] = headCcSeq[h][c];
				msg.headCcValue[h][c] = headCcValue[h][c];
			}
		}
	}

	static uint8_t cvToNote(float cv) {
		int note = (int)std::round(cv * 12.f) + 60;
		return (uint8_t)clamp(note, 0, 127);
	}

	static uint8_t cvToCc(float cv) {
		int v = (int)std::round(clamp(cv, 0.f, 10.f) / 10.f * 127.f);
		return (uint8_t)clamp(v, 0, 127);
	}

	bool gateFor(const spacetime::HeadStatus& st, int source) const {
		if (source == GATE_P1)
			return st.pulse1 != 0;
		if (source == GATE_P2)
			return st.pulse2 != 0;
		return st.allPulse != 0;
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

	void sendNoteOff(int h) {
		if (!noteActive[h])
			return;
		sendMidi(0x8, outChannel[h], activeNote[h], 0);
		noteActive[h] = false;
	}

	void handleOutputStatus(const spacetime::HeadsToAnchorMsg& status, float dt) {
		bool present[spacetime::kMaxHeads] = {};
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			ccTimer[h] += dt;
			if (noteActive[h]) {
				noteTimer[h] -= dt;
				if (noteTimer[h] <= 0.f)
					sendNoteOff(h);
			}
		}

		for (int i = 0; i < status.headCount && i < spacetime::kMaxHeads; i++) {
			const spacetime::HeadStatus& st = status.status[i];
			int h = st.headId;
			if (h < 0 || h >= spacetime::kMaxHeads)
				continue;
			present[h] = true;

			if (outMode[h] == OUT_NOTES) {
				bool gate = gateFor(st, outGateSource[h]);
				bool rising = gate && !prevGate[h];
				bool falling = !gate && prevGate[h];
				prevGate[h] = gate;
				if (rising && st.quantized) {
					sendNoteOff(h);
					activeNote[h] = cvToNote(st.cv);
					sendMidi(0x9, outChannel[h], activeNote[h], 100);
					noteActive[h] = true;
					// P1/P2 notes normally end on gate fall; ALL is a trigger
					// lane, so give it a short, explicit note length.
					noteTimer[h] = outGateSource[h] == GATE_ALL ? 0.03f : 30.f;
					outNoteCount++;
				}
				if (falling && outGateSource[h] != GATE_ALL)
					sendNoteOff(h);
				if (st.runState == spacetime::RUN_STOPPED)
					sendNoteOff(h);
			}
			else {
				prevGate[h] = gateFor(st, outGateSource[h]);
				sendNoteOff(h);
			}

			if (outMode[h] == OUT_CC7) {
				int v = cvToCc(st.cv);
				if (v != lastCcValue[h] && ccTimer[h] >= 0.003f) {
					sendMidi(0xB, outChannel[h], outCc[h], (uint8_t)v);
					lastCcValue[h] = v;
					ccTimer[h] = 0.f;
					outCcCount++;
				}
			}
		}

		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			if (!present[h]) {
				prevGate[h] = false;
				sendNoteOff(h);
			}
		}
	}

	void process(const ProcessArgs& args) override {
		midiInput.channel = -1;  // SpaceTime routes channels internally
		midiOutput.channel = -1;
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame))
			handleMessage(msg);

		bool rightIsProgram = spacetime::modelIs(rightExpander.module, modelProgram);
		bool leftIsHead = spacetime::modelIs(leftExpander.module, modelHead);

		const spacetime::AnchorToHeadsMsg* fromProgram = rightPort.consume(rightExpander);
		bool broadcastValid = rightIsProgram && fromProgram->valid;
		if (leftIsHead) {
			spacetime::AnchorToHeadsMsg* toHead =
				spacetime::leftNeighborProducer<spacetime::AnchorToHeadsMsg>(this, modelHead);
			if (toHead) {
				if (broadcastValid) {
					*toHead = *fromProgram;
					injectMidi(*toHead);
				}
				else {
					toHead->valid = false;
				}
				spacetime::flipLeftNeighbor(this);
			}
		}

		if (rightIsProgram) {
			spacetime::HeadsToAnchorMsg* toProgram =
				spacetime::rightNeighborProducer<spacetime::HeadsToAnchorMsg>(this, modelProgram);
			if (toProgram) {
				const spacetime::HeadsToAnchorMsg* fromHead = leftPort.consume(leftExpander);
				bool statusValid = leftIsHead && fromHead->valid;
				if (statusValid)
					handleOutputStatus(*fromHead, args.sampleTime);
				if (statusValid) {
					*toProgram = *fromHead;
				}
				else {
					*toProgram = spacetime::HeadsToAnchorMsg();
					toProgram->valid = true;
				}
				toProgram->midiEventCount = programEventCount;
				toProgram->midiEventSeq = programEventSeq;
				for (int i = 0; i < programEventCount && i < spacetime::kMaxMidiProgramEvents; i++)
					toProgram->midiEvents[i] = programEvents[i];
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
		json_object_set_new(root, "controlChannel", json_integer(controlChannel));
		json_object_set_new(root, "midiInput", midiInput.toJson());
		json_object_set_new(root, "midiOutput", midiOutput.toJson());
		json_t* lanes = json_array();
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			json_t* lane = json_object();
			json_object_set_new(lane, "mode", json_integer(outMode[h]));
			json_object_set_new(lane, "channel", json_integer(outChannel[h]));
			json_object_set_new(lane, "gate", json_integer(outGateSource[h]));
			json_object_set_new(lane, "cc", json_integer(outCc[h]));
			json_array_append_new(lanes, lane);
		}
		json_object_set_new(root, "outLanes", lanes);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "controlChannel")))
			controlChannel = clamp((int)json_integer_value(j), 0, 15);
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
					outMode[h] = (uint8_t)clamp((int)json_integer_value(k), 0, 2);
				if ((k = json_object_get(lane, "channel")))
					outChannel[h] = (uint8_t)clamp((int)json_integer_value(k), 0, 15);
				if ((k = json_object_get(lane, "gate")))
					outGateSource[h] = (uint8_t)clamp((int)json_integer_value(k), 0, 2);
				if ((k = json_object_get(lane, "cc")))
					outCc[h] = (uint8_t)clamp((int)json_integer_value(k), 0, 127);
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
		std::string text = module ? string::f("CH%02d", module->controlChannel + 1) : "CH16";
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 11.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xc1, 0x3c, 0x36));
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
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
		menu->addChild(createIndexSubmenuItem("PROGRAM/stage channel",
			chLabels,
			[=]() { return module->controlChannel; },
			[=](int i) { module->controlChannel = i; }));

		menu->addChild(createMenuLabel("HEAD channels fixed: 1-8"));
		menu->addChild(createMenuLabel("CC maps fixed; no base offset"));
		if (module->lastStatus >= 0) {
			std::string last = module->lastChannel >= 0 ?
				string::f("Last: ch %d, %s %d, value %d -> %s",
					module->lastChannel + 1,
					((module->lastStatus >> 4) == 0xC) ? "PC" : "CC",
					module->lastNumber,
					module->lastValue,
					module->lastRoute.c_str()) :
				string::f("Last: status 0x%02x -> %s",
					module->lastStatus,
					module->lastRoute.c_str());
			menu->addChild(createMenuLabel(last));
			menu->addChild(createMenuLabel(string::f("PROGRAM event seq: %u",
				module->programEventSeq)));
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
				modeLabels[module->outMode[h]],
				[=](Menu* sub) {
					sub->addChild(createIndexSubmenuItem("Mode",
						modeLabels,
						[=]() { return (size_t)module->outMode[hh]; },
						[=](size_t i) { module->outMode[hh] = (uint8_t)i; }));
					sub->addChild(createIndexSubmenuItem("Channel",
						chLabels,
						[=]() { return (size_t)module->outChannel[hh]; },
						[=](size_t i) { module->outChannel[hh] = (uint8_t)i; }));
					sub->addChild(createIndexSubmenuItem("Note gate source",
						gateLabels,
						[=]() { return (size_t)module->outGateSource[hh]; },
						[=](size_t i) { module->outGateSource[hh] = (uint8_t)i; }));
					sub->addChild(createIndexSubmenuItem("CC number",
						ccLabels,
						[=]() { return (size_t)module->outCc[hh]; },
						[=](size_t i) { module->outCc[hh] = (uint8_t)i; }));
					sub->addChild(createMenuLabel("Notes require quantized stages"));
				}));
		}
		menu->addChild(createMenuLabel(string::f("Output notes/CCs: %u/%u",
			module->outNoteCount,
			module->outCcCount)));
	}
};

Model* modelMidi = createModel<Midi, MidiWidget>("Midi");
