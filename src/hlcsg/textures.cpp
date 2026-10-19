#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <deque>
#include <array>
#include <bit>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "hlcsg/hlcsg.h"
#include "hlcsg/wadpath.h"
#include "common/cmdlib.h"
#include "common/filelib.h"
#include "common/threads.h"
#include "common/log.h"
#include "common/bspfile.h"

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
    char name[MAX_TEXTURE_NAME_LENGTH]; // must be null terminated // upper case

    int iTexFile; // index of the wad this texture is located in

} lumpinfo_t;

// The first 32 bytes are the WAD3 directory entry, read straight from the file.
static_assert(sizeof(wadinfo_t) == 12);
static_assert(offsetof(lumpinfo_t, name) == 16 && offsetof(lumpinfo_t, iTexFile) == 32);
constexpr int WAD_LUMP_ENTRY_SIZE = 32;

std::deque<std::string> g_WadInclude;

static constexpr int MAX_TEXFILES = 128;

static int nummiptex = 0;
static lumpinfo_t miptex[MAX_MAP_TEXTURES];
static int nTexLumps = 0;
static std::vector<lumpinfo_t> lumpinfo; // nTexLumps entries
static int nTexFiles = 0;
static UniqueFile texfiles[MAX_TEXFILES];
static wadpath_t *texwadpathes[MAX_TEXFILES]; // maps index of the wad to its path (in g_WadPaths)
// The old buggy code in effect limit the number of brush sides to MAX_MAP_BRUSHES
// Texture name of each texinfo before WriteMiptex. Stored while brushes are created and only read
// afterwards (a deque never moves its elements, so the returned names stay valid).
static std::deque<std::string> texmap;
static int numtexmap = 0;

// Exact-result indexes for the two lookups that scanned every entry (most of hlcsg's time on
// large maps). Each returns the entry the scan returned: names are unique in miptex[], and a
// texinfo key holds everything the scan compared.
static std::unordered_map<std::string, int> s_miptex_index; // name -> index in miptex[]

struct TexinfoKey
{
    std::string name;
    int flags;
    std::array<std::uint32_t, 8> vecs; // float bits, -0 stored as +0 (the scan compared with ==)
    bool operator==(const TexinfoKey &) const = default;
};
struct TexinfoKeyHash
{
    std::size_t operator()(const TexinfoKey &k) const
    {
        std::size_t h = std::hash<std::string>{}(k.name) ^ (static_cast<std::size_t>(k.flags) * 0x9E3779B97F4A7C15ull);
        for (std::uint32_t v : k.vecs)
        {
            h = h * 1000003u ^ v;
        }
        return h;
    }
};
static std::unordered_map<TexinfoKey, int, TexinfoKeyHash> s_texinfo_index; // -> first texinfo

// nullopt if a component is NaN: NaN compared unequal to everything, so such a texinfo never matched.
static std::optional<TexinfoKey> MakeTexinfoKey(const char *name, const texinfo_t &tx)
{
    TexinfoKey key{name, tx.flags, {}};
    for (int j = 0; j < 2; j++)
    {
        for (int k = 0; k < 4; k++)
        {
            const float v = tx.vecs[j][k];
            if (std::isnan(v))
            {
                return std::nullopt;
            }
            key.vecs[j * 4 + k] = std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v);
        }
    }
    return key;
}

static int texmap_store(const char *texname, bool shouldlock = true)
// This function should never be called unless a new entry in g_texinfo is being allocated.
{
    if (shouldlock)
    {
        ThreadLock();
    }

    hlassume(numtexmap < MAX_INTERNAL_MAP_TEXINFO, assume_MAX_MAP_TEXINFO); // This error should never appear.

    int i = numtexmap;
    texmap.emplace_back(texname);
    numtexmap++;
    if (shouldlock)
    {
        ThreadUnlock();
    }
    return i;
}

static const char *texmap_retrieve(int index)
{
    hlassume(0 <= index && index < numtexmap, assume_first);
    return texmap[index].c_str();
}

static void texmap_clear()
{
    ThreadLock();
    texmap.clear();
    numtexmap = 0;
    s_texinfo_index.clear(); // the index holds names, which are only valid while texmap is
    ThreadUnlock();
}

static void CleanupName(const char *const in, char *out)
{
    int i;

    for (i = 0; i < MAX_TEXTURE_NAME_LENGTH; i++)
    {
        if (!in[i])
        {
            break;
        }

        out[i] = toupper(static_cast<unsigned char>(in[i])); // was toupper(char): undefined for bytes >= 0x80
    }

    for (; i < MAX_TEXTURE_NAME_LENGTH; i++)
    {
        out[i] = 0;
    }
}

static int lump_sorter_by_wad_and_name(const void *lump1, const void *lump2)
{
    auto *plump1 = (lumpinfo_t *)lump1;
    auto *plump2 = (lumpinfo_t *)lump2;

    if (plump1->iTexFile == plump2->iTexFile)
    {
        return std::strcmp(plump1->name, plump2->name);
    }
    else
    {
        return plump1->iTexFile - plump2->iTexFile;
    }
}

static int lump_sorter_by_name(const void *lump1, const void *lump2)
{
    auto *plump1 = (lumpinfo_t *)lump1;
    auto *plump2 = (lumpinfo_t *)lump2;

    return std::strcmp(plump1->name, plump2->name);
}

// Find and allocate a texture into the lump data
static int FindMiptex(const char *const name)
{
    int i;
    if (std::strlen(name) >= MAX_TEXTURE_NAME_LENGTH)
    {
        Error("Texture name is too long (%s)\n", name);
    }

    ThreadLock();
    const auto found = s_miptex_index.find(name);
    i = found != s_miptex_index.end() ? found->second : nummiptex;
#ifdef VHLT_VERIFY_INDEXES
    {
        int scanned;
        for (scanned = 0; scanned < nummiptex && std::strcmp(name, miptex[scanned].name); scanned++)
        {
        }
        if (scanned != i)
            Error("miptex index mismatch for %s: index %d, linear scan %d", name, i, scanned);
    }
#endif
    if (i < nummiptex)
    {
        ThreadUnlock();
        return i;
    }

    hlassume(nummiptex < MAX_MAP_TEXTURES, assume_MAX_MAP_TEXTURES);
    safe_strncpy(miptex[i].name, name, MAX_TEXTURE_NAME_LENGTH);
    s_miptex_index.emplace(miptex[i].name, i);
    nummiptex++;
    ThreadUnlock();
    return i;
}

// After miptex[] is reordered (sorted, or entries replaced by their wad lumps).
static void RebuildMiptexIndex()
{
    s_miptex_index.clear();
    for (int i = 0; i < nummiptex; i++)
    {
        s_miptex_index.emplace(miptex[i].name, i); // keeps the first if a name repeats, like the scan
    }
}

static bool TEX_InitFromWad()
{
    wadinfo_t wadinfo;

    Log("\n");
    // update wad inclusion
    for (wadpath_t &wad : g_WadPaths)
    {
        wadpath_t *currentwad = &wad;
        if (!g_wadtextures) // '-nowadtextures'
        {
            currentwad->usedbymap = false; // include this wad
        }
        for (auto it = g_WadInclude.begin(); it != g_WadInclude.end(); it++) // '-wadinclude xxx.wad'
        {
            if (stristr(currentwad->path.c_str(), it->c_str()))
            {
                currentwad->usedbymap = false; // include this wad
            }
        }
    }

    const char *pszWadroot = getenv("WADROOT");

    // for eachwadpath
    for (wadpath_t &wad : g_WadPaths)
    {
        wadpath_t *currentwad = &wad;
        const char *pszWadFile = currentwad->path.c_str();

        texwadpathes[nTexFiles] = currentwad;
        texfiles[nTexFiles].reset(std::fopen(pszWadFile, "rb"));

        if (!texfiles[nTexFiles])
        {
            // cant find it, maybe this wad file has a hard code drive
            if (pszWadFile[1] == ':')
            {
                pszWadFile += 2; // skip past the drive
                texfiles[nTexFiles].reset(std::fopen(pszWadFile, "rb"));
            }
        }

        if (!texfiles[nTexFiles] && pszWadroot)
        {
            char szTmp[_MAX_PATH];
            char szFile[_MAX_PATH];
            char szSubdir[_MAX_PATH];

            ExtractFile(pszWadFile, szFile);

            ExtractFilePath(pszWadFile, szTmp);
            ExtractFile(szTmp, szSubdir);

            // szSubdir will have a trailing separator
            safe_snprintf(szTmp, _MAX_PATH, "%s" SYSTEM_SLASH_STR "%s%s", pszWadroot, szSubdir, szFile);
            texfiles[nTexFiles].reset(std::fopen(szTmp, "rb"));
        }

        if (!texfiles[nTexFiles] && pszWadFile[0] == '\\')
        {
            char tmp[_MAX_PATH];
            for (int l = 'C'; l <= 'Z'; ++l)
            {
                safe_snprintf(tmp, _MAX_PATH, "%c:%s", l, pszWadFile);
                texfiles[nTexFiles].reset(std::fopen(tmp, "rb"));
                if (texfiles[nTexFiles])
                {
                    break;
                }
            }
        }

        if (!texfiles[nTexFiles])
        {
            pszWadFile = currentwad->path.c_str(); // correct it back
            // still cant find it, error out
            Fatal(assume_COULD_NOT_FIND_WAD, "Could not open wad file %s", pszWadFile);
            continue;
        }

        pszWadFile = currentwad->path.c_str(); // correct it back

        // temp assignment to make things cleaner:
        std::FILE *texfile = texfiles[nTexFiles].get();

        // read in this wadfiles information
        const long long wadsize = q_filelength(texfile);
        if (wadsize < static_cast<long long>(sizeof(wadinfo)))
        {
            Error("%s is too small to be a wad file (%lld bytes)", pszWadFile, wadsize);
        }
        SafeRead(texfile, &wadinfo, sizeof(wadinfo));

        // make sure its a valid format
        if (std::strncmp(wadinfo.identification, "WAD3", 4))
        {
            Log(" - ");
            Error("%s isn't a Wadfile!", pszWadFile);
        }
        if (wadinfo.numlumps < 0 || wadinfo.infotableofs < 0 ||
            wadinfo.infotableofs + static_cast<long long>(wadinfo.numlumps) * WAD_LUMP_ENTRY_SIZE > wadsize)
        {
            Error("%s: bad lump directory (%d lumps at offset %d, file is %lld bytes)",
                  pszWadFile, wadinfo.numlumps, wadinfo.infotableofs, wadsize);
        }

        // read in lump
        if (std::fseek(texfile, wadinfo.infotableofs, SEEK_SET))
            Warning("fseek to %d in wadfile %s failed\n", wadinfo.infotableofs, pszWadFile);

        // memalloc for this lump
        lumpinfo.resize(nTexLumps + wadinfo.numlumps);

        // for each texlump
        for (int j = 0; j < wadinfo.numlumps; j++, nTexLumps++)
        {
            SafeRead(texfile, &lumpinfo[nTexLumps], WAD_LUMP_ENTRY_SIZE); // iTexFile is NOT read from file
            if (lumpinfo[nTexLumps].filepos < 0 || lumpinfo[nTexLumps].disksize < 0 ||
                lumpinfo[nTexLumps].filepos + static_cast<long long>(lumpinfo[nTexLumps].disksize) > wadsize)
            {
                Error("%s: lump %d lies outside the file (offset %d, size %d, file is %lld bytes)",
                      pszWadFile, j, lumpinfo[nTexLumps].filepos, lumpinfo[nTexLumps].disksize, wadsize);
            }

            if (!TerminatedString(lumpinfo[nTexLumps].name, MAX_TEXTURE_NAME_LENGTH))
            {
                lumpinfo[nTexLumps].name[MAX_TEXTURE_NAME_LENGTH - 1] = 0;
                Log(" - ");
                Warning("Unterminated texture name : wad[%s] texture[%d] name[%s]\n", pszWadFile, nTexLumps, lumpinfo[nTexLumps].name);
            }

            CleanupName(lumpinfo[nTexLumps].name, lumpinfo[nTexLumps].name);

            lumpinfo[nTexLumps].filepos = lumpinfo[nTexLumps].filepos;
            lumpinfo[nTexLumps].disksize = lumpinfo[nTexLumps].disksize;
            lumpinfo[nTexLumps].iTexFile = nTexFiles;

            if (lumpinfo[nTexLumps].disksize > MAX_TEXTURE_SIZE)
            {
                Log(" - ");
                Warning("Larger than expected texture (%d bytes): '%s'",
                        lumpinfo[nTexLumps].disksize, lumpinfo[nTexLumps].name);
            }
        }

        // AJM: this feature is dependant on autowad. :(
        // CONSIDER: making it standard?
        currentwad->totaltextures = wadinfo.numlumps;

        nTexFiles++;
        hlassume(nTexFiles < MAX_TEXFILES, assume_MAX_TEXFILES);
    }

    // sort texlumps in memory by name
    qsort(lumpinfo.data(), (std::size_t)nTexLumps, sizeof(lumpinfo[0]), lump_sorter_by_name);

    CheckFatal();
    return true;
}

static lumpinfo_t *FindTexture(const lumpinfo_t *const source)
{
    lumpinfo_t *found = nullptr;

    found = (lumpinfo_t *)bsearch(source, lumpinfo.data(), (std::size_t)nTexLumps, sizeof(lumpinfo[0]), lump_sorter_by_name);
    if (!found)
    {
        Warning("::FindTexture() texture %s not found!", source->name);
        if (!std::strcmp(source->name, "NULL") || !std::strcmp(source->name, "SKIP"))
        {
            Log("Are you sure you included zhlt.wad in your wadpath list?\n");
        }
    }

    if (found)
    {
        // get the first and last matching lump (by index: first - 1 could point before the array)
        std::ptrdiff_t firstindex = found - lumpinfo.data();
        std::ptrdiff_t lastindex = firstindex;
        while (firstindex > 0 && lump_sorter_by_name(&lumpinfo[firstindex - 1], source) == 0)
        {
            firstindex--;
        }
        while (lastindex + 1 < nTexLumps && lump_sorter_by_name(&lumpinfo[lastindex + 1], source) == 0)
        {
            lastindex++;
        }
        lumpinfo_t *first = &lumpinfo[firstindex];
        lumpinfo_t *last = &lumpinfo[lastindex];
        // find the best matching lump
        lumpinfo_t *best = nullptr;
        for (found = first; found < last + 1; found++)
        {
            bool better = false;
            if (best == nullptr)
            {
                better = true;
            }
            else if (found->iTexFile != best->iTexFile)
            {
                wadpath_t *found_wadpath = texwadpathes[found->iTexFile];
                wadpath_t *best_wadpath = texwadpathes[best->iTexFile];
                if (found_wadpath->usedbymap != best_wadpath->usedbymap)
                {
                    better = !found_wadpath->usedbymap; // included wad is better
                }
                else
                {
                    better = found->iTexFile < best->iTexFile; // upper in the wad list is better
                }
            }
            else if (found->filepos != best->filepos)
            {
                better = found->filepos < best->filepos; // when there are several lumps with the same name in one wad file
            }

            if (better)
            {
                best = found;
            }
        }
        found = best;
    }
    return found;
}

// writewad_data: the whole lump, when the texture stays in its wad (only its header goes into the
// bsp); nullopt otherwise.
static int LoadLump(const lumpinfo_t *const source, byte *dest, int *texsize, int dest_maxsize, std::optional<std::vector<byte>> &writewad_data)
{
    writewad_data.reset();

    *texsize = 0;
    if (source->filepos)
    {
        std::FILE *const texfile = texfiles[source->iTexFile].get();
        if (std::fseek(texfile, source->filepos, SEEK_SET))
        {
            Warning("fseek to %d failed\n", source->filepos);
            Error("File read failure");
        }
        *texsize = source->disksize;

        if (texwadpathes[source->iTexFile]->usedbymap)
        {
            // Just read the miptex header and zero out the data offsets.
            // We will load the entire texture from the WAD at engine runtime
            auto *miptex = (miptex_t *)dest;
            hlassume((int)sizeof(miptex_t) <= dest_maxsize, assume_MAX_MAP_MIPTEX);
            SafeRead(texfile, dest, sizeof(miptex_t));

            for (int i = 0; i < MIPLEVELS; i++)
                miptex->offsets[i] = 0;
            if (std::fseek(texfile, source->filepos, SEEK_SET))
                Error("File read failure");
            writewad_data.emplace(source->disksize); // compat: also for disksize 0 (an empty lump)
            SafeRead(texfile, writewad_data->data(), source->disksize);
            return sizeof(miptex_t);
        }
        else
        {
            // Load the entire texture here so the BSP contains the texture
            hlassume(source->disksize <= dest_maxsize, assume_MAX_MAP_MIPTEX);
            SafeRead(texfile, dest, source->disksize);
            return source->disksize;
        }
    }

    Error("::LoadLump() texture %s not found!", source->name);
    return 0;
}

static void AddAnimatingTextures()
{
    char name[MAX_TEXTURE_NAME_LENGTH];

    int base = nummiptex;

    for (int i = 0; i < base; i++)
    {
        if ((miptex[i].name[0] != '+') && (miptex[i].name[0] != '-'))
        {
            continue;
        }

        safe_strncpy(name, miptex[i].name, MAX_TEXTURE_NAME_LENGTH);

        for (int j = 0; j < 20; j++)
        {
            if (j < 10)
            {
                name[1] = '0' + j;
            }
            else
            {
                name[1] = 'A' + j - 10; // alternate animation
            }

            // see if this name exists in the wadfile
            for (int k = 0; k < nTexLumps; k++)
            {
                if (!std::strcmp(name, lumpinfo[k].name))
                {
                    FindMiptex(name); // add to the miptex list
                    break;
                }
            }
        }
    }

    if (nummiptex - base)
    {
        Log("added %i additional animating textures.\n", nummiptex - base);
    }
}

void WriteMiptex()
{
    int texsize;
    int totaltexsize = 0;

    g_texdatasize = 0;

    {
        if (!TEX_InitFromWad())
            return;

        AddAnimatingTextures();
    }

    for (int i = 0; i < nummiptex; i++)
    {
        lumpinfo_t *found;

        found = FindTexture(miptex + i);
        if (found)
        {
            miptex[i] = *found;
            texwadpathes[found->iTexFile]->usedtextures++;
        }
        else
        {
            miptex[i].iTexFile = miptex[i].filepos = miptex[i].disksize = 0;
        }
    }
    RebuildMiptexIndex(); // entries were replaced by their wad lumps

    // Now we have filled lumpinfo for each miptex and the number of used textures for each wad.
    {
        char szTmpWad[MAX_VAL];

        szTmpWad[0] = 0;
        for (int i = 0; i < nTexFiles; i++)
        {
            wadpath_t *currentwad = texwadpathes[i];
            if (!currentwad->usedbymap && (currentwad->usedtextures > 0 || !g_bWadAutoDetect))
            {
                Log("Including Wadfile: %s\n", currentwad->path.c_str());
                double percused = (double)currentwad->usedtextures / (double)nummiptex * 100;
                Log(" - Contains %i used texture%s, %2.2f percent of map (%d textures in wad)\n",
                    currentwad->usedtextures, currentwad->usedtextures == 1 ? "" : "s", percused, currentwad->totaltextures);
            }
        }
        for (int i = 0; i < nTexFiles; i++)
        {
            wadpath_t *currentwad = texwadpathes[i];
            if (currentwad->usedbymap && (currentwad->usedtextures > 0 || !g_bWadAutoDetect))
            {
                Log("Using Wadfile: %s\n", currentwad->path.c_str());
                double percused = (double)currentwad->usedtextures / (double)nummiptex * 100;
                Log(" - Contains %i used texture%s, %2.2f percent of map (%d textures in wad)\n",
                    currentwad->usedtextures, currentwad->usedtextures == 1 ? "" : "s", percused, currentwad->totaltextures);
                char tmp[_MAX_PATH];
                ExtractFile(currentwad->path.c_str(), tmp);
                safe_strncat(szTmpWad, tmp, MAX_VAL);
                safe_strncat(szTmpWad, ";", MAX_VAL);
            }
        }

        Log("\n");
        if (*szTmpWad)
        {
            Log("Wad files required to run the map: \"%s\"\n", szTmpWad);
        }
        else
        {
            Log("Wad files required to run the map: (None)\n");
        }
        SetKeyValue(&g_entities[0], "wad", szTmpWad);
    }

    {
        texinfo_t *tx = g_texinfo;

        // Sort them FIRST by wadfile and THEN by name for most efficient loading in the engine.
        qsort((void *)miptex, (std::size_t)nummiptex, sizeof(miptex[0]), lump_sorter_by_wad_and_name);
        RebuildMiptexIndex();

        // Sleazy Hack 104 Pt 2 - After sorting the miptex array, reset the texinfos to point to the right miptexs
        for (int i = 0; i < g_numtexinfo; i++, tx++)
        {
            const char *miptex_name = texmap_retrieve(tx->miptex);

            tx->miptex = FindMiptex(miptex_name);
        }
        texmap_clear();
    }
    {
        // Now setup to get the miptex data (or just the headers if using -wadtextures) from the wadfile
        MiptexLumpView lump = TextureLump();
        const long long headersize = MiptexLumpView::DataofsPosition(nummiptex);
        hlassume(headersize <= lump.capacity(), assume_MAX_MAP_MIPTEX);
        byte *data = g_dtexdata + headersize;
        lump.set_count(nummiptex);
        byte *const texdata_end = g_dtexdata + lump.capacity(); // was g_max_map_miptex, which could exceed the buffer
        char writewad_name[_MAX_PATH];
        typedef struct
        {
            int filepos;
            int disksize;
            int size;
            char type;
            char compression;
            char pad1, pad2;
            char name[MAX_TEXTURE_NAME_LENGTH];
        } dlumpinfo_t;

        wadinfo_t writewad_header;
        safe_snprintf(writewad_name, _MAX_PATH, "%s.wa_", g_Mapname);
        UniqueFile writewad_file(SafeOpenWrite(writewad_name));
        std::vector<dlumpinfo_t> writewad_lumpinfos(nummiptex);
        writewad_header.identification[0] = 'W';
        writewad_header.identification[1] = 'A';
        writewad_header.identification[2] = 'D';
        writewad_header.identification[3] = '3';
        writewad_header.numlumps = 0;
        if (std::fseek(writewad_file.get(), sizeof(wadinfo_t), SEEK_SET))
            Error("File write failure");
        std::optional<std::vector<byte>> writewad_data;
        for (int i = 0; i < nummiptex; i++)
        {
            lump.set_dataofs(i, static_cast<int>(data - g_dtexdata));
            int len = LoadLump(miptex + i, data, &texsize, static_cast<int>(texdata_end - data), writewad_data);
            if (writewad_data)
            {
                dlumpinfo_t *writewad_lumpinfo = &writewad_lumpinfos[writewad_header.numlumps];
                writewad_lumpinfo->filepos = std::ftell(writewad_file.get());
                writewad_lumpinfo->disksize = static_cast<int>(writewad_data->size());
                writewad_lumpinfo->size = miptex[i].size;
                writewad_lumpinfo->type = miptex[i].type;
                writewad_lumpinfo->compression = miptex[i].compression;
                writewad_lumpinfo->pad1 = miptex[i].pad1;
                writewad_lumpinfo->pad2 = miptex[i].pad2;
                std::memcpy(writewad_lumpinfo->name, miptex[i].name, MAX_TEXTURE_NAME_LENGTH);
                writewad_header.numlumps++;
                SafeWrite(writewad_file.get(), writewad_data->data(), static_cast<int>(writewad_data->size()));
            }

            if (!len)
            {
                lump.set_dataofs(i, -1); // didn't find the texture
            }
            else
            {
                totaltexsize += texsize;

                hlassume(totaltexsize < g_max_map_miptex, assume_MAX_MAP_MIPTEX);
            }
            data += len;
        }
        g_texdatasize = data - g_dtexdata;
        writewad_header.infotableofs = std::ftell(writewad_file.get());
        SafeWrite(writewad_file.get(), writewad_lumpinfos.data(), writewad_header.numlumps * sizeof(dlumpinfo_t));
        if (std::fseek(writewad_file.get(), 0, SEEK_SET))
            Error("File write failure");
        SafeWrite(writewad_file.get(), &writewad_header, sizeof(wadinfo_t));
        if (std::fclose(writewad_file.release()))
            Error("File write failure");
    }
    Log("Texture usage is at %1.2f mb (of %1.2f mb MAX)\n", (float)totaltexsize / (1024 * 1024),
        (float)g_max_map_miptex / (1024 * 1024));
}

int TexinfoForBrushTexture(brush_texture_t *bt, const vec3_t origin)
{
    texinfo_t tx;
    int i;

    if (!strncasecmp(bt->name, "NULL", 4))
    {
        return -1;
    }
    std::memset(&tx, 0, sizeof(tx));
    FindMiptex(bt->name);

    // set the special flag
    if (bt->name[0] == '*' || !strncasecmp(bt->name, "sky", 3) ||
        !strncasecmp(bt->name, "env_sky", 5) ||
        !strncasecmp(bt->name, "origin", 6) ||
        !strncasecmp(bt->name, "null", 4) ||
        !strncasecmp(bt->name, "aaatrigger", 10))
    {
        // actually only 'sky' and 'aaatrigger' needs this. --vluzacn
        tx.flags |= TEX_SPECIAL;
    }

    if (!bt->vects.scale[0])
    {
        bt->vects.scale[0] = 1;
    }
    if (!bt->vects.scale[1])
    {
        bt->vects.scale[1] = 1;
    }

    vec_t scale = 1 / bt->vects.scale[0];
    VectorScale(bt->vects.UAxis, scale, tx.vecs[0]);

    scale = 1 / bt->vects.scale[1];
    VectorScale(bt->vects.VAxis, scale, tx.vecs[1]);

    tx.vecs[0][3] = bt->vects.shift[0] + DotProduct(origin, tx.vecs[0]);
    tx.vecs[1][3] = bt->vects.shift[1] + DotProduct(origin, tx.vecs[1]);
    // find the g_texinfo
    ThreadLock();
    const std::optional<TexinfoKey> key = MakeTexinfoKey(bt->name, tx);
    i = g_numtexinfo;
    if (key)
    {
        const auto found = s_texinfo_index.find(*key);
        if (found != s_texinfo_index.end())
        {
            i = found->second;
        }
    }
#ifdef VHLT_VERIFY_INDEXES
    {
        int scanned;
        for (scanned = 0; scanned < g_numtexinfo; scanned++)
        {
            const texinfo_t *tc = &g_texinfo[scanned];
            bool same = !std::strcmp(texmap_retrieve(tc->miptex), bt->name) && tc->flags == tx.flags;
            for (int j = 0; j < 2 && same; j++)
                for (int k = 0; k < 4 && same; k++)
                    same = tc->vecs[j][k] == tx.vecs[j][k];
            if (same)
                break;
        }
        if (scanned != i)
            Error("texinfo index mismatch for %s: index %d, linear scan %d", bt->name, i, scanned);
    }
#endif
    if (i < g_numtexinfo)
    {
        ThreadUnlock();
        return i;
    }

    hlassume(g_numtexinfo < MAX_INTERNAL_MAP_TEXINFO, assume_MAX_MAP_TEXINFO);

    texinfo_t *tc = &g_texinfo[i];
    *tc = tx;
    tc->miptex = texmap_store(bt->name, false);
    if (key)
    {
        s_texinfo_index.emplace(*key, i);
    }
    g_numtexinfo++;
    ThreadUnlock();
    return i;
}

// Before WriteMiptex(), for each texinfo in g_texinfo, .miptex is a string rather than texture index, so this function should be used instead of GetTextureByNumber.
const char *GetTextureByNumber_CSG(int texturenumber)
{
    if (texturenumber == -1)
        return "";
    return texmap_retrieve(g_texinfo[texturenumber].miptex);
}