#include <cstdio>

#include "hlrad.h"
#include "common/blockmem.h"
#include "common/log.h"

/*
 * =============
 * writetransfers
 * =============
 */

void writetransfers(const char *const transferfile, const long total_patches)
{
    std::FILE *file = std::fopen(transferfile, "w+b");
    if (file != nullptr)
    {

        Log("Writing transfers file [%s]\n", transferfile);

        unsigned amtwritten = std::fwrite(&total_patches, sizeof(total_patches), 1, file);
        if (amtwritten != 1)
        {
            goto FailedWrite;
        }

        long patchcount = total_patches;
        for (patch_t *patch = g_patches; patchcount-- > 0; patch++)
        {
            amtwritten = std::fwrite(&patch->iIndex, sizeof(patch->iIndex), 1, file);
            if (amtwritten != 1)
            {
                goto FailedWrite;
            }

            if (patch->iIndex)
            {
                amtwritten = std::fwrite(patch->tIndex, sizeof(transfer_index_t), patch->iIndex, file);
                if (amtwritten != patch->iIndex)
                {
                    goto FailedWrite;
                }
            }

            amtwritten = std::fwrite(&patch->iData, sizeof(patch->iData), 1, file);
            if (amtwritten != 1)
            {
                goto FailedWrite;
            }
            if (patch->iData)
            {
                if (g_rgb_transfers)
                {
                    amtwritten = std::fwrite(patch->tRGBData, vector_size[g_rgbtransfer_compress_type], patch->iData, file);
                }
                else
                {
                    amtwritten = std::fwrite(patch->tData, float_size[g_transfer_compress_type], patch->iData, file);
                }
                if (amtwritten != patch->iData)
                {
                    goto FailedWrite;
                }
            }
        }

        std::fclose(file);
    }
    else
    {
        Error("Failed to open incremenetal file [%s] for writing\n", transferfile);
    }
    return;

FailedWrite:
    std::fclose(file);
    unlink(transferfile);
    //Warning("Failed to generate incremental file [%s] (probably ran out of disk space)\n");
    Warning("Failed to generate incremental file [%s] (probably ran out of disk space)\n", transferfile); //--vluzacn
}

/*
 * =============
 * readtransfers
 * =============
 */

bool readtransfers(const char *const transferfile, const long numpatches)
{
    long total_patches;

    std::FILE *file = std::fopen(transferfile, "rb");
    if (file != nullptr)
    {

        Log("Reading transfers file [%s]\n", transferfile);

        unsigned amtread = std::fread(&total_patches, sizeof(total_patches), 1, file);
        if (amtread != 1)
        {
            goto FailedRead;
        }
        if (total_patches != numpatches)
        {
            goto FailedRead;
        }

        long patchcount = total_patches;
        for (patch_t *patch = g_patches; patchcount-- > 0; patch++)
        {
            amtread = std::fread(&patch->iIndex, sizeof(patch->iIndex), 1, file);
            if (amtread != 1)
            {
                goto FailedRead;
            }
            if (patch->iIndex)
            {
                patch->tIndex = (transfer_index_t *)AllocBlock(patch->iIndex * sizeof(transfer_index_t *));
                hlassume(patch->tIndex != nullptr, assume_NoMemory);
                amtread = std::fread(patch->tIndex, sizeof(transfer_index_t), patch->iIndex, file);
                if (amtread != patch->iIndex)
                {
                    goto FailedRead;
                }
            }

            amtread = std::fread(&patch->iData, sizeof(patch->iData), 1, file);
            if (amtread != 1)
            {
                goto FailedRead;
            }
            if (patch->iData)
            {
                if (g_rgb_transfers)
                {
                    patch->tRGBData = (rgb_transfer_data_t *)AllocBlock(patch->iData * vector_size[g_rgbtransfer_compress_type] + unused_size);
                    hlassume(patch->tRGBData != nullptr, assume_NoMemory);
                    amtread = std::fread(patch->tRGBData, vector_size[g_rgbtransfer_compress_type], patch->iData, file);
                }
                else
                {
                    patch->tData = (transfer_data_t *)AllocBlock(patch->iData * float_size[g_transfer_compress_type] + unused_size);
                    hlassume(patch->tData != nullptr, assume_NoMemory);
                    amtread = std::fread(patch->tData, float_size[g_transfer_compress_type], patch->iData, file);
                }
                if (amtread != patch->iData)
                {
                    goto FailedRead;
                }
            }
        }

        std::fclose(file);
        //Warning("Finished reading transfers file [%s] %d\n", transferfile);
        Warning("Finished reading transfers file [%s]\n", transferfile); //--vluzacn
        return true;
    }
    Warning("Failed to open transfers file [%s]\n", transferfile);
    return false;

FailedRead:
{
    patch_t *patch = g_patches;

    for (unsigned x = 0; x < g_num_patches; x++, patch++)
    {
        FreeBlock(patch->tData);
        FreeBlock(patch->tIndex);
        patch->iData = 0;
        patch->iIndex = 0;
        patch->tData = nullptr;
        patch->tIndex = nullptr;
    }
}
    std::fclose(file);
    unlink(transferfile);
    return false;
}
