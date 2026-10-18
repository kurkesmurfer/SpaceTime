#pragma once

namespace spacetime {

static const int kInstrumentIdCount = 16;

inline int clampInstrumentId(int value) {
	return value < 0 ? 0 : (value >= kInstrumentIdCount ? kInstrumentIdCount - 1 : value);
}

inline char instrumentIdHex(int value) {
	value = clampInstrumentId(value);
	return value < 10 ? (char)('0' + value) : (char)('A' + value - 10);
}

} // namespace spacetime
