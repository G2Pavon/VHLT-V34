#include <cstdlib>
#include <cstring>
#include <cstddef>

#include "compress.h"
#include "common/log.h"

const std::size_t unused_size = 3u; // located at the end of a block

const char *float_type_string[float_type_count] =
    {
        "32bit",
        "16bit",
        "8bit"};

const std::size_t float_size[float_type_count] =
    {
        4u,
        2u,
        1u};

const char *vector_type_string[vector_type_count] =
    {
        "96bit",
        "48bit",
        "32bit",
        "24bit"};

const std::size_t vector_size[vector_type_count] =
    {
        12u,
        6u,
        4u,
        3u};