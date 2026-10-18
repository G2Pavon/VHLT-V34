#pragma once

#include <bit>
#include <cstddef>
#include <cstring>

extern const std::size_t unused_size; // located at the end of a block

typedef enum
{
    FLOAT32 = 0,
    FLOAT16,
    FLOAT8,
    float_type_count
} float_type;

extern const char *float_type_string[];

extern const std::size_t float_size[];

typedef enum
{
    VECTOR96 = 0,
    VECTOR48,
    VECTOR32,
    VECTOR24,
    vector_type_count
} vector_type;

extern const char *vector_type_string[];

extern const std::size_t vector_size[];

// Entries are packed at 1-, 2-, 3-, 4-, 6- or 12-byte strides and accessed as 32-bit words (a word
// may cover the next entries' bytes, and past the last entry the block's unused_size bytes): memcpy
// keeps those accesses defined at any alignment.
inline unsigned int load32(const void *s)
{
    unsigned int i;
    std::memcpy(&i, s, sizeof(i));
    return i;
}

inline void store32(void *s, unsigned int i)
{
    std::memcpy(s, &i, sizeof(i));
}

// Bits [start, end) of i. end == 32 keeps the high bits: GCC folded the oracle's ~0u << 32 (undefined)
// that way at every call site, which all pass constants.
inline unsigned int bitget(unsigned int i, unsigned int start, unsigned int end)
{
    return (end >= 32 ? i : i & ~(~0u << end)) >> start;
}

inline unsigned int bitput(unsigned int i, unsigned int start)
{
    return i << start;
}

inline unsigned int bitclr(unsigned int i, unsigned int start, unsigned int end)
{
    return i & (~(~0u << start) | (~0u << end));
}

// compat: i >> n with n computed at run time, possibly >= 32 (undefined); the oracle's x86 shr uses
// n mod 32, so a zero or much smaller component does not become 0.
inline unsigned int shr32(unsigned int i, unsigned int n)
{
    return i >> (n & 31u);
}

inline unsigned int float_iswrong(unsigned int i)
{
    return i >= 0x7F800000u;
}

inline unsigned int float_istoobig(unsigned int i)
{
    return i >= 0x40000000u;
}

inline unsigned int float_istoosmall(unsigned int i)
{
    return i < 0x30800000u;
}

inline void float_compress(float_type t, void *s, const float *f)
{
    const unsigned int p = std::bit_cast<unsigned int>(*f);
    unsigned int m;
    switch (t)
    {
    case FLOAT32:
        store32(s, p);
        break;
    case FLOAT16:
        m = bitclr(load32(s), 0, 16);
        if (float_iswrong(p))
            ;
        else if (float_istoobig(p))
            m |= bitget(~0u, 0, 16);
        else if (float_istoosmall(p))
            ;
        else
            m |= bitget(p, 12, 28);
        store32(s, m);
        break;
    case FLOAT8:
        m = bitclr(load32(s), 0, 8);
        if (float_iswrong(p))
            ;
        else if (float_istoobig(p))
            m |= bitget(~0u, 0, 8);
        else if (float_istoosmall(p))
            ;
        else
            m |= bitget(p, 20, 28);
        store32(s, m);
        break;
    default:;
    }
}

inline void float_decompress(float_type t, const void *s, float *f)
{
    const unsigned int m = load32(s);
    unsigned int p;
    switch (t)
    {
    case FLOAT32:
        p = m;
        break;
    case FLOAT16:
        if (bitget(m, 0, 16) == 0)
            p = 0;
        else
            p = bitput(1, 11) | bitput(bitget(m, 0, 16), 12) | bitput(3, 28);
        break;
    case FLOAT8:
        if (bitget(m, 0, 8) == 0)
            p = 0;
        else
            p = bitput(1, 19) | bitput(bitget(m, 0, 8), 20) | bitput(3, 28);
        break;
    default:
        return;
    }
    *f = std::bit_cast<float>(p);
}

inline void vector_compress(vector_type t, void *s, const float *f1, const float *f2, const float *f3)
{
    unsigned char *b = static_cast<unsigned char *>(s);
    const unsigned int p1 = std::bit_cast<unsigned int>(*f1);
    const unsigned int p2 = std::bit_cast<unsigned int>(*f2);
    const unsigned int p3 = std::bit_cast<unsigned int>(*f3);
    unsigned int m0, m1, max, i1, i2, i3;
    switch (t)
    {
    case VECTOR96:
        store32(b, p1);
        store32(b + 4, p2);
        store32(b + 8, p3);
        break;
    case VECTOR48:
        if (float_iswrong(p1) || float_iswrong(p2) || float_iswrong(p3))
            break;
        m0 = 0, m1 = bitclr(load32(b + 4), 0, 16);
        if (float_istoobig(p1))
            m0 |= bitget(~0u, 0, 16);
        else if (float_istoosmall(p1))
            ;
        else
            m0 |= bitget(p1, 12, 28);
        if (float_istoobig(p2))
            m0 |= bitput(bitget(~0u, 0, 16), 16);
        else if (float_istoosmall(p2))
            ;
        else
            m0 |= bitput(bitget(p2, 12, 28), 16);
        if (float_istoobig(p3))
            m1 |= bitget(~0u, 0, 16);
        else if (float_istoosmall(p3))
            ;
        else
            m1 |= bitget(p3, 12, 28);
        store32(b, m0);
        store32(b + 4, m1);
        break;
    case VECTOR32:
    case VECTOR24:
        if (float_iswrong(p1) || float_iswrong(p2) || float_iswrong(p3))
        {
            max = i1 = i2 = i3 = 0;
        }
        else
        {
            max = p1 > p2 ? (p1 > p3 ? p1 : p3) : (p2 > p3 ? p2 : p3);
            max = float_istoobig(max) ? 0x7F : float_istoosmall(max) ? 0x60
                                                                     : bitget(max, 23, 31);
            i1 = float_istoobig(p1) ? ~0u : shr32(bitget(p1, 0, 23) | bitput(1, 23), 1 + max - bitget(p1, 23, 31));
            i2 = float_istoobig(p2) ? ~0u : shr32(bitget(p2, 0, 23) | bitput(1, 23), 1 + max - bitget(p2, 23, 31));
            i3 = float_istoobig(p3) ? ~0u : shr32(bitget(p3, 0, 23) | bitput(1, 23), 1 + max - bitget(p3, 23, 31));
        }
        if (t == VECTOR32)
            store32(b, 0 | bitput(bitget(i1, 14, 23), 0) | bitput(bitget(i2, 14, 23), 9) | bitput(bitget(i3, 14, 23), 18) | bitput(bitget(max, 0, 5), 27));
        else
            store32(b, bitclr(load32(b), 0, 24) | bitput(bitget(i1, 17, 23), 0) | bitput(bitget(i2, 17, 23), 6) | bitput(bitget(i3, 17, 23), 12) | bitput(bitget(max, 0, 5), 18));
        break;
    default:;
    }
}

inline void vector_decompress(vector_type t, const void *s, float *f1, float *f2, float *f3)
{
    const unsigned char *b = static_cast<const unsigned char *>(s);
    const unsigned int m0 = load32(b);
    switch (t)
    {
    case VECTOR96:
        *f1 = std::bit_cast<float>(m0);
        *f2 = std::bit_cast<float>(load32(b + 4));
        *f3 = std::bit_cast<float>(load32(b + 8));
        break;
    case VECTOR48:
    {
        const unsigned int m1 = load32(b + 4);
        *f1 = std::bit_cast<float>(bitget(m0, 0, 16) == 0 ? 0u : bitput(1, 11) | bitput(bitget(m0, 0, 16), 12) | bitput(3, 28));
        *f2 = std::bit_cast<float>(bitget(m0, 16, 32) == 0 ? 0u : bitput(1, 11) | bitput(bitget(m0, 16, 32), 12) | bitput(3, 28));
        *f3 = std::bit_cast<float>(bitget(m1, 0, 16) == 0 ? 0u : bitput(1, 11) | bitput(bitget(m1, 0, 16), 12) | bitput(3, 28));
        break;
    }
    case VECTOR32:
    case VECTOR24:
    {
        unsigned int p1, p2, p3, e;
        if (t == VECTOR32)
        {
            p1 = bitput(1, 13) | bitput(bitget(m0, 0, 9), 14) | bitput(bitget(m0, 27, 32), 23) | bitput(3, 28);
            p2 = bitput(1, 13) | bitput(bitget(m0, 9, 18), 14) | bitput(bitget(m0, 27, 32), 23) | bitput(3, 28);
            p3 = bitput(1, 13) | bitput(bitget(m0, 18, 27), 14) | bitput(bitget(m0, 27, 32), 23) | bitput(3, 28);
            e = bitput(bitget(m0, 27, 32), 23) | bitput(3, 28);
        }
        else
        {
            p1 = bitput(1, 16) | bitput(bitget(m0, 0, 6), 17) | bitput(bitget(m0, 18, 23), 23) | bitput(3, 28);
            p2 = bitput(1, 16) | bitput(bitget(m0, 6, 12), 17) | bitput(bitget(m0, 18, 23), 23) | bitput(3, 28);
            p3 = bitput(1, 16) | bitput(bitget(m0, 12, 18), 17) | bitput(bitget(m0, 18, 23), 23) | bitput(3, 28);
            e = bitput(bitget(m0, 18, 23), 23) | bitput(3, 28);
        }
        const float f = std::bit_cast<float>(e);
        *f1 = (std::bit_cast<float>(p1) - f) * 2.f;
        *f2 = (std::bit_cast<float>(p2) - f) * 2.f;
        *f3 = (std::bit_cast<float>(p3) - f) * 2.f;
        break;
    }
    default:;
    }
}
