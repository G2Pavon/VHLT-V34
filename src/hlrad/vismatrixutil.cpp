#include <cstddef>
#include <cmath>

#include "hlrad/hlrad.h"
#include "common/mathtypes.h"
#include "common/mathlib.h"
#include "common/threads.h"

#define MAX_COMPRESSED_TRANSFER_INDEX_SIZE ((1 << 12) - 1)

funcCheckVisBit g_CheckVisBit = nullptr;

std::size_t g_total_transfer = 0;
std::size_t g_transfer_index_bytes = 0;
std::size_t g_transfer_data_bytes = 0;

static unsigned GetLengthOfRun(const transfer_raw_index_t *raw, const transfer_raw_index_t *const end)
{
    unsigned run_size = 0;

    while (raw < end)
    {
        if (((*raw) + 1) == (*(raw + 1)))
        {
            raw++;
            run_size++;

            if (run_size >= MAX_COMPRESSED_TRANSFER_INDEX_SIZE)
            {
                return run_size;
            }
        }
        else
        {
            return run_size;
        }
    }
    return run_size;
}

static std::unique_ptr<transfer_index_t[]> CompressTransferIndicies(transfer_raw_index_t *tRaw, const unsigned rawSize, unsigned *iSize)
{
    unsigned size = rawSize;
    unsigned compressed_count = 0;

    transfer_raw_index_t *raw = tRaw;
    transfer_raw_index_t *end = tRaw + rawSize - 1; // -1 since we are comparing current with next and get errors when bumping into the 'end'

    unsigned compressed_count_1 = 0;

    for (unsigned x = 0; x < rawSize; x++)
    {
        x += GetLengthOfRun(tRaw + x, tRaw + rawSize - 1);
        compressed_count_1++;
    }

    if (!compressed_count_1)
    {
        return nullptr;
    }

    auto CompressedArray = std::make_unique<transfer_index_t[]>(compressed_count_1);
    transfer_index_t *compressed = CompressedArray.get();

    for (unsigned x = 0; x < size; x++, raw++, compressed++)
    {
        compressed->index = (*raw);
        compressed->size = GetLengthOfRun(raw, end); // Zero based (count 0 still implies 1 item in the list, so 256 max entries result)
        raw += compressed->size;
        x += compressed->size;
        compressed_count++; // number of entries in compressed table
    }

    *iSize = compressed_count;

    if (compressed_count != compressed_count_1)
    {
        Error("CompressTransferIndicies: internal error");
    }

    ThreadLock();
    g_transfer_index_bytes += sizeof(transfer_index_t) * compressed_count;
    ThreadUnlock();

    return CompressedArray;
}

//This is the primary time sink.
//It can be run multi threaded.

#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter

void MakeScales(int /*threadnum*/)
{
    unsigned j;
    vec3_t delta;
    patch_t *patch2;
    vec3_t origin;

    unsigned int fastfind_index;

    std::vector<transfer_raw_index_t> tIndex_All(g_num_patches + 1);
    std::vector<float> tData_All(g_num_patches + 1);

    int count = 0;

    while (1)
    {
        int i = GetThreadWork();
        if (i == -1)
            break;

        patch_t *patch = g_patches + i;
        patch_transfers_t &transfers = g_patch_transfers[i];
        fastfind_index = TransparencySearchStart(i);
        transfers.iIndex = 0;
        transfers.iData = 0;

        transfer_raw_index_t *tIndex = tIndex_All.data();
        float *tData = tData_All.data();

        VectorCopy(patch->origin, origin);
        const vec_t *normal1 = getPlaneFromFaceNumber(patch->faceNumber)->normal;

        vec3_t backorigin;
        vec3_t backnormal;
        if (patch->translucent_b)
        {
            VectorMA(patch->origin, -(g_translucentdepth + 2 * PATCH_HUNT_OFFSET), normal1, backorigin);
            VectorSubtract(vec3_origin, normal1, backnormal);
        }
        int miptex = g_texinfo[g_dfaces[patch->faceNumber].texinfo].miptex;
        vec_t lighting_power = g_lightingconeinfo[miptex][0];
        vec_t lighting_scale = g_lightingconeinfo[miptex][1];
        bool lighting_diversify = (lighting_power != 1.0 || lighting_scale != 1.0);

        // find out which patch2's will collect light
        // from patch
        // HLRAD_NOSWAP: patch collect light from patch2

        for (j = 0, patch2 = g_patches; j < g_num_patches; j++, patch2++)
        {
            vec3_t transparency = {1.0, 1.0, 1.0};
            bool useback = false;

            if (!g_CheckVisBit(i, j, transparency, fastfind_index) || (i == j))
            {
                if (patch->translucent_b)
                {
                    if ((i == j) ||
                        !CheckVisBitBackwards(i, j, backorigin, backnormal, transparency))
                    {
                        continue;
                    }
                    useback = true;
                }
                else
                {
                    continue;
                }
            }

            const vec_t *normal2 = getPlaneFromFaceNumber(patch2->faceNumber)->normal;

            // calculate transferemnce
            VectorSubtract(patch2->origin, origin, delta);
            if (useback)
            {
                VectorSubtract(patch2->origin, backorigin, delta);
            }
            // move emitter back to its plane
            VectorMA(delta, -PATCH_HUNT_OFFSET, normal2, delta);

            vec_t dist = VectorNormalize(delta);
            vec_t dot1 = DotProduct(delta, normal1);
            if (useback)
            {
                dot1 = DotProduct(delta, backnormal);
            }
            vec_t dot2 = -DotProduct(delta, normal2);
            bool light_behind_surface = false;
            if (dot1 <= NORMAL_EPSILON)
            {
                light_behind_surface = true;
            }
            if (dot2 * dist <= MINIMUM_PATCH_DISTANCE)
            {
                continue;
            }

            if (lighting_diversify && !light_behind_surface)
            {
                dot1 = lighting_scale * std::pow(dot1, lighting_power);
            }
            float trans = (dot1 * dot2) / (dist * dist); // Inverse square falloff factoring angle between patch normals
            if (trans * patch2->area > 0.8f)
                trans = 0.8f / patch2->area;
            if (dist < patch2->emitter_range - ON_EPSILON)
            {
                if (light_behind_surface)
                {
                    trans = 0.0;
                }
                const Winding *emitter_winding;
                const vec_t *receiver_origin = origin;
                const vec_t *receiver_normal = normal1;
                if (useback)
                {
                    receiver_origin = backorigin;
                    receiver_normal = backnormal;
                }
                emitter_winding = patch2->winding;
                vec_t sightarea = CalcSightArea(receiver_origin, receiver_normal, emitter_winding, patch2->emitter_skylevel, lighting_power, lighting_scale);

                vec_t frac = dist / patch2->emitter_range;
                frac = (frac - 0.5f) * 2.0f; // make a smooth transition between the two methods
                frac = qmax(0, qmin(frac, 1));
                trans = frac * trans + (1 - frac) * (sightarea / patch2->area); // because later we will multiply this back
            }
            else
            {
                if (light_behind_surface)
                {
                    continue;
                }
            }

            trans *= patch2->exposure;
            trans = trans * VectorAvg(transparency); //hullu: add transparency effect
            if (patch->translucent_b)
            {
                if (useback)
                {
                    trans *= VectorAvg(patch->translucent_v);
                }
                else
                {
                    trans *= 1 - VectorAvg(patch->translucent_v);
                }
            }

            {

                trans = trans * patch2->area;
            }
            if (trans <= 0.0)
            {
                continue;
            }

            *tData = trans;
            *tIndex = j;
            tData++;
            tIndex++;
            transfers.iData++;
            count++;
        }

        // copy the transfers out
        if (transfers.iData)
        {
            unsigned data_size = transfers.iData * float_size[g_transfer_compress_type] + unused_size;

            transfers.data = std::make_unique<unsigned char[]>(data_size);
            transfers.index = CompressTransferIndicies(tIndex_All.data(), transfers.iData, &transfers.iIndex);

            ThreadLock();
            g_transfer_data_bytes += data_size;
            ThreadUnlock();

            vec_t total = 1 / Q_PI;
            {
                unsigned x;
                transfer_data_t *t1 = transfers.data.get();
                float *t2 = tData_All.data();

                float f;
                for (x = 0; x < transfers.iData; x++, t1 += float_size[g_transfer_compress_type], t2++)
                {
                    f = (*t2) * total;
                    float_compress(g_transfer_compress_type, t1, &f);
                }
            }
        }
    }


    ThreadLock();
    g_total_transfer += count;
    ThreadUnlock();
}
#pragma warning(pop)

/*
 * =============
 * SwapTransfersTask
 * 
 * Change transfers from light sent out to light collected in.
 * In an ideal world, they would be exactly symetrical, but
 * because the form factors are only aproximated, then normalized,
 * they will actually be rather different.
 * =============
 */

#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter

void MakeRGBScales(int /*threadnum*/)
{
    unsigned j;
    vec3_t delta;
    float trans[3];
    patch_t *patch2;
    vec3_t origin;

    unsigned int fastfind_index;

    std::vector<transfer_raw_index_t> tIndex_All(g_num_patches + 1);
    std::vector<float> tRGBData_All(3 * (g_num_patches + 1));

    int count = 0;

    while (1)
    {
        int i = GetThreadWork();
        if (i == -1)
            break;

        patch_t *patch = g_patches + i;
        patch_transfers_t &transfers = g_patch_transfers[i];
        fastfind_index = TransparencySearchStart(i);
        transfers.iIndex = 0;
        transfers.iData = 0;

        transfer_raw_index_t *tIndex = tIndex_All.data();
        float *tRGBData = tRGBData_All.data();

        VectorCopy(patch->origin, origin);
        const vec_t *normal1 = getPlaneFromFaceNumber(patch->faceNumber)->normal;

        vec3_t backorigin;
        vec3_t backnormal;
        if (patch->translucent_b)
        {
            VectorMA(patch->origin, -(g_translucentdepth + 2 * PATCH_HUNT_OFFSET), normal1, backorigin);
            VectorSubtract(vec3_origin, normal1, backnormal);
        }
        int miptex = g_texinfo[g_dfaces[patch->faceNumber].texinfo].miptex;
        vec_t lighting_power = g_lightingconeinfo[miptex][0];
        vec_t lighting_scale = g_lightingconeinfo[miptex][1];
        bool lighting_diversify = (lighting_power != 1.0 || lighting_scale != 1.0);

        // find out which patch2's will collect light
        // from patch
        // HLRAD_NOSWAP: patch collect light from patch2

        for (j = 0, patch2 = g_patches; j < g_num_patches; j++, patch2++)
        {
            vec3_t transparency = {1.0, 1.0, 1.0};
            bool useback = false;

            if (!g_CheckVisBit(i, j, transparency, fastfind_index) || (i == j))
            {
                if (patch->translucent_b)
                {
                    if (!CheckVisBitBackwards(i, j, backorigin, backnormal, transparency) || (i == j))
                    {
                        continue;
                    }
                    useback = true;
                }
                else
                {
                    continue;
                }
            }

            const vec_t *normal2 = getPlaneFromFaceNumber(patch2->faceNumber)->normal;

            // calculate transferemnce
            VectorSubtract(patch2->origin, origin, delta);
            if (useback)
            {
                VectorSubtract(patch2->origin, backorigin, delta);
            }
            // move emitter back to its plane
            VectorMA(delta, -PATCH_HUNT_OFFSET, normal2, delta);

            vec_t dist = VectorNormalize(delta);
            vec_t dot1 = DotProduct(delta, normal1);
            if (useback)
            {
                dot1 = DotProduct(delta, backnormal);
            }
            vec_t dot2 = -DotProduct(delta, normal2);
            bool light_behind_surface = false;
            if (dot1 <= NORMAL_EPSILON)
            {
                light_behind_surface = true;
            }
            if (dot2 * dist <= MINIMUM_PATCH_DISTANCE)
            {
                continue;
            }

            if (lighting_diversify && !light_behind_surface)
            {
                dot1 = lighting_scale * std::pow(dot1, lighting_power);
            }
            float trans_one = (dot1 * dot2) / (dist * dist); // Inverse square falloff factoring angle between patch normals

            if (trans_one * patch2->area > 0.8f)
            {
                trans_one = 0.8f / patch2->area;
            }
            if (dist < patch2->emitter_range - ON_EPSILON)
            {
                if (light_behind_surface)
                {
                    trans_one = 0.0;
                }
                const Winding *emitter_winding;
                const vec_t *receiver_origin = origin;
                const vec_t *receiver_normal = normal1;
                if (useback)
                {
                    receiver_origin = backorigin;
                    receiver_normal = backnormal;
                }
                emitter_winding = patch2->winding;
                vec_t sightarea = CalcSightArea(receiver_origin, receiver_normal, emitter_winding, patch2->emitter_skylevel, lighting_power, lighting_scale);

                vec_t frac = dist / patch2->emitter_range;
                frac = (frac - 0.5f) * 2.0f; // make a smooth transition between the two methods
                frac = qmax(0, qmin(frac, 1));
                trans_one = frac * trans_one + (1 - frac) * (sightarea / patch2->area); // because later we will multiply this back
            }
            else
            {
                if (light_behind_surface)
                {
                    continue;
                }
            }
            trans_one *= patch2->exposure;
            VectorFill(trans, trans_one);
            VectorMultiply(trans, transparency, trans); //hullu: add transparency effect
            if (patch->translucent_b)
            {
                if (useback)
                {
                    for (int x = 0; x < 3; x++)
                    {
                        trans[x] = patch->translucent_v[x] * trans[x];
                    }
                }
                else
                {
                    for (int x = 0; x < 3; x++)
                    {
                        trans[x] = (1 - patch->translucent_v[x]) * trans[x];
                    }
                }
            }

            if (trans_one <= 0.0)
            {
                continue;
            }
            {

                VectorScale(trans, patch2->area, trans);
            }

            VectorCopy(trans, tRGBData);
            *tIndex = j;
            tRGBData += 3;
            tIndex++;
            transfers.iData++;
            count++;
        }

        // copy the transfers out
        if (transfers.iData)
        {
            unsigned data_size = transfers.iData * vector_size[g_rgbtransfer_compress_type] + unused_size;

            transfers.data = std::make_unique<unsigned char[]>(data_size);
            transfers.index = CompressTransferIndicies(tIndex_All.data(), transfers.iData, &transfers.iIndex);

            ThreadLock();
            g_transfer_data_bytes += data_size;
            ThreadUnlock();

            vec_t total = 1 / Q_PI;
            {
                unsigned x;
                rgb_transfer_data_t *t1 = transfers.data.get();
                float *t2 = tRGBData_All.data();

                float f[3];
                for (x = 0; x < transfers.iData; x++, t1 += vector_size[g_rgbtransfer_compress_type], t2 += 3)
                {
                    VectorScale(t2, total, f);
                    vector_compress(g_rgbtransfer_compress_type, t1, &f[0], &f[1], &f[2]);
                }
            }
        }
    }


    ThreadLock();
    g_total_transfer += count;
    ThreadUnlock();
}

#pragma warning(pop)

//More human readable numbers
void DumpTransfersMemoryUsage()
{
    if (g_total_transfer > 1000 * 1000)
        Log("Transfer Lists : %11.0f : %8.2fM transfers\n", (double)g_total_transfer, (double)g_total_transfer / (1000.0f * 1000.0f));
    else if (g_total_transfer > 1000)
        Log("Transfer Lists : %11.0f : %8.2fk transfers\n", (double)g_total_transfer, (double)g_total_transfer / 1000.0f);
    else
        Log("Transfer Lists : %11.0f transfers\n", (double)g_total_transfer);

    if (g_transfer_index_bytes > 1024 * 1024)
        Log("       Indices : %11.0f : %8.2fM bytes\n", (double)g_transfer_index_bytes, (double)g_transfer_index_bytes / (1024.0f * 1024.0f));
    else if (g_transfer_index_bytes > 1024)
        Log("       Indices : %11.0f : %8.2fk bytes\n", (double)g_transfer_index_bytes, (double)g_transfer_index_bytes / 1024.0f);
    else
        Log("       Indices : %11.0f bytes\n", (double)g_transfer_index_bytes);

    if (g_transfer_data_bytes > 1024 * 1024)
        Log("          Data : %11.0f : %8.2fM bytes\n", (double)g_transfer_data_bytes, (double)g_transfer_data_bytes / (1024.0f * 1024.0f));
    else if (g_transfer_data_bytes > 1024)
        Log("          Data : %11.0f : %8.2fk bytes\n", (double)g_transfer_data_bytes, (double)g_transfer_data_bytes / 1024.0f);
    else
        Log("          Data : %11.0f bytes\n", (double)g_transfer_data_bytes);
}