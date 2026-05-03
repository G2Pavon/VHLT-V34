/***
*
*	Copyright (c) 1998, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
****/

// csg4.c

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "ripent.h"
#include "common/hlassert.h"
#include "common/log.h"
#include "common/cmdlinecfg.h"
#include "common/filelib.h"
#include "common/bspfile.h"
#include "common/cmdlib.h"

typedef enum
{
    hl_undefined = -1,
    hl_export = 0,
    hl_import = 1
} hl_types;

static hl_types g_mode = hl_undefined;
static hl_types g_texturemode = hl_undefined;

// g_parse: command line switch (-parse).
// Added by: Ryan Gregg aka Nem
bool g_parse = DEFAULT_PARSE;
bool g_textureparse = DEFAULT_TEXTUREPARSE;

bool g_chart = DEFAULT_CHART;

bool g_info = DEFAULT_INFO;

bool g_pause = false;

bool g_writeextentfile = DEFAULT_WRITEEXTENTFILE;

bool g_deleteembeddedlightmaps = DEFAULT_DELETEEMBEDDEDLIGHTMAPS;

// ScanForToken()
// Added by: Ryan Gregg aka Nem
//
// Scans entity data starting  at iIndex for cToken.  Every time a \n char
// is encountered iLine is incremented.  If iToken is not null, the index
// cToken was found at is inserted into it.
static bool ScanForToken(char cToken, int &iIndex, int &iLine, bool bIgnoreWhiteSpace, bool bIgnoreOthers, int *iToken = 0)
{
    for (; iIndex < g_entdatasize; iIndex++)
    {
        // If we found a null char, consider it end of data.
        if (g_dentdata[iIndex] == '\0')
        {
            iIndex = g_entdatasize;
            return false;
        }

        // Count lines (for error message).
        if (g_dentdata[iIndex] == '\n')
        {
            iLine++;
        }

        // Ignore white space, if we are ignoring it.
        if (!bIgnoreWhiteSpace && std::isspace(g_dentdata[iIndex]))
        {
            continue;
        }

        if (g_dentdata[iIndex] != cToken)
        {
            if (bIgnoreOthers)
                continue;
            else
                return false;
        }

        // Advance the index past the token.
        iIndex++;

        // Return the index of the token if requested.
        if (iToken != 0)
        {
            *iToken = iIndex - 1;
        }

        return true;
    }

    // End of data.
    return false;
}

#include <list>
typedef std::list<char *> CEntityPairList;
typedef std::list<CEntityPairList *> CEntityList;

// ParseEntityData()
// Added by: Ryan Gregg aka Nem
//
// Pareses and reformats entity data stripping all non essential
// formatting  and using the formatting  options passed through this
// function.  The length is specified because in some cases (i.e. the
// terminator) a null char is desired to be printed.
static void ParseEntityData(const char *cTab, int iTabLength, const char *cNewLine, int iNewLineLength, const char *cTerminator, int iTerminatorLength)
{
    CEntityList EntityList; // Parsed entities.

    int iIndex = 0; // Current char in g_dentdata.
    int iLine = 0;  // Current line in g_dentdata.

    char cError[256] = "";

    try
    {
        //
        // Parse entity data.
        //

        Log("\nParsing entity data.\n");

        while (true)
        {
            // Parse the start of an entity.
            if (!ScanForToken('{', iIndex, iLine, false, false))
            {
                if (iIndex == g_entdatasize)
                {
                    // We read all the entities.
                    break;
                }
                else
                {
                    sprintf_s(cError, "expected token %s on line %d.", "{", iLine);
                    throw cError;
                }
            }

            CEntityPairList *EntityPairList = new CEntityPairList();

            // Parse the rest of the entity.
            while (true)
            {
                // Parse the key and value.
                for (int j = 0; j < 2; j++)
                {
                    int iStart;
                    // Parse the start of a string.
                    if (!ScanForToken('\"', iIndex, iLine, false, false, &iStart))
                    {
                        sprintf_s(cError, "expected token %s on line %d.", "\"", iLine);
                        throw cError;
                    }

                    int iEnd;
                    // Parse the end of a string.
                    if (!ScanForToken('\"', iIndex, iLine, true, true, &iEnd))
                    {
                        sprintf_s(cError, "expected token %s on line %d.", "\"", iLine);
                        throw cError;
                    }

                    // Extract the string.
                    int iLength = iEnd - iStart - 1;
                    char *cString = new char[iLength + 1];
                    std::memcpy(cString, &g_dentdata[iStart + 1], iLength);
                    cString[iLength] = '\0';

                    // Save it.
                    EntityPairList->push_back(cString);
                }

                // Parse the end of an entity.
                if (!ScanForToken('}', iIndex, iLine, false, false))
                {
                    if (g_dentdata[iIndex] == '\"')
                    {
                        // We arn't done the entity yet.
                        continue;
                    }
                    else
                    {
                        sprintf_s(cError, "expected token %s on line %d.", "}", iLine);
                        throw cError;
                    }
                }

                // We read the entity.
                EntityList.push_back(EntityPairList);
                break;
            }
        }

        Log("%d entities parsed.\n", (int)EntityList.size());

        //
        // Calculate new data length.
        //

        int iNewLength = 0;

        for (CEntityList::iterator i = EntityList.begin(); i != EntityList.end(); ++i)
        {
            // Opening brace.
            iNewLength += 1;

            // New line.
            iNewLength += iNewLineLength;

            CEntityPairList *EntityPairList = *i;

            for (CEntityPairList::iterator j = EntityPairList->begin(); j != EntityPairList->end(); ++j)
            {
                // Tab.
                iNewLength += iTabLength;

                // String.
                iNewLength += 1;
                iNewLength += (int)std::strlen(*j);
                iNewLength += 1;

                // String seperator.
                iNewLength += 1;

                ++j;

                // String.
                iNewLength += 1;
                iNewLength += (int)std::strlen(*j);
                iNewLength += 1;

                // New line.
                iNewLength += iNewLineLength;
            }

            // Closing brace.
            iNewLength += 1;

            // New line.
            iNewLength += iNewLineLength;
        }

        // Terminator.
        iNewLength += iTerminatorLength;

        //
        // Check our parsed data.
        //

        assume(iNewLength != 0, "No entity data.");
        assume(iNewLength < sizeof(g_dentdata), "Entity data size exceedes dentdata limit.");

        //
        // Clear current data.
        //

        g_entdatasize = 0;

        //
        // Fill new data.
        //

        Log("Formating entity data.\n\n");

        for (CEntityList::iterator i = EntityList.begin(); i != EntityList.end(); ++i)
        {
            // Opening brace.
            g_dentdata[g_entdatasize] = '{';
            g_entdatasize += 1;

            // New line.
            std::memcpy(&g_dentdata[g_entdatasize], cNewLine, iNewLineLength);
            g_entdatasize += iNewLineLength;

            CEntityPairList *EntityPairList = *i;

            for (CEntityPairList::iterator j = EntityPairList->begin(); j != EntityPairList->end(); ++j)
            {
                // Tab.
                std::memcpy(&g_dentdata[g_entdatasize], cTab, iTabLength);
                g_entdatasize += iTabLength;

                // String.
                g_dentdata[g_entdatasize] = '\"';
                g_entdatasize += 1;
                std::memcpy(&g_dentdata[g_entdatasize], *j, std::strlen(*j));
                g_entdatasize += (int)std::strlen(*j);
                g_dentdata[g_entdatasize] = '\"';
                g_entdatasize += 1;

                // String seperator.
                g_dentdata[g_entdatasize] = ' ';
                g_entdatasize += 1;

                ++j;

                // String.
                g_dentdata[g_entdatasize] = '\"';
                g_entdatasize += 1;
                std::memcpy(&g_dentdata[g_entdatasize], *j, std::strlen(*j));
                g_entdatasize += (int)std::strlen(*j);
                g_dentdata[g_entdatasize] = '\"';
                g_entdatasize += 1;

                // New line.
                std::memcpy(&g_dentdata[g_entdatasize], cNewLine, iNewLineLength);
                g_entdatasize += iNewLineLength;
            }

            // Closing brace.
            g_dentdata[g_entdatasize] = '}';
            g_entdatasize += 1;

            // New line.
            std::memcpy(&g_dentdata[g_entdatasize], cNewLine, iNewLineLength);
            g_entdatasize += iNewLineLength;
        }

        // Terminator.
        std::memcpy(&g_dentdata[g_entdatasize], cTerminator, iTerminatorLength);
        g_entdatasize += iTerminatorLength;

        //
        // Delete entity data.
        //

        for (CEntityList::iterator i = EntityList.begin(); i != EntityList.end(); ++i)
        {
            CEntityPairList *EntityPairList = *i;

            for (CEntityPairList::iterator j = EntityPairList->begin(); j != EntityPairList->end(); ++j)
            {
                delete[] *j;
            }

            delete EntityPairList;
        }

        //return true;
    }
    catch (...)
    {
        //
        // Delete entity data.
        //

        for (CEntityList::iterator i = EntityList.begin(); i != EntityList.end(); ++i)
        {
            CEntityPairList *EntityPairList = *i;

            for (CEntityPairList::iterator j = EntityPairList->begin(); j != EntityPairList->end(); ++j)
            {
                delete[] *j;
            }

            delete EntityPairList;
        }

        // If we threw the error cError wont be null, this is
        // a message, print it.
        if (*cError != '\0')
        {
            Error(cError);
        }
        Error("unknowen exception.");

        //return false;
    }
}

static void ReadBSP(const char *const name)
{
    char filename[_MAX_PATH];

    safe_snprintf(filename, _MAX_PATH, "%s.bsp", name);

    LoadBSPFile(filename);
    if (g_writeextentfile)
    {
        hlassume(CalcFaceExtents_test(), assume_first);
        char extentfilename[_MAX_PATH];
        safe_snprintf(extentfilename, _MAX_PATH, "%s.ext", name);
        Log("\nWriting %s.\n", extentfilename);
        WriteExtentFile(extentfilename);
    }
}

static void WriteBSP(const char *const name)
{
    char filename[_MAX_PATH];

    safe_snprintf(filename, _MAX_PATH, "%s.bsp", name);

    Log("\nUpdating %s.\n", filename); //--vluzacn
    WriteBSPFile(filename);
}

typedef struct
{
    char identification[4]; // should be WAD3
    int numlumps;
    int infotableofs;
} wadinfo_t;
typedef struct
{
    int filepos;
    int disksize;
    int size; // uncompressed
    char type;
    char compression;
    char pad1, pad2;
    char name[MAX_TEXTURE_NAME_LENGTH]; // must be null terminated
} lumpinfo_t;

static void WriteTextures(const char *const name)
{
    char wadfilename[_MAX_PATH];
    std::FILE *wadfile;
    safe_snprintf(wadfilename, _MAX_PATH, "%s.wad", name);
    _unlink(wadfilename);
    wadfile = SafeOpenWrite(wadfilename);
    Log("\nWriting %s.\n", wadfilename);

    char texfilename[_MAX_PATH];
    std::FILE *texfile;
    safe_snprintf(texfilename, _MAX_PATH, "%s.tex", name);
    _unlink(texfilename);
    if (!g_textureparse)
    {
        int dataofs = (int)(intptr_t)&((dmiptexlump_t *)NULL)->dataofs[((dmiptexlump_t *)g_dtexdata)->nummiptex];
        int wadofs = sizeof(wadinfo_t);

        wadinfo_t header;
        header.identification[0] = 'W';
        header.identification[1] = 'A';
        header.identification[2] = 'D';
        header.identification[3] = '3';
        header.numlumps = ((dmiptexlump_t *)g_dtexdata)->nummiptex;
        header.infotableofs = g_texdatasize - dataofs + wadofs;
        SafeWrite(wadfile, &header, wadofs);

        SafeWrite(wadfile, (byte *)g_dtexdata + dataofs, g_texdatasize - dataofs);

        lumpinfo_t *info;
        info = (lumpinfo_t *)std::malloc(((dmiptexlump_t *)g_dtexdata)->nummiptex * sizeof(lumpinfo_t));
        hlassume(info != nullptr, assume_NoMemory);
        std::memset(info, 0, header.numlumps * sizeof(lumpinfo_t));

        for (int i = 0; i < header.numlumps; i++)
        {
            int ofs = ((dmiptexlump_t *)g_dtexdata)->dataofs[i];
            int size = 0;
            if (ofs >= 0)
            {
                size = g_texdatasize - ofs;
                for (int j = 0; j < ((dmiptexlump_t *)g_dtexdata)->nummiptex; ++j)
                    if (ofs < ((dmiptexlump_t *)g_dtexdata)->dataofs[j] &&
                        ofs + size > ((dmiptexlump_t *)g_dtexdata)->dataofs[j])
                        size = ((dmiptexlump_t *)g_dtexdata)->dataofs[j] - ofs;
            }
            info[i].filepos = ofs - dataofs + wadofs;
            info[i].disksize = size;
            info[i].size = size;
            info[i].type = (ofs >= 0 && ((miptex_t *)(g_dtexdata + ofs))->offsets[0] > 0) ? 67 : 0; // prevent invalid texture from being processed by Wally
            info[i].compression = 0;
            std::strcpy(info[i].name, ofs >= 0 ? ((miptex_t *)(g_dtexdata + ofs))->name : "\rTEXTUREMISSING");
        }
        SafeWrite(wadfile, info, header.numlumps * sizeof(lumpinfo_t));
        std::free(info);
    }
    else
    {
        texfile = SafeOpenWrite(texfilename);
        Log("\nWriting %s.\n", texfilename);

        wadinfo_t header;
        header.identification[0] = 'W';
        header.identification[1] = 'A';
        header.identification[2] = 'D';
        header.identification[3] = '3';
        header.numlumps = 0;

        lumpinfo_t *info;
        info = (lumpinfo_t *)std::malloc(((dmiptexlump_t *)g_dtexdata)->nummiptex * sizeof(lumpinfo_t)); // might be more than needed
        hlassume(info != nullptr, assume_NoMemory);

        std::fprintf(texfile, "%d\r\n", ((dmiptexlump_t *)g_dtexdata)->nummiptex);
        std::fseek(wadfile, sizeof(wadinfo_t), SEEK_SET);

        for (int itex = 0; itex < ((dmiptexlump_t *)g_dtexdata)->nummiptex; ++itex)
        {
            int ofs = ((dmiptexlump_t *)g_dtexdata)->dataofs[itex];
            miptex_t *tex = (miptex_t *)(g_dtexdata + ofs);
            if (ofs < 0)
            {
                std::fprintf(texfile, "[-1]\r\n");
            }
            else
            {
                int size = g_texdatasize - ofs;
                for (int j = 0; j < ((dmiptexlump_t *)g_dtexdata)->nummiptex; ++j)
                    if (ofs < ((dmiptexlump_t *)g_dtexdata)->dataofs[j] &&
                        ofs + size > ((dmiptexlump_t *)g_dtexdata)->dataofs[j])
                        size = ((dmiptexlump_t *)g_dtexdata)->dataofs[j] - ofs;
                bool included = false;
                if (tex->offsets[0] > 0)
                    included = true;
                if (included)
                {
                    std::memset(&info[header.numlumps], 0, sizeof(lumpinfo_t));
                    info[header.numlumps].filepos = std::ftell(wadfile);
                    SafeWrite(wadfile, tex, size);
                    info[header.numlumps].disksize = std::ftell(wadfile) - info[header.numlumps].filepos;
                    info[header.numlumps].size = info[header.numlumps].disksize;
                    info[header.numlumps].type = 67;
                    info[header.numlumps].compression = 0;
                    std::strcpy(info[header.numlumps].name, tex->name);
                    header.numlumps++;
                }
                std::fprintf(texfile, "[%d]", (int)std::strlen(tex->name));
                SafeWrite(texfile, tex->name, std::strlen(tex->name));
                std::fprintf(texfile, " %d %d\r\n", tex->width, tex->height);
            }
        }
        header.infotableofs = std::ftell(wadfile);
        SafeWrite(wadfile, info, header.numlumps * sizeof(lumpinfo_t));
        std::fseek(wadfile, 0, SEEK_SET);
        SafeWrite(wadfile, &header, sizeof(wadinfo_t));

        std::fclose(texfile);
        std::free(info);
    }
    std::fclose(wadfile);
}
inline void skipspace(std::FILE *f) { std::fscanf(f, "%*[ \t\r\n]s"); }
inline void skipline(std::FILE *f) { std::fscanf(f, "%*[^\r\n]s"); }
static void ReadTextures(const char *name)
{
    char wadfilename[_MAX_PATH];
    std::FILE *wadfile;
    safe_snprintf(wadfilename, _MAX_PATH, "%s.wad", name);
    wadfile = SafeOpenRead(wadfilename);
    Log("\nReading %s.\n", wadfilename);

    char texfilename[_MAX_PATH];
    std::FILE *texfile;
    safe_snprintf(texfilename, _MAX_PATH, "%s.tex", name);
    if (!g_textureparse)
    {
        wadinfo_t header;
        int wadofs = sizeof(wadinfo_t);
        SafeRead(wadfile, &header, wadofs);
        ((dmiptexlump_t *)g_dtexdata)->nummiptex = header.numlumps;
        int dataofs = (int)(intptr_t)&((dmiptexlump_t *)NULL)->dataofs[((dmiptexlump_t *)g_dtexdata)->nummiptex];
        g_texdatasize = header.infotableofs - wadofs + dataofs;

        SafeRead(wadfile, (byte *)g_dtexdata + dataofs, g_texdatasize - dataofs);

        lumpinfo_t *info;
        info = (lumpinfo_t *)std::malloc(header.numlumps * sizeof(lumpinfo_t));
        hlassume(info != nullptr, assume_NoMemory);
        SafeRead(wadfile, info, header.numlumps * sizeof(lumpinfo_t));

        for (int i = 0; i < header.numlumps; i++)
        {
            ((dmiptexlump_t *)g_dtexdata)->dataofs[i] = info[i].filepos - wadofs + dataofs;
        }

        std::free(info);
    }
    else
    {
        texfile = SafeOpenRead(texfilename);
        Log("\nReading %s.\n", texfilename);

        wadinfo_t header;
        SafeRead(wadfile, &header, sizeof(wadinfo_t));
        std::fseek(wadfile, header.infotableofs, SEEK_SET);

        lumpinfo_t *info;
        info = (lumpinfo_t *)std::malloc(header.numlumps * sizeof(lumpinfo_t));
        hlassume(info != nullptr, assume_NoMemory);
        SafeRead(wadfile, info, header.numlumps * sizeof(lumpinfo_t));

        int nummiptex = 0;
        if (skipspace(texfile), std::fscanf(texfile, "%d", &nummiptex) != 1)
            Error("File read failure");
        ((dmiptexlump_t *)g_dtexdata)->nummiptex = nummiptex;
        g_texdatasize = (byte *)(&((dmiptexlump_t *)g_dtexdata)->dataofs[nummiptex]) - g_dtexdata;

        for (int itex = 0; itex < nummiptex; ++itex)
        {
            int len;
            if (skipspace(texfile), std::fscanf(texfile, "[%d]", &len) != 1)
                Error("File read failure");
            if (len < 0)
            {
                ((dmiptexlump_t *)g_dtexdata)->dataofs[itex] = -1;
            }
            else
            {
                char name[MAX_TEXTURE_NAME_LENGTH];
                if (len > 15)
                    Error("Texture name is too long");
                std::memset(name, '\0', MAX_TEXTURE_NAME_LENGTH);
                SafeRead(texfile, name, len);
                ((dmiptexlump_t *)g_dtexdata)->dataofs[itex] = g_texdatasize;
                miptex_t *tex = (miptex_t *)(g_dtexdata + g_texdatasize);
                int j;
                for (j = 0; j < header.numlumps; ++j)
                    if (!strcasecmp(name, info[j].name))
                        break;
                if (j == header.numlumps)
                {
                    int w, h;
                    if (skipspace(texfile), std::fscanf(texfile, "%d", &w) != 1)
                        Error("File read failure");
                    if (skipspace(texfile), std::fscanf(texfile, "%d", &h) != 1)
                        Error("File read failure");
                    g_texdatasize += sizeof(miptex_t);
                    hlassume(g_texdatasize < g_max_map_miptex, assume_MAX_MAP_MIPTEX);
                    std::memset(tex, 0, sizeof(miptex_t));
                    std::strcpy(tex->name, name);
                    tex->width = w;
                    tex->height = h;
                    for (int k = 0; k < MIPLEVELS; k++)
                        tex->offsets[k] = 0;
                }
                else
                {
                    std::fseek(wadfile, info[j].filepos, SEEK_SET);
                    g_texdatasize += info[j].disksize;
                    hlassume(g_texdatasize < g_max_map_miptex, assume_MAX_MAP_MIPTEX);
                    SafeRead(wadfile, tex, info[j].disksize);
                }
            }
            skipline(texfile);
        }

        std::fclose(texfile);
        std::free(info);
    }
    std::fclose(wadfile);
}

static void WriteEntities(const char *const name)
{
    char *bak_dentdata;
    int bak_entdatasize;
    char filename[_MAX_PATH];

    safe_snprintf(filename, _MAX_PATH, "%s.ent", name);
    _unlink(filename);

    {
        if (g_parse) // Added by Nem.
        {
            bak_entdatasize = g_entdatasize;
            bak_dentdata = (char *)std::malloc(g_entdatasize);
            hlassume(bak_dentdata != nullptr, assume_NoMemory);
            std::memcpy(bak_dentdata, g_dentdata, g_entdatasize);
            ParseEntityData("  ", 2, "\r\n", 2, "", 0);
        }

        std::FILE *f = SafeOpenWrite(filename);
        Log("\nWriting %s.\n", filename); // Added by Nem.
        SafeWrite(f, g_dentdata, g_entdatasize);
        std::fclose(f);
        if (g_parse)
        {
            g_entdatasize = bak_entdatasize;
            std::memcpy(g_dentdata, bak_dentdata, bak_entdatasize);
            std::free(bak_dentdata);
        }
    }
}

static void ReadEntities(const char *const name)
{
    char filename[_MAX_PATH];

    safe_snprintf(filename, _MAX_PATH, "%s.ent", name);

    {
        std::FILE *f = SafeOpenRead(filename);
        Log("\nReading %s.\n", filename); // Added by Nem.

        g_entdatasize = q_filelength(f);

        assume(g_entdatasize != 0, "No entity data.");
        assume(g_entdatasize < sizeof(g_dentdata), "Entity data size exceedes dentdata limit.");

        SafeRead(f, g_dentdata, g_entdatasize);

        std::fclose(f);

        if (g_dentdata[g_entdatasize - 1] != 0)
        {
            //            Log("g_dentdata[g_entdatasize-1] = %d\n", g_dentdata[g_entdatasize-1]);

            if (g_parse) // Added by Nem.
            {
                ParseEntityData("", 0, "\n", 1, "\0", 1);
            }
            else
            {
                if (g_dentdata[g_entdatasize - 1] != '\0')
                {
                    g_dentdata[g_entdatasize] = '\0';
                    g_entdatasize++;
                }
            }
        }
    }
}

//======================================================================

static void Usage(void)
{
    //Log("%s " ZHLT_VERSIONSTRING "\n" MODIFICATIONS_STRING "\n", g_Program);
    //Log("  Usage: ripent [-import|-export] [-texdata n] bspname\n");

    // Modified to behave like other tools by Nem.

    Banner();
    Log("\n-= %s Options =-\n\n", g_Program);
    Log("    -export         : Export entity data\n");
    Log("    -import         : Import entity data\n\n");

    Log("    -parse          : Parse and format entity data\n\n");
    Log("    -textureexport  : Export texture data\n");
    Log("    -textureimport  : Import texture data\n");
    Log("    -textureparse   : Parse and format texture data\n\n");
    Log("    -writeextentfile : Create extent file for the map\n");
    Log("    -deleteembeddedlightmaps : Delete textures created by hlrad\n");

    Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n");
    Log("    -lightdata #    : Alter maximum lighting memory limit (in kb)\n");
    Log("    -chart          : Display bsp statitics\n");
    Log("    -noinfo         : Do not show tool configuration information\n\n");
    Log("    -pause          : Pause before exit\n\n");

    Log("    mapfile         : The mapfile to process\n\n");

    std::exit(1);
}

void waitForKey()
{
    if (g_pause)
    {
        Log("\nPress any key to continue\n");
        std::getchar();
    }
}

// =====================================================================================
//  Settings
// =====================================================================================
static void Settings()
{
    char *tmp;

    if (!g_info)
    {
        return;
    }

    Log("\n-= Current %s Settings =-\n", g_Program);
    Log("Name               |  Setting  |  Default\n"
        "-------------------|-----------|-------------------------\n");

    // ZHLT Common Settings
    Log("chart               [ %7s ] [ %7s ]\n", g_chart ? "on" : "off", DEFAULT_CHART ? "on" : "off");
    Log("max texture memory  [ %7d ] [ %7d ]\n", g_max_map_miptex, DEFAULT_MAX_MAP_MIPTEX);
    Log("max lighting memory [ %7d ] [ %7d ]\n", g_max_map_lightdata, DEFAULT_MAX_MAP_LIGHTDATA);

    switch (g_mode)
    {
    case hl_import:
        tmp = "Import";
        break;
    case hl_export:
        tmp = "Export";
        break;
    case hl_undefined:
    default:
        tmp = "N/A";
        break;
    }

    Log("\n");

    // RipEnt Specific Settings
    Log("mode                [ %7s ] [ %7s ]\n", tmp, "N/A");
    Log("parse               [ %7s ] [ %7s ]\n", g_parse ? "on" : "off", DEFAULT_PARSE ? "on" : "off");
    switch (g_texturemode)
    {
    case hl_import:
        tmp = "Import";
        break;
    case hl_export:
        tmp = "Export";
        break;
    case hl_undefined:
    default:
        tmp = "N/A";
        break;
    }
    Log("texture mode        [ %7s ] [ %7s ]\n", tmp, "N/A");
    Log("texture parse       [ %7s ] [ %7s ]\n", g_textureparse ? "on" : "off", DEFAULT_TEXTUREPARSE ? "on" : "off");
    Log("write extent file   [ %7s ] [ %7s ]\n", g_writeextentfile ? "on" : "off", DEFAULT_WRITEEXTENTFILE ? "on" : "off");
    Log("delete rad textures [ %7s ] [ %7s ]\n", g_deleteembeddedlightmaps ? "on" : "off", DEFAULT_DELETEEMBEDDEDLIGHTMAPS ? "on" : "off");

    Log("\n\n");
}

/*
 * ============
 * main
 * ============
 */
int main(int argc, char **argv)
{
    int i;
    double start, end;

    g_Program = "ripent";

    std::atexit(&waitForKey);
    int argcold = argc;
    char **argvold = argv;
    {
        int argc;
        char **argv;
        ParseParamFile(argcold, argvold, argc, argv);
        {
            if (argc == 1)
            {
                Usage();
            }

            for (i = 1; i < argc; i++)
            {
                if (!strcasecmp(argv[i], "-import"))
                {
                    g_mode = hl_import;
                }
                else if (!strcasecmp(argv[i], "-export"))
                {
                    g_mode = hl_export;
                }
                // g_parse: command line switch (-parse).
                // Added by: Ryan Gregg aka Nem
                else if (!strcasecmp(argv[i], "-parse"))
                {
                    g_parse = true;
                }
                else if (!strcasecmp(argv[i], "-texdata"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        int x = std::atoi(argv[++i]) * 1024;

                        //if (x > g_max_map_miptex) //--vluzacn
                        {
                            g_max_map_miptex = x;
                        }
                    }
                    else
                    {
                        Usage();
                    }
                }
                else if (!strcasecmp(argv[i], "-lightdata"))
                {
                    if (i + 1 < argc) //added "1" .--vluzacn
                    {
                        int x = std::atoi(argv[++i]) * 1024;

                        //if (x > g_max_map_lightdata) //--vluzacn
                        {
                            g_max_map_lightdata = x;
                        }
                    }
                    else
                    {
                        Usage();
                    }
                }
                else if (!strcasecmp(argv[i], "-chart"))
                {
                    g_chart = true;
                }
                else if (!strcasecmp(argv[i], "-noinfo"))
                {
                    g_info = false;
                }
                else if (!strcasecmp(argv[i], "-pause"))
                {
                    g_pause = true;
                }
                else if (!strcasecmp(argv[i], "-textureimport"))
                {
                    g_texturemode = hl_import;
                }
                else if (!strcasecmp(argv[i], "-textureexport"))
                {
                    g_texturemode = hl_export;
                }
                else if (!strcasecmp(argv[i], "-textureparse"))
                {
                    g_textureparse = true;
                }
                else if (!strcasecmp(argv[i], "-writeextentfile"))
                {
                    g_writeextentfile = true;
                }
                else if (!strcasecmp(argv[i], "-deleteembeddedlightmaps"))
                {
                    g_deleteembeddedlightmaps = true;
                }
                else if (argv[i][0] == '-') //--vluzacn
                {
                    Log("Unknown option: '%s'\n", argv[i]);
                    Usage();
                }
                else
                {
                    safe_strncpy(g_Mapname, argv[i], _MAX_PATH);
                    FlipSlashes(g_Mapname);
                    StripExtension(g_Mapname);
                }
            }

            char source[_MAX_PATH];
            safe_snprintf(source, _MAX_PATH, "%s.bsp", g_Mapname);
            if (!q_exists(source))
            {
                Log("bspfile '%s' does not exist\n", source); //--vluzacn
                Usage();
            }

            LogStart(argcold, argvold);
            {
                int i;
                Log("Arguments: ");
                for (i = 1; i < argc; i++)
                {
                    if (strchr(argv[i], ' '))
                    {
                        Log("\"%s\" ", argv[i]);
                    }
                    else
                    {
                        Log("%s ", argv[i]);
                    }
                }
                Log("\n");
            }
            std::atexit(LogEnd);

            Settings();

            dtexdata_init();
            std::atexit(dtexdata_free);

            // BEGIN RipEnt
            start = I_FloatTime();

            ReadBSP(g_Mapname);
            bool updatebsp = false;
            if (g_deleteembeddedlightmaps)
            {
                DeleteEmbeddedLightmaps();
                updatebsp = true;
            }
            switch (g_mode)
            {
            case hl_import:
                ReadEntities(g_Mapname);
                updatebsp = true;
                break;
            case hl_export:
                WriteEntities(g_Mapname);
                break;
            case hl_undefined:
                break;
            }
            switch (g_texturemode)
            {
            case hl_import:
                ReadTextures(g_Mapname);
                updatebsp = true;
                break;
            case hl_export:
                WriteTextures(g_Mapname);
                break;
            case hl_undefined:
                break;
            }
            if (g_chart)
            {
                if (!CalcFaceExtents_test())
                {
                    Warning("internal error: CalcFaceExtents_test failed.");
                }
                PrintBSPFileSizes();
            }
            if (updatebsp)
            {
                WriteBSP(g_Mapname);
            }

            end = I_FloatTime();
            LogTimeElapsed(end - start);
            // END RipEnt
        }
    }

    return 0;
}