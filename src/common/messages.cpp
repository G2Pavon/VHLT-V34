#include "messages.h"
#include "cmdlib.h"

// AJM: because these are repeated, they use up redundant memory.
//  consequently ive made them into const strings which each occurance can point to.

// Common descriptions
constexpr char *const INTERNALLIMIT = "The compiler tool hit an internal limit";
constexpr char *const INTERNALERROR = "The compiler tool had an internal error";
constexpr char *const MAPERROR = "The map has a problem which must be fixed";

// Common explanations
constexpr char *const SELFEXPLANITORY = "self explanitory";
constexpr char *const REFERENCE = "Check the file http://www.zhlt.info/common-mapping-problems.html for a detailed explanation of this problem";
constexpr char *const SIMPLIFY = "The map is too complex for the game engine/compile tools to handle.  Simplify";
constexpr char *const CONTACTMERL = "contact amckern@yahoo.com concerning this issue.";
constexpr char *const CONTACT = "contact vluzacn@163.com concerning this issue.";
static const MessageTable_t assumes[assume_last] = {
    {"invalid assume message", "This is a message should never be printed.", CONTACT},

    // generic
    {"Memory allocation failure", "The program failled to allocate a block of memory.",
#ifdef HLRAD
     sizeof(intptr_t) <= 4 ? "The map is too complex for the compile tools to handle. Switch to the 64-bit version of hlrad if possible." : "Likely causes are (in order of likeliness) : the partition holding the swapfile is full; swapfile size is smaller than required; memory fragmentation; heap corruption"
#else
     CONTACT
#endif
    },
    {"NULL Pointer", INTERNALERROR, CONTACT},
    {"Bad Thread Workcount", INTERNALERROR, CONTACT},

    // qcsg
    {"Missing '[' in texturedef (U)", MAPERROR, REFERENCE},
    {"plane with no normal", MAPERROR, REFERENCE},
    {"brush with coplanar faces", MAPERROR, REFERENCE},
    {"brush outside world", MAPERROR, REFERENCE},
    {"mixed face contents", MAPERROR, REFERENCE},
    {"Brush type not allowed in world", MAPERROR, REFERENCE},
    {"Brush type not allowed in entity", MAPERROR, REFERENCE},
    {"No visibile brushes", "All brushes are CLIP or ORIGIN (at least one must be normal/visible)", SELFEXPLANITORY},
    {"Entity with ONLY an ORIGIN brush", "All entities need at least one visible brush to function properly.  CLIP, HINT, ORIGIN, do not count as visible brushes.", SELFEXPLANITORY},
    {"Could not find WAD file", "The compile tools could not locate a wad file that the map was referencing.", "Make sure the wad's listed in the level editor actually all exist"},
    {"Exceeded MAX_TRIANGLES", INTERNALLIMIT, CONTACT},
    {"Exceeded MAX_SWITCHED_LIGHTS", "The maximum number of switchable light entities has been reached", SELFEXPLANITORY},
    {"Exceeded MAX_TEXFILES", INTERNALLIMIT, CONTACT},

    // qbsp
    {"LEAK in the map", MAPERROR, REFERENCE},
    {"Exceeded MAX_LEAF_FACES", "This error is almost always caused by an invalid brush, by having huge rooms, or scaling a texture down to extremely small values (between -1 and 1)",
     "Find the invalid brush.  Any imported prefabs, carved brushes, or vertex manipulated brushes should be suspect"},
    {"Exceeded MAX_WEDGES", INTERNALLIMIT, CONTACT},
    {"Exceeded MAX_WVERTS", INTERNALLIMIT, CONTACT},
    {"Exceeded MAX_SUPERFACEEDGES", INTERNALLIMIT, CONTACT},
    {"Empty Solid Entity", "A solid entity in the map (func_wall for example) has no brushes.", "If using Worldcraft, do a check for problems and fix any occurences of 'Empty solid'"},

    // vis
    {"Leaf portal saw into leaf", MAPERROR, REFERENCE},
    {"Exceeded MAX_PORTALS_ON_LEAF", MAPERROR, REFERENCE},
    {"Invalid client/server state", INTERNALERROR, CONTACT},

    // qrad
    {"Exceeded MAX_TEXLIGHTS", "The maximum number of texture lights in use by a single map has been reached",
     "Use fewer texture lights."},
    {"Exceeded MAX_PATCHES", MAPERROR, REFERENCE},
    {"Transfer < 0", INTERNALERROR, CONTACT},
    {"Bad Surface Extents", MAPERROR, REFERENCE},
    {"Malformed face normal", "The texture alignment of a visible face is unusable", "If using Worldcraft, do a check for problems and fix any occurences of 'Texture axis perpindicular to face'"},
    {"No Lights!", "lighting of map halted (I assume you do not want a pitch black map!)", "Put some lights in the map."},
    {"Bad Light Type", INTERNALERROR, CONTACT},
    {"Exceeded MAX_SINGLEMAP", INTERNALLIMIT, CONTACT},

    // common
    {"Unable to create thread", INTERNALERROR, CONTACT},
    {"Exceeded MAX_MAP_PLANES", "The maximum number of plane definitions has been reached",
     "The map has grown too complex"},
    {"Exceeded MAX_MAP_TEXTURES", "The maximum number of textures for a map has been reached", SELFEXPLANITORY},

    {"Exceeded MAX_MAP_MIPTEX", "Texture memory usage on the map has exceeded the limit",
     "Merge similar textures, remove unused textures from the map"},
    {"Exceeded MAX_MAP_TEXINFO", INTERNALLIMIT, CONTACT},
    {"Exceeded MAX_MAP_SIDES", INTERNALLIMIT, CONTACT},
    {"Exceeded MAX_MAP_BRUSHES", "The maximum number of brushes for a map has been reached", SELFEXPLANITORY},
    {"Exceeded MAX_MAP_ENTITIES", "The maximum number of entities for the compile tools has been reached", SELFEXPLANITORY},
    {"Exceeded MAX_ENGINE_ENTITIES", "The maximum number of entities for the half-life engine has been reached", SELFEXPLANITORY},

    {"Exceeded MAX_MAP_MODELS", "The maximum number of brush based entities has been reached",
     "Remove unnecessary brush entities, consolidate similar entities into a single entity"},
    {"Exceeded MAX_MAP_VERTS", "The maximum number of vertices for a map has been reached", SIMPLIFY}, // internallimit, contact //--vluzacn
    {"Exceeded MAX_MAP_EDGES", INTERNALLIMIT, CONTACT},

    {"Exceeded MAX_MAP_CLIPNODES", MAPERROR, REFERENCE},
    {"Exceeded MAX_MAP_MARKSURFACES", INTERNALLIMIT, CONTACT},
    {"Exceeded MAX_MAP_FACES", "The maximum number of faces for a map has been reached", "This error is typically caused by having a large face with a small texture scale on it, or overly complex maps."},
    {"Exceeded MAX_MAP_SURFEDGES", INTERNALLIMIT, CONTACT},
    {"Exceeded MAX_MAP_NODES", "The maximum number of nodes for a map has been reached", SIMPLIFY},
    {"CompressVis Overflow", INTERNALERROR, CONTACT},
    {"DecompressVis Overflow", INTERNALERROR, CONTACT},
    {"Exceeded MAX_MAP_LEAFS", "The maximum number of leaves for a map has been reached", SIMPLIFY},
    {"Execution Cancelled", "Tool execution was cancelled either by the user or due to a fatal compile setting", SELFEXPLANITORY},
    {"Internal Error", INTERNALERROR, CONTACT},
    //KGP added
    {"Exceeded MAX_MAP_LIGHTING", "You have run out of light data memory", "Use the -lightdata <#> command line option to increase your maximum light memory.  The default is 32768 (KB)."}, // 6144 (KB) //--vluzacn
    {"Exceeded MAX_INTERNAL_MAP_PLANES", "The maximum number of plane definitions has been reached", "The map has grown too complex"},
    {"Could not locate WAD file", "The compile tools could not locate a wad file that the map was referencing.",
     "Make sure the file '<mapname>.wa_' exists. This is a file generated by hlcsg and you should not delete it. If you have to run hlrad without this file, use '-waddir' to specify folders where hlrad can find all the wad files."},
    {"Couldn't open extent file", "<mapname>.ext doesn't exist. This file is required by the " PLATFORM_VERSIONSTRING " version of hlrad.", "Make sure hlbsp has run correctly. Alternatively, run 'ripent.exe -writeextentfile <mapname>' to create the extent file."},
};

const MessageTable_t *GetAssume(assume_msgs id)
{
    if (!(id > assume_first && id < assume_last)) //(!(id > assume_first) && (id < assume_last)) --vluzacn
    {
        id = assume_first;
    }
    return &assumes[id];
}
