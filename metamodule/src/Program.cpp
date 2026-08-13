#include <rack.hpp>
#include <metamodule/VCVTextDisplay.hpp>
#include "SpaceTimeEngine.hpp"
#include "TimingBus.hpp"
#include "RemoteBus.hpp"

#include <algorithm>
#include <cstdio>

using namespace rack;

extern Plugin* pluginInstance;

namespace {

// Renamed from SpaceTimeCore/slug "Core" (2026-08-09, Peet): the merged
// module now carries all of VCV Program.cpp's role -- key/scale, presets,
// and the twelve stage-modifier gestures -- on top of what Core already had
// (MIDI ingestion, the full 64-stage table, globals). Slug is now "Program",
// matching VCV's real Program.cpp by explicit instruction: "The metamodule
// parallel MUST have the same slug... that the mechanics are all different
// is irrelevant, as that does not show up in the yml." vcv/src/Program.cpp
// is untouched and unrelated in implementation -- same slug, deliberately
// different mechanics (this module owns MIDI ingestion and the full table
// directly; VCV's Program.cpp owns neither, relying on Stage4/Midi
// neighbors instead). See METAMODULE_EXPANDER_BUS_PLAN.md.
//
// 2026-08-09 (Peet, correcting the first pass of this section): "did not
// carry over completely... my intention was to copy the program UI
// including everything, incl polyphonic cv." The field-select-knob +
// Apply+/- shortcut below (kept in a prior revision, described in the now-
// stale paragraph this replaces) undershot that: it reached every
// dsp::ProgramLogic entry point functionally, but collapsed VCV's twelve
// individually-named gesture controls, five Limited-octave buttons and four
// Time-range buttons down to three widgets, which isn't a port, it's a
// different, smaller interface. This revision gives every one of those
// twenty-one VCV controls its own button (spacetime::SpringSwitch3 itself
// still isn't available to this build -- it lives in vcv/widgets -- so each
// spring is realized as an explicit Up/Down LEDButton pair rather than one
// three-position widget; functionally identical, same emitModifier() call
// per press). PRESET_PARAM/SAVE_PARAM/LOAD_PARAM remain a direct knob +
// two buttons rather than VCV's twelve-button press-mode-then-digit pad
// (dsp::PresetRowLogic, a second, separate modal class VCV's Program.cpp
// uses beyond dsp::ProgramLogic) -- reaches the same 12 slots and key/scale
// range with no loss of range, just a different control, kept deliberately
// rather than adding another 16 widgets to an already-large panel. Flagged
// to Peet as the one remaining intentional compaction, distinct from the
// gap this revision actually closes.
struct SpaceTimeProgram : Module {
	enum ParamId {
		STAGE_PARAM,
		PRESET_PARAM,
		CONTROL_CHANNEL_PARAM,
		SLIDER_CHANNEL_PARAM,
		SAVE_PARAM,
		LOAD_PARAM,
		CLEAR_PARAM,
		PULSE_RETRIG_PARAM,
		KEY_PARAM,
		SCALE_PARAM,
		BULK_PARAM,
		ENUMS(GESTURE_UP_PARAMS, 12),
		ENUMS(GESTURE_DOWN_PARAMS, 12),
		ENUMS(LTD_PARAMS, 5),
		ENUMS(TRANGE_PARAMS, 4),
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(EXT_INPUTS, 4),
		INPUTS_LEN
	};
	enum OutputId {
		SELECTED_V_OUTPUT,
		SELECTED_TIME_OUTPUT,
		// Real polyphonic output, heads 1-4 (Peet, 2026-08-09): MetaModule's
		// Port caps at PORT_MAX_CHANNELS = 4 (confirmed in the SDK), vs VCV's
		// single POLY_OUTPUT carrying all 8 heads. Per Peet's decision, heads
		// 5-8 simply have no CV output here -- no second jack, no dynamic
		// active-head selection, just a fixed 4-channel window onto heads 1-4.
		POLY_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		MIDI_IN_LIGHT,
		MIDI_CLOCK_LIGHT,
		MIDI_OUT_LIGHT,
		STATUS_DISPLAY,
		LIGHTS_LEN
	};

	// GESTURE_UP_PARAMS/GESTURE_DOWN_PARAMS index -> dsp/StageTable.hpp
	// Field, same order as VCV Program.cpp's gestureMap (QUANTIZE/SLOPE/
	// RANGE/VSOURCE/STOP/SUSTAIN/ENABLE/FIRST/LAST/TSOURCE/PULSE1/PULSE2).
	static const int kModFieldCount = 12;
	static constexpr spacetime::Field kModFields[kModFieldCount] = {
		spacetime::Field::Quantize, spacetime::Field::Slew, spacetime::Field::Range,
		spacetime::Field::VoltageSource, spacetime::Field::Stop, spacetime::Field::Sustain,
		spacetime::Field::Enable, spacetime::Field::First, spacetime::Field::Last,
		spacetime::Field::TimeSource, spacetime::Field::Pulse1, spacetime::Field::Pulse2,
	};
	static constexpr const char* kModFieldNames[kModFieldCount] = {
		"Quantize", "Slew", "Range", "Voltage source", "Stop", "Sustain",
		"Enable", "First", "Last", "Time source", "Pulse 1", "Pulse 2",
	};
	// Down/Up tooltip pairs, exactly matching VCV Program.cpp's spring3
	// label text per field (vcv/src/Program.cpp's configSpring3 calls).
	static constexpr const char* kModDownLabels[kModFieldCount] = {
		"Continuous", "Reduce slew", "Half (0-5 V)", "Internal",
		"Remove", "Remove", "Remove", "Remove", "Remove",
		"Internal (slider + range)", "Remove from stage", "Remove from stage",
	};
	static constexpr const char* kModUpLabels[kModFieldCount] = {
		"Quantize", "Add slew", "Full (0-10 V)", "External (slider selects A-D)",
		"Add", "Add", "Add", "Set", "Set",
		"External (A-D CV)", "Add to stage", "Add to stage",
	};

	spacetime::SpaceTimeEngine engine;
	midi::InputQueue midiInput;
	midi::Output midiOutput;
	dsp::SchmittTrigger saveTrigger;
	dsp::SchmittTrigger loadTrigger;
	dsp::SchmittTrigger clearTrigger;
	dsp::SchmittTrigger bulkTrigger;
	dsp::SchmittTrigger gestureUpTrigger[kModFieldCount];
	dsp::SchmittTrigger gestureDownTrigger[kModFieldCount];
	dsp::SchmittTrigger ltdTrigger[5];
	dsp::SchmittTrigger trangeTrigger[4];
	dsp::ClockDivider controlDivider;
	float midiLight = 0.f;
	float clockLight = 0.f;
	float outLight = 0.f;
	// EB8 addendum: raw event counters alongside the decayed lights above --
	// see MetaModuleMidiStatusRegistry's header comment (dsp/MetaModuleRemoteBus.hpp)
	// for why a Midi monitor reads counters, not this module's own already-
	// decayed brightness values.
	uint32_t midiInSeq = 0;
	uint32_t midiClkSeq = 0;
	uint32_t midiOutSeq = 0;
	uint32_t busToken = timingBusRegistry.makeToken();
	int instrumentId = 0;
	bool ownsTimingBus = false;

	// EB3 wiring (2026-08-09, Peet: "a stage does not have much data... best
	// to keep that all in core... StageRemote would send commands to
	// update, midi does that too"): per-bank feed-forward shadow, the same
	// trick HeadRemote already uses for MIDI-vs-knob contention, just
	// running here since this module (not Stage4) owns the table. Program's
	// own table write (MIDI/preset/gesture) always wins over a stale Stage4
	// publish; a genuine Stage4 slider move is folded in via EditOp/
	// applyEdit, the same call MIDI's own slider CC already uses. See
	// syncStageBanks() below.
	float bankShadowVoltage[16][4] = {};
	float bankShadowTime[16][4] = {};
	bool bankWasValid[16] = {};

	struct RackMidiSink : spacetime::MidiOutputSink {
		SpaceTimeProgram* owner;
		explicit RackMidiSink(SpaceTimeProgram* module) : owner(module) {}
		void send(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) override {
			midi::Message message;
			message.setStatus(status);
			message.setChannel(channel & 0xf);
			message.setNote(data1);
			message.setValue(data2);
			owner->midiOutput.sendMessage(message);
			owner->outLight = 1.f;
			owner->midiOutSeq++;
		}
	};

	SpaceTimeProgram() {
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
		configParam(KEY_PARAM, 0.f, 11.f, 0.f, "Key", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configSwitch(SCALE_PARAM, 0.f, 2.f, 0.f, "Scale", {"Major", "Minor", "Chromatic"});
		configButton(BULK_PARAM, "Arm bulk edit (next gesture applies to all stages)");
		for (int i = 0; i < kModFieldCount; i++) {
			configButton(GESTURE_DOWN_PARAMS + i, string::f("%s: %s", kModFieldNames[i], kModDownLabels[i]));
			configButton(GESTURE_UP_PARAMS + i, string::f("%s: %s", kModFieldNames[i], kModUpLabels[i]));
		}
		{
			static const char* ltdNames[5] = {"-2", "-1", "0", "+1", "+2"};
			for (int i = 0; i < 5; i++)
				configButton(LTD_PARAMS + i, string::f("Limited range, octave %s", ltdNames[i]));
		}
		{
			static const char* trangeNames[4] = {
				"Time range 0.002-0.03 s", "Time range 0.02-0.3 s",
				"Time range 0.2-3 s", "Time range 2-30 s"};
			for (int i = 0; i < 4; i++)
				configButton(TRANGE_PARAMS + i, trangeNames[i]);
		}
		for (int i = 0; i < 4; i++)
			configInput(EXT_INPUTS + i, string::f("External %c", 'A' + i));
		configOutput(SELECTED_V_OUTPUT, "Selected stage voltage");
		configOutput(SELECTED_TIME_OUTPUT, "Selected stage time slider");
		configOutput(POLY_OUTPUT, "Heads CV (polyphonic, heads 1-4)");
		configLight(MIDI_IN_LIGHT, "MIDI input activity");
		configLight(MIDI_CLOCK_LIGHT, "MIDI clock activity");
		configLight(MIDI_OUT_LIGHT, "MIDI output activity");
		midiInput.channel = -1;
		midiOutput.channel = -1;
		controlDivider.setDivision(16);
		ownsTimingBus = timingBusRegistry.registerCore(instrumentId, busToken);
	}

	~SpaceTimeProgram() override {
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
			midiInSeq++;
			if (status == 0xF8) {
				clockLight = 1.f;
				midiClkSeq++;
			}
			if (engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SELECT_PREV ||
				engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SELECT_NEXT)
				params[STAGE_PARAM].setValue((float)engine.program().selectedStage());
			if (engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SET_PULSE_RETRIG)
				params[PULSE_RETRIG_PARAM].setValue(engine.globals().pulseRetrig ? 1.f : 0.f);
			if (engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SET_KEY)
				params[KEY_PARAM].setValue((float)engine.program().scaleKey().key);
			if (engine.lastAppliedMidiType() == spacetime::MIDI_PROG_SET_SCALE)
				params[SCALE_PARAM].setValue((float)engine.program().scaleKey().scale);
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
			spacetime::ScaleKey scaleKey = engine.program().scaleKey();
			scaleKey.key = (uint8_t)clamp((int)std::round(params[KEY_PARAM].getValue()), 0, 11);
			scaleKey.scale = (uint8_t)clamp((int)std::round(params[SCALE_PARAM].getValue()), 0, 2);
			engine.program().setScaleKey(scaleKey);
			for (int i = 0; i < 4; i++)
				engine.setExternal(i, inputs[EXT_INPUTS + i].getVoltage(), inputs[EXT_INPUTS + i].isConnected());

			int preset = clamp((int)std::round(params[PRESET_PARAM].getValue()), 0, 11);
			if (saveTrigger.process(params[SAVE_PARAM].getValue()))
				engine.savePreset(preset);
			if (loadTrigger.process(params[LOAD_PARAM].getValue()))
				engine.loadPreset(preset);
			if (clearTrigger.process(params[CLEAR_PARAM].getValue()))
				applyOps(engine.program().emitClear(engine.table(),
					opsBuffer, kOpsBufferLen));
			if (bulkTrigger.process(params[BULK_PARAM].getValue()))
				engine.program().armBulkOnce();
			for (int i = 0; i < kModFieldCount; i++) {
				if (gestureUpTrigger[i].process(params[GESTURE_UP_PARAMS + i].getValue()))
					applyOps(engine.program().emitModifier(kModFields[i], 1, engine.table(),
						opsBuffer, kOpsBufferLen));
				if (gestureDownTrigger[i].process(params[GESTURE_DOWN_PARAMS + i].getValue()))
					applyOps(engine.program().emitModifier(kModFields[i], -1, engine.table(),
						opsBuffer, kOpsBufferLen));
			}
			for (int i = 0; i < 5; i++)
				if (ltdTrigger[i].process(params[LTD_PARAMS + i].getValue()))
					applyOps(engine.program().emitLimited(i, engine.table(), opsBuffer, kOpsBufferLen));
			for (int i = 0; i < 4; i++)
				if (trangeTrigger[i].process(params[TRANGE_PARAMS + i].getValue()))
					applyOps(engine.program().emitTimeRange(i, engine.table(), opsBuffer, kOpsBufferLen));
			syncStageBanks();
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

		// Heads CV, polyphonic, heads 1-4 only (Peet, 2026-08-09) -- see the
		// OutputId comment for why this doesn't cover heads 5-8.
		outputs[POLY_OUTPUT].setChannels(4);
		for (int h = 0; h < 4; h++)
			outputs[POLY_OUTPUT].setVoltage(engine.headOut(h).cv, h);
		midiLight = std::fmax(0.f, midiLight - args.sampleTime * 8.f);
		clockLight = std::fmax(0.f, clockLight - args.sampleTime * 10.f);
		outLight = std::fmax(0.f, outLight - args.sampleTime * 4.f);
		lights[MIDI_IN_LIGHT].setBrightnessSmooth(midiLight, args.sampleTime);
		lights[MIDI_CLOCK_LIGHT].setBrightnessSmooth(clockLight, args.sampleTime);
		lights[MIDI_OUT_LIGHT].setBrightnessSmooth(outLight, args.sampleTime);
	}

	// Shared scratch buffer for the panel-driven emit*() calls above -- sized
	// for the worst case (emitLimited, up to 2 ops/stage, bulk-armed across
	// all 64 stages), applied immediately within the same control tick via
	// applyEdit (same pattern CLEAR_PARAM already used before this file grew
	// to cover the rest of Program's gestures).
	static const int kOpsBufferLen = spacetime::kMaxStages * 2;
	spacetime::EditOp opsBuffer[kOpsBufferLen];

	void applyOps(int count) {
		for (int i = 0; i < count && i < kOpsBufferLen; i++)
			engine.applyEdit(opsBuffer[i]);
	}

	// Reads all 16 stage-bank slots (MetaModuleStageBankRegistry, EB3) and
	// arbitrates each of their 4 stages' voltage/time against this module's
	// own table -- see the bankShadow* member comment for why. A bank with
	// no bound Stage4 (or a duplicate) reads as invalid and is skipped
	// entirely; this module's table is authoritative regardless either way,
	// so nothing here is required for correctness, only for a bound Stage4's
	// slider motion to actually reach the table.
	void syncStageBanks() {
		int opCount = 0;
		for (int b = 0; b < 16; b++) {
			spacetime::BlockSegment seg;
			bool valid = stageBankRegistry.readBank(instrumentId, b, seg);
			if (!valid) {
				bankWasValid[b] = false;
				continue;
			}
			if (!bankWasValid[b]) {
				// Freshly bound: trust Stage4's current sliders as the new
				// baseline, matching plugging in a physical slider module
				// (its position IS the value, not something to reconcile
				// against table history) -- rather than guessing whether
				// the table's pre-existing value or the fresh module's
				// default slider position should win.
				for (int i = 0; i < 4; i++) {
					bankShadowVoltage[b][i] = seg.voltage[i];
					bankShadowTime[b][i] = seg.time[i];
				}
				bankWasValid[b] = true;
				continue;
			}
			for (int i = 0; i < 4; i++) {
				int stage = b * spacetime::kStagesPerBlock + i;
				float localV = engine.table().voltage[stage];
				if (localV != bankShadowVoltage[b][i]) {
					// This module's own table moved since last sync (MIDI,
					// preset load, gesture) -- that wins. Stage4's slider
					// display just goes stale until physically touched,
					// same as HeadRemote's knobs under MIDI control.
					bankShadowVoltage[b][i] = localV;
				} else if (seg.voltage[i] != bankShadowVoltage[b][i] && opCount < kOpsBufferLen) {
					opsBuffer[opCount++] = spacetime::EditOp((uint8_t)stage, spacetime::Field::Voltage, seg.voltage[i]);
					bankShadowVoltage[b][i] = seg.voltage[i];
				}
				float localT = engine.table().time[stage];
				if (localT != bankShadowTime[b][i]) {
					bankShadowTime[b][i] = localT;
				} else if (seg.time[i] != bankShadowTime[b][i] && opCount < kOpsBufferLen) {
					opsBuffer[opCount++] = spacetime::EditOp((uint8_t)stage, spacetime::Field::Time, seg.time[i]);
					bankShadowTime[b][i] = seg.time[i];
				}
			}
		}
		applyOps(opCount);
	}

	// EB6 (METAMODULE_EXPANDER_BUS_PLAN.md): mirrors Program's VCV-side
	// convention exactly -- a bare '!' appended next to the field it
	// qualifies, no second warning language. On VCV, Program infers a broken
	// chain from a gap in the physically walked expander run (ChainAdapter.hpp
	// / dsp/Chain.hpp). This module has no positional chain to walk; the one
	// directly analogous, currently-real hazard is a second instance bound to
	// this same Instrument ID (the timing bus's own coreCount, already relied
	// on by publishTiming() and by TimingMonitor.cpp's identical idiom).
	//
	// This deliberately does NOT check MetaModuleStageBankRegistry /
	// MetaModuleHeadRegistry (EB3-EB5) for "missing" banks or heads: this
	// module currently holds its full 64-stage table and all 8 heads
	// internally and never defers to a Remote for them, so every one of
	// those 16+8 slots reads as unclaimed by design today, not as a fault.
	bool coreConflict() const {
		return timingBusRegistry.bus(instrumentId).coreCount.load(std::memory_order_acquire) != 1;
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
			"STAGE %02d V %.2f T %.3f%s\nID %c P%02d S%02d K%02d SC%d\nRUN 1-8 %s\nMIDI CH%02d %s%03d V%03d %s",
			selected + 1, engine.table().voltage[selected], engine.table().time[selected],
			coreConflict() ? "!" : "",
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

		// EB8 (METAMODULE_EXPANDER_BUS_PLAN.md): purely additive head-
		// relocation broadcast, gated by the exact same "sole owner" check
		// above rather than re-deriving it -- MetaModuleStageTableRegistry
		// and MetaModuleHeadMidiRegistry both deliberately have no ownership
		// CAS of their own (see their header comments) and rely on the
		// caller only publishing while it verifiably holds ownership, which
		// is precisely what the early-return above already guarantees.
		stageTableRegistry.publishTable(instrumentId, engine.table());
		stageTableRegistry.publishContext(instrumentId, engine.ext(), engine.globals(),
			engine.program().scaleKey());

		spacetime::MetaModuleHeadMidiSnapshot midiSnapshot;
		midiSnapshot.midiClockSeq = engine.midi().midiClockSeq;
		midiSnapshot.midiStartSeq = engine.midi().midiStartSeq;
		midiSnapshot.midiStopSeq = engine.midi().midiStopSeq;
		midiSnapshot.midiContinueSeq = engine.midi().midiContinueSeq;
		for (int h = 0; h < spacetime::kMaxHeads; h++) {
			for (int c = 0; c < spacetime::kMidiHeadControls; c++) {
				midiSnapshot.headCcSeq[h][c] = engine.midi().headCcSeq[h][c];
				midiSnapshot.headCcValue[h][c] = engine.midi().headCcValue[h][c];
			}
		}
		headMidiRegistry.publishMidi(instrumentId, midiSnapshot);

		spacetime::MetaModuleMidiStatusSnapshot statusSnapshot;
		statusSnapshot.inSeq = midiInSeq;
		statusSnapshot.clkSeq = midiClkSeq;
		statusSnapshot.outSeq = midiOutSeq;
		statusSnapshot.controlChannel = (uint8_t)engine.midi().controlChannel;
		statusSnapshot.sliderChannel = (uint8_t)engine.midi().sliderChannel;
		statusSnapshot.lastStatus = engine.midi().lastStatus;
		statusSnapshot.lastChannel = engine.midi().lastChannel;
		statusSnapshot.lastNumber = engine.midi().lastNumber;
		statusSnapshot.lastValue = engine.midi().lastValue;
		statusSnapshot.lastRoute = engine.midi().lastRoute;
		midiStatusRegistry.publish(instrumentId, statusSnapshot);
	}
};

struct SpaceTimeProgramWidget : ModuleWidget {
	SpaceTimeProgramWidget(SpaceTimeProgram* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Program.svg")));
		auto display = createWidget<MetaModule::VCVTextDisplay>(mm2px(Vec(4.f, 12.f)));
		display->box.size = mm2px(Vec(73.f, 27.f));
		display->firstLightId = SpaceTimeProgram::STATUS_DISPLAY;
		display->font = "Default_10";
		display->color = Colors565::White;
		addChild(display);

		// Standard-height, full-display-width layout. The previous 16 HP panel
		// was stretched to 305 mm to stack these controls vertically, which is
		// not a valid Eurorack/MetaModule panel. At 32 HP the same complete
		// control set fits at 128.5 mm without overlap or hidden rows.
		static const float topX[4] = {91.f, 111.f, 131.f, 151.f};
		for (int i = 0; i < 4; i++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(topX[i], 20.f)), module, SpaceTimeProgram::STAGE_PARAM + i));
		}
		addParam(createParamCentered<LEDButton>(mm2px(Vec(topX[0], 36.f)), module, SpaceTimeProgram::SAVE_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(topX[1], 36.f)), module, SpaceTimeProgram::LOAD_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(topX[2], 36.f)), module, SpaceTimeProgram::CLEAR_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(topX[3], 36.f)), module, SpaceTimeProgram::PULSE_RETRIG_PARAM));

		static const float extX[4] = {9.f, 23.f, 37.f, 51.f};
		for (int i = 0; i < 4; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(extX[i], 52.f)), module, SpaceTimeProgram::EXT_INPUTS + i));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(68.f, 52.f)), module, SpaceTimeProgram::SELECTED_V_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(82.f, 52.f)), module, SpaceTimeProgram::SELECTED_TIME_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(96.f, 52.f)), module, SpaceTimeProgram::POLY_OUTPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(116.f, 52.f)), module, SpaceTimeProgram::KEY_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(137.f, 52.f)), module, SpaceTimeProgram::SCALE_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(156.f, 52.f)), module, SpaceTimeProgram::BULK_PARAM));

		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(69.f, 6.f)), module, SpaceTimeProgram::MIDI_IN_LIGHT));
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(75.f, 6.f)), module, SpaceTimeProgram::MIDI_CLOCK_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(81.f, 6.f)), module, SpaceTimeProgram::MIDI_OUT_LIGHT));

		// Four modifier fields per row: Down/Up pairs across eight columns.
		static const float gestureX[8] = {11.f, 31.f, 51.f, 71.f, 91.f, 111.f, 131.f, 151.f};
		static const float gestureY[3] = {76.f, 93.f, 110.f};
		for (int row = 0; row < 3; row++) {
			for (int pair = 0; pair < 4; pair++) {
				int field = row * 4 + pair;
				addParam(createParamCentered<LEDButton>(mm2px(Vec(gestureX[pair * 2], gestureY[row])), module, SpaceTimeProgram::GESTURE_DOWN_PARAMS + field));
				addParam(createParamCentered<LEDButton>(mm2px(Vec(gestureX[pair * 2 + 1], gestureY[row])), module, SpaceTimeProgram::GESTURE_UP_PARAMS + field));
			}
		}

		static const float ltdX[5] = {8.f, 22.f, 36.f, 50.f, 64.f};
		for (int i = 0; i < 5; i++)
			addParam(createParamCentered<LEDButton>(mm2px(Vec(ltdX[i], 123.f)), module, SpaceTimeProgram::LTD_PARAMS + i));
		static const float trangeX[4] = {96.f, 115.f, 134.f, 153.f};
		for (int i = 0; i < 4; i++)
			addParam(createParamCentered<LEDButton>(mm2px(Vec(trangeX[i], 123.f)), module, SpaceTimeProgram::TRANGE_PARAMS + i));
	}

	void appendContextMenu(Menu* menu) override {
		SpaceTimeProgram* module = getModule<SpaceTimeProgram>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		static const char* instrumentLabels[] = {"A", "B", "C", "D"};
		menu->addChild(createSubmenuItem("Instrument ID", [=]() {
			// EB6: same '!' convention as the panel display, spelled out here
			// since the menu has room for the word rather than just the glyph.
			return std::string(instrumentLabels[module->instrumentId]) +
				(module->coreConflict() ? " (duplicate Program!)" : "");
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

Model* modelSpaceTimeProgram = createModel<SpaceTimeProgram, SpaceTimeProgramWidget>("Program");
