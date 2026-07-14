#include "doctest.h"
#include "GlueBus.hpp"

using namespace spacetime;

TEST_CASE("Glue queue preserves transient messages in order") {
	GlueQueue<AnchorToBlocksMsg, 4> queue;
	for (uint32_t seq = 1; seq <= 4; seq++) {
		AnchorToBlocksMsg msg;
		msg.valid = true;
		msg.seq = seq;
		msg.opCount = 1;
		msg.ops[0] = EditOp((uint8_t)seq, Field::Voltage, (float)seq);
		CHECK(queue.push(7, msg));
	}
	AnchorToBlocksMsg overflow;
	CHECK_FALSE(queue.push(7, overflow));
	CHECK(queue.drops() == 1);
	for (uint32_t seq = 1; seq <= 4; seq++) {
		AnchorToBlocksMsg msg;
		REQUIRE(queue.pop(7, msg));
		CHECK(msg.seq == seq);
		CHECK(msg.ops[0].stageIndex == seq);
	}
	CHECK_FALSE(queue.pop(7, overflow));
}

TEST_CASE("Glue queue discards messages from an earlier pairing") {
	GlueQueue<BlockToAnchorMsg, 4> queue;
	BlockToAnchorMsg oldMsg;
	oldMsg.valid = true;
	oldMsg.table.count = 4;
	BlockToAnchorMsg newMsg;
	newMsg.valid = true;
	newMsg.table.count = 12;
	CHECK(queue.push(2, oldMsg));
	CHECK(queue.push(3, newMsg));
	BlockToAnchorMsg out;
	REQUIRE(queue.pop(3, out));
	CHECK(out.table.count == 12);
	CHECK_FALSE(queue.pop(3, out));
}

TEST_CASE("Glue bus links only one matching endpoint of each side") {
	GlueBus bus;
	CHECK_FALSE(bus.linked());
	bus.leftOwner.store(10);
	bus.rightOwner.store(20);
	bus.leftMode.store(GLUE_MODE_HEAD);
	bus.rightMode.store(GLUE_MODE_STAGE);
	CHECK_FALSE(bus.linked());
	bus.rightMode.store(GLUE_MODE_HEAD);
	CHECK(bus.linked());
	bus.leftOwner.store(-1);
	CHECK_FALSE(bus.linked());
}
