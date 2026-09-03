#include "doctest.h"
#include "MMModuleContracts.hpp"
#include "SpaceTimeEngine.hpp"

using namespace spacetime;

TEST_CASE("portable MM module slugs and contract sizes are frozen") {
	CHECK(std::string(MMProgramContract::slug) == "MMProgram");
	CHECK(std::string(MMStage4Contract::slug) == "MMStage4");
	CHECK(std::string(MMHeadContract::slug) == "MMHead");
	CHECK(MMProgramContract::PARAMS_LEN == 48);
	CHECK(MMProgramContract::INPUTS_LEN == 4);
	CHECK(MMProgramContract::OUTPUTS_LEN == 3);
	CHECK(MMStage4Contract::PARAMS_LEN == 11);
	CHECK(MMHeadContract::PARAMS_LEN == 14);
	CHECK(MMHeadContract::INPUTS_LEN == 8);
	CHECK(MMHeadContract::OUTPUTS_LEN == 7);
}

TEST_CASE("stage count uses one four-stage-bank mechanism on every target") {
	CHECK(stageCountFromBanks(-3) == 4);
	for (int banks = 1; banks <= 16; banks++) {
		CAPTURE(banks);
		CHECK(stageCountFromBanks(banks) == banks * 4);
		CHECK(stageBanksFromCount(banks * 4) == banks);
	}
	CHECK(stageCountFromBanks(99) == 64);
	CHECK(stageBanksFromCount(1) == 1);
	CHECK(stageBanksFromCount(4) == 1);
	CHECK(stageBanksFromCount(5) == 2);
	CHECK(stageBanksFromCount(63) == 16);
	CHECK(stageBanksFromCount(64) == 16);
}

TEST_CASE("reducing active stages deterministically clamps a running head") {
	SpaceTimeEngine engine;
	engine.table().time[63] = 1.f;
	engine.headConfig(0).addressKnob = 10.f;
	engine.headConfig(0).continuous = true;
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headOut(0).currentStage == 63);

	engine.setStageBanks(2);
	engine.processHeads(1.f / 48000.f);
	CHECK(engine.headOut(0).currentStage < 8);
}

TEST_CASE("changing active stage banks clamps Program selection and retains data") {
	SpaceTimeEngine engine;
	engine.table().voltage[63] = 8.75f;
	engine.program().setSelected(63);
	engine.setStageBanks(4);
	CHECK(engine.stageCount() == 16);
	CHECK(engine.program().selectedStage() == 15);
	CHECK(engine.table().voltage[63] == doctest::Approx(8.75f));
	engine.setStageBanks(16);
	CHECK(engine.stageCount() == 64);
	CHECK(engine.table().voltage[63] == doctest::Approx(8.75f));
}
