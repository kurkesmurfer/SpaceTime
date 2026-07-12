// SpaceTime WP3 — preset row modal logic tests.
#include "doctest.h"
#include "PresetRow.hpp"

using namespace spacetime;

TEST_CASE("preset row starts idle; slot presses while idle do nothing") {
	PresetRowLogic p;
	CHECK(p.mode() == PresetMode::Idle);
	for (uint8_t s = 0; s < 12; s++) {
		PresetAction a = p.onSlotPress(s);
		CHECK(a.type == PresetAction::None);
		CHECK(p.mode() == PresetMode::Idle);
	}
}

TEST_CASE("arm, cancel by same mode, re-arm by other mode") {
	PresetRowLogic p;
	p.onModePress(PresetMode::Load);
	CHECK(p.mode() == PresetMode::Load);
	p.onModePress(PresetMode::Load);          // same again cancels
	CHECK(p.mode() == PresetMode::Idle);
	p.onModePress(PresetMode::Save);
	p.onModePress(PresetMode::Key);           // different mode re-arms
	CHECK(p.mode() == PresetMode::Key);
	p.onModePress(PresetMode::Idle);          // no-op
	CHECK(p.mode() == PresetMode::Key);
	p.cancel();
	CHECK(p.mode() == PresetMode::Idle);
}

TEST_CASE("load and save produce the pressed slot and disarm") {
	PresetRowLogic p;
	for (uint8_t s = 0; s < 12; s++) {
		p.onModePress(PresetMode::Load);
		PresetAction a = p.onSlotPress(s);
		CHECK(a.type == PresetAction::Load);
		CHECK(a.index == s);
		CHECK(p.mode() == PresetMode::Idle);

		p.onModePress(PresetMode::Save);
		a = p.onSlotPress(s);
		CHECK(a.type == PresetAction::Save);
		CHECK(a.index == s);
		CHECK(p.mode() == PresetMode::Idle);
	}
}

TEST_CASE("key select maps slots to keys C..B") {
	PresetRowLogic p;
	for (uint8_t s = 0; s < 12; s++) {
		p.onModePress(PresetMode::Key);
		PresetAction a = p.onSlotPress(s);
		CHECK(a.type == PresetAction::SetKey);
		CHECK(a.index == s);
	}
}

TEST_CASE("scale accepts only slots 10/11/12 (Major/Minor/Chromatic)") {
	PresetRowLogic p;
	p.onModePress(PresetMode::Scale);
	for (uint8_t s = 0; s < 9; s++) {
		PresetAction a = p.onSlotPress(s);
		CHECK(a.type == PresetAction::None);
		CHECK(p.mode() == PresetMode::Scale);  // invalid slot: stays armed
	}
	PresetAction a = p.onSlotPress(9);
	CHECK(a.type == PresetAction::SetScale);
	CHECK(a.index == SCALE_MAJOR);
	CHECK(p.mode() == PresetMode::Idle);

	p.onModePress(PresetMode::Scale);
	a = p.onSlotPress(10);
	CHECK(a.index == SCALE_MINOR);
	p.onModePress(PresetMode::Scale);
	a = p.onSlotPress(11);
	CHECK(a.index == SCALE_CHROMATIC);
}

TEST_CASE("out-of-range slots are rejected without state change") {
	PresetRowLogic p;
	p.onModePress(PresetMode::Load);
	PresetAction a = p.onSlotPress(12);
	CHECK(a.type == PresetAction::None);
	CHECK(p.mode() == PresetMode::Load);  // still armed
	a = p.onSlotPress(255);
	CHECK(a.type == PresetAction::None);
	CHECK(p.mode() == PresetMode::Load);
}
