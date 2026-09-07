#ifndef WIT_X_RIIVOLUTION_H
#define WIT_X_RIIVOLUTION_H 1

#include "lib-std.h"
#include "lib-sf.h"

// Forward declarations
typedef struct riiv_param_t riiv_param_t;
typedef struct riiv_choice_t riiv_choice_t;
typedef struct riiv_option_t riiv_option_t;
typedef struct riiv_section_t riiv_section_t;
typedef struct riiv_patch_ref_t riiv_patch_ref_t;
typedef struct riiv_patch_t riiv_patch_t;
typedef struct riiv_file_t riiv_file_t;
typedef struct riiv_folder_t riiv_folder_t;
typedef struct riiv_savegame_t riiv_savegame_t;
typedef struct riiv_memory_t riiv_memory_t;
typedef struct riiv_doc_t riiv_doc_t;

struct riiv_param_t
{
    char *name;
    char *value;
    riiv_param_t *next;
};

struct riiv_patch_ref_t
{
    char *id;
    riiv_param_t *params;
    riiv_patch_ref_t *next;
};

struct riiv_choice_t
{
    char *name;
    riiv_param_t *params;
    riiv_patch_ref_t *patch_refs;
    riiv_choice_t *next;
};

struct riiv_option_t
{
    char *id;
    char *name;
    uint default_choice;  // 1-based, 0 = disabled
    uint selected_choice; // 1-based, 0 = disabled
    riiv_param_t *params;
    riiv_choice_t *choices;
    uint n_choices;
    riiv_option_t *next;
};

struct riiv_section_t
{
    char *name;
    riiv_option_t *options;
    riiv_section_t *next;
};

struct riiv_file_t
{
    char *disc;
    char *external;
    bool resize;
    bool create;
    u32 offset;
    u32 fileoffset;
    u32 length;
    riiv_file_t *next;
};

struct riiv_folder_t
{
    char *disc;
    char *external;
    bool resize;
    bool create;
    bool recursive;
    u32 length;
    riiv_folder_t *next;
};

struct riiv_savegame_t
{
    char *external;
    bool clone;
    riiv_savegame_t *next;
};

struct riiv_memory_t
{
    u32 offset;
    u8 *value;
    uint value_len;
    char *valuefile;
    u8 *original;
    uint original_len;
    bool ocarina;
    bool search;
    u32 align;
    riiv_memory_t *next;
};

struct riiv_patch_t
{
    char *id;
    char *root;
    riiv_file_t *files;
    riiv_folder_t *folders;
    riiv_savegame_t *savegames;
    riiv_memory_t *memories;
    riiv_patch_t *next;
};

struct riiv_doc_t
{
    char *xml_path;
    int version;
    char *root;

    char *filter_game;
    char *filter_developer;
    int filter_disc;
    int filter_version;
    StringField_t filter_regions;

    riiv_section_t *sections;
    riiv_patch_t *patches;
};

typedef struct RiivolutionOptions_t
{
    ccp source_image;        // Input game image or extracted FST directory
    ccp xml_file;            // Riivolution XML file path
    ccp dest_path;           // Destination output path (image or FST directory)
    ccp root_dir;            // Explicit external mod root directory (or NULL for auto)
    ccp save_xml_path;       // Output path for resolved XML (or NULL)

    StringField_t choices;   // Choice specifications (e.g. "opt=choice", "1,2,1")
    bool all_choices;        // Select first available choice for all options
    bool default_choices;    // Select only default choices from XML
    bool interactive;        // Prompt user interactively for choices, ID6, and title
    bool info_only;          // Inspect and display XML info, do not build
    bool ignore_regions;     // Ignore game ID and region filter mismatches
    bool keep_temp;          // Do not delete temporary directory
    bool overwrite;          // Overwrite destination if it exists
    int testmode;            // Test / dry-run mode
    int verbose;             // Verbosity level

    // Output metadata
    ccp custom_id;           // Custom ID (6 chars, e.g. "NMGP01" or "...P01")
    ccp custom_name;         // Custom disc name
    ccp custom_dol;          // Explicit custom main.dol replacement
    StringField_t gct_files; // List of GCT/GCH files to inject via wstrt
    enumOFT output_oft;      // Requested output format (OFT_ISO, OFT_WBFS, OFT_FST, etc.)
} RiivolutionOptions_t;

void InitRiivolutionOptions ( RiivolutionOptions_t *opt );
riiv_doc_t * RiivolutionParseXmlFile ( ccp xml_path );
void RiivolutionFreeDoc ( riiv_doc_t *doc );
enumError RiivolutionInspect ( riiv_doc_t *doc, int verbose );
enumError RiivolutionBuild ( RiivolutionOptions_t *opt );
enumError RiivolutionCommand ( RiivolutionOptions_t *opt );

#endif // WIT_X_RIIVOLUTION_H
