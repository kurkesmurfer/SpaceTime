#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Include every dsp/ header so the empty scaffold is compile-checked
// even before any tests exist.
#include "StageTable.hpp"
#include "HeadDSP.hpp"
#include "ProgramLogic.hpp"
#include "Chain.hpp"
