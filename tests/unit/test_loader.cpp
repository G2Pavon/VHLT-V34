// LoadBSPFile rejection paths. Error() exits the process, so each case runs bsp_probe on a
// hand-built file and checks its exit status and message.

#include "doctest/doctest.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "common/bspfile.h"

namespace fs = std::filesystem;

namespace
{
struct ProbeResult
{
    int exit_code; // -1 if killed by a signal
    std::string output;
};

ProbeResult Probe(const fs::path &file)
{
    const std::string cmd = std::string("'") + VHLT_BSP_PROBE + "' load '" + file.string() + "' 2>&1";
    std::FILE *p = popen(cmd.c_str(), "r");
    REQUIRE(p != nullptr);
    std::string out;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), p)) > 0;)
    {
        out.append(buf, n);
    }
    const int status = pclose(p);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, out};
}

// A BSP image: header plus lump data, every lump empty at the end of the header by default.
struct Image
{
    dheader_t header{};
    std::vector<unsigned char> data;

    Image()
    {
        header.version = BSPVERSION;
        for (auto &l : header.lumps)
        {
            l.fileofs = sizeof(dheader_t);
            l.filelen = 0;
        }
    }

    // Append a lump's bytes and point the header at them.
    void Set(int lump, size_t bytes)
    {
        header.lumps[lump].fileofs = static_cast<std::int32_t>(sizeof(dheader_t) + data.size());
        header.lumps[lump].filelen = static_cast<std::int32_t>(bytes);
        data.resize(data.size() + bytes, 0);
    }

    // Record `index` of a lump added with Set (call after all Set calls: Set may reallocate).
    template <typename T>
    T &At(int lump, int index)
    {
        return *reinterpret_cast<T *>(data.data() + header.lumps[lump].fileofs - sizeof(dheader_t) + index * sizeof(T));
    }

    fs::path Write(const std::string &name, size_t truncate_to = SIZE_MAX) const
    {
        const fs::path dir = fs::temp_directory_path() / ("vhlt_loader_tests_" + std::to_string(getpid()));
        fs::create_directories(dir);
        const fs::path path = dir / name;
        std::vector<unsigned char> bytes(sizeof(dheader_t));
        std::memcpy(bytes.data(), &header, sizeof(header));
        bytes.insert(bytes.end(), data.begin(), data.end());
        if (truncate_to < bytes.size())
        {
            bytes.resize(truncate_to);
        }
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path;
    }
};

void CheckRejected(const Image &img, const std::string &name, const std::string &message, size_t truncate_to = SIZE_MAX)
{
    const fs::path path = img.Write(name, truncate_to);
    const ProbeResult r = Probe(path);
    CAPTURE(r.output);
    CHECK(r.exit_code == 1);
    CHECK(r.output.find(message) != std::string::npos);
    fs::remove(path);
}
} // namespace

TEST_SUITE("loader")
{
    TEST_CASE("an empty but well-formed BSP is accepted")
    {
        Image img;
        img.Set(LUMP_ENTITIES, 1);
        const fs::path path = img.Write("empty.bsp");
        const ProbeResult r = Probe(path);
        CAPTURE(r.output);
        CHECK(r.exit_code == 0);
        fs::remove(path);
    }

    TEST_CASE("a file smaller than the header is rejected")
    {
        CheckRejected(Image(), "short.bsp", "is too small to be a BSP file (100 bytes, header is 124)", 100);
        CheckRejected(Image(), "zero.bsp", "is too small to be a BSP file (0 bytes", 0);
    }

    TEST_CASE("the version message is unchanged")
    {
        Image img;
        img.header.version = 29;
        CheckRejected(img, "v29.bsp", "BSP is version 29, not 30");
    }

    TEST_CASE("negative offsets and lengths are rejected")
    {
        Image img;
        img.header.lumps[LUMP_PLANES].fileofs = -8;
        CheckRejected(img, "negofs.bsp", "LoadBSPFile: lump planes (1) has a negative offset or length (ofs -8, len 0)");
        Image img2;
        img2.header.lumps[LUMP_FACES].filelen = -20;
        CheckRejected(img2, "neglen.bsp", "LoadBSPFile: lump faces (7) has a negative offset or length (ofs 124, len -20)");
    }

    TEST_CASE("lumps that extend past the end of the file are rejected")
    {
        Image img;
        img.header.lumps[LUMP_PLANES].filelen = 40;
        CheckRejected(img, "past.bsp", "LoadBSPFile: lump planes (1) extends past end of file (ofs 124 + len 40 > 124)");

        Image img2; // offset + length overflows 32 bits
        img2.header.lumps[LUMP_EDGES].fileofs = 0x7FFFFFF0;
        img2.header.lumps[LUMP_EDGES].filelen = 0x7FFFFFF0;
        CheckRejected(img2, "overflow.bsp", "LoadBSPFile: lump edges (12) extends past end of file (ofs 2147483632 + len 2147483632 > 124)");

        Image img3; // one byte short
        img3.Set(LUMP_MODELS, sizeof(dmodel_t));
        CheckRejected(img3, "cut.bsp", "LoadBSPFile: lump models (14) extends past end of file (ofs 124 + len 64 > 187)", sizeof(dheader_t) + sizeof(dmodel_t) - 1);
    }

    TEST_CASE("the odd lump size message is unchanged")
    {
        Image img;
        img.Set(LUMP_PLANES, sizeof(dplane_t) + 1);
        CheckRejected(img, "odd.bsp", "LoadBSPFile: odd lump size");
    }

    TEST_CASE("lumps with more records than the arrays hold are rejected")
    {
        Image img;
        img.Set(LUMP_MODELS, sizeof(dmodel_t) * (MAX_MAP_MODELS + 1));
        CheckRejected(img, "models.bsp", "LoadBSPFile: lump models (14) has 513 records, limit is 512");

        Image img2;
        img2.Set(LUMP_ENTITIES, MAX_MAP_ENTSTRING + 1);
        CheckRejected(img2, "ents.bsp", "LoadBSPFile: lump entities (0) has 2097153 records, limit is 2097152");
    }

    TEST_CASE("texture and lighting limits keep their hlassume messages")
    {
        Image img;
        img.Set(LUMP_TEXTURES, DEFAULT_MAX_MAP_MIPTEX); // the check is strict: equal to the limit fails
        CheckRejected(img, "tex.bsp", "Exceeded MAX_MAP_MIPTEX");

        Image img2;
        img2.Set(LUMP_LIGHTING, DEFAULT_MAX_MAP_LIGHTDATA);
        CheckRejected(img2, "light.bsp", "Exceeded MAX_MAP_LIGHTING");
    }

    TEST_CASE("references to records that do not exist are rejected")
    {
        // one face with 4 edges, but only 2 surfedges
        Image img;
        img.Set(LUMP_PLANES, sizeof(dplane_t));
        img.Set(LUMP_TEXINFO, sizeof(texinfo_t));
        img.Set(LUMP_FACES, sizeof(dface_t));
        img.Set(LUMP_SURFEDGES, 2 * sizeof(int));
        img.At<dface_t>(LUMP_FACES, 0).numedges = 4;
        CheckRejected(img, "face_surfedges.bsp", "LoadBSPFile: face 0 uses surfedges 0..3, but there are only 2");

        img.At<dface_t>(LUMP_FACES, 0).numedges = 2;
        img.At<dface_t>(LUMP_FACES, 0).texinfo = 1;
        CheckRejected(img, "face_texinfo.bsp", "LoadBSPFile: face 0 refers to texinfo 1, but there are only 1");

        Image se; // surfedge pointing past the edges, and the most negative value
        se.Set(LUMP_EDGES, 2 * sizeof(dedge_t));
        se.Set(LUMP_SURFEDGES, 2 * sizeof(int));
        se.At<int>(LUMP_SURFEDGES, 1) = 5;
        CheckRejected(se, "surfedge.bsp", "LoadBSPFile: surfedge 1 refers to edge 5, but there are only 2");
        se.At<int>(LUMP_SURFEDGES, 1) = INT32_MIN;
        CheckRejected(se, "surfedge_min.bsp", "LoadBSPFile: surfedge 1 refers to edge -2147483648, but there are only 2");

        Image ed;
        ed.Set(LUMP_VERTEXES, sizeof(dvertex_t));
        ed.Set(LUMP_EDGES, 2 * sizeof(dedge_t));
        ed.At<dedge_t>(LUMP_EDGES, 1).v[1] = 1;
        CheckRejected(ed, "edge.bsp", "LoadBSPFile: edge 1 refers to vertex 1, but there are only 1");

        Image nd;
        nd.Set(LUMP_PLANES, sizeof(dplane_t));
        nd.Set(LUMP_LEAFS, sizeof(dleaf_t));
        nd.Set(LUMP_NODES, sizeof(dnode_t));
        nd.At<dnode_t>(LUMP_NODES, 0).children[0] = -5;
        nd.At<dnode_t>(LUMP_NODES, 0).children[1] = -1;
        CheckRejected(nd, "node.bsp", "LoadBSPFile: node 0 child 0 refers to leaf 4, but there are only 1");

        Image md;
        md.Set(LUMP_LEAFS, sizeof(dleaf_t));
        md.Set(LUMP_MODELS, sizeof(dmodel_t));
        md.At<dmodel_t>(LUMP_MODELS, 0).headnode[0] = -1;
        md.At<dmodel_t>(LUMP_MODELS, 0).headnode[1] = 3;
        CheckRejected(md, "model.bsp", "LoadBSPFile: model 0 hull 1 refers to clipnode 3, but there are only 0");
    }

    TEST_CASE("the dummy edge 0 is only checked when a surfedge uses it")
    {
        Image img; // what hlbsp writes for a map without geometry: edge 0 = (0, 0), no vertexes
        img.Set(LUMP_EDGES, sizeof(dedge_t));
        const fs::path path = img.Write("dummy_edge.bsp");
        const ProbeResult r = Probe(path);
        CAPTURE(r.output);
        CHECK(r.exit_code == 0);
        fs::remove(path);

        img.Set(LUMP_SURFEDGES, sizeof(int)); // surfedge 0 -> edge 0
        CheckRejected(img, "used_edge0.bsp", "LoadBSPFile: edge 0 refers to vertex 0, but there are only 0");
    }

    TEST_CASE("garbage visofs is not a load error (only hlrad reads it)")
    {
        Image img;
        img.Set(LUMP_LEAFS, 2 * sizeof(dleaf_t));
        img.At<dleaf_t>(LUMP_LEAFS, 1).visofs = INT32_MIN;
        const fs::path path = img.Write("visofs.bsp");
        CHECK(Probe(path).exit_code == 0);
        fs::remove(path);
    }

    TEST_CASE("the first broken lump in load order is the one reported")
    {
        Image img; // models come first in load order, planes later
        img.header.lumps[LUMP_PLANES].filelen = 40;
        img.header.lumps[LUMP_MODELS].filelen = 64;
        CheckRejected(img, "order.bsp", "lump models (14) extends past end of file");
    }
}
