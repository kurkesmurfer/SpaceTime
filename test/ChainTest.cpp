// SpaceTime WP4 — expander chain protocol tests.
#include "doctest.h"
#include "Chain.hpp"

#include <vector>

using namespace spacetime;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static BlockSegment makeSegment(int b) {
	BlockSegment seg;
	for (int i = 0; i < kStagesPerBlock; i++) {
		seg.voltage[i] = (float)(b * 10 + i);
		seg.time[i] = (float)(b * kStagesPerBlock + i) / 100.f;
		seg.program[i].setPulse1((b + i) % 2);
		seg.program[i].setTimeRange((uint8_t)(b % 4));
	}
	return seg;
}

// Vector-backed neighbour view: index 0 unused; [1..] = modules outward.
struct TestView : NeighborView {
	std::vector<ModuleType> right, left;

	ModuleType at(const std::vector<ModuleType>& v, int n) const {
		if (n < 1 || n > (int)v.size())
			return ModuleType::None;
		return v[n - 1];
	}
	ModuleType rightAt(int n) const override { return at(right, n); }
	ModuleType leftAt(int n) const override { return at(left, n); }
};

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------
TEST_CASE("enumerateChain counts 0..8 contiguous blocks and heads") {
	for (int nb = 0; nb <= kMaxBlocks; nb++) {
		for (int nh = 0; nh <= kMaxHeads; nh += 4) {
			TestView v;
			for (int i = 0; i < nb; i++) v.right.push_back(ModuleType::Stage4);
			for (int i = 0; i < nh; i++) v.left.push_back(ModuleType::Head);
			ChainLayout lay = enumerateChain(v);
			CHECK(lay.blockCount == nb);
			CHECK(lay.headCount == nh);
			CHECK_FALSE(lay.brokenRight);
			CHECK_FALSE(lay.brokenLeft);
		}
	}
}

TEST_CASE("foreign module terminates the chain") {
	TestView v;
	v.right = {ModuleType::Stage4, ModuleType::Stage4, ModuleType::Foreign,
	           ModuleType::Stage4};
	v.left = {ModuleType::Head, ModuleType::Foreign, ModuleType::Head};
	ChainLayout lay = enumerateChain(v);
	CHECK(lay.blockCount == 2);
	CHECK(lay.headCount == 1);
	// chain modules stranded beyond the terminator -> warning
	CHECK(lay.brokenRight);
	CHECK(lay.brokenLeft);
}

TEST_CASE("MIDI gateway directly left of PROGRAM is transparent") {
	TestView v;
	// Physical order: HEAD HEAD MIDI PROGRAM. The vector is from PROGRAM
	// outward to the left, so MIDI is first.
	v.left = {ModuleType::Midi, ModuleType::Head, ModuleType::Head};
	ChainLayout lay = enumerateChain(v);
	CHECK(lay.headCount == 2);
	CHECK_FALSE(lay.brokenLeft);
}

TEST_CASE("gap (empty space) terminates the chain; stranded blocks warn") {
	TestView v;
	v.right = {ModuleType::Stage4, ModuleType::None, ModuleType::Stage4};
	ChainLayout lay = enumerateChain(v);
	CHECK(lay.blockCount == 1);
	CHECK(lay.brokenRight);

	TestView clean;
	clean.right = {ModuleType::Stage4, ModuleType::Stage4};
	lay = enumerateChain(clean);
	CHECK(lay.blockCount == 2);
	CHECK_FALSE(lay.brokenRight);
}

TEST_CASE("more than 8 contiguous blocks/heads counts 8 and warns") {
	TestView v;
	for (int i = 0; i < 10; i++) {
		v.right.push_back(ModuleType::Stage4);
		v.left.push_back(ModuleType::Head);
	}
	ChainLayout lay = enumerateChain(v);
	CHECK(lay.blockCount == kMaxBlocks);
	CHECK(lay.headCount == kMaxHeads);
	CHECK(lay.brokenRight);
	CHECK(lay.brokenLeft);
}

// ---------------------------------------------------------------------------
// Relays
// ---------------------------------------------------------------------------
TEST_CASE("blockRelayLeft: single block produces its own 4 stages") {
	BlockToAnchorMsg out;
	BlockSegment seg = makeSegment(0);
	blockRelayLeft(seg, NULL, out);
	CHECK(out.valid);
	CHECK(out.table.count == 4);
	for (int i = 0; i < 4; i++) {
		CHECK(out.table.voltage[i] == seg.voltage[i]);
		CHECK(out.table.program[i].bits == seg.program[i].bits);
	}
}

TEST_CASE("blockRelayLeft: prepending keeps left-to-right stage order") {
	// Simulate blocks 0..2 (0 nearest anchor); relay right-to-left.
	BlockSegment segs[3] = {makeSegment(0), makeSegment(1), makeSegment(2)};
	BlockToAnchorMsg m2, m1, m0;
	blockRelayLeft(segs[2], NULL, m2);
	blockRelayLeft(segs[1], &m2.table, m1);
	blockRelayLeft(segs[0], &m1.table, m0);
	CHECK(m0.table.count == 12);
	// Must equal WP2 concatenation of the same segments.
	StageTable ref;
	concatenate(segs, 3, ref);
	for (int s = 0; s < 12; s++) {
		CHECK(m0.table.voltage[s] == ref.voltage[s]);
		CHECK(m0.table.time[s] == ref.time[s]);
		CHECK(m0.table.program[s].bits == ref.program[s].bits);
	}
}

TEST_CASE("blockRelayLeft: overlong chain clamps to 32 stages, keeps nearest blocks") {
	BlockToAnchorMsg msg, next;
	blockRelayLeft(makeSegment(9), NULL, msg);
	for (int b = 8; b >= 0; b--) {  // 10 blocks total
		blockRelayLeft(makeSegment(b), &msg.table, next);
		msg = next;
	}
	CHECK(msg.table.count == kMaxStages);
	CHECK(msg.table.voltage[0] == makeSegment(0).voltage[0]);  // nearest kept
}

TEST_CASE("headRelayLeft increments hop index and preserves payload") {
	AnchorToHeadsMsg in;
	in.valid = true;
	in.hopIndex = 0;
	in.ext[2] = -7.5f;
	in.scaleKey.key = 9;
	in.globals.slewFrac2 = 0.5f;
	in.table.count = 8;
	in.table.voltage[7] = 3.3f;

	AnchorToHeadsMsg out;
	headRelayLeft(in, out);
	CHECK(out.hopIndex == 1);
	CHECK(out.valid);
	CHECK(out.ext[2] == -7.5f);
	CHECK(out.scaleKey.key == 9);
	CHECK(out.table.voltage[7] == 3.3f);
	AnchorToHeadsMsg out2;
	headRelayLeft(out, out2);
	CHECK(out2.hopIndex == 2);
}

TEST_CASE("headRelayRight merges up to 8 statuses, appending own") {
	HeadsToAnchorMsg msg;
	// Leftmost head (id 7) starts, relaying right toward the anchor.
	for (int id = 7; id >= 0; id--) {
		HeadStatus own;
		own.headId = (uint8_t)id;
		own.currentStage = (uint8_t)(id * 2);
		own.runState = RUN_RUNNING;
		HeadsToAnchorMsg out;
		headRelayRight(own, (id == 7) ? NULL : &msg, out);
		msg = out;
	}
	CHECK(msg.valid);
	CHECK(msg.headCount == 8);
	// Order: farthest head first, nearest (id 0) appended last.
	for (int i = 0; i < 8; i++) {
		CHECK(msg.status[i].headId == 7 - i);
		CHECK(msg.status[i].currentStage == (7 - i) * 2);
	}
}

TEST_CASE("headRelayRight: a 9th head cannot overflow the merge") {
	HeadsToAnchorMsg full;
	full.valid = true;
	full.headCount = kMaxHeads;
	HeadStatus own;
	own.headId = 8;
	HeadsToAnchorMsg out;
	headRelayRight(own, &full, out);
	CHECK(out.headCount == kMaxHeads);           // clamped
	CHECK(out.status[kMaxHeads - 1].headId == 8);  // own still appended last
}

// ---------------------------------------------------------------------------
// Edit ops through the rightward relay
// ---------------------------------------------------------------------------
TEST_CASE("op targeting selects the owning block only") {
	for (int s = 0; s < kMaxStages; s++) {
		EditOp op((uint8_t)s, Field::Pulse1, 1.f);
		for (int b = 0; b < kMaxBlocks; b++)
			CHECK(opTargetsBlock(op, b) == (s / kStagesPerBlock == b));
	}
}

TEST_CASE("applyOpsToSegment applies only local, valid ops") {
	AnchorToBlocksMsg msg;
	msg.valid = true;
	CHECK(pushOp(msg, EditOp(4, Field::Pulse1, 1.f)));    // block 1, local 0
	CHECK(pushOp(msg, EditOp(7, Field::Voltage, 9.5f)));  // block 1, local 3
	CHECK(pushOp(msg, EditOp(3, Field::Pulse2, 1.f)));    // block 0 — not ours
	CHECK(pushOp(msg, EditOp(5, Field::Slew, 7.f)));      // invalid value
	CHECK(pushOp(msg, EditOp(6, Field::Time, 2.f)));      // out of range

	BlockSegment seg;
	int applied = applyOpsToSegment(seg, msg, 1);
	CHECK(applied == 2);
	CHECK(seg.program[0].pulse1());
	CHECK(seg.voltage[3] == 9.5f);
	CHECK(seg.program[1].slew() == SLEW_STEPPED);  // rejected op left default
	CHECK(seg.time[2] == 0.5f);

	// Same message on block 0 applies only the pulse2 op.
	BlockSegment seg0;
	CHECK(applyOpsToSegment(seg0, msg, 0) == 1);
	CHECK(seg0.program[3].pulse2());
}

TEST_CASE("pushOp reports overflow at kMaxOpsPerTick") {
	AnchorToBlocksMsg msg;
	for (int i = 0; i < kMaxOpsPerTick; i++)
		CHECK(pushOp(msg, EditOp(0, Field::Pulse1, 1.f)));
	CHECK_FALSE(pushOp(msg, EditOp(0, Field::Pulse1, 1.f)));
	CHECK(msg.opCount == kMaxOpsPerTick);
}

TEST_CASE("blockRelayRight increments hop index, payload intact") {
	AnchorToBlocksMsg in;
	in.valid = true;
	in.selectedStage = 13;
	in.headCount = 2;
	in.status[1].headId = 1;
	in.status[1].currentStage = 30;
	CHECK(pushOp(in, EditOp(9, Field::Stop, 1.f)));

	AnchorToBlocksMsg out;
	blockRelayRight(in, out);
	CHECK(out.hopIndex == 1);
	CHECK(out.selectedStage == 13);
	CHECK(out.opCount == 1);
	CHECK(out.status[1].currentStage == 30);
}

// ---------------------------------------------------------------------------
// Per-hop one-tick latency simulation (double-buffered links, flip per tick)
// ---------------------------------------------------------------------------
// Link with Rack-style producer/consumer flip.
template <typename T>
struct Link {
	T bufs[2];
	int producerIdx;

	Link() : producerIdx(0) {}
	T& producer() { return bufs[producerIdx]; }
	const T& consumer() const { return bufs[1 - producerIdx]; }
	void flip() { producerIdx = 1 - producerIdx; }
};

TEST_CASE("leftward table flow reaches the anchor after one tick per hop") {
	const int N = 8;
	BlockSegment segs[N];
	for (int b = 0; b < N; b++)
		segs[b] = makeSegment(b);
	// link[i]: from block i toward its left neighbour (anchor for i = 0).
	Link<BlockToAnchorMsg> link[N];

	StageTable ref;
	concatenate(segs, N, ref);

	int coherentAt = -1;
	for (int tick = 1; tick <= 2 * N; tick++) {
		// Every block writes its left link's producer from its right link's consumer.
		for (int i = 0; i < N; i++) {
			const StageTable* fromRight = NULL;
			if (i + 1 < N && link[i + 1].consumer().valid)
				fromRight = &link[i + 1].consumer().table;
			blockRelayLeft(segs[i], fromRight, link[i].producer());
		}
		for (int i = 0; i < N; i++)
			link[i].flip();

		const BlockToAnchorMsg& atAnchor = link[0].consumer();
		if (atAnchor.valid && atAnchor.table.count == N * kStagesPerBlock && coherentAt < 0) {
			bool equal = true;
			for (int s = 0; s < ref.count; s++)
				equal = equal && atAnchor.table.voltage[s] == ref.voltage[s]
				              && atAnchor.table.time[s] == ref.time[s]
				              && atAnchor.table.program[s].bits == ref.program[s].bits;
			if (equal)
				coherentAt = tick;
		}
	}
	CHECK(coherentAt == N);  // exactly one tick per hop

	// And it stays coherent afterwards (checked at 2N above by not resetting).
	const BlockToAnchorMsg& atAnchor = link[0].consumer();
	CHECK(atAnchor.table.count == ref.count);
	for (int s = 0; s < ref.count; s++)
		CHECK(atAnchor.table.voltage[s] == ref.voltage[s]);
}

TEST_CASE("anchor broadcast reaches the farthest head with correct hop ids") {
	const int H = 8;
	// link[i]: from (anchor if i==0 else head i-1) toward head i.
	Link<AnchorToHeadsMsg> link[H];

	for (int tick = 1; tick <= H + 2; tick++) {
		// Anchor writes head 0's port.
		AnchorToHeadsMsg& first = link[0].producer();
		first = AnchorToHeadsMsg();
		first.valid = true;
		first.hopIndex = 0;
		first.table.count = 16;
		first.ext[3] = 4.2f;
		// Each head relays what it consumed to the next head out.
		for (int i = 0; i + 1 < H; i++) {
			if (link[i].consumer().valid)
				headRelayLeft(link[i].consumer(), link[i + 1].producer());
			else
				link[i + 1].producer().valid = false;
		}
		for (int i = 0; i < H; i++)
			link[i].flip();
	}
	for (int i = 0; i < H; i++) {
		CHECK(link[i].consumer().valid);
		CHECK(link[i].consumer().hopIndex == i);  // head id by hop count
		CHECK(link[i].consumer().ext[3] == 4.2f);
		CHECK(link[i].consumer().table.count == 16);
	}
}
