// Transparency Arrays for sparse and vismatrix methods
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <climits>

#include "hlrad/hlrad.h"
#include "common/log.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"
#include "common/threads.h"
#include "common/win32fix.h"

#include <algorithm>
#include <memory>
#include <tuple>

// compat: the arrays grow as they did with realloc (by 1.41, at least 64 entries, new entries zeroed),
// because the logs report their capacity.
template <class T>
static void GrowArray(std::unique_ptr<T[]> &array, unsigned int &max_count, const char *function)
{
    unsigned int old_max_count = max_count;
    max_count = qmax(64u, (unsigned int)((double)max_count * 1.41));
    if (max_count >= (unsigned int)INT_MAX)
    {
        Error("%s: array size exceeded INT_MAX", function);
    }
    auto bigger = std::make_unique<T[]>(max_count);
    if (old_max_count)
    {
        std::memcpy(bigger.get(), array.get(), sizeof(T) * old_max_count);
    }
    array = std::move(bigger);
}

typedef struct
{
    unsigned p1;
    unsigned p2;
    unsigned data_index;
} transList_t;

static std::unique_ptr<vec3_t[]> s_trans_list;
static unsigned int s_trans_count = 0;
static unsigned int s_max_trans_count = 0;

static std::unique_ptr<transList_t[]> s_raw_list;
static unsigned int s_raw_count = 0;
static unsigned int s_max_raw_count = 0; // Current array maximum (used for reallocs)

static std::unique_ptr<transList_t[]> s_sorted_list; // Sorted first by p1 then p2
static unsigned int s_sorted_count = 0;

static unsigned AddTransparencyToDataList(const vec3_t trans)
{
    //Check if this value is in list already
    for (unsigned int i = 0; i < s_trans_count; i++)
    {
        if (VectorCompare(trans, s_trans_list[i]))
        {
            return i;
        }
    }

    //realloc if needed
    while (s_trans_count >= s_max_trans_count)
    {
        unsigned int old_max_count = s_max_trans_count;
        GrowArray(s_trans_list, s_max_trans_count, "AddTransparencyToDataList");

        if (old_max_count == 0)
        {
            VectorFill(s_trans_list[0], 1.0);
            s_trans_count++;
        }
    }

    VectorCopy(trans, s_trans_list[s_trans_count]);

    return (s_trans_count++);
}

void AddTransparencyToRawArray(const unsigned p1, const unsigned p2, const vec3_t trans)
{
    //make thread safe
    ThreadLock();

    unsigned data_index = AddTransparencyToDataList(trans);

    //realloc if needed
    while (s_raw_count >= s_max_raw_count)
    {
        GrowArray(s_raw_list, s_max_raw_count, "AddTransparencyToRawArray");
    }

    s_raw_list[s_raw_count].p1 = p1;
    s_raw_list[s_raw_count].p2 = p2;
    s_raw_list[s_raw_count].data_index = data_index;

    s_raw_count++;

    //unlock list
    ThreadUnlock();
}

// compat: stable, as glibc's qsort (a merge sort) was
static bool TransLess(const transList_t &a, const transList_t &b)
{
    return std::tie(a.p1, a.p2) < std::tie(b.p1, b.p2);
}

void CreateFinalTransparencyArrays(const char *print_name)
{
    if (s_raw_count == 0)
    {
        s_raw_list.reset();
        s_raw_count = s_max_raw_count = 0;
        return;
    }

    //double sized (faster find function for sorted list)
    s_sorted_count = s_raw_count * 2;
    s_sorted_list = std::make_unique_for_overwrite<transList_t[]>(s_sorted_count);

    //First half have p1>p2
    for (unsigned int i = 0; i < s_raw_count; i++)
    {
        s_sorted_list[i].p1 = s_raw_list[i].p2;
        s_sorted_list[i].p2 = s_raw_list[i].p1;
        s_sorted_list[i].data_index = s_raw_list[i].data_index;
    }
    //Second half have p1<p2
    std::memcpy(&s_sorted_list[s_raw_count], s_raw_list.get(), sizeof(transList_t) * s_raw_count);

    //free old array
    s_raw_list.reset();
    s_raw_count = s_max_raw_count = 0;

    //need to sorted for fast search function
    std::stable_sort(s_sorted_list.get(), s_sorted_list.get() + s_sorted_count, TransLess);

    std::size_t size = s_sorted_count * sizeof(transList_t) + s_max_trans_count * sizeof(vec3_t);
    if (size > 1024 * 1024)
        Log("%-20s: %5.1f megs \n", print_name, (double)size / (1024.0 * 1024.0));
    else if (size > 1024)
        Log("%-20s: %5.1f kilos\n", print_name, (double)size / 1024.0);
    else
        Log("%-20s: %5.1f bytes\n", print_name, (double)size); //--vluzacn
}

void FreeTransparencyArrays()
{
    s_trans_list.reset();
    s_sorted_list.reset();

    s_max_trans_count = s_trans_count = s_sorted_count = 0;
}

unsigned int TransparencySearchStart(unsigned p1)
{
    const transList_t *list = s_sorted_list.get();
    return std::lower_bound(list, list + s_sorted_count, p1, [](const transList_t &e, unsigned p)
                            { return e.p1 < p; }) -
           list;
}

// find transparency from list. remembers last location
void GetTransparency(const unsigned p1, const unsigned p2, vec3_t &trans, unsigned int &next_index)
{
    VectorFill(trans, 1.0);

    for (unsigned i = next_index; i < s_sorted_count; i++)
    {
        if (s_sorted_list[i].p1 < p1)
        {
            continue;
        }
        else if (s_sorted_list[i].p1 == p1)
        {
            if (s_sorted_list[i].p2 < p2)
            {
                continue;
            }
            else if (s_sorted_list[i].p2 == p2)
            {
                VectorCopy(s_trans_list[s_sorted_list[i].data_index], trans);
                next_index = i + 1;

                return;
            }
            else //if ( s_sorted_list[i].p2 > p2 )
            {
                next_index = i;

                return;
            }
        }
        else //if ( s_sorted_list[i].p1 > p1 )
        {
            next_index = i;

            return;
        }
    }

    next_index = s_sorted_count;
}

typedef struct
{
    unsigned p1;
    unsigned p2;
    char style;
} styleList_t;
static std::unique_ptr<styleList_t[]> s_style_list;
static unsigned int s_style_count = 0;
static unsigned int s_max_style_count = 0;

void AddStyleToStyleArray(const unsigned p1, const unsigned p2, const int style)
{
    if (style == -1)
        return;
    //make thread safe
    ThreadLock();

    //realloc if needed
    while (s_style_count >= s_max_style_count)
    {
        GrowArray(s_style_list, s_max_style_count, "AddStyleToStyleArray");
    }

    s_style_list[s_style_count].p1 = p1;
    s_style_list[s_style_count].p2 = p2;
    s_style_list[s_style_count].style = (char)style;

    s_style_count++;

    //unlock list
    ThreadUnlock();
}

// compat: stable, as glibc's qsort (a merge sort) was
static bool StyleLess(const styleList_t &a, const styleList_t &b)
{
    return std::tie(a.p1, a.p2) < std::tie(b.p1, b.p2);
}

void CreateFinalStyleArrays(const char *print_name)
{
    if (s_style_count == 0)
    {
        return;
    }
    //need to sorted for fast search function
    std::stable_sort(s_style_list.get(), s_style_list.get() + s_style_count, StyleLess);

    std::size_t size = s_max_style_count * sizeof(styleList_t);
    if (size > 1024 * 1024)
        Log("%-20s: %5.1f megs \n", print_name, (double)size / (1024.0 * 1024.0));
    else if (size > 1024)
        Log("%-20s: %5.1f kilos\n", print_name, (double)size / 1024.0);
    else
        Log("%-20s: %5.1f bytes\n", print_name, (double)size); //--vluzacn
}

void FreeStyleArrays()
{
    s_style_list.reset();

    s_max_style_count = s_style_count = 0;
}

unsigned int StyleSearchStart(unsigned p1)
{
    const styleList_t *list = s_style_list.get();
    return std::lower_bound(list, list + s_style_count, p1, [](const styleList_t &e, unsigned p)
                            { return e.p1 < p; }) -
           list;
}

void GetStyle(const unsigned p1, const unsigned p2, int &style, unsigned int &next_index)
{
    style = -1;

    for (unsigned i = next_index; i < s_style_count; i++)
    {
        if (s_style_list[i].p1 < p1)
        {
            continue;
        }
        else if (s_style_list[i].p1 == p1)
        {
            if (s_style_list[i].p2 < p2)
            {
                continue;
            }
            else if (s_style_list[i].p2 == p2)
            {
                style = (int)s_style_list[i].style;
                next_index = i + 1;

                return;
            }
            else //if ( s_style_list[i].p2 > p2 )
            {
                next_index = i;

                return;
            }
        }
        else //if ( s_style_list[i].p1 > p1 )
        {
            next_index = i;

            return;
        }
    }

    next_index = s_style_count;
}