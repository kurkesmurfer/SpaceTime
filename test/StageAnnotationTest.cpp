#include "doctest.h"
#include "StageAnnotation.hpp"

using namespace spacetime;

TEST_CASE("stage annotation follows pitch range, quantization and cents") {
	ScaleKey scaleKey;
	ProgramWord word;
	word.setQuantize(true);
	CHECK(makeStageAnnotation(0.f, 0.5f, word, scaleKey, true).pitch == "C4");
	CHECK(makeStageAnnotation(1.f / 12.f, 0.5f, word, scaleKey, true).pitch == "C#4");

	word.setQuantize(false);
	StageAnnotation continuous = makeStageAnnotation(0.02f, 0.5f, word, scaleKey, true);
	CHECK(continuous.pitch == "C4");
	CHECK(continuous.cents == "+24 c");

	word.setRange(RANGE_LIMITED);
	word.setLimitedOctave(0);
	CHECK(makeStageAnnotation(0.f, 0.5f, word, scaleKey, true).pitch == "C2");
}

TEST_CASE("stage annotation marks external sources and formats nominal time") {
	ScaleKey scaleKey;
	ProgramWord word;
	word.setTimeRange(0);
	StageAnnotation annotation = makeStageAnnotation(0.f, 0.5f, word, scaleKey, true);
	CHECK(annotation.duration == "16 ms");

	word.setVoltageSource(SOURCE_EXTERNAL);
	word.setTimeSource(SOURCE_EXTERNAL);
	annotation = makeStageAnnotation(0.f, 0.5f, word, scaleKey, true);
	CHECK(annotation.pitch == "EXT CV");
	CHECK(annotation.duration == "EXT TIME");
}
