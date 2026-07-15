#pragma once
// SpaceTime — expander chain protocol (WP4).
// Rack-free: no Rack SDK includes permitted in this file.
//
// Rack placement: [HEAD]...[HEAD][PROGRAM][STAGE4]...[STAGE4], contiguous.
// Stage index runs left-to-right across blocks; head index 0 sits adjacent
// to PROGRAM, increasing leftward. A gap or foreign module terminates the
// chain on that side.
//
// The protocol is self-organizing — no module needs global knowledge:
//   * leftward block tables build by PREPENDING the local segment to the
//     table received from the right (so the block nearest the anchor holds
//     the lowest stage indices);
//   * module indices derive from hop counters incremented at each relay;
//   * head statuses merge by APPENDING at each rightward hop.
// Latency is one tick per hop, control-rate data only (spec: irrelevant).
//
// All message structs are fixed-size and must stay assignable by plain
// copy — they live in double-buffered expander memory allocated in module
// constructors, NEVER in process().

#include <cstdint>
#include <cmath>
#include "StageTable.hpp"

namespace spacetime {

static const uint32_t kChainProtocolVersion = 3;
static const int kMidiHeadControls = 14;
static const int kMaxMidiProgramEvents = 64;

// ---- Globals travelling with the anchor broadcast ---------------------------
// Context-menu globals (owned by PROGRAM). Slew fractions are the Q1
// placeholder constants — single definition point, WP5 consumes them.
struct Globals {
	float slewFrac1;      // slew level 1: fraction of stage interval
	float slewFrac2;      // slew level 2: fraction of stage interval
	uint8_t slopeLaw;     // 0 = linear, 1 = exponential
	uint8_t addressScale; // 0 = self-normalizing 0-10 V, 1 = fixed 0.5 V/stage
	bool pulseRetrig;     // hardware-compatible P1/P2 low notch at stage boundaries

	Globals()
		: slewFrac1(0.25f), slewFrac2(0.5f), slopeLaw(0), addressScale(0),
		  pulseRetrig(true) {}
};

struct ScaleKey {
	uint8_t key;    // 0..11 = C..B
	uint8_t scale;  // ScaleId (PresetRow.hpp): 0 Major, 1 Minor, 2 Chromatic

	ScaleKey() : key(0), scale(2) {}
};

// ---- Head status -------------------------------------------------------------
enum RunState { RUN_STOPPED = 0, RUN_RUNNING = 1, RUN_HOLDING = 2 };

struct HeadStatus {
	uint8_t headId;        // 0..7, 0 adjacent to PROGRAM
	uint8_t currentStage;  // global stage index 0..63
	uint8_t runState;      // RunState
	uint8_t display;       // 1 = this head requests Display (manual: FG "display" switch)
	uint8_t pulse1;        // current Pulse 1 gate level, for MIDI out
	uint8_t pulse2;        // current Pulse 2 gate level, for MIDI out
	uint8_t allPulse;      // current ALL trigger level, for MIDI out
	uint8_t quantized;     // current stage is quantized; note output uses this
	float phase;           // 0..1 position within the stage interval
	float cv;              // head CV output (WP7: POLY OUT channel payload)

	HeadStatus()
		: headId(0), currentStage(0), runState(RUN_STOPPED), display(0),
		  pulse1(0), pulse2(0), allPulse(0), quantized(0),
		  phase(0.f), cv(0.f) {}
};

enum MidiProgramEventType {
	MIDI_PROG_NONE = 0,
	MIDI_PROG_SELECT_PREV = 1,
	MIDI_PROG_SELECT_NEXT = 2,
	MIDI_PROG_CLEAR = 3,
	MIDI_PROG_BULK_ARM = 4,
	MIDI_PROG_GESTURE = 5,
	MIDI_PROG_LIMITED = 6,
	MIDI_PROG_TIME_RANGE = 7,
	MIDI_PROG_SLIDER = 8,
	MIDI_PROG_PRESET_LOAD = 9,
	MIDI_PROG_SET_KEY = 10,
	MIDI_PROG_SET_SCALE = 11,
	MIDI_PROG_SET_PULSE_RETRIG = 12
};

struct MidiProgramEvent {
	uint32_t seq;    // monotonic event id; PROGRAM applies each id once
	uint8_t type;    // MidiProgramEventType
	uint8_t index;   // field/button/stage/slot index, depending on type
	uint8_t value;   // raw MIDI value or small enum
	float fvalue;    // scaled slider value when type == MIDI_PROG_SLIDER
	uint8_t flags;   // EDIT_OP_* presentation/takeover behavior for slider events

	MidiProgramEvent()
		: seq(0), type(MIDI_PROG_NONE), index(0), value(0), fvalue(0.f), flags(0) {}
};

// ---- Messages ----------------------------------------------------------------
// Leftward, block -> left neighbour (block or anchor): table of the sub-chain
// starting at the sender.
struct BlockToAnchorMsg {
	StageTable table;
	bool valid;

	BlockToAnchorMsg() : valid(false) {}
};

// Leftward, anchor -> heads, relayed head-to-head.
struct AnchorToHeadsMsg {
	StageTable table;
	float ext[4];         // EXT A-D input voltages
	bool extConnected[4]; // WP7: HeadDSP's external-time fallback needs this
	Globals globals;
	ScaleKey scaleKey;
	uint8_t hopIndex;     // head id of the receiver; incremented at each relay
	// Display arbitration (manual: only one Display LED active at a time):
	// heads unlatch when the owner is another head or when cancelSeq changes
	// (stage-select scroll cancels Display).
	uint8_t displayOwner;       // head id, 0xFF = none
	uint32_t displayCancelSeq;
	uint32_t midiClockSeq;      // global MIDI clock edge counter
	uint32_t midiStartSeq;      // global MIDI Start counter
	uint32_t midiStopSeq;       // global MIDI Stop counter
	uint32_t midiContinueSeq;   // global MIDI Continue counter
	uint32_t headCcSeq[kMaxHeads][kMidiHeadControls];
	float headCcValue[kMaxHeads][kMidiHeadControls];
	bool valid;

	AnchorToHeadsMsg()
		: hopIndex(0), displayOwner(0xFF), displayCancelSeq(0),
		  midiClockSeq(0), midiStartSeq(0), midiStopSeq(0), midiContinueSeq(0),
		  valid(false) {
		for (int i = 0; i < 4; i++) {
			ext[i] = 0.f;
			extConnected[i] = false;
		}
		for (int h = 0; h < kMaxHeads; h++) {
			for (int c = 0; c < kMidiHeadControls; c++) {
				headCcSeq[h][c] = 0;
				headCcValue[h][c] = 0.f;
			}
		}
	}
};

// Rightward, head -> right neighbour (head or anchor): own status appended to
// the statuses of heads further left.
struct HeadsToAnchorMsg {
	HeadStatus status[kMaxHeads];
	uint8_t headCount;
	MidiProgramEvent midiEvents[kMaxMidiProgramEvents];
	uint8_t midiEventCount;
	uint32_t midiEventSeq;
	bool valid;

	HeadsToAnchorMsg() : headCount(0), midiEventCount(0), midiEventSeq(0), valid(false) {}
};

// Rightward, anchor -> blocks, relayed block-to-block: edit ops, selected
// stage for the edit LEDs, merged head statuses for the position dots.
// A Limited-range bulk gesture can emit Range + octave for every stage.
static const int kMaxOpsPerTick = 2 * kMaxStages;

struct AnchorToBlocksMsg {
	EditOp ops[kMaxOpsPerTick];
	uint8_t opCount;
	uint8_t selectedStage;         // global stage index for edit-select LED
	HeadStatus status[kMaxHeads];  // merged, for head-position dots
	uint8_t headCount;
	uint8_t hopIndex;              // block id of the receiver
	uint32_t seq;                  // WP7: batch id; blocks apply ops of each seq once
	bool valid;

	AnchorToBlocksMsg()
		: opCount(0), selectedStage(0), headCount(0), hopIndex(0), seq(0), valid(false) {}
};

// ---- Relay functions (pure) ---------------------------------------------------
// Block side, leftward: prepend own segment to the table from the right
// neighbour (pass fromRight = NULL if there is none / it is invalid).
inline void blockRelayLeft(const BlockSegment& own, const StageTable* fromRight,
                           BlockToAnchorMsg& out) {
	int rightCount = fromRight ? fromRight->count : 0;
	if (rightCount > kMaxStages - kStagesPerBlock)
		rightCount = kMaxStages - kStagesPerBlock;
	for (int i = 0; i < kStagesPerBlock; i++) {
		out.table.voltage[i] = own.voltage[i];
		out.table.time[i] = own.time[i];
		out.table.program[i] = own.program[i];
	}
	for (int i = 0; i < rightCount; i++) {
		out.table.voltage[kStagesPerBlock + i] = fromRight->voltage[i];
		out.table.time[kStagesPerBlock + i] = fromRight->time[i];
		out.table.program[kStagesPerBlock + i] = fromRight->program[i];
	}
	out.table.count = (uint8_t)(kStagesPerBlock + rightCount);
	out.valid = true;
}

// Head side, leftward: relay the anchor broadcast, incrementing the hop
// counter. The receiver's head id is in.hopIndex BEFORE this call; the sender
// calls this to produce what the NEXT head sees.
inline void headRelayLeft(const AnchorToHeadsMsg& in, AnchorToHeadsMsg& out) {
	out = in;
	out.hopIndex = (uint8_t)(in.hopIndex + 1);
}

// Head side, rightward: own status + statuses of heads further left.
// fromLeft = NULL if this is the leftmost head.
inline void headRelayRight(const HeadStatus& own, const HeadsToAnchorMsg* fromLeft,
                           HeadsToAnchorMsg& out) {
	int n = (fromLeft && fromLeft->valid) ? fromLeft->headCount : 0;
	if (n > kMaxHeads - 1)
		n = kMaxHeads - 1;
	for (int i = 0; i < n; i++)
		out.status[i] = fromLeft->status[i];
	out.status[n] = own;
	out.headCount = (uint8_t)(n + 1);
	out.midiEventCount = fromLeft ? fromLeft->midiEventCount : 0;
	out.midiEventSeq = fromLeft ? fromLeft->midiEventSeq : 0;
	for (int i = 0; i < out.midiEventCount && i < kMaxMidiProgramEvents; i++)
		out.midiEvents[i] = fromLeft->midiEvents[i];
	out.valid = true;
}

// Block side, rightward: relay the anchor message, incrementing the hop
// counter (receiver's block id is in.hopIndex before the call).
inline void blockRelayRight(const AnchorToBlocksMsg& in, AnchorToBlocksMsg& out) {
	out = in;
	out.hopIndex = (uint8_t)(in.hopIndex + 1);
}

// ---- Edit-op targeting ---------------------------------------------------------
inline bool opTargetsBlock(const EditOp& op, int blockIndex) {
	return (int)(op.stageIndex / kStagesPerBlock) == blockIndex;
}

// Queue an op into the anchor's outgoing message. False when full (emitters —
// WP6 — must respect this; ops are never silently dropped inside the chain).
inline bool pushOp(AnchorToBlocksMsg& msg, const EditOp& op) {
	if (msg.opCount >= kMaxOpsPerTick)
		return false;
	msg.ops[msg.opCount++] = op;
	return true;
}

// Apply the ops addressed to this block onto its own segment. Validation
// mirrors WP2 apply(): out-of-range / non-finite / non-integral values are
// rejected per op. Returns the number of ops applied.
inline int applyOpsToSegment(BlockSegment& seg, const AnchorToBlocksMsg& msg, int blockIndex) {
	int applied = 0;
	for (int i = 0; i < msg.opCount && i < kMaxOpsPerTick; i++) {
		const EditOp& op = msg.ops[i];
		if (!opTargetsBlock(op, blockIndex))
			continue;
		int local = op.stageIndex - blockIndex * kStagesPerBlock;
		if (local < 0 || local >= kStagesPerBlock)
			continue;
		if (!std::isfinite(op.value))
			continue;
		if (op.field == Field::Voltage) {
			if (op.value < kVoltageMin || op.value > kVoltageMax)
				continue;
			seg.voltage[local] = op.value;
			applied++;
		}
		else if (op.field == Field::Time) {
			if (op.value < kTimeSliderMin || op.value > kTimeSliderMax)
				continue;
			seg.time[local] = op.value;
			applied++;
		}
		else {
			if (op.value < 0.f || op.value != std::floor(op.value))
				continue;
			if (setProgramField(seg.program[local], op.field, (uint32_t)op.value))
				applied++;
		}
	}
	return applied;
}

// ---- Chain enumeration (pure, over an abstract neighbour view) ------------------
// The anchor uses this for the stage-count display, broken-chain warning and
// validation; module indices themselves come from the hop counters above.
enum class ModuleType : uint8_t { None = 0, Program, Stage4, Head, Midi, Foreign };

struct NeighborView {
	virtual ~NeighborView() {}
	// Module type at the n-th position (n >= 1) right/left of the anchor.
	virtual ModuleType rightAt(int n) const = 0;
	virtual ModuleType leftAt(int n) const = 0;
};

struct ChainLayout {
	uint8_t blockCount;  // contiguous STAGE4 blocks to the right, 0..16
	uint8_t headCount;   // contiguous HEADs to the left, 0..8
	// True if a further chain module sits beyond the counted run but is
	// separated by a gap/foreign module (or exceeds the maximum) — the panel
	// shows a broken-chain warning.
	bool brokenRight;
	bool brokenLeft;

	ChainLayout() : blockCount(0), headCount(0), brokenRight(false), brokenLeft(false) {}
};

inline ChainLayout enumerateChain(const NeighborView& v) {
	ChainLayout lay;
	int n = 1;
	while (lay.blockCount < kMaxBlocks && v.rightAt(n) == ModuleType::Stage4) {
		lay.blockCount++;
		n++;
	}
	// Anything chain-like beyond the terminator means a broken/overlong chain.
	ModuleType t = v.rightAt(n);
	if (t == ModuleType::Stage4)
		lay.brokenRight = true;  // more than kMaxBlocks contiguous
	else if (t == ModuleType::Foreign || t == ModuleType::None) {
		for (int k = n + 1; k <= n + kMaxBlocks; k++) {
			if (v.rightAt(k) == ModuleType::Stage4) {
				lay.brokenRight = true;
				break;
			}
			if (v.rightAt(k) == ModuleType::None && v.rightAt(k - 1) == ModuleType::None)
				break;  // two empty spaces: stop scanning
		}
	}

	n = 1;
	bool midiSeen = false;
	while (lay.headCount < kMaxHeads) {
		ModuleType lt = v.leftAt(n);
		if (lt == ModuleType::Midi && !midiSeen) {
			midiSeen = true;
			n++;
			continue;
		}
		if (lt != ModuleType::Head)
			break;
		lay.headCount++;
		n++;
	}
	t = v.leftAt(n);
	if (t == ModuleType::Head)
		lay.brokenLeft = true;
	else if (t == ModuleType::Foreign || t == ModuleType::None) {
		for (int k = n + 1; k <= n + kMaxHeads; k++) {
			ModuleType kt = v.leftAt(k);
			if (kt == ModuleType::Head || kt == ModuleType::Midi) {
				lay.brokenLeft = true;
				break;
			}
			if (v.leftAt(k) == ModuleType::None && v.leftAt(k - 1) == ModuleType::None)
				break;
		}
	}
	return lay;
}

} // namespace spacetime
