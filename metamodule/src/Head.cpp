#include <rack.hpp>
#include <metamodule/VCVTextDisplay.hpp>

#include "TimingBus.hpp"
#include "RemoteBus.hpp"
#include "HeadRemoteController.hpp"

#include <algorithm>
#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

// All positions in mm; MUST match artwork/Head.svg.
namespace LayoutHR {
constexpr float IH_LABEL_Y = 46.0f, IH_KNOB_Y = 52.2f;
constexpr float IH_X0 = 20.f, IH_X1 = 61.f;
constexpr float DIV2_Y = 58.2f;
constexpr float TR_LABEL_Y = 62.0f, TR_BTN_Y = 68.2f;
constexpr float TR_X0 = 11.f, TR_PITCH = 20.f;
constexpr float DIV3_Y = 74.2f;
constexpr float CFG_LABEL_Y = 78.0f, CFG_CTRL_Y = 84.2f;
constexpr float CFG_X0 = 4.5f, CFG_PITCH = 10.4f;
constexpr float DIV4_Y = 90.2f;
constexpr float IN_HDR_Y = 94.4f, IN_LABEL_Y = 97.1f, IN_JACK_Y = 102.2f;
constexpr float IN_X0 = 4.5f, IN_PITCH = 10.4f;
constexpr float DIV5_Y = 108.2f;
constexpr float OUT_HDR_Y = 112.4f, OUT_LABEL_Y = 115.1f, OUT_JACK_Y = 120.2f;
constexpr float OUT_X0 = 6.f, OUT_PITCH = 11.7f;
} // namespace LayoutHR

namespace {

// EB8 (METAMODULE_EXPANDER_BUS_PLAN.md): head relocation. HeadRemote owns
// and runs its own HeadDSP (via dsp::HeadRemoteController) instead of Core
// computing every head internally -- Core stays fused for Program (table,
// presets, globals, MIDI ingestion) and broadcasts what a head needs
// (table/context/per-head MIDI CC + shared transport) over the EB8 buses;
// this module reads that broadcast, runs the head, and reports its output
// back for Core (or anyone else) to read.
//
// Same "LINK/WAIT/DUP" convention as TimingMonitor.cpp, applied to the
// table broadcast's freshness/coreCount rather than the timing bus's own --
// both ultimately key off the same underlying fact (MetaModuleTimingBusRegistry's
// coreCount for the chosen Instrument ID), since the EB8 registries
// deliberately have no ownership CAS of their own (see MetaModuleRemoteBus.hpp).
// A second, independent conflict is tracked alongside it: headRegistry's own
// per-head-slot ownership count, catching two HeadRemotes bound to the same
// head index -- the mirror image of Core.cpp's own coreConflict().
//
// Full VCV Head.cpp param/jack parity (2026-08-09, revised per Peet: "midi
// uses a 'to signal' translation... feed forward lock logic is sufficient").
// The earlier v1 of this module dropped ADDRESS/DIRECTION/CLK_DIV/LOOP knobs
// on the theory that MIDI CC and a physical knob would be two uncoordinated
// writers to HeadRemoteController's config_. That framing was wrong: it's
// the same situation Core.cpp's own STAGE_PARAM already solves, and Program/
// Head.cpp solve for every one of these exact fields -- a Param IS the
// single source of truth, MIDI writes into it, nothing needs a lock because
// process() is single-threaded and control-rate updates happen in a fixed
// order within one tick. `HeadRemoteController::config()`/`clockSource()`
// return mutable references/setters (not a Param, since dsp/ is Rack-free),
// so the mirror lives here instead: `mirrorContinuous`/`mirrorDiscrete`/
// `mirrorBool`/`mirrorClockSource` below compare each field's current value
// against a per-field shadow of "what we last synced the param to" --
// whichever side (MIDI-driven config, or the physical knob) changed since
// that shadow wins and is pushed to the other, every control tick. No lock,
// no polling loop, just a feed-forward compare-and-push, matching Peet's
// own description exactly.
struct SpaceTimeHead : Module {
	enum ParamId {
		INSTRUMENT_PARAM,
		HEAD_PARAM,
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
		RESET_INPUT,
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
		LINK_LIGHT,
		STATUS_DISPLAY,
		LIGHTS_LEN
	};

	enum class CoreLink { Waiting, Linked, Duplicate };

	spacetime::HeadRemoteController controller;
	spacetime::StageTable table;
	spacetime::ExtInputs ext;
	spacetime::Globals globals;
	spacetime::ScaleKey scaleKey;
	spacetime::HeadOut out;

	dsp::ClockDivider controlDivider;
	dsp::SchmittTrigger resetButtonTrigger;
	dsp::SchmittTrigger resetInputTrigger;
	dsp::SchmittTrigger addrModeStrobeTrigger;
	bool strobePending = false;

	uint32_t busToken = headRegistry.makeToken();
	int instrumentId = 0;
	int headIndex = 0;
	bool ownsHead = false;

	uint32_t lastTableHeartbeat = 0;
	float staleTime = 1.f;
	CoreLink coreLink = CoreLink::Waiting;
	bool headConflict = false;

	// Feed-forward mirror shadows: "what we last synced the Param to." See
	// the struct-level comment above for the reasoning.
	float shadowAddress = 0.f;
	float shadowAddrSource = 0.f;
	float shadowAddrMode = 1.f;
	float shadowDirection = 0.f;
	float shadowClkSource = 0.f;
	float shadowClkDiv = 4.f;
	float shadowTimeCv = 0.f;
	float shadowLoop = 1.f;

	SpaceTimeHead() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INSTRUMENT_PARAM, 0.f, 3.f, 0.f, "Instrument ID", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configParam(HEAD_PARAM, 0.f, 7.f, 0.f, "Head index", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configButton(START_PARAM, "Start");
		configButton(STOP_PARAM, "Stop");
		configButton(ADVANCE_PARAM, "Advance (force next stage)");
		configButton(RESET_PARAM, "Reset to first stage");
		configParam(ADDRESS_PARAM, 0.f, 10.f, 0.f, "Address", " V");
		configSwitch(ADDR_SOURCE_PARAM, 0.f, 1.f, 0.f, "Address source",
			{"Internal (knob)", "External (ADDRESS CV)"});
		configSwitch(ADDR_MODE_PARAM, 0.f, 2.f, 1.f,
			"Addressing mode (Strobe fires once; Sequential/Continuous latch)",
			{"Strobe (momentary: load stage at address)", "Sequential",
			 "Continuous (address sweeps stages)"});
		configSwitch(DIRECTION_PARAM, 0.f, 4.f, 0.f, "Direction",
			{"Forward", "Reverse", "Pendulum", "Random", "Brownian"});
		configSwitch(CLK_SOURCE_PARAM, 0.f, 3.f, 0.f, "Clock source",
			{"Internal (per-stage times)", "External CLK input", "MIDI clock", "Virtual clock"});
		configSwitch(CLK_DIV_PARAM, 0.f, 8.f, 4.f, "Clock div/mult",
			{"/16", "/8", "/4", "/2", "x1", "x2", "x4", "x8", "x16"});
		configParam(TIMECV_PARAM, -1.f, 1.f, 0.f, "Time CV amount", "%", 0.f, 100.f);
		configSwitch(LOOP_PARAM, 0.f, 2.f, 1.f, "Loop mode",
			{"One-shot", "Cycle first-last", "Full chain"});
		configInput(START_INPUT, "Start (pulse; Sustain gate / Enable >5 V)");
		configInput(STOP_INPUT, "Stop (pulse)");
		configInput(ADVANCE_INPUT, "Advance (pulse)");
		configInput(STROBE_INPUT, "Strobe (pulse)");
		configInput(ADDRESS_INPUT, "Address CV");
		configInput(CLK_INPUT, "External clock");
		configInput(TIMECV_INPUT, "Time CV");
		configInput(RESET_INPUT, "Reset to first stage (gate/trigger)");
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
		configLight(LINK_LIGHT, "Linked to Core, no conflicts");
		controlDivider.setDivision(16);
		ownsHead = headRegistry.registerHead(instrumentId, headIndex, busToken);
	}

	~SpaceTimeHead() override {
		headRegistry.unregisterHead(instrumentId, headIndex, busToken);
	}

	void process(const ProcessArgs& args) override {
		if (controlDivider.process())
			processControl(args.sampleTime * controlDivider.getDivision());

		spacetime::HeadSignals local;
		local.start = std::fmax(inputs[START_INPUT].getVoltage(),
			params[START_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		local.stop = std::fmax(inputs[STOP_INPUT].getVoltage(),
			params[STOP_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		local.advance = std::fmax(inputs[ADVANCE_INPUT].getVoltage(),
			params[ADVANCE_PARAM].getValue() > 0.5f ? 10.f : 0.f);
		// Momentary local pulse (jack) OR'd with a one-sample pulse from the
		// ADDR_MODE switch's momentary-strobe position -- same merge Head.cpp
		// itself does (STROBE_INPUT max'd with a locally latched strobePending),
		// see processControl()'s addrModeStrobeTrigger.
		local.strobe = std::fmax(inputs[STROBE_INPUT].getVoltage(), strobePending ? 10.f : 0.f);
		strobePending = false;
		local.addressCv = inputs[ADDRESS_INPUT].getVoltage();
		local.extClock = inputs[CLK_INPUT].getVoltage();
		local.timeCv = inputs[TIMECV_INPUT].getVoltage();
		local.reset = resetButtonTrigger.process(params[RESET_PARAM].getValue()) ||
			resetInputTrigger.process(inputs[RESET_INPUT].getVoltage());

		controller.tick(table, ext, globals, scaleKey, local, args.sampleTime, out);

		outputs[CV_OUTPUT].setVoltage(out.cv);
		outputs[TIME_OUTPUT].setVoltage(out.timeOut);
		outputs[REF_OUTPUT].setVoltage(out.ref);
		outputs[ALL_OUTPUT].setVoltage(out.allPulse ? 10.f : 0.f);
		outputs[PULSE1_OUTPUT].setVoltage(out.pulse1 ? 10.f : 0.f);
		outputs[PULSE2_OUTPUT].setVoltage(out.pulse2 ? 10.f : 0.f);
		outputs[EOC_OUTPUT].setVoltage(out.eoc ? 10.f : 0.f);

		lights[RUN_LIGHT].setBrightnessSmooth(out.runState == spacetime::RUN_RUNNING ? 1.f : 0.f, args.sampleTime);
		lights[HOLD_LIGHT].setBrightnessSmooth(out.runState == spacetime::RUN_HOLDING ? 1.f : 0.f, args.sampleTime);
		lights[STOPPED_LIGHT].setBrightnessSmooth(out.runState == spacetime::RUN_STOPPED ? 1.f : 0.f, args.sampleTime);
		lights[LINK_LIGHT].setBrightnessSmooth(
			coreLink == CoreLink::Linked && !headConflict ? 1.f : 0.f, args.sampleTime);
	}

	void processControl(float dt) {
		int nextInstrument = clamp((int)std::round(params[INSTRUMENT_PARAM].getValue()), 0, 3);
		int nextHead = clamp((int)std::round(params[HEAD_PARAM].getValue()), 0, 7);
		if (nextInstrument != instrumentId || nextHead != headIndex)
			setBinding(nextInstrument, nextHead);

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
		headConflict = headRegistry.headLinkCount(instrumentId, headIndex) > 1;

		stageTableRegistry.readTable(instrumentId, coreValid, table);
		stageTableRegistry.readContext(instrumentId, coreValid, ext, globals, scaleKey);

		spacetime::MetaModuleHeadMidiSnapshot midi;
		if (headMidiRegistry.readMidi(instrumentId, coreValid, midi)) {
			controller.applyMidi(midi.headCcSeq[headIndex], midi.headCcValue[headIndex],
				midi.midiClockSeq, midi.midiStartSeq, midi.midiStopSeq, midi.midiContinueSeq);
		}

		// Local panel <-> MIDI CC feed-forward sync for every HeadConfig
		// field. Order doesn't matter for correctness (each field is
		// independent), only that applyMidi() above has already run so a
		// fresh MIDI value is visible to compare against this tick.
		spacetime::HeadConfig& cfg = controller.config();
		mirrorContinuous(ADDRESS_PARAM, cfg.addressKnob, shadowAddress);
		mirrorBool(ADDR_SOURCE_PARAM, cfg.addrExt, shadowAddrSource);
		processAddrMode();
		mirrorDiscrete(DIRECTION_PARAM, cfg.direction, shadowDirection, 4);
		mirrorClockSource();
		mirrorDiscrete(CLK_DIV_PARAM, cfg.clkDivIndex, shadowClkDiv, 8);
		mirrorContinuous(TIMECV_PARAM, cfg.timeCvAmount, shadowTimeCv);
		mirrorDiscrete(LOOP_PARAM, cfg.loopMode, shadowLoop, 2);

		// Same discipline as Core.cpp's publishTiming(): only publish while
		// verifiably the sole owner of this head slot. This doubles as the
		// tick/ack channel the EB8 plan settled on -- a fresh output
		// heartbeat within a caller's own staleness window is the ack, no
		// separate request/response round trip needed.
		if (!ownsHead)
			ownsHead = headRegistry.tryClaimHead(instrumentId, headIndex, busToken);
		if (ownsHead && headRegistry.headLinkCount(instrumentId, headIndex) == 1)
			headRegistry.publishHeadOutput(instrumentId, headIndex, out);
	}

	// ---- Feed-forward mirror helpers ---------------------------------------
	// Each: if the dsp-side value has moved away from what we last pushed to
	// the Param (a fresh MIDI CC applied above), push it into the Param. Else
	// if the Param itself has moved away from that same shadow (the user
	// turned the knob), push it into the dsp-side value. Exactly one of
	// those can be true in a given tick under normal use; if a MIDI CC and a
	// knob turn land in the same 16-sample window, MIDI is checked first and
	// wins that tick, same as Core.cpp's STAGE_PARAM/MIDI ordering.
	void mirrorContinuous(int paramId, float& target, float& shadow) {
		if (target != shadow) {
			params[paramId].setValue(target);
			shadow = target;
		} else {
			float knob = params[paramId].getValue();
			if (knob != shadow) {
				target = knob;
				shadow = knob;
			}
		}
	}

	void mirrorDiscrete(int paramId, uint8_t& target, float& shadow, int maxValue) {
		float current = (float)target;
		if (current != shadow) {
			params[paramId].setValue(current);
			shadow = current;
		} else {
			float knob = clamp(std::round(params[paramId].getValue()), 0.f, (float)maxValue);
			if (knob != shadow) {
				target = (uint8_t)knob;
				shadow = knob;
			}
		}
	}

	void mirrorBool(int paramId, bool& target, float& shadow) {
		float current = target ? 1.f : 0.f;
		if (current != shadow) {
			params[paramId].setValue(current);
			shadow = current;
		} else {
			float knob = params[paramId].getValue() > 0.5f ? 1.f : 0.f;
			if (knob != shadow) {
				target = knob > 0.5f;
				shadow = knob;
			}
		}
	}

	void mirrorClockSource() {
		float current = (float)controller.clockSource();
		if (current != shadowClkSource) {
			params[CLK_SOURCE_PARAM].setValue(current);
			shadowClkSource = current;
		} else {
			float knob = clamp(std::round(params[CLK_SOURCE_PARAM].getValue()), 0.f, 3.f);
			if (knob != shadowClkSource) {
				controller.setClockSource((int)knob);
				shadowClkSource = knob;
			}
		}
	}

	// ADDR_MODE is not a plain 3-state field: position 0 (Strobe) is
	// momentary on real hardware and on VCV's own LatchSpringSwitch3 (spring-
	// returns after release). A plain CKSSThree has no spring return, so
	// here position 0 is instead treated purely as a trigger zone -- moving
	// the switch to 0 fires one strobe pulse (mirroring MIDI CC7 mode 0's
	// own "pulse, don't change continuous" behavior exactly) and the
	// continuous/sequential state underneath is left untouched regardless of
	// how long the switch dwells at 0. Only positions 1/2 participate in the
	// feed-forward mirror against cfg.continuous.
	void processAddrMode() {
		float knob = params[ADDR_MODE_PARAM].getValue();
		if (addrModeStrobeTrigger.process(knob < 0.5f))
			strobePending = true;
		spacetime::HeadConfig& cfg = controller.config();
		float target = cfg.continuous ? 2.f : 1.f;
		if (target != shadowAddrMode) {
			params[ADDR_MODE_PARAM].setValue(target);
			shadowAddrMode = target;
		} else if (knob >= 0.5f && knob != shadowAddrMode) {
			cfg.continuous = knob > 1.5f;
			shadowAddrMode = knob;
		}
	}

	void setBinding(int nextInstrument, int nextHead) {
		headRegistry.unregisterHead(instrumentId, headIndex, busToken);
		instrumentId = nextInstrument;
		headIndex = nextHead;
		ownsHead = headRegistry.registerHead(instrumentId, headIndex, busToken);
		controller.reset(1);
		table = spacetime::StageTable();
		lastTableHeartbeat = 0;
		staleTime = 1.f;
		coreLink = CoreLink::Waiting;
		// Force every mirror to re-sync from the (freshly reset, then
		// re-applied-from-bus) controller state on the very next control
		// tick, rather than assuming the old shadow values still apply to a
		// different head.
		shadowAddress = shadowAddrSource = shadowDirection = shadowClkSource =
			shadowClkDiv = shadowTimeCv = shadowLoop = shadowAddrMode = -1000.f;
	}

	size_t get_display_text(int lightId, std::span<char> text) override {
		if (lightId != STATUS_DISPLAY || text.empty())
			return 0;
		const char* linkName = coreLink == CoreLink::Linked ? "LINK" :
			(coreLink == CoreLink::Duplicate ? "DUP" : "WAIT");
		const char* runName = out.runState == spacetime::RUN_STOPPED ? "STOP" :
			(out.runState == spacetime::RUN_HOLDING ? "HOLD" : "RUN");
		const char* sourceNames[] = {"INT", "EXT CV", "MIDI", "VIRTUAL"};
		const char* divisionNames[] = {"/16", "/8", "/4", "/2", "x1", "x2", "x4", "x8", "x16"};
		const char* directionNames[] = {"FWD", "REV", "PEND", "RAND", "BROWN"};
		const char* loopNames[] = {"1-SHOT", "F-L", "FULL"};
		const spacetime::HeadConfig& cfg = controller.config();
		int source = clamp(controller.clockSource(), 0, 3);
		int division = clamp((int)cfg.clkDivIndex, 0, 8);
		int direction = clamp((int)cfg.direction, 0, 4);
		int loop = clamp((int)cfg.loopMode, 0, 2);
		char buffer[128];
		int length = std::snprintf(buffer, sizeof(buffer),
			"ID %c HEAD %d%s\n%s %s S%02d\nCLK %s %s\nDIR %s LOOP %s",
			(char)('A' + instrumentId), headIndex + 1, headConflict ? "!" : "",
			linkName, runName, (int)out.currentStage + 1,
			sourceNames[source], divisionNames[division],
			directionNames[direction], loopNames[loop]);
		if (length < 0)
			return 0;
		size_t copyLength = std::min(text.size(), (size_t)length);
		std::copy(buffer, buffer + copyLength, text.begin());
		return copyLength;
	}

	// Every field except followMidiTransport is now a real Param (Rack
	// auto-persists params[] on every module, no manual JSON needed) --
	// including instrumentId/headIndex, which processControl() re-derives
	// from INSTRUMENT_PARAM/HEAD_PARAM every control tick regardless of
	// whether they changed via a live knob turn or a patch reload. This
	// matches VCV Head.cpp's own dataToJson exactly, which persists only
	// this one non-Param field.
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "followMidiTransport", json_boolean(controller.followsMidiTransport()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* value = json_object_get(root, "followMidiTransport");
		if (value)
			controller.setFollowsMidiTransport(json_boolean_value(value));
	}
};

struct SpaceTimeHeadWidget : ModuleWidget {
	SpaceTimeHeadWidget(SpaceTimeHead* module) {
		using namespace LayoutHR;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Head.svg")));

		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(4.f, 14.2f)));
		display->box.size = mm2px(Vec(73.f, 27.f));
		display->firstLightId = SpaceTimeHead::STATUS_DISPLAY;
		display->font = "Default_10";
		display->color = Colors565::White;
		addChild(display);

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(IH_X0, IH_KNOB_Y)), module,
			SpaceTimeHead::INSTRUMENT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(IH_X1, IH_KNOB_Y)), module,
			SpaceTimeHead::HEAD_PARAM));

		addParam(createParamCentered<LEDButton>(mm2px(Vec(TR_X0 + 0 * TR_PITCH, TR_BTN_Y)), module, SpaceTimeHead::START_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(TR_X0 + 1 * TR_PITCH, TR_BTN_Y)), module, SpaceTimeHead::STOP_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(TR_X0 + 2 * TR_PITCH, TR_BTN_Y)), module, SpaceTimeHead::ADVANCE_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(TR_X0 + 3 * TR_PITCH, TR_BTN_Y)), module, SpaceTimeHead::RESET_PARAM));

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(CFG_X0 + 0 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::ADDRESS_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(CFG_X0 + 1 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::ADDR_SOURCE_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(CFG_X0 + 2 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::ADDR_MODE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(CFG_X0 + 3 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::DIRECTION_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(CFG_X0 + 4 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::CLK_SOURCE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(CFG_X0 + 5 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::CLK_DIV_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(CFG_X0 + 6 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::TIMECV_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(CFG_X0 + 7 * CFG_PITCH, CFG_CTRL_Y)), module, SpaceTimeHead::LOOP_PARAM));

		static const int inputIds[8] = {
			SpaceTimeHead::START_INPUT, SpaceTimeHead::STOP_INPUT,
			SpaceTimeHead::ADVANCE_INPUT, SpaceTimeHead::STROBE_INPUT,
			SpaceTimeHead::ADDRESS_INPUT, SpaceTimeHead::CLK_INPUT,
			SpaceTimeHead::TIMECV_INPUT, SpaceTimeHead::RESET_INPUT,
		};
		for (int i = 0; i < 8; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(IN_X0 + i * IN_PITCH, IN_JACK_Y)), module, inputIds[i]));

		for (int i = 0; i < 7; i++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X0 + i * OUT_PITCH, OUT_JACK_Y)), module,
				SpaceTimeHead::CV_OUTPUT + i));

		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(50.f, 6.5f)), module, SpaceTimeHead::RUN_LIGHT));
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(58.f, 6.5f)), module, SpaceTimeHead::HOLD_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(66.f, 6.5f)), module, SpaceTimeHead::STOPPED_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(74.f, 6.5f)), module, SpaceTimeHead::LINK_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		SpaceTimeHead* module = getModule<SpaceTimeHead>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		static const char* instrumentLabels[] = {"A", "B", "C", "D"};
		menu->addChild(createSubmenuItem("Instrument ID", [=]() {
			return std::string(instrumentLabels[module->instrumentId]) +
				(module->headConflict ? " (duplicate head slot!)" : "");
		}, [=](Menu* ids) {
			for (int id = 0; id < 4; id++)
				ids->addChild(createCheckMenuItem(instrumentLabels[id], "",
					[=]() { return module->instrumentId == id; },
					[=]() {
						module->params[SpaceTimeHead::INSTRUMENT_PARAM].setValue((float)id);
						module->setBinding(id, module->headIndex);
					}));
		}));
		menu->addChild(createSubmenuItem("Head index", [=]() {
			return string::f("%d", module->headIndex + 1);
		}, [=](Menu* heads) {
			for (int h = 0; h < spacetime::kMaxHeads; h++)
				heads->addChild(createCheckMenuItem(string::f("%d", h + 1), "",
					[=]() { return module->headIndex == h; },
					[=]() {
						module->params[SpaceTimeHead::HEAD_PARAM].setValue((float)h);
						module->setBinding(module->instrumentId, h);
					}));
		}));
		menu->addChild(createCheckMenuItem("Follow MIDI transport", "global Start/Stop/Continue",
			[=]() { return module->controller.followsMidiTransport(); },
			[=]() { module->controller.setFollowsMidiTransport(!module->controller.followsMidiTransport()); }));
	}
};

} // namespace

Model* modelSpaceTimeHead = createModel<SpaceTimeHead, SpaceTimeHeadWidget>("Head");
