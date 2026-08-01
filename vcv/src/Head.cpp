#include "plugin.hpp"
#include "paneltheme.hpp"
#include "spacetime_widgets.hpp"
#include "ChainAdapter.hpp"
#include "HeadDSP.hpp"

// ============================================================================
// HEAD — WP7 integrated.
// One Function Generator (dsp/HeadDSP.hpp). The DSP ticks at AUDIO rate
// (slew, REF ramp and pulses are sample-accurate, per spec); expander
// traffic and lights run at control rate (÷16).
//
// All positions in mm; MUST match res/Head.svg.
// ============================================================================

namespace LayoutH {
constexpr float CX = 25.4f;
constexpr float BTN_X0 = 9.4f, BTN_PITCH = 11.f, BTN_Y = 21.5f;
constexpr float STATUS_X0 = 17.4f, STATUS_PITCH = 8.f, STATUS_Y = 30.5f;
constexpr float ADDR_Y = 45.f;
constexpr float ADDR_KNOB_X = 10.f, ADDR_SRC_X = 25.4f, ADDR_MODE_X = 41.f;
constexpr float PLAY_Y = 64.5f;
constexpr float DIR_X = 10.f, CLKSRC_X = 25.4f, CLKDIV_X = 41.f;
constexpr float PLAY2_Y = 78.f;
constexpr float TIMECV_X = 10.f, LOOP_X = 25.4f, HEADID_X = 41.f;
constexpr float JACK_X0 = 9.4f, JACK_PITCH = 11.f;
constexpr float IN1_Y = 89.f, IN2_Y = 98.5f, OUT1_Y = 110.f, OUT2_Y = 119.5f;
} // namespace LayoutH

struct Head : Module {
	enum ParamId {
		START_PARAM,
		STOP_PARAM,
		ADVANCE_PARAM,
		RESET_PARAM,
		ADDRESS_PARAM,
		ADDR_SOURCE_PARAM,
		ADDR_MODE_PARAM,
		DIRECTION_PARAM,
		CLK_SOURCE_PARAM,
		CLK_DIV_PARAM,
		TIMECV_PARAM,
		LOOP_PARAM,
		DISPLAY_PARAM,  // appended (patch compatibility)
		PARAMS_LEN
	};
	enum InputId {
		START_INPUT,
		STOP_INPUT,
		ADVANCE_INPUT,
		STROBE_INPUT,
		ADDRESS_INPUT,
		CLK_INPUT,
		TIMECV_INPUT,
		RESET_INPUT,  // appended (patch compatibility)
		INPUTS_LEN
	};
	enum OutputId {
		CV_OUTPUT,
		TIME_OUTPUT,
		REF_OUTPUT,
		ALL_OUTPUT,
		PULSE1_OUTPUT,
		PULSE2_OUTPUT,
		EOC_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RUN_LIGHT,
		HOLD_LIGHT,
		STOPPED_LIGHT,
		ENUMS(HEADID_LIGHT, 3),
		DISPLAY_LIGHT,  // appended (patch compatibility)
		LIGHTS_LEN
	};

	spacetime::HeadDSP dsp;
	spacetime::HeadOut out;
	spacetime::HeadConfig cfg;

	// Latest broadcast from the anchor side (copied at control rate).
	spacetime::StageTable table;
	spacetime::ExtInputs ext;
	spacetime::Globals globals;
	spacetime::ScaleKey scaleKey;
	int headId = 0;
	bool chainOk = false;

	spacetime::MessagePort<spacetime::AnchorToHeadsMsg> rightPort;  // broadcast in
	spacetime::MessagePort<spacetime::HeadsToAnchorMsg> leftPort;   // statuses in

	rack::dsp::BooleanTrigger resetTrigger, resetInputTrigger, strobeDownTrigger, displayTrigger;
	rack::dsp::BooleanTrigger feedbackAdvanceTrigger;
	bool resetPending = false, strobePending = false;
	bool displayLatch = false;
	bool displayClaimPending = false;
	uint32_t lastCancelSeq = 0;
	uint32_t lastMidiClockSeq = 0, lastMidiStartSeq = 0, lastMidiStopSeq = 0, lastMidiContinueSeq = 0;
	uint32_t lastHeadCcSeq[spacetime::kMidiHeadControls] = {};
	uint32_t lastHeadAllCcSeq[spacetime::kHeadAllControls] = {};
	uint32_t lastHeadAllClockSeq = 0;
	float midiClockTimer = 0.f, virtualClockTimer = 0.f, headAllClockTimer = 0.f;
	float midiStartTimer = 0.f, midiStopTimer = 0.f, midiAdvanceTimer = 0.f;
	float headAllStartGate = 0.f, headAllAddressCv = 0.f, headAllTimeCv = 0.f;
	bool headAllAddressConnected = false, headAllTimeConnected = false;
	bool lastHeadAllValid = false;
	bool followMidiTransport = false;
	int lastMidiCc = -1;
	float lastMidiCcValue = 0.f;
	uint32_t lastAppliedHeadCcSeq = 0;
	uint32_t feedbackAdvanceSeq = 0, feedbackResetSeq = 0;
	rack::dsp::ClockDivider divider;

	Head() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configButton(START_PARAM, "Start");
		configButton(STOP_PARAM, "Stop");
		configButton(ADVANCE_PARAM, "Advance (force next stage)");
		configButton(RESET_PARAM, "Reset to First stage");
		configParam(ADDRESS_PARAM, 0.f, 10.f, 0.f, "Address", " V");
		configSwitch(ADDR_SOURCE_PARAM, 0.f, 1.f, 0.f, "Address source",
			{"Internal (knob)", "External (ADDRESS CV)"});
		configSwitch(ADDR_MODE_PARAM, 0.f, 2.f, 1.f,
			"Addressing mode (up/center latch, down is momentary)",
			{"Strobe (momentary: load stage at address)", "Sequential",
			 "Continuous (address sweeps stages)"});
		configSwitch(DIRECTION_PARAM, 0.f, 4.f, 0.f, "Direction",
			{"Forward", "Reverse", "Pendulum", "Random", "Brownian"});
		configSwitch(CLK_SOURCE_PARAM, 0.f, 3.f, 0.f, "Clock source",
			{"Internal (per-stage times)", "External CLK input", "MIDI clock", "Virtual clock"});
		configSwitch(CLK_DIV_PARAM, 0.f, 8.f, 4.f, "Clock div/mult",
			{"/16", "/8", "/4", "/2", "×1", "×2", "×4", "×8", "×16"});
		configParam(TIMECV_PARAM, -1.f, 1.f, 0.f, "Time CV amount", "%", 0.f, 100.f);
		configSwitch(LOOP_PARAM, 0.f, 2.f, 1.f, "Loop mode",
			{"One-shot", "Cycle First–Last", "Full chain"});
		configButton(DISPLAY_PARAM,
			"Display (programming LEDs follow this head; press again or scroll to release)");

		configInput(START_INPUT, "Start (pulse; Sustain gate / Enable >5 V)");
		configInput(STOP_INPUT, "Stop (pulse)");
		configInput(ADVANCE_INPUT, "Advance (pulse)");
		configInput(STROBE_INPUT, "Strobe (pulse)");
		configInput(ADDRESS_INPUT, "Address CV");
		configInput(CLK_INPUT, "External clock");
		configInput(TIMECV_INPUT, "Time CV");
		configInput(RESET_INPUT, "Reset to First stage (gate/trigger)");

		configOutput(CV_OUTPUT, "Stage CV (1 V/oct when quantized)");
		configOutput(TIME_OUTPUT, "Time (current stage's time slider)");
		configOutput(REF_OUTPUT, "Reference ramp (downward, spans interval)");
		configOutput(ALL_OUTPUT, "All pulse (every new stage)");
		configOutput(PULSE1_OUTPUT, "Pulse 1");
		configOutput(PULSE2_OUTPUT, "Pulse 2");
		configOutput(EOC_OUTPUT, "End of cycle");

		configLight(RUN_LIGHT, "Run");
		configLight(HOLD_LIGHT, "Hold (Sustain/Enable waiting)");
		configLight(STOPPED_LIGHT, "Stopped");
		configLight(HEADID_LIGHT, "Head index colour");
		configLight(DISPLAY_LIGHT, "Display selected");

		rightPort.attach(rightExpander);
		leftPort.attach(leftExpander);
		divider.setDivision(16);
	}

	void onReset() override {
		dsp.reset(1);
	}

	static float pulseGate(float& timer, float dt) {
		float v = timer > 0.f ? 10.f : 0.f;
		timer = std::fmax(0.f, timer - dt);
		return v;
	}

	void applyHeadCc(int cc, float value) {
		lastMidiCc = cc;
		lastMidiCcValue = value;
		switch (cc) {
			case 0: virtualClockTimer = 1e-3f; break;
			case 1: midiStartTimer = 1e-3f; break;
			case 2: midiStopTimer = 1e-3f; break;
			case 3: midiAdvanceTimer = 1e-3f; break;
			case 4: resetPending = true; break;
			case 5: params[ADDRESS_PARAM].setValue(clamp(value, 0.f, 10.f)); break;
			case 6: params[ADDR_SOURCE_PARAM].setValue(value >= 0.5f ? 1.f : 0.f); break;
			case 7: {
				int mode = clamp((int)std::round(value), 0, 2);
				if (mode == 0)
					strobePending = true;
				else
					params[ADDR_MODE_PARAM].setValue((float)mode);
				break;
			}
			case 8: params[DIRECTION_PARAM].setValue((float)clamp((int)std::round(value), 0, 4)); break;
			case 9: params[CLK_SOURCE_PARAM].setValue((float)clamp((int)std::round(value), 0, 3)); break;
			case 10: params[CLK_DIV_PARAM].setValue((float)clamp((int)std::round(value), 0, 8)); break;
			case 11: params[TIMECV_PARAM].setValue(clamp(value, -1.f, 1.f)); break;
			case 12: params[LOOP_PARAM].setValue((float)clamp((int)std::round(value), 0, 2)); break;
			case 13: displayLatch = !displayLatch; break;
		}
	}

	void process(const ProcessArgs& args) override {
		using namespace spacetime;

		bool ctrl = divider.process();
		if (ctrl) {
			float dt = args.sampleTime * divider.getDivision();
			bool midiDisplayToggle = false;
			bool leftIsStatusSource = modelIs(leftExpander.module, modelHead, modelHeadAll,
				modelGlueLeft);
			const HeadsToAnchorMsg* lm = leftPort.consume(leftExpander);

			// ---- Broadcast in (anchor side)
			bool rightIsChain = modelIs(rightExpander.module, modelHead, modelProgram, modelGlueRight) ||
				modelIs(rightExpander.module, modelMidi);
			const AnchorToHeadsMsg* bm = rightPort.consume(rightExpander);
			chainOk = rightIsChain && bm->valid;
			if (chainOk) {
				table = bm->table;
				for (int i = 0; i < 4; i++) {
					ext.v[i] = bm->ext[i];
					ext.connected[i] = bm->extConnected[i];
				}
				globals = bm->globals;
				scaleKey = bm->scaleKey;
				headId = bm->hopIndex;
				if (bm->midiClockSeq != lastMidiClockSeq) {
					lastMidiClockSeq = bm->midiClockSeq;
					midiClockTimer = 1e-3f;
				}
				bool midiStartChanged = bm->midiStartSeq != lastMidiStartSeq;
				bool midiContinueChanged = bm->midiContinueSeq != lastMidiContinueSeq;
				bool midiStopChanged = bm->midiStopSeq != lastMidiStopSeq;
				lastMidiStartSeq = bm->midiStartSeq;
				lastMidiContinueSeq = bm->midiContinueSeq;
				lastMidiStopSeq = bm->midiStopSeq;
				if (followMidiTransport && midiStartChanged) {
					midiStartTimer = 1e-3f;
				}
				if (followMidiTransport && midiContinueChanged) {
					midiStartTimer = 1e-3f;
				}
				if (followMidiTransport && midiStopChanged) {
					midiStopTimer = 1e-3f;
				}
				if (headId >= 0 && headId < kMaxHeads) {
					for (int c = 0; c < kMidiHeadControls; c++) {
						uint32_t seq = bm->headCcSeq[headId][c];
						if (seq != lastHeadCcSeq[c]) {
							lastHeadCcSeq[c] = seq;
							lastAppliedHeadCcSeq = seq;
							if (c == 13)
								midiDisplayToggle = true;
							else
								applyHeadCc(c, bm->headCcValue[headId][c]);
						}
					}
				}
				// Display arbitration: another owner or a scroll-cancel unlatches.
				if (bm->displayCancelSeq != lastCancelSeq) {
					lastCancelSeq = bm->displayCancelSeq;
					displayLatch = false;
					displayClaimPending = false;
				}
				if (bm->displayOwner == (uint8_t)headId)
					displayClaimPending = false;
				else if (bm->displayOwner != 0xFF && !displayClaimPending)
					displayLatch = false;
			}
			else {
				table.count = 0;
				displayLatch = false;
				displayClaimPending = false;
			}

			// ---- Common controls from HEAD ALL (far-left source)
			bool headAllValid = leftIsStatusSource && lm->valid && lm->headAll.valid;
			if (headAllValid) {
				for (int c = 0; c < kHeadAllControls; c++) {
					uint32_t seq = lm->headAll.controlSeq[c];
					if (!lastHeadAllValid || seq != lastHeadAllCcSeq[c]) {
						lastHeadAllCcSeq[c] = seq;
						applyHeadCc(c, lm->headAll.controlValue[c]);
					}
				}
				if (lm->headAll.externalClockSeq != lastHeadAllClockSeq) {
					lastHeadAllClockSeq = lm->headAll.externalClockSeq;
					headAllClockTimer = 1e-3f;
				}
				headAllStartGate = lm->headAll.startGate;
				headAllAddressCv = lm->headAll.addressCv;
				headAllTimeCv = lm->headAll.timeCv;
				headAllAddressConnected = lm->headAll.addressConnected;
				headAllTimeConnected = lm->headAll.timeConnected;
				lastHeadAllValid = true;
			}
			else {
				headAllStartGate = 0.f;
				headAllAddressConnected = false;
				headAllTimeConnected = false;
				lastHeadAllValid = false;
			}
			// Apply MIDI Display after arbitration, matching the panel button.
			// Otherwise the previous owner's broadcast clears a new MIDI claim
			// before this head can report it back to PROGRAM.
			if (midiDisplayToggle) {
				displayLatch = !displayLatch;
				displayClaimPending = displayLatch;
			}
			if (displayTrigger.process(params[DISPLAY_PARAM].getValue() > 0.5f)) {
				displayLatch = !displayLatch;
				displayClaimPending = displayLatch;
			}

			// ---- Config from panel
			cfg.continuous = params[ADDR_MODE_PARAM].getValue() > 1.5f;
			cfg.addrExt = params[ADDR_SOURCE_PARAM].getValue() > 0.5f;
			cfg.addressKnob = params[ADDRESS_PARAM].getValue();
			cfg.direction = (uint8_t)std::round(params[DIRECTION_PARAM].getValue());
			int clockSource = clamp((int)std::round(params[CLK_SOURCE_PARAM].getValue()), 0, 3);
			cfg.clkExt = clockSource != 0;
			cfg.clkDivIndex = (uint8_t)std::round(params[CLK_DIV_PARAM].getValue());
			cfg.timeCvAmount = params[TIMECV_PARAM].getValue();
			cfg.loopMode = (uint8_t)std::round(params[LOOP_PARAM].getValue());

			if (resetTrigger.process(params[RESET_PARAM].getValue() > 0.5f))
				resetPending = true;
			// Momentary-down on the addressing switch fires one strobe.
			if (strobeDownTrigger.process(params[ADDR_MODE_PARAM].getValue() < 0.5f))
				strobePending = true;

			// ---- Relay the broadcast to the next head (leftward)
			if (modelIs(leftExpander.module, modelHead, modelGlueLeft, modelHeadAll)) {
				AnchorToHeadsMsg* lo = leftNeighborProducer<AnchorToHeadsMsg>(
					this, modelHead, modelGlueLeft, modelHeadAll);
				if (lo) {
					if (chainOk)
						headRelayLeft(*bm, *lo);
					else
						lo->valid = false;
					flipLeftNeighbor(this);
				}
			}

			// ---- Status out (rightward, merged with further heads)
			bool rightTakesStatus = modelIs(rightExpander.module, modelHead, modelProgram, modelGlueRight) ||
				modelIs(rightExpander.module, modelMidi);
			if (rightTakesStatus) {
				HeadsToAnchorMsg* ro = rightNeighborProducer<HeadsToAnchorMsg>(
					this, modelHead, modelProgram, modelGlueRight);
				if (!ro)
					ro = rightNeighborProducer<HeadsToAnchorMsg>(this, modelMidi);
				if (ro) {
					HeadStatus own;
					own.headId = (uint8_t)headId;
					own.currentStage = out.currentStage;
					own.runState = out.runState;
					own.display = displayLatch ? 1 : 0;
					own.addressExternal = cfg.addrExt ? 1 : 0;
					own.addressMode = (uint8_t)clamp(
						(int)std::round(params[ADDR_MODE_PARAM].getValue()), 0, 2);
					own.direction = cfg.direction;
					own.clockSource = (uint8_t)clockSource;
					own.clockDivIndex = cfg.clkDivIndex;
					own.loopMode = cfg.loopMode;
					own.followMidiTransport = followMidiTransport ? 1 : 0;
					own.pulse1 = out.pulse1 ? 1 : 0;
					own.pulse2 = out.pulse2 ? 1 : 0;
					own.allPulse = out.allPulse ? 1 : 0;
					own.quantized = (out.currentStage < table.count &&
						table.program[out.currentStage].quantize()) ? 1 : 0;
					own.address = cfg.addressKnob;
					own.timeCvAmount = cfg.timeCvAmount;
					own.phase = out.phase;
					own.cv = out.cv;
					own.advanceSeq = feedbackAdvanceSeq;
					own.resetSeq = feedbackResetSeq;
					headRelayRight(own, (leftIsStatusSource && lm->valid) ? lm : NULL, *ro);
					flipRightNeighbor(this);
				}
			}

			// ---- Lights
			lights[RUN_LIGHT].setBrightnessSmooth(out.runState == RUN_RUNNING ? 1.f : 0.f, dt);
			lights[HOLD_LIGHT].setBrightnessSmooth(out.runState == RUN_HOLDING ? 1.f : 0.f, dt);
			lights[STOPPED_LIGHT].setBrightnessSmooth(out.runState == RUN_STOPPED ? 1.f : 0.f, dt);
			spacetime::setHeadDot(this, HEADID_LIGHT, headId & 7, chainOk ? 1.f : 0.f, dt);
			lights[DISPLAY_LIGHT].setBrightnessSmooth(displayLatch ? 1.f : 0.f, dt);
		}

		// ---- DSP at audio rate (sample-accurate slew/ramp/pulses)
		HeadSignals sig;
		float commonClockPulse = pulseGate(headAllClockTimer, args.sampleTime);
		sig.start = std::fmax(inputs[START_INPUT].getVoltage(),
			params[START_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		sig.start = std::fmax(sig.start, pulseGate(midiStartTimer, args.sampleTime));
		sig.start = std::fmax(sig.start, headAllStartGate);
		sig.stop = std::fmax(inputs[STOP_INPUT].getVoltage(),
			params[STOP_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		sig.stop = std::fmax(sig.stop, pulseGate(midiStopTimer, args.sampleTime));
		sig.advance = std::fmax(inputs[ADVANCE_INPUT].getVoltage(),
			params[ADVANCE_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		sig.advance = std::fmax(sig.advance, pulseGate(midiAdvanceTimer, args.sampleTime));
		sig.strobe = std::fmax(inputs[STROBE_INPUT].getVoltage(),
			strobePending ? 10.f : 0.f);
		sig.addressCv = inputs[ADDRESS_INPUT].isConnected() ?
			inputs[ADDRESS_INPUT].getVoltage() :
			(headAllAddressConnected ? headAllAddressCv : 0.f);
		int clockSource = clamp((int)std::round(params[CLK_SOURCE_PARAM].getValue()), 0, 3);
		if (clockSource == 2)
			sig.extClock = pulseGate(midiClockTimer, args.sampleTime);
		else if (clockSource == 3)
			sig.extClock = pulseGate(virtualClockTimer, args.sampleTime);
		else if (clockSource == 1)
			sig.extClock = inputs[CLK_INPUT].isConnected() ?
				inputs[CLK_INPUT].getVoltage() : commonClockPulse;
		else
			sig.extClock = inputs[CLK_INPUT].getVoltage();
		sig.timeCv = inputs[TIMECV_INPUT].isConnected() ?
			inputs[TIMECV_INPUT].getVoltage() :
			(headAllTimeConnected ? headAllTimeCv : 0.f);
		sig.reset = resetPending ||
			resetInputTrigger.process(inputs[RESET_INPUT].getVoltage() >= 1.f);
		if (feedbackAdvanceTrigger.process(sig.advance >= 1.f))
			feedbackAdvanceSeq++;
		if (sig.reset)
			feedbackResetSeq++;
		resetPending = false;
		strobePending = false;

		dsp.tick(table, ext, globals, scaleKey, cfg, sig, args.sampleTime, out);

		outputs[CV_OUTPUT].setVoltage(out.cv);
		outputs[TIME_OUTPUT].setVoltage(out.timeOut);
		outputs[REF_OUTPUT].setVoltage(out.ref);
		outputs[ALL_OUTPUT].setVoltage(out.allPulse ? 10.f : 0.f);
		outputs[PULSE1_OUTPUT].setVoltage(out.pulse1 ? 10.f : 0.f);
		outputs[PULSE2_OUTPUT].setVoltage(out.pulse2 ? 10.f : 0.f);
		outputs[EOC_OUTPUT].setVoltage(out.eoc ? 10.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "followMidiTransport", json_boolean(followMidiTransport));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		// Migration from the earlier MIDI Part I context-menu override. Values
		// 0..3 become the visible four-way clock-source switch; -1 meant "use
		// panel switch", so the saved Rack param already carries the setting.
		if ((j = json_object_get(root, "midiClockSource"))) {
			int src = clamp((int)json_integer_value(j), -1, 3);
			if (src >= 0)
				params[CLK_SOURCE_PARAM].setValue((float)src);
		}
		if ((j = json_object_get(root, "followMidiTransport")))
			followMidiTransport = json_boolean_value(j);
	}
};

struct HeadWidget : ModuleWidget {
	HeadWidget(Head* module) {
		using namespace LayoutH;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Head.svg")));

#ifndef METAMODULE
		spacetime::addTitle(this, CX, 5.6f, "Head");
		spacetime::addSubtitle(this, CX, 10.2f, "FUNCTION GENERATOR");

		spacetime::addSectionHeading(this, CX, 14.7f, "TRANSPORT");
		{
			static const char* btnNames[4] = {"START", "STOP", "ADV", "RST"};
			for (int i = 0; i < 4; i++)
				spacetime::addCvLabel(this, BTN_X0 + BTN_PITCH * i, 26.5f, btnNames[i]);
		}
		{
			static const char* statusNames[3] = {"RUN", "HOLD", "STOP"};
			for (int i = 0; i < 3; i++)
				spacetime::addMicroLabel(this, STATUS_X0 + STATUS_PITCH * i, 33.8f, statusNames[i]);
		}
		spacetime::addMicroLabel(this, 41.f, 33.8f, "DISP");

		spacetime::addSectionHeading(this, CX, 36.7f, "ADDRESS");
		spacetime::addCvLabel(this, ADDR_KNOB_X, 53.f, "ADDRESS");
		spacetime::addMicroLabel(this, ADDR_SRC_X, 40.f, "EXT");
		spacetime::addMicroLabel(this, ADDR_SRC_X, 50.6f, "INT");
		spacetime::addMicroLabel(this, ADDR_MODE_X + 5.f, 41.f, "CONT");
		spacetime::addMicroLabel(this, ADDR_MODE_X + 5.f, 45.f, "SEQ");
		spacetime::addMicroLabel(this, ADDR_MODE_X + 5.f, 49.f, "STRB");

		spacetime::addSectionHeading(this, CX, 56.2f, "PLAYBACK");
		spacetime::addCvLabel(this, DIR_X, 71.8f, "DIRECTION");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 58.6f, "VCLK");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 62.0f, "MIDI");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 65.4f, "CV");
		spacetime::addMicroLabel(this, CLKSRC_X + 5.4f, 68.8f, "INT");
		spacetime::addCvLabel(this, CLKDIV_X, 71.8f, "DIV / MULT");
		spacetime::addCvLabel(this, TIMECV_X, 83.8f, "TIME CV");
		spacetime::addMicroLabel(this, LOOP_X + 5.6f, 74.f, "ALL");
		spacetime::addMicroLabel(this, LOOP_X + 5.6f, 78.f, "F-L");
		spacetime::addMicroLabel(this, LOOP_X + 5.6f, 82.f, "1-SHOT");
		spacetime::addMicroLabel(this, HEADID_X, 83.8f, "HEAD");

		{
			static const char* in1[4] = {"START", "STOP", "ADV", "STRB"};
			static const char* in2[4] = {"ADDR", "CLK", "TIME", "RST"};
			static const char* out1[4] = {"CV", "TIME", "REF", "ALL"};
			static const char* out2[3] = {"P1", "P2", "EOC"};
			for (int i = 0; i < 4; i++)
				spacetime::addIoLabel(this, JACK_X0 + JACK_PITCH * i, IN1_Y + 4.6f, in1[i]);
			for (int i = 0; i < 4; i++)
				spacetime::addIoLabel(this, JACK_X0 + JACK_PITCH * i, IN2_Y + 4.6f, in2[i]);
			for (int i = 0; i < 4; i++)
				spacetime::addIoLabel(this, JACK_X0 + JACK_PITCH * i, OUT1_Y + 4.6f, out1[i]);
			for (int i = 0; i < 3; i++)
				spacetime::addIoLabel(this, JACK_X0 + JACK_PITCH * i, OUT2_Y + 4.6f, out2[i]);
		}
#endif

		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0, BTN_Y)), module, Head::START_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0 + BTN_PITCH, BTN_Y)), module, Head::STOP_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0 + 2 * BTN_PITCH, BTN_Y)), module, Head::ADVANCE_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(BTN_X0 + 3 * BTN_PITCH, BTN_Y)), module, Head::RESET_PARAM));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(STATUS_X0, STATUS_Y)), module, Head::RUN_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(STATUS_X0 + STATUS_PITCH, STATUS_Y)), module, Head::HOLD_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(STATUS_X0 + 2 * STATUS_PITCH, STATUS_Y)), module, Head::STOPPED_LIGHT));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(41.f, STATUS_Y)), module, Head::DISPLAY_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(41.f, STATUS_Y)), module, Head::DISPLAY_LIGHT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(ADDR_KNOB_X, ADDR_Y)), module, Head::ADDRESS_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(ADDR_SRC_X, ADDR_Y)), module, Head::ADDR_SOURCE_PARAM));
		addParam(createParamCentered<spacetime::LatchSpringSwitch3>(mm2px(Vec(ADDR_MODE_X, ADDR_Y)), module, Head::ADDR_MODE_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(DIR_X, PLAY_Y)), module, Head::DIRECTION_PARAM));
		addParam(createParamCentered<spacetime::Switch4>(mm2px(Vec(CLKSRC_X, PLAY_Y)), module, Head::CLK_SOURCE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(CLKDIV_X, PLAY_Y)), module, Head::CLK_DIV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(TIMECV_X, PLAY2_Y)), module, Head::TIMECV_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(LOOP_X, PLAY2_Y)), module, Head::LOOP_PARAM));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(HEADID_X, PLAY2_Y)), module, Head::HEADID_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0, IN1_Y)), module, Head::START_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + JACK_PITCH, IN1_Y)), module, Head::STOP_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 2 * JACK_PITCH, IN1_Y)), module, Head::ADVANCE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 3 * JACK_PITCH, IN1_Y)), module, Head::STROBE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0, IN2_Y)), module, Head::ADDRESS_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + JACK_PITCH, IN2_Y)), module, Head::CLK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 2 * JACK_PITCH, IN2_Y)), module, Head::TIMECV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 3 * JACK_PITCH, IN2_Y)), module, Head::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JACK_X0, OUT1_Y)), module, Head::CV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + JACK_PITCH, OUT1_Y)), module, Head::TIME_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 2 * JACK_PITCH, OUT1_Y)), module, Head::REF_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 3 * JACK_PITCH, OUT1_Y)), module, Head::ALL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JACK_X0, OUT2_Y)), module, Head::PULSE1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + JACK_PITCH, OUT2_Y)), module, Head::PULSE2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JACK_X0 + 2 * JACK_PITCH, OUT2_Y)), module, Head::EOC_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Head* module = getModule<Head>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI"));
		if (!module) {
			menu->addChild(createMenuLabel("Settings appear when module exists"));
			return;
		}
		menu->addChild(createMenuLabel(string::f("Head id/channel: %d",
			module->headId + 1)));
		menu->addChild(createMenuLabel(string::f("Last head CC: %d value %.3f seq %u",
			module->lastMidiCc,
			module->lastMidiCcValue,
			module->lastAppliedHeadCcSeq)));
		menu->addChild(createCheckMenuItem("Follow MIDI transport",
			"global Start/Stop/Continue",
			[=]() { return module->followMidiTransport; },
			[=]() { module->followMidiTransport = !module->followMidiTransport; }));
	}
};

Model* modelHead = createModel<Head, HeadWidget>("Head");
