#include "plugin.hpp"
#include "paneltheme.hpp"
#include "spacetime_widgets.hpp"
#include "ChainAdapter.hpp"
#include "ProgramLogic.hpp"
#include "PresetRow.hpp"

// ============================================================================
// PROGRAM — WP7 integrated.
// The programming section anchor: consumes the concatenated stage table from
// the blocks, emits edit-ops (spring-switch gestures, limited/time-range
// banks, Clear, preset recall), broadcasts table + EXT A-D + globals +
// key/scale to the heads, merges head statuses into POLY OUT and the block
// LEDs, and shows the selected stage's programming on the modifier LEDs.
// All logic lives in dsp/ (ProgramLogic, PresetRow, Chain); this module is
// plumbing. Control-rate only (÷16).
//
// All positions in mm; MUST match res/Program.svg.
// ============================================================================

namespace Layout {
constexpr float ROW1_Y = 20.f;
constexpr float DISPLAY_X = 14.f;
constexpr float SCROLL_X = 30.f;
constexpr float CLEAR_X = 43.f;
constexpr float PULSE1_X = 57.f;
constexpr float PULSE2_X = 69.f;
constexpr float VOLT_Y = 41.5f;
constexpr float QUANT_X = 13.f;
constexpr float SLOPE_X = 29.f;
constexpr float RANGE_X = 45.f;
constexpr float VSRC_X = 61.f;
constexpr float LTD_Y = 57.5f;
constexpr float LTD_X0 = 13.f, LTD_PITCH = 12.f;
constexpr float MODE_Y = 75.5f;
constexpr float MODE_X0 = 13.f, MODE_PITCH = 13.f;
constexpr float TIME_Y = 94.f;
constexpr float TRANGE_X0 = 13.f, TRANGE_PITCH = 10.f;
constexpr float TSRC_X = 64.f;
constexpr float PRESET_Y = 110.f;
constexpr float PRESET_X0 = 5.5f, PRESET_PITCH = 6.3f;
constexpr float PROW2_Y = 120.5f;
constexpr float LOAD_X = 14.f, SAVE_X = 28.f, KEY_X = 42.f, SCALE_X = 56.f;
constexpr float EXT_X = 84.f;
constexpr float EXT_Y0 = 20.f, EXT_PITCH = 9.5f;
constexpr float PULSE_RETRIG_X = 84.f, PULSE_RETRIG_Y = 73.f;
constexpr float POLY_X = 84.f, POLY_Y = 112.5f;
} // namespace Layout

struct Program : Module {
	enum ParamId {
		STAGE_SCROLL_PARAM,
		CLEAR_PARAM,
		QUANTIZE_PARAM,
		SLOPE_PARAM,
		RANGE_PARAM,
		VSOURCE_PARAM,
		ENUMS(LTD_PARAMS, 5),
		STOP_PARAM,
		SUSTAIN_PARAM,
		ENABLE_PARAM,
		FIRST_PARAM,
		LAST_PARAM,
		ENUMS(TRANGE_PARAMS, 4),
		TSOURCE_PARAM,
		PULSE1_PARAM,
		PULSE2_PARAM,
		ENUMS(PRESET_PARAMS, 12),
		LOAD_PARAM,
		SAVE_PARAM,
		KEY_PARAM,
		SCALE_PARAM,
		PULSE_RETRIG_PARAM,  // appended for patch compatibility
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(EXT_INPUTS, 4),
		INPUTS_LEN
	};
	enum OutputId {
		POLY_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		QUANTIZE_LIGHT,
		SLOPE1_LIGHT,
		SLOPE2_LIGHT,
		RANGE_FULL_LIGHT,
		RANGE_HALF_LIGHT,
		RANGE_LTD_LIGHT,
		VSOURCE_LIGHT,
		ENUMS(LTD_LIGHTS, 5),
		STOP_LIGHT,
		SUSTAIN_LIGHT,
		ENABLE_LIGHT,
		FIRST_LIGHT,
		LAST_LIGHT,
		ENUMS(TRANGE_LIGHTS, 4),
		TSOURCE_LIGHT,
		PULSE1_LIGHT,
		PULSE2_LIGHT,
		ENUMS(PRESET_LIGHTS, 12),
		LOAD_LIGHT,
		SAVE_LIGHT,
		KEY_LIGHT,
		SCALE_LIGHT,
		LIGHTS_LEN
	};

	// The 12 spring-switch gesture fields, in param order QUANTIZE..PULSE2.
	struct GestureMap {
		int param;
		spacetime::Field field;
	};
	static const GestureMap gestureMap[12];

	spacetime::ProgramLogic logic;
	spacetime::PresetRowLogic presetRow;
	spacetime::StageTable table;      // latest from the blocks
	spacetime::Globals globals;       // context-menu globals
	uint32_t opSeq = 0;

	spacetime::MessagePort<spacetime::BlockToAnchorMsg> rightPort;
	spacetime::MessagePort<spacetime::HeadsToAnchorMsg> leftPort;

	dsp::BooleanTrigger gestureUp[12], gestureDown[12];
	dsp::BooleanTrigger clearTrigger, ltdTriggers[5], trangeTriggers[4];
	dsp::BooleanTrigger slotTriggers[12], modeTriggers[4];
	dsp::BooleanTrigger scrollPressTrigger;
	dsp::ClockDivider divider;

	// FG Display arbitration (manual: only one Display LED at a time).
	bool prevDisplay[8] = {};
	int displayOwner = -1;
	uint32_t displayCancelSeq = 0;
	uint32_t lastMidiEventSeq = 0;
	uint8_t lastMidiType = 0;
	uint8_t lastMidiIndex = 0;
	uint8_t lastMidiValue = 0;
	float lastMidiFValue = 0.f;
	uint8_t lastMidiOpCount = 0;
	float midiKeyFeedback = 0.f;
	float midiScaleFeedback = 0.f;
	uint8_t midiKeyFeedbackIndex = 0;
	uint8_t midiScaleFeedbackIndex = 0;

	// For the stage-count display widget.
	int displayCount = 0;
	bool displayBroken = false;

	Program() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		spacetime::configSpring3(this, STAGE_SCROLL_PARAM, "Stage select scroll",
			"Scroll left", "Scroll right");
		configButton(CLEAR_PARAM, "Clear all stages to defaults");
		spacetime::configSpring3(this, PULSE1_PARAM, "Pulse 1",
			"Remove from stage", "Add to stage");
		spacetime::configSpring3(this, PULSE2_PARAM, "Pulse 2",
			"Remove from stage", "Add to stage");
		spacetime::configSpring3(this, QUANTIZE_PARAM, "Quantize / Continuous",
			"Continuous", "Quantize");
		spacetime::configSpring3(this, SLOPE_PARAM, "Sloped / Stepped",
			"Reduce slew", "Add slew");
		spacetime::configSpring3(this, RANGE_PARAM, "Voltage range",
			"Half (0-5 V)", "Full (0-10 V)");
		spacetime::configSpring3(this, VSOURCE_PARAM, "Voltage source",
			"Internal", "External (slider selects A-D)");
		for (int i = 0; i < 5; i++)
			configButton(LTD_PARAMS + i,
				string::f("Limited range, octave %+d", i - 2));
		spacetime::configSpring3(this, STOP_PARAM, "Stop", "Remove", "Add");
		spacetime::configSpring3(this, SUSTAIN_PARAM, "Sustain", "Remove", "Add");
		spacetime::configSpring3(this, ENABLE_PARAM, "Enable", "Remove", "Add");
		spacetime::configSpring3(this, FIRST_PARAM, "Cycle first stage", "Remove", "Set");
		spacetime::configSpring3(this, LAST_PARAM, "Cycle last stage", "Remove", "Set");
		static const char* trangeNames[4] = {
			"Time range 0.002–0.03 s", "Time range 0.02–0.3 s",
			"Time range 0.2–3 s", "Time range 2–30 s"};
		for (int i = 0; i < 4; i++)
			configButton(TRANGE_PARAMS + i, trangeNames[i]);
		spacetime::configSpring3(this, TSOURCE_PARAM, "Time source",
			"Internal (slider + range)", "External (A-D CV)");
		for (int i = 0; i < 12; i++) {
			static const char* notes[12] = {"C", "C#", "D", "D#", "E", "F",
				"F#", "G", "G#", "A", "A#", "B"};
			configButton(PRESET_PARAMS + i,
				string::f("Preset %d / key %s", i + 1, notes[i]));
		}
		configButton(LOAD_PARAM, "Load preset (then press 1-12)");
		configButton(SAVE_PARAM, "Save preset (then press 1-12)");
		configButton(KEY_PARAM, "Key select (then press 1-12)");
		configButton(SCALE_PARAM, "Scale select (then 10=Major 11=Minor 12=Chromatic)");
		configSwitch(PULSE_RETRIG_PARAM, 0.f, 1.f, 1.f,
			"Pulse retrigger", {"Continuous across adjacent pulse stages",
			"Hardware-compatible retrigger notch"});
		for (int i = 0; i < 4; i++)
			configInput(EXT_INPUTS + i, string::f("External %c", 'A' + i));
		configOutput(POLY_OUTPUT, "Heads CV (polyphonic, channel per head)");

		rightPort.attach(rightExpander);
		leftPort.attach(leftExpander);
		divider.setDivision(16);
	}

	int springDir(int paramId) {
		float v = params[paramId].getValue();
		return v > 1.5f ? 1 : (v < 0.5f ? -1 : 0);
	}

	spacetime::Field midiGestureField(int cc) {
		using namespace spacetime;
		switch (cc) {
			case 68: return Field::Quantize;
			case 69: return Field::Slew;
			case 70: return Field::Range;
			case 71: return Field::VoltageSource;
			case 77: return Field::Stop;
			case 78: return Field::Sustain;
			case 79: return Field::Enable;
			case 80: return Field::First;
			case 81: return Field::Last;
			case 86: return Field::TimeSource;
			case 87: return Field::Pulse1;
			case 88: return Field::Pulse2;
			default: return Field::Count_;
		}
	}

	void process(const ProcessArgs& args) override {
		if (!divider.process())
			return;
		float dt = args.sampleTime * divider.getDivision();
		using namespace spacetime;
		globals.pulseRetrig = params[PULSE_RETRIG_PARAM].getValue() > 0.5f;
		midiKeyFeedback = std::fmax(0.f, midiKeyFeedback - dt);
		midiScaleFeedback = std::fmax(0.f, midiScaleFeedback - dt);

		bool rightIsBlock = modelIs(rightExpander.module, modelStage4, modelGlueRight);
		bool leftIsStatusChain = modelIs(leftExpander.module, modelHead, modelMidi, modelGlueLeft);

		// ---- Table in (from the block chain)
		const BlockToAnchorMsg* tm = rightPort.consume(rightExpander);
		if (rightIsBlock && tm->valid)
			table = tm->table;
		else
			table = StageTable();

		// ---- Chain layout for the display
		RackNeighborView view(this, modelProgram, modelStage4, modelHead, modelHeadAll,
			modelMidi);
		ChainLayout lay = enumerateChain(view);
		displayCount = table.count;
		displayBroken = lay.brokenRight || lay.brokenLeft ||
			(int)table.count != (int)lay.blockCount * kStagesPerBlock;

		// ---- Statuses in (from the heads) — consumed early: the Display
		// override must be settled before gestures pick their target stage.
		const HeadsToAnchorMsg* sm = leftPort.consume(leftExpander);
		bool statusValid = leftIsStatusChain && sm->valid;

		// ---- FG Display arbitration: newest press wins, one owner at most.
		bool present[8] = {};
		if (statusValid) {
			for (int i = 0; i < sm->headCount && i < kMaxHeads; i++) {
				int h = sm->status[i].headId & 7;
				present[h] = true;
				if (sm->status[i].display && !prevDisplay[h])
					displayOwner = h;
				prevDisplay[h] = sm->status[i].display;
			}
		}
		for (int h = 0; h < 8; h++)
			if (!present[h])
				prevDisplay[h] = false;
		int overrideStage = -1;
		if (displayOwner >= 0) {
			bool ownerActive = false;
			if (statusValid) {
				for (int i = 0; i < sm->headCount && i < kMaxHeads; i++) {
					if (sm->status[i].headId == displayOwner && sm->status[i].display) {
						ownerActive = true;
						overrideStage = sm->status[i].currentStage;
					}
				}
			}
			if (!ownerActive)
				displayOwner = -1;
		}

		// ---- Scroll (a press while displaying cancels Display and jumps
		// the selection to stage 1, per manual)
		int dir = springDir(STAGE_SCROLL_PARAM);
		bool cancelledNow = false;
		if (scrollPressTrigger.process(dir != 0) && displayOwner >= 0) {
			displayCancelSeq++;
			displayOwner = -1;
			overrideStage = -1;
			logic.setSelected(0);
			cancelledNow = true;  // swallow this press: no scroll step
		}
		logic.setSelectOverride(overrideStage);
		logic.tickScroll(cancelledNow ? 0 : dir, dt, table.count);

		// ---- Gestures -> ops
		AnchorToBlocksMsg om;
		EditOp buf[kMaxOpsPerTick];
		int room;
		#define PUSH_OPS(N) for (int _i = 0; _i < (N); _i++) pushOp(om, buf[_i])

		for (int i = 0; i < 12; i++) {
			int p = gestureMap[i].param;
			Field f = gestureMap[i].field;
			room = kMaxOpsPerTick - om.opCount;
			if (gestureUp[i].process(params[p].getValue() > 1.5f)) {
				int n = logic.emitModifier(f, +1, table, buf, room);
				PUSH_OPS(n);
			}
			room = kMaxOpsPerTick - om.opCount;
			if (gestureDown[i].process(params[p].getValue() < 0.5f)) {
				int n = logic.emitModifier(f, -1, table, buf, room);
				PUSH_OPS(n);
			}
		}
		for (int i = 0; i < 5; i++) {
			if (ltdTriggers[i].process(params[LTD_PARAMS + i].getValue() > 0.5f)) {
				room = kMaxOpsPerTick - om.opCount;
				int n = logic.emitLimited(i, table, buf, room);
				PUSH_OPS(n);
			}
		}
		for (int i = 0; i < 4; i++) {
			if (trangeTriggers[i].process(params[TRANGE_PARAMS + i].getValue() > 0.5f)) {
				room = kMaxOpsPerTick - om.opCount;
				int n = logic.emitTimeRange(i, table, buf, room);
				PUSH_OPS(n);
			}
		}
		if (clearTrigger.process(params[CLEAR_PARAM].getValue() > 0.5f)) {
			room = kMaxOpsPerTick - om.opCount;
			int n = logic.emitClear(table, buf, room);
			PUSH_OPS(n);
		}

		// ---- MIDI module -> PROGRAM events
		auto midiSelectRelative = [&](int delta) {
			if (displayOwner >= 0) {
				int displayedStage = overrideStage;
				displayCancelSeq++;
				displayOwner = -1;
				logic.setSelectOverride(-1);
				if (displayedStage >= 0)
					logic.setSelected(displayedStage);
			}
			logic.selectRelative(delta, table.count);
		};
		if (statusValid) {
			for (int e = 0; e < sm->midiEventCount && e < kMaxMidiProgramEvents; e++) {
				const MidiProgramEvent& ev = sm->midiEvents[e];
				if (ev.seq <= lastMidiEventSeq)
					continue;
				lastMidiEventSeq = ev.seq;
				lastMidiType = ev.type;
				lastMidiIndex = ev.index;
				lastMidiValue = ev.value;
				lastMidiFValue = ev.fvalue;
				if (ev.type == MIDI_PROG_SELECT_PREV) {
					midiSelectRelative(-1);
				}
				else if (ev.type == MIDI_PROG_SELECT_NEXT) {
					midiSelectRelative(+1);
				}
				else if (ev.type == MIDI_PROG_BULK_ARM) {
					logic.armBulkOnce();
				}
				else if (ev.type == MIDI_PROG_CLEAR) {
					room = kMaxOpsPerTick - om.opCount;
					int n = logic.emitClear(table, buf, room);
					PUSH_OPS(n);
				}
				else if (ev.type == MIDI_PROG_PRESET_LOAD) {
					logic.loadPreset(ev.index, table);
				}
				else if (ev.type == MIDI_PROG_SET_KEY) {
					ScaleKey sk = logic.scaleKey();
					sk.key = ev.index < 12 ? ev.index : 11;
					logic.setScaleKey(sk);
					midiKeyFeedbackIndex = sk.key;
					midiKeyFeedback = 1.f;
					midiScaleFeedback = 0.f;
				}
				else if (ev.type == MIDI_PROG_SET_SCALE) {
					ScaleKey sk = logic.scaleKey();
					sk.scale = ev.index < 3 ? ev.index : 2;
					logic.setScaleKey(sk);
					midiScaleFeedbackIndex = sk.scale;
					midiScaleFeedback = 1.f;
					midiKeyFeedback = 0.f;
				}
				else if (ev.type == MIDI_PROG_SET_PULSE_RETRIG) {
					params[PULSE_RETRIG_PARAM].setValue(ev.index ? 1.f : 0.f);
				}
				else if (ev.type == MIDI_PROG_SLIDER) {
					if (ev.index < kMaxStages)
						pushOp(om, EditOp(ev.index, Field::Voltage, ev.fvalue, ev.flags));
					else if (ev.index < 2 * kMaxStages)
						pushOp(om, EditOp((uint8_t)(ev.index - kMaxStages), Field::Time, ev.fvalue, ev.flags));
				}
				else if (ev.type == MIDI_PROG_GESTURE) {
					Field f = midiGestureField(ev.index);
					if (f != Field::Count_) {
						room = kMaxOpsPerTick - om.opCount;
						int n = logic.emitModifier(f, ev.value >= 64 ? +1 : -1, table, buf, room);
						PUSH_OPS(n);
					}
				}
				else if (ev.type == MIDI_PROG_LIMITED) {
					if (ev.value >= 64) {
						room = kMaxOpsPerTick - om.opCount;
						int n = logic.emitLimited(ev.index, table, buf, room);
						PUSH_OPS(n);
					}
				}
				else if (ev.type == MIDI_PROG_TIME_RANGE) {
					if (ev.value >= 64) {
						room = kMaxOpsPerTick - om.opCount;
						int n = logic.emitTimeRange(ev.index, table, buf, room);
						PUSH_OPS(n);
					}
				}
				lastMidiOpCount = om.opCount;
			}
		}

		// ---- Preset row (modal)
		static const PresetMode modes[4] = {PresetMode::Load, PresetMode::Save,
			PresetMode::Key, PresetMode::Scale};
		for (int i = 0; i < 4; i++) {
			if (modeTriggers[i].process(params[LOAD_PARAM + i].getValue() > 0.5f))
				presetRow.onModePress(modes[i]);
		}
		for (int i = 0; i < 12; i++) {
			if (slotTriggers[i].process(params[PRESET_PARAMS + i].getValue() > 0.5f)) {
				PresetAction act = presetRow.onSlotPress((uint8_t)i);
				logic.handlePresetAction(act, table);
			}
		}
		room = kMaxOpsPerTick - om.opCount;
		if (room > 0 && logic.pendingOps() > 0) {
			int n = logic.drainPendingOps(buf, room);
			PUSH_OPS(n);
		}
		#undef PUSH_OPS

		// ---- Message out to the blocks
		om.selectedStage = (uint8_t)logic.selectedStage();
		om.headCount = statusValid ? sm->headCount : 0;
		for (int i = 0; i < om.headCount; i++)
			om.status[i] = sm->status[i];
		om.hopIndex = 0;
		om.seq = ++opSeq;
		om.valid = true;
		if (rightIsBlock) {
			AnchorToBlocksMsg* bo = rightNeighborProducer<AnchorToBlocksMsg>(
				this, modelStage4, modelGlueRight);
			if (bo) {
				*bo = om;
				flipRightNeighbor(this);
			}
		}

		// ---- Broadcast out to the heads
		if (leftIsStatusChain) {
			AnchorToHeadsMsg* ho = leftNeighborProducer<AnchorToHeadsMsg>(
				this, modelHead, modelMidi, modelGlueLeft);
			if (ho) {
				ho->table = table;
				for (int i = 0; i < 4; i++) {
					ho->ext[i] = inputs[EXT_INPUTS + i].getVoltage();
					ho->extConnected[i] = inputs[EXT_INPUTS + i].isConnected();
				}
				ho->globals = globals;
				ho->scaleKey = logic.scaleKey();
				ho->selectedStage = om.selectedStage;
				ho->bulkArmed = logic.bulkArmed();
				ho->hopIndex = 0;
				ho->displayOwner = displayOwner >= 0 ? (uint8_t)displayOwner : 0xFF;
				ho->displayCancelSeq = displayCancelSeq;
				ho->valid = true;
				flipLeftNeighbor(this);
			}
		}

		// ---- POLY OUT: channel per head (by head id)
		int channels = statusValid ? sm->headCount : 0;
		outputs[POLY_OUTPUT].setChannels(channels > 0 ? channels : 1);
		if (channels == 0)
			outputs[POLY_OUTPUT].setVoltage(0.f, 0);
		for (int i = 0; i < channels; i++) {
			int ch = sm->status[i].headId;
			if (ch >= 0 && ch < channels)
				outputs[POLY_OUTPUT].setVoltage(sm->status[i].cv, ch);
		}

		// ---- Modifier LEDs: the selected stage's programming
		ProgramWord w;
		bool haveStage = table.count > 0;
		if (haveStage)
			w = table.program[logic.selectedStage()];
		auto led = [&](int id, bool on) {
			lights[id].setBrightnessSmooth((haveStage && on) ? 1.f : 0.f, dt);
		};
		led(QUANTIZE_LIGHT, w.quantize());
		led(SLOPE1_LIGHT, w.slew() >= SLEW_1);
		led(SLOPE2_LIGHT, w.slew() >= SLEW_2);
		led(RANGE_FULL_LIGHT, w.range() == RANGE_FULL);
		led(RANGE_HALF_LIGHT, w.range() == RANGE_HALF);
		led(RANGE_LTD_LIGHT, w.range() == RANGE_LIMITED);
		for (int i = 0; i < 5; i++)
			led(LTD_LIGHTS + i, w.range() == RANGE_LIMITED && w.limitedOctave() == i);
		led(VSOURCE_LIGHT, w.voltageSource() == SOURCE_EXTERNAL);
		led(STOP_LIGHT, w.stop());
		led(SUSTAIN_LIGHT, w.sustain());
		led(ENABLE_LIGHT, w.enable());
		led(FIRST_LIGHT, w.first());
		led(LAST_LIGHT, w.last());
		for (int i = 0; i < 4; i++)
			led(TRANGE_LIGHTS + i, w.timeRange() == i);
		led(TSOURCE_LIGHT, w.timeSource() == SOURCE_EXTERNAL);
		led(PULSE1_LIGHT, w.pulse1());
		led(PULSE2_LIGHT, w.pulse2());

		// ---- Preset row LEDs: armed mode + used slots
		for (int i = 0; i < 4; i++)
			lights[LOAD_LIGHT + i].setBrightnessSmooth(
				(presetRow.mode() == modes[i] ||
				 (i == 2 && midiKeyFeedback > 0.f) ||
				 (i == 3 && midiScaleFeedback > 0.f)) ? 1.f : 0.f, dt);
		for (int i = 0; i < 12; i++) {
			bool feedback = (midiKeyFeedback > 0.f && i == midiKeyFeedbackIndex) ||
				(midiScaleFeedback > 0.f && i == 9 + midiScaleFeedbackIndex);
			lights[PRESET_LIGHTS + i].setBrightnessSmooth(
				feedback ? 1.f : (logic.slotUsed(i) ? 0.35f : 0.f), dt);
		}
	}

	// ---- Persistence: presets, key/scale, globals --------------------------
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "key", json_integer(logic.scaleKey().key));
		json_object_set_new(root, "scale", json_integer(logic.scaleKey().scale));
		json_object_set_new(root, "slewFrac1", json_real(globals.slewFrac1));
		json_object_set_new(root, "slewFrac2", json_real(globals.slewFrac2));
		json_object_set_new(root, "slopeLaw", json_integer(globals.slopeLaw));
		json_object_set_new(root, "addressScale", json_integer(globals.addressScale));

		json_t* presets = json_array();
		for (int i = 0; i < spacetime::kPresetSlots; i++) {
			const spacetime::PresetSlot& s = logic.slot(i);
			if (!s.used)
				continue;
			json_t* jp = json_object();
			json_object_set_new(jp, "slot", json_integer(i));
			json_object_set_new(jp, "count", json_integer(s.table.count));
			json_object_set_new(jp, "key", json_integer(s.scaleKey.key));
			json_object_set_new(jp, "scale", json_integer(s.scaleKey.scale));
			json_t* jv = json_array();
			json_t* jt = json_array();
			json_t* jw = json_array();
			for (int st = 0; st < s.table.count; st++) {
				json_array_append_new(jv, json_real(s.table.voltage[st]));
				json_array_append_new(jt, json_real(s.table.time[st]));
				json_array_append_new(jw, json_integer((json_int_t)s.table.program[st].bits));
			}
			json_object_set_new(jp, "voltage", jv);
			json_object_set_new(jp, "time", jt);
			json_object_set_new(jp, "program", jw);
			json_array_append_new(presets, jp);
		}
		json_object_set_new(root, "presets", presets);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		spacetime::ScaleKey sk;
		if ((j = json_object_get(root, "key"))) sk.key = (uint8_t)json_integer_value(j);
		if ((j = json_object_get(root, "scale"))) sk.scale = (uint8_t)json_integer_value(j);
		logic.setScaleKey(sk);
		if ((j = json_object_get(root, "slewFrac1"))) globals.slewFrac1 = (float)json_real_value(j);
		if ((j = json_object_get(root, "slewFrac2"))) globals.slewFrac2 = (float)json_real_value(j);
		if ((j = json_object_get(root, "slopeLaw"))) globals.slopeLaw = (uint8_t)json_integer_value(j);
		if ((j = json_object_get(root, "addressScale"))) globals.addressScale = (uint8_t)json_integer_value(j);

		json_t* presets = json_object_get(root, "presets");
		if (!presets)
			return;
		size_t idx;
		json_t* jp;
		json_array_foreach(presets, idx, jp) {
			json_t* js = json_object_get(jp, "slot");
			if (!js)
				continue;
			int slotIdx = (int)json_integer_value(js);
			if (slotIdx < 0 || slotIdx >= spacetime::kPresetSlots)
				continue;
			spacetime::PresetSlot s;
			s.used = true;
			if ((j = json_object_get(jp, "count")))
				s.table.count = (uint8_t)std::min((json_int_t)spacetime::kMaxStages,
					json_integer_value(j));
			if ((j = json_object_get(jp, "key"))) s.scaleKey.key = (uint8_t)json_integer_value(j);
			if ((j = json_object_get(jp, "scale"))) s.scaleKey.scale = (uint8_t)json_integer_value(j);
			json_t* jv = json_object_get(jp, "voltage");
			json_t* jt = json_object_get(jp, "time");
			json_t* jw = json_object_get(jp, "program");
			for (int st = 0; st < s.table.count; st++) {
				if (jv && json_array_get(jv, st))
					s.table.voltage[st] = (float)json_real_value(json_array_get(jv, st));
				if (jt && json_array_get(jt, st))
					s.table.time[st] = (float)json_real_value(json_array_get(jt, st));
				if (jw && json_array_get(jw, st)) {
					uint32_t bits = (uint32_t)json_integer_value(json_array_get(jw, st));
					if (bits < (1u << 19))
						s.table.program[st].bits = bits;
				}
			}
			logic.setSlot(slotIdx, s);
		}
	}
};

const Program::GestureMap Program::gestureMap[12] = {
	{Program::QUANTIZE_PARAM, spacetime::Field::Quantize},
	{Program::SLOPE_PARAM, spacetime::Field::Slew},
	{Program::RANGE_PARAM, spacetime::Field::Range},
	{Program::VSOURCE_PARAM, spacetime::Field::VoltageSource},
	{Program::STOP_PARAM, spacetime::Field::Stop},
	{Program::SUSTAIN_PARAM, spacetime::Field::Sustain},
	{Program::ENABLE_PARAM, spacetime::Field::Enable},
	{Program::FIRST_PARAM, spacetime::Field::First},
	{Program::LAST_PARAM, spacetime::Field::Last},
	{Program::TSOURCE_PARAM, spacetime::Field::TimeSource},
	{Program::PULSE1_PARAM, spacetime::Field::Pulse1},
	{Program::PULSE2_PARAM, spacetime::Field::Pulse2},
};

// Stage count display: N = 4 x block count; '!' marks a broken chain.
struct StageCountDisplay : Widget {
	Program* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		std::string text = "16";
		if (module) {
			if (module->logic.bulkArmed())
				text = "AL";  // next gesture applies to all stages
			else if (module->displayCount == 0)
				text = "--";
			else
				text = string::f("%d%s", module->displayCount,
					module->displayBroken ? "!" : "");
		}
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 14.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xc1, 0x3c, 0x36));
		nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), NULL);
	}
};

struct ProgramWidget : ModuleWidget {
	ProgramWidget(Program* module) {
		using namespace Layout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Program.svg")));

#ifndef METAMODULE
		spacetime::addTitle(this, 45.72f, 5.6f, "Program");
		spacetime::addSubtitle(this, 45.72f, 10.2f, "PROGRAMMING SECTION");
		spacetime::addKnobLabel(this, DISPLAY_X, 13.2f, "STAGE");
		spacetime::addKnobLabel(this, SCROLL_X, 13.2f, "SELECT");
		spacetime::addKnobLabel(this, CLEAR_X, 13.2f, "CLEAR");
		spacetime::addKnobLabel(this, PULSE1_X, 13.2f, "PULSE 1");
		spacetime::addKnobLabel(this, PULSE2_X, 13.2f, "PULSE 2");
		spacetime::addSectionHeading(this, 37.f, 28.1f, "OUTPUT VOLTAGE");
		spacetime::addKnobLabel(this, QUANT_X, 33.4f, "QUANT");
		spacetime::addKnobLabel(this, SLOPE_X, 33.4f, "SLOPE");
		spacetime::addKnobLabel(this, RANGE_X, 33.4f, "RANGE");
		spacetime::addKnobLabel(this, VSRC_X, 33.4f, "SOURCE");
		spacetime::addCvLabel(this, QUANT_X, 48.9f, "CONT");
		spacetime::addCvLabel(this, SLOPE_X, 48.9f, "STEP");
		spacetime::addCvLabel(this, VSRC_X, 48.9f, "INT / EXT");
		spacetime::addMicroLabel(this, SLOPE_X + 7.8f, 39.6f, "1");
		spacetime::addMicroLabel(this, SLOPE_X + 7.8f, 43.6f, "2");
		spacetime::addMicroLabel(this, RANGE_X + 8.f, 38.6f, "F");
		spacetime::addMicroLabel(this, RANGE_X + 8.f, 41.6f, "H");
		spacetime::addMicroLabel(this, RANGE_X + 8.f, 44.6f, "L");
		spacetime::addCvLabel(this, 37.f, 52.5f, "LIMITED RANGE / OCTAVE");
		{
			static const char* ltdNames[5] = {"-2", "-1", "0", "+1", "+2"};
			for (int i = 0; i < 5; i++)
				spacetime::addCvLabel(this, LTD_X0 + LTD_PITCH * i, 61.9f, ltdNames[i]);
		}
		spacetime::addSectionHeading(this, 39.f, 66.1f, "OPERATING MODE");
		{
			static const char* modeNames[5] = {"STOP", "SUST", "ENBL", "FIRST", "LAST"};
			for (int i = 0; i < 5; i++)
				spacetime::addKnobLabel(this, MODE_X0 + MODE_PITCH * i, 82.6f, modeNames[i]);
		}
		spacetime::addMicroLabel(this, MODE_X0 + MODE_PITCH * 3.5f, 85.4f, "- CYCLE -");
		spacetime::addSectionHeading(this, 39.f, 88.f, "INTERVAL TIME");
		{
			static const char* trangeLabels[4] = {".03", ".3", "3", "30"};
			for (int i = 0; i < 4; i++)
				spacetime::addCvLabel(this, TRANGE_X0 + TRANGE_PITCH * i, 98.5f, trangeLabels[i]);
		}
		spacetime::addMicroLabel(this, 52.5f, 98.5f, "SEC");
		spacetime::addCvLabel(this, TSRC_X, 101.2f, "SOURCE");
		spacetime::addSectionHeading(this, 39.f, 104.3f, "PRESETS / KEY / SCALE");
		{
			static const char* notes[12] = {"C", "C#", "D", "D#", "E", "F",
				"F#", "G", "G#", "A", "A#", "B"};
			for (int i = 0; i < 12; i++)
				spacetime::addMicroLabel(this, PRESET_X0 + PRESET_PITCH * i, 114.4f, notes[i]);
		}
		spacetime::addKnobLabel(this, LOAD_X, 124.7f, "LOAD");
		spacetime::addKnobLabel(this, SAVE_X, 124.7f, "SAVE");
		spacetime::addKnobLabel(this, KEY_X, 124.7f, "KEY");
		spacetime::addKnobLabel(this, SCALE_X, 124.7f, "SCALE");
		spacetime::addIoLabel(this, EXT_X, 13.2f, "EXT");
		for (int i = 0; i < 4; i++) {
			static const char* extNames[4] = {"A", "B", "C", "D"};
			spacetime::addIoLabel(this, EXT_X - 5.7f, EXT_Y0 + EXT_PITCH * i, extNames[i]);
		}
		spacetime::addIoLabel(this, PULSE_RETRIG_X, 58.f, "PULSE");
		spacetime::addIoLabel(this, PULSE_RETRIG_X, 62.f, "RETRIG");
		spacetime::addMicroLabel(this, PULSE_RETRIG_X - 5.f, 69.f, "ON");
		spacetime::addMicroLabel(this, PULSE_RETRIG_X - 5.f, 77.f, "OFF");
		spacetime::addIoLabel(this, POLY_X, 104.9f, "POLY");
		spacetime::addIoLabel(this, POLY_X, 117.9f, "OUT");
#endif

		StageCountDisplay* display = createWidget<StageCountDisplay>(mm2px(Vec(8.f, 16.5f)));
		display->box.size = mm2px(Vec(12.f, 7.f));
		display->module = module;
		addChild(display);

		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(SCROLL_X, ROW1_Y)), module, Program::STAGE_SCROLL_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(CLEAR_X, ROW1_Y)), module, Program::CLEAR_PARAM));
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(PULSE1_X, ROW1_Y)), module, Program::PULSE1_PARAM));
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(PULSE2_X, ROW1_Y)), module, Program::PULSE2_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(PULSE1_X + 4.5f, 15.5f)), module, Program::PULSE1_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(PULSE2_X + 4.5f, 15.5f)), module, Program::PULSE2_LIGHT));

		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(QUANT_X, VOLT_Y)), module, Program::QUANTIZE_PARAM));
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(SLOPE_X, VOLT_Y)), module, Program::SLOPE_PARAM));
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(RANGE_X, VOLT_Y)), module, Program::RANGE_PARAM));
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(VSRC_X, VOLT_Y)), module, Program::VSOURCE_PARAM));

		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(QUANT_X + 4.5f, VOLT_Y)), module, Program::QUANTIZE_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(SLOPE_X + 4.5f, 39.5f)), module, Program::SLOPE1_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(SLOPE_X + 4.5f, 43.5f)), module, Program::SLOPE2_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(RANGE_X + 4.5f, 38.5f)), module, Program::RANGE_FULL_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(RANGE_X + 4.5f, 41.5f)), module, Program::RANGE_HALF_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(RANGE_X + 4.5f, 44.5f)), module, Program::RANGE_LTD_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(VSRC_X + 4.5f, VOLT_Y)), module, Program::VSOURCE_LIGHT));

		spacetime::addLimitedBank(this, module, LTD_X0, LTD_Y, LTD_PITCH,
			Program::LTD_PARAMS, Program::LTD_LIGHTS);

		static const int modeParams[5] = {Program::STOP_PARAM, Program::SUSTAIN_PARAM,
			Program::ENABLE_PARAM, Program::FIRST_PARAM, Program::LAST_PARAM};
		static const int modeLights[5] = {Program::STOP_LIGHT, Program::SUSTAIN_LIGHT,
			Program::ENABLE_LIGHT, Program::FIRST_LIGHT, Program::LAST_LIGHT};
		for (int i = 0; i < 5; i++) {
			float x = MODE_X0 + MODE_PITCH * i;
			addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(x, MODE_Y)), module, modeParams[i]));
			addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(x + 4.2f, 71.5f)), module, modeLights[i]));
		}

		for (int i = 0; i < 4; i++) {
			float x = TRANGE_X0 + TRANGE_PITCH * i;
			addParam(createParamCentered<LEDButton>(mm2px(Vec(x, TIME_Y)), module, Program::TRANGE_PARAMS + i));
			addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(x, TIME_Y)), module, Program::TRANGE_LIGHTS + i));
		}
		addParam(createParamCentered<spacetime::SpringSwitch3>(mm2px(Vec(TSRC_X, TIME_Y)), module, Program::TSOURCE_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(TSRC_X + 4.2f, 90.5f)), module, Program::TSOURCE_LIGHT));

		spacetime::addPresetRow(this, module, PRESET_X0, PRESET_Y, PRESET_PITCH,
			Program::PRESET_PARAMS, Program::PRESET_LIGHTS);
		for (int i = 0; i < 4; i++)
			spacetime::addPresetModeButton(this, module, LOAD_X + 14.f * i, PROW2_Y,
				Program::LOAD_PARAM + i, Program::LOAD_LIGHT + i);

		for (int i = 0; i < 4; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(EXT_X, EXT_Y0 + EXT_PITCH * i)), module, Program::EXT_INPUTS + i));
		addParam(createParamCentered<CKSS>(mm2px(Vec(PULSE_RETRIG_X, PULSE_RETRIG_Y)),
			module, Program::PULSE_RETRIG_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(POLY_X, POLY_Y)), module, Program::POLY_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Program* module = getModule<Program>();
		menu->addChild(new MenuSeparator);
		// Single-pointer alternative to the hardware's hold-scroll-and-press
		// bulk gesture (hold+press needs two hands / MIDI / MetaModule).
		menu->addChild(createCheckMenuItem("Apply next edit to ALL stages",
			"display shows AL",
			[=]() { return module->logic.bulkArmed(); },
			[=]() {
				if (module->logic.bulkArmed())
					module->logic.disarmBulkOnce();
				else
					module->logic.armBulkOnce();
			}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Globals"));
		menu->addChild(createMenuLabel(string::f("Last MIDI seq: %u",
			module->lastMidiEventSeq)));
		menu->addChild(createMenuLabel(string::f("Last MIDI event: type %d index %d value %d f %.3f",
			module->lastMidiType,
			module->lastMidiIndex,
			module->lastMidiValue,
			module->lastMidiFValue)));
		menu->addChild(createMenuLabel(string::f("Last MIDI op count: %d",
			module->lastMidiOpCount)));
		menu->addChild(createIndexSubmenuItem("Slew level 1",
			{"10% of interval", "25% of interval", "40% of interval"},
			[=]() {
				float f = module->globals.slewFrac1;
				return f < 0.175f ? 0 : (f < 0.325f ? 1 : 2);
			},
			[=](int i) {
				static const float v[3] = {0.1f, 0.25f, 0.4f};
				module->globals.slewFrac1 = v[i];
			}));
		menu->addChild(createIndexSubmenuItem("Slew level 2",
			{"50% of interval", "75% of interval", "100% of interval"},
			[=]() {
				float f = module->globals.slewFrac2;
				return f < 0.625f ? 0 : (f < 0.875f ? 1 : 2);
			},
			[=](int i) {
				static const float v[3] = {0.5f, 0.75f, 1.f};
				module->globals.slewFrac2 = v[i];
			}));
		menu->addChild(createIndexSubmenuItem("ADDRESS scaling",
			{"0-10 V spans chain (self-normalizing)", "Fixed 0.5 V per stage"},
			[=]() { return (int)module->globals.addressScale; },
			[=](int i) { module->globals.addressScale = (uint8_t)i; }));
		menu->addChild(createIndexSubmenuItem("Slope law",
			{"Linear", "Exponential (reserved)"},
			[=]() { return (int)module->globals.slopeLaw; },
			[=](int i) { module->globals.slopeLaw = (uint8_t)i; }));
	}
};

Model* modelProgram = createModel<Program, ProgramWidget>("Program");
