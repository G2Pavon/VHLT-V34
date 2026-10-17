// Characterization tests for src/common.
//
// These tests pin the behaviour of the pre-modernization code (tag oracle-base),
// including quirks that affect compiler output. A refactor must keep them green;
// a test may only change together with a deliberate, documented behaviour change.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
