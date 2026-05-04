#include "hlcsg/hlcsg.h"
#include "common/mathtypes.h"

vec3_t g_hull_size[NUM_HULLS][2] =
    {
        {// 0x0x0
         {0, 0, 0},
         {0, 0, 0}},
        {// 32x32x72
         {-16, -16, -36},
         {16, 16, 36}},
        {// 64x64x64
         {-32, -32, -32},
         {32, 32, 32}},
        {// 32x32x36
         {-16, -16, -18},
         {16, 16, 18}}};