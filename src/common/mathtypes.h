#pragma once

using byte = unsigned char;

#ifdef DOUBLEVEC_T
using vec_t = double;
#else
using vec_t = float;
#endif
using vec3_t = vec_t[3]; // x,y,z
