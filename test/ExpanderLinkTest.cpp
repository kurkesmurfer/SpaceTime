#include "doctest.h"
#include "ExpanderLink.hpp"

using namespace spacetime;

TEST_CASE("ExpanderMailbox reports only newly published values") {
	ExpanderMailbox<float> mailbox;
	uint32_t lastSeq = 0;
	float value = 0.f;
	CHECK_FALSE(mailbox.read(lastSeq, value));

	mailbox.publish(2.5f);
	CHECK(mailbox.read(lastSeq, value));
	CHECK(value == doctest::Approx(2.5f));
	CHECK_FALSE(mailbox.read(lastSeq, value));

	mailbox.publish(-1.0f);
	CHECK(mailbox.read(lastSeq, value));
	CHECK(value == doctest::Approx(-1.0f));
}

TEST_CASE("ExpanderSnapshot returns a coherent multi-field snapshot") {
	ExpanderSnapshot<4> snapshot;
	uint32_t fields[4] = {1, 1, 1, 1};

	// Before any publish, the slot is "unclaimed": read() still succeeds
	// and returns the zero-initialized default rather than failing. This
	// is the behavior EB5 depends on for unclaimed stage/head slots.
	CHECK(snapshot.read(fields));
	CHECK(fields[0] == 0);
	CHECK(fields[1] == 0);
	CHECK(fields[2] == 0);
	CHECK(fields[3] == 0);
	CHECK(snapshot.heartbeat() == 0);

	uint32_t published[4] = {10, 20, 30, 40};
	snapshot.publish(published);
	CHECK(snapshot.read(fields));
	CHECK(fields[0] == 10);
	CHECK(fields[1] == 20);
	CHECK(fields[2] == 30);
	CHECK(fields[3] == 40);
	CHECK(snapshot.heartbeat() == 1);

	// Unlike ExpanderMailbox, a snapshot always returns the current value,
	// whether or not it has changed since the caller's last read.
	CHECK(snapshot.read(fields));
	CHECK(fields[0] == 10);
}
