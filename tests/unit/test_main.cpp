// Characterization tests for src/common.
//
// These tests pin the behaviour of the pre-modernization code (tag oracle-base),
// including quirks that affect compiler output. A refactor must keep them green;
// a test may only change together with a deliberate, documented behaviour change.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "common/bspfile.h"

// Symbols common/ expects the tool to define.
#ifdef HLCSG
bool g_nolightopt = true;
#endif

void GetParamsFromEnt(entity_t *)
{
}
