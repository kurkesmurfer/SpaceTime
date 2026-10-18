#include "doctest.h"
#include "MMModuleContracts.hpp"
#include "NativeModuleContracts.hpp"
#include "SpaceTimeEngine.hpp"

using namespace spacetime;

TEST_CASE("portable MM module slugs and contract sizes are frozen") {
	CHECK(std::string(MMProgramContract::slug) == "MMProgram");
	CHECK(std::string(MMStage4Contract::slug) == "MMStage4");
	CHECK(std::string(MMHeadContract::slug) == "MMHead");
	CHECK(MMProgramContract::PARAMS_LEN == 38);
	CHECK(MMProgramContract::INPUTS_LEN == 4);
	CHECK(MMProgramContract::OUTPUTS_LEN == 3);
	CHECK(MMStage4Contract::PARAMS_LEN == 11);
	CHECK(MMHeadContract::PARAMS_LEN == 14);
	CHECK(MMHeadContract::INPUTS_LEN == 8);
	CHECK(MMHeadContract::OUTPUTS_LEN == 7);
}

TEST_CASE("original module IDs are the portable MetaModule contract") {
	CHECK(std::string(NativeProgramContract::slug) == "Program");
	CHECK(NativeProgramContract::STAGE_SCROLL_DOWN_PARAM == 0);
	CHECK(NativeProgramContract::STAGE_SCROLL_UP_PARAM == 1);
	CHECK(NativeProgramContract::PULSE1_DOWN_PARAM == 32);
	CHECK(NativeProgramContract::PULSE1_UP_PARAM == 33);
	CHECK(NativeProgramContract::PULSE_RETRIG_PARAM == 52);
	CHECK(NativeProgramContract::INSTRUMENT_PARAM == 53);
	CHECK(NativeProgramContract::LTD_SELECTOR_PARAM == 54);
	CHECK(NativeProgramContract::TRANGE_SELECTOR_PARAM == 55);
	CHECK(NativeProgramContract::INSTRUMENT_DOWN_PARAM == 56);
	CHECK(NativeProgramContract::INSTRUMENT_UP_PARAM == 57);
	CHECK(NativeProgramContract::NATIVE_PARAMS_LEN == 58);
	CHECK(NativeProgramContract::POLY_OUTPUT == 0);

	CHECK(std::string(NativeStage4Contract::slug) == "Stage4");
	CHECK(NativeStage4Contract::VOLTAGE_PARAMS == 0);
	CHECK(NativeStage4Contract::TIME_PARAMS == 4);
	CHECK(NativeStage4Contract::INSTRUMENT_PARAM == 8);

	CHECK(std::string(NativeHeadContract::slug) == "Head");
	CHECK(NativeHeadContract::START_PARAM == 0);
	CHECK(NativeHeadContract::ADDR_CONTINUOUS_PARAM == 6);
	CHECK(NativeHeadContract::ADDR_STROBE_PARAM == 7);
	CHECK(NativeHeadContract::DISPLAY_PARAM == 13);
	CHECK(NativeHeadContract::INSTRUMENT_PARAM == 14);
	CHECK(NativeHeadContract::HEAD_PARAM == 15);
	CHECK(NativeHeadContract::PARAMS_LEN == 16);
	CHECK(NativeHeadContract::RESET_INPUT == 7);
	CHECK(NativeHeadContract::EOC_OUTPUT == 6);
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
