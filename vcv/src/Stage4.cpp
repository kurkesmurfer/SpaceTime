#include "plugin.hpp"
#include "paneltheme.hpp"
#include "spacetime_widgets.hpp"
#include "ChainAdapter.hpp"
#include "ProgramLogic.hpp"  // SliderTakeover

// ============================================================================
// STAGE4 — WP7 integrated.
// Owns its 4 stages' program words and slider values (spec rev 4: blocks own
// their data). Edited remotely via edit-ops from PROGRAM; relays the chain
// messages in both directions; shows the edit-select LED and the head
// position dots.
//
// All positions in mm; MUST match res/Stage4.svg.
// ============================================================================

namespace Layout4 {
constexpr float COL_X0 = 9.4f, COL_PITCH = 11.f;
constexpr float VSLIDER_Y = 33.f;
constexpr float TSLIDER_Y = 81.5f;
constexpr float DOTS_Y0 = 55.5f;
constexpr float LEFT_X = 3.2f;
constexpr float RIGHT_X = 47.8f;
} // namespace Layout4

struct Stage4 : Module {
	enum ParamId {
		ENUMS(VOLTAGE_PARAMS, 4),
		ENUMS(TIME_PARAMS, 4),
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(EDIT_LIGHTS, 4),
		ENUMS(HEAD_LIGHTS, 4 * 8 * 3),
		LIGHTS_LEN
	};

	// Owned per-stage state (persisted)
	spacetime::ProgramWord program[4];
	spacetime::SliderTakeover takeV[4], takeT[4];

	// Expander plumbing (buffers owned here, allocated with the module)
	spacetime::MessagePort<spacetime::BlockToAnchorMsg> rightPort;   // table from the next block
	spacetime::MessagePort<spacetime::AnchorToBlocksMsg> leftPort;   // ops/status from the anchor side

	uint32_t lastSeq = 0;
	uint32_t lastSeenSeq = 0;
	uint8_t lastSeenOps = 0;
	uint8_t lastAppliedOps = 0;
	uint8_t lastOpStage = 0;
	uint8_t lastOpField = 0;
	float lastOpValue = 0.f;
	dsp::ClockDivider divider;

	Stage4() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int s = 0; s < 4; s++) {
			configParam(VOLTAGE_PARAMS + s, 0.f, 10.f, 0.f,
				string::f("Stage %d voltage", s + 1), " V");
			configParam(TIME_PARAMS + s, 0.f, 1.f, 0.5f,
				string::f("Stage %d time (within range)", s + 1));
			configLight(EDIT_LIGHTS + s, string::f("Stage %d edit select", s + 1));
			for (int h = 0; h < 8; h++)
				configLight(HEAD_LIGHTS + (s * 8 + h) * 3,
					string::f("Stage %d head %d position", s + 1, h + 1));
		}
		rightPort.attach(rightExpander);
		leftPort.attach(leftExpander);
		divider.setDivision(16);
	}

	void onReset() override {
		for (int s = 0; s < 4; s++) {
			program[s] = spacetime::ProgramWord();
			takeV[s].release();
			takeT[s].release();
		}
	}

	float effVoltage(int s) { return takeV[s].value(params[VOLTAGE_PARAMS + s].getValue()); }
	float effTime(int s) { return takeT[s].value(params[TIME_PARAMS + s].getValue()); }

	void process(const ProcessArgs& args) override {
		if (!divider.process())
			return;
		float dt = args.sampleTime * divider.getDivision();
		using namespace spacetime;

		bool leftIsChain = modelIs(leftExpander.module, modelStage4, modelProgram, modelGlueLeft);
		bool rightIsBlock = modelIs(rightExpander.module, modelStage4, modelGlueRight);

		// ---- Consume the anchor-side message (ops, selection, statuses)
		const AnchorToBlocksMsg* am = leftPort.consume(leftExpander);
		bool amValid = leftIsChain && am->valid;
		int blockIndex = amValid ? am->hopIndex : 0;

		if (amValid && am->seq != lastSeq) {
			lastSeq = am->seq;
			uint8_t appliedNow = 0;
			if (am->opCount > 0) {
				lastSeenSeq = am->seq;
				lastSeenOps = am->opCount;
				lastAppliedOps = 0;
			}
			for (int i = 0; i < am->opCount && i < kMaxOpsPerTick; i++) {
				const EditOp& op = am->ops[i];
				if (i == 0) {
					lastOpStage = op.stageIndex;
					lastOpField = (uint8_t)op.field;
					lastOpValue = op.value;
				}
				if (!opTargetsBlock(op, blockIndex))
					continue;
				int local = op.stageIndex - blockIndex * kStagesPerBlock;
				if (local < 0 || local >= 4 || !std::isfinite(op.value))
					continue;
				if (op.field == Field::Voltage) {
					if (op.value >= kVoltageMin && op.value <= kVoltageMax) {
						if (op.flags & EDIT_OP_MOVE_SLIDER) {
							params[VOLTAGE_PARAMS + local].setValue(op.value);
							takeV[local].release();
						}
						else {
							takeV[local].engage(op.value, params[VOLTAGE_PARAMS + local].getValue());
						}
						appliedNow++;
					}
				}
				else if (op.field == Field::Time) {
					if (op.value >= kTimeSliderMin && op.value <= kTimeSliderMax) {
						if (op.flags & EDIT_OP_MOVE_SLIDER) {
							params[TIME_PARAMS + local].setValue(op.value);
							takeT[local].release();
						}
						else {
							takeT[local].engage(op.value, params[TIME_PARAMS + local].getValue());
						}
						appliedNow++;
					}
				}
				else if (op.value >= 0.f && op.value == std::floor(op.value)) {
					if (setProgramField(program[local], op.field, (uint32_t)op.value))
						appliedNow++;
				}
			}
			if (am->opCount > 0)
				lastAppliedOps = appliedNow;
		}

		// ---- Relay the anchor message rightward (next block dedups by seq)
		if (rightIsBlock) {
			AnchorToBlocksMsg* out = rightNeighborProducer<AnchorToBlocksMsg>(
				this, modelStage4, modelGlueRight);
			if (out) {
				if (amValid)
					blockRelayRight(*am, *out);
				else
					out->valid = false;
				flipRightNeighbor(this);
			}
		}

		// ---- Build and send the leftward table (own segment + sub-chain)
		if (leftIsChain) {
			BlockToAnchorMsg* out = leftNeighborProducer<BlockToAnchorMsg>(
				this, modelStage4, modelProgram, modelGlueLeft);
			if (out) {
				BlockSegment seg;
				for (int s = 0; s < 4; s++) {
					seg.voltage[s] = effVoltage(s);
					seg.time[s] = effTime(s);
					seg.program[s] = program[s];
				}
				const BlockToAnchorMsg* rm = rightPort.consume(rightExpander);
				const StageTable* fromRight =
					(rightIsBlock && rm->valid) ? &rm->table : NULL;
				blockRelayLeft(seg, fromRight, *out);
				flipLeftNeighbor(this);
			}
		}

		// ---- LEDs
		for (int s = 0; s < 4; s++) {
			bool sel = amValid &&
				(int)am->selectedStage == blockIndex * kStagesPerBlock + s;
			lights[EDIT_LIGHTS + s].setBrightnessSmooth(sel ? 1.f : 0.f, dt);
			// Head position dots: repaint from the merged statuses.
			for (int h = 0; h < 8; h++) {
				float b = 0.f;
				if (amValid) {
					for (int i = 0; i < am->headCount && i < kMaxHeads; i++) {
						const HeadStatus& st = am->status[i];
						if (st.headId == h &&
						    (int)st.currentStage == blockIndex * kStagesPerBlock + s)
							b = (st.runState == RUN_STOPPED) ? 0.35f : 1.f;
					}
				}
				setHeadDot(this, HEAD_LIGHTS + (s * 8 + h) * 3, h, b, dt);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* words = json_array();
		for (int s = 0; s < 4; s++)
			json_array_append_new(words, json_integer((json_int_t)program[s].bits));
		json_object_set_new(root, "program", words);

		json_t* take = json_array();
		for (int s = 0; s < 4; s++) {
			json_t* pair = json_object();
			json_object_set_new(pair, "vStored", json_real(takeV[s].stored));
			json_object_set_new(pair, "vActive", json_boolean(takeV[s].active));
			json_object_set_new(pair, "vSide", json_integer(takeV[s].side));
			json_object_set_new(pair, "tStored", json_real(takeT[s].stored));
			json_object_set_new(pair, "tActive", json_boolean(takeT[s].active));
			json_object_set_new(pair, "tSide", json_integer(takeT[s].side));
			json_array_append_new(take, pair);
		}
		json_object_set_new(root, "takeover", take);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* words = json_object_get(root, "program");
		if (words) {
			for (int s = 0; s < 4; s++) {
				json_t* w = json_array_get(words, s);
				if (w) {
					uint32_t bits = (uint32_t)json_integer_value(w);
					if (bits < (1u << 19))
						program[s].bits = bits;
				}
			}
		}
		json_t* take = json_object_get(root, "takeover");
		if (take) {
			for (int s = 0; s < 4; s++) {
				json_t* pair = json_array_get(take, s);
				if (!pair)
					continue;
				json_t* j;
				if ((j = json_object_get(pair, "vStored"))) takeV[s].stored = (float)json_real_value(j);
				if ((j = json_object_get(pair, "vActive"))) takeV[s].active = json_boolean_value(j);
				if ((j = json_object_get(pair, "vSide"))) takeV[s].side = (int)json_integer_value(j);
				if ((j = json_object_get(pair, "tStored"))) takeT[s].stored = (float)json_real_value(j);
				if ((j = json_object_get(pair, "tActive"))) takeT[s].active = json_boolean_value(j);
				if ((j = json_object_get(pair, "tSide"))) takeT[s].side = (int)json_integer_value(j);
			}
		}
	}
};

struct Stage4Widget : ModuleWidget {
	Stage4Widget(Stage4* module) {
		using namespace Layout4;
		setModule(module);
		setPanel(spacetime::createThemedPanel(
			asset::plugin(pluginInstance, "res/Stage4-light.svg"),
			asset::plugin(pluginInstance, "res/Stage4.svg")));

#ifndef METAMODULE
		spacetime::addTitle(this, 25.4f, 5.6f, "Stage4");
		spacetime::addSubtitle(this, 25.4f, 10.2f, "STAGE BLOCK");
		spacetime::addSectionHeading(this, 25.4f, 15.8f, "OUTPUT VOLTAGE");
		spacetime::addSectionHeading(this, 25.4f, 64.f, "INTERVAL TIME");
		{
			static const char* quart[4] = {"D", "C", "B", "A"};
			for (int i = 0; i < 4; i++) {
				spacetime::addMicroLabel(this, LEFT_X, 22.5f + 6.f * i, quart[i]);
				spacetime::addMicroLabel(this, LEFT_X, 71.f + 6.f * i, quart[i]);
			}
		}
		spacetime::addMicroLabel(this, RIGHT_X, 21.f, "10");
		spacetime::addMicroLabel(this, RIGHT_X, 45.f, "0");
		spacetime::addMicroLabel(this, RIGHT_X, 69.5f, "30");
		spacetime::addMicroLabel(this, RIGHT_X, 93.5f, "2");
		for (int s = 0; s < 4; s++)
			spacetime::addKnobLabel(this, COL_X0 + COL_PITCH * s, 100.5f,
				string::f("%d", s + 1));
		addChild(new spacetime::CornerMark(50.8f, 128.5f, 0.70f, 3.21f, 4.f));
#endif

		for (int s = 0; s < 4; s++) {
			float x = COL_X0 + COL_PITCH * s;
			addParam(createParamCentered<VCVSlider>(mm2px(Vec(x, VSLIDER_Y)), module, Stage4::VOLTAGE_PARAMS + s));
			addParam(createParamCentered<VCVSlider>(mm2px(Vec(x, TSLIDER_Y)), module, Stage4::TIME_PARAMS + s));
			spacetime::addStageLedCluster(this, module, x, DOTS_Y0,
				Stage4::EDIT_LIGHTS + s, Stage4::HEAD_LIGHTS + s * 8 * 3);
		}
	}

	void appendContextMenu(Menu* menu) override {
		Stage4* module = getModule<Stage4>();
		menu->addChild(new MenuSeparator);
		spacetime::appendPanelThemeMenu(menu);
		menu->addChild(new MenuSeparator);
		if (!module) {
			menu->addChild(createMenuLabel("STAGE4 diagnostics unavailable"));
			return;
		}
		menu->addChild(createMenuLabel(string::f("Last non-empty op seq: %u",
			module->lastSeenSeq)));
		menu->addChild(createMenuLabel(string::f("Last non-empty ops seen/applied: %d/%d",
			module->lastSeenOps,
			module->lastAppliedOps)));
		menu->addChild(createMenuLabel(string::f("First op: stage %d field %d value %.3f",
			module->lastOpStage + 1,
			module->lastOpField,
			module->lastOpValue)));
	}
};

Model* modelStage4 = createModel<Stage4, Stage4Widget>("Stage4");
