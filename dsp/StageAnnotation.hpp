#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "HeadDSP.hpp"

namespace spacetime {

struct StageAnnotation {
	std::string pitch;
	std::string cents;
	std::string duration;
};

inline int annotationFloorDiv12(int value) {
	int quotient = value / 12;
	if (value < 0 && value % 12)
		quotient--;
	return quotient;
}

inline std::string annotationNoteName(int semitone) {
	static const char* names[12] = {
		"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};
	int octaveOffset = annotationFloorDiv12(semitone);
	int pitchClass = semitone - octaveOffset * 12;
	return names[pitchClass] + std::to_string(4 + octaveOffset);
}

inline std::string annotationNominalTime(float normalized, uint8_t range) {
	int r = range > 3 ? 3 : (int)range;
	if (normalized < 0.f) normalized = 0.f;
	if (normalized > 1.f) normalized = 1.f;
	float seconds = kTimeRangeMin[r]
		+ normalized * (kTimeRangeMax[r] - kTimeRangeMin[r]);
	char buffer[16];
	if (seconds < 1.f)
		std::snprintf(buffer, sizeof(buffer), "%d ms", (int)std::round(seconds * 1000.f));
	else if (seconds < 10.f)
		std::snprintf(buffer, sizeof(buffer), "%.1f s", seconds);
	else
		std::snprintf(buffer, sizeof(buffer), "%.0f s", seconds);
	return buffer;
}

inline StageAnnotation makeStageAnnotation(float voltage, float time,
		const ProgramWord& word, const ScaleKey& scaleKey, bool musicalContextValid) {
	StageAnnotation out;
	out.duration = word.timeSource() == SOURCE_EXTERNAL
		? "EXT TIME" : annotationNominalTime(time, word.timeRange());
	if (word.voltageSource() == SOURCE_EXTERNAL) {
		out.pitch = "EXT CV";
		return out;
	}
	if (!musicalContextValid) {
		out.pitch = "--";
		return out;
	}

	switch (word.range()) {
		case RANGE_HALF:
			voltage *= 0.5f;
			break;
		case RANGE_LIMITED:
			voltage = voltage * 0.2f + (float)limitedOctaveOffset(word.limitedOctave());
			break;
		default:
			break;
	}
	if (word.quantize())
		voltage = quantize1Voct(voltage, scaleKey.scale, scaleKey.key);
	int semitone = (int)std::round(voltage * 12.f);
	out.pitch = annotationNoteName(semitone);
	if (!word.quantize()) {
		int cents = (int)std::round((voltage * 12.f - semitone) * 100.f);
		char buffer[12];
		std::snprintf(buffer, sizeof(buffer), "%+d c", cents);
		out.cents = buffer;
	}
	return out;
}

} // namespace spacetime
