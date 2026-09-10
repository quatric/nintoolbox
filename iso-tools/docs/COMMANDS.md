# Command Reference & New Tools Guide

This document provides a comprehensive reference for all new commands, standalone capabilities, and extended options introduced in **Wiimms ISO Tools Plus** beyond upstream Wiimms ISO Tools (v3.05a).

For the upstream command reference (for standard Wii and GameCube ISO, WBFS, and WDF operations), see the [Official Wiimms ISO Tools Documentation](https://wit.wiimm.de/).

---

## Tool & Command Matrix

| Command | Aliases | Primary Focus | Input Containers | Output Targets | Key Capabilities |
|---|---|---|---|---|---|
| **`wit XINFO`** | `XI` | Container Identification & Metadata | WUD, WUX, NDS, WAD, NFS, 3DS (CCI/CIA), Switch (XCI/NSP), NKit | Text / Stdout | Fast magic sniffing, partition layout display, container geometry inspection |
| **`wit XEXTRACT`** | `XX` | Non-Wii Container Unpacking | NDS, WAD, NFS, WUD, WUX, CCI, CIA, XCI, NSP | Extracted Directory (NFS: a Wii ISO) | Extracts ROM binaries, overlays, decrypted file systems, installable title contents; NFS is rebuilt into a Wii ISO |
| **`wit XCREATE`** | `XC` | Non-Wii Container Repacking | Extracted Directory (NFS: a Wii ISO) | NDS, WAD, NFS | Rebuilds compact NDS cartridges (FNT/FAT generation), re-encrypts Wii WADs, and rebuilds Wii-VC `hif_*.nfs` sets from a Wii ISO |
| **`wit XCONVERT`** | `XV` | Container Transcoding & NKit Restoration | WUD, WUX, NKit (`.nkit.iso`) | WUD, WUX, ISO (GC / Wii) | Sparse WUX $\leftrightarrow$ WUD conversion; byte-exact 1:1 NKit restore with H0–H3 and CRC validation |
| **`wit RIIVOLUTION`** | `RIIV`, `RII` | Riivolution Mod Engine & ISO Builder | Disc image (WBFS, ISO, WDF) or FST dir + Mod XML | Patched image (WBFS, ISO, etc.) or patched FST dir | Full XML spec: file/folder replacement, DOL memory patching, dynamic sections, GCT injection |
| **`wit BRAWLBUILDER`** | `BRAWL-BUILDER`, `BRAWL` | Smash Bros. Brawl Mod Builder | Brawl image (RSBE01.wbfs/.iso) or FST dir + Mod folder | Patched image (WBFS, ISO, etc.) or patched FST dir | Project M/Project+/Brawl- pipeline: stage padding, REL duplication, Subspace removal, DOL hooks |
| **Native RVZ Support** | — | Dolphin RVZ Disc Reading | Dolphin `.rvz` images | All WIT operations | Built-in Zstandard decompression, sub-2 MiB chunking, and Lagged Fibonacci Generator (LFG) padding |

---

## 1. `wit XINFO` / `wit XI`

Identifies and describes containers that are not standard Wii or GameCube disc images: Wii U (WUD/WUX), Nintendo DS (`.nds`), Wii WAD (`.wad`), Nintendo 3DS (`.3ds`, `.cci`, `.cia`), Nintendo Switch (`.xci`, `.nsp`), and NKit disc images.

```bash
wit XINFO <source>... [--quiet] [--verbose]
# Alias:
wit XI <source>...
```

### Supported Containers
- **Wii U Optical Disc Images**: `.wud` (raw uncompressed disc) and `.wux` (sparse deduplicated disc). Displays internal sector size, total sectors, deduplicated block counts, and compression savings.
- **Nintendo DS / DSi Cartridges**: `.nds`, `.srl`, `.dsi`. Displays game title, game code, maker code, unit code, ROM size, arm9/arm7 entry points and load addresses, and overlay table counts.
- **Wii Installable Titles (WAD)**: `.wad`. Displays WAD type, certificate size, ticket size, TMD size, number of contents, and boot index.
- **Wii U "Wii Virtual Console" content (NFS)**: `hif_*.nfs` (the `hif_000000.nfs` file carries the `EGGS` header). Displays the EGGS version, the sparse-image data runs, and the total payload size.
- **Nintendo 3DS Containers**: `.3ds`, `.cci`, `.cia`. Detects NCSD partition headers, partition IDs, and CIA component structures.
- **Nintendo Switch Packages**: `.xci`, `.nsp`. Detects HFS0 cartridge partitions and PFS0 packages.
- **NKit Images**: `.nkit.iso`. Detects whether the payload is GameCube or Wii, reads the NKit version, and extracts the target Redump CRC32.

### Examples
```bash
# Identify a foreign image:
wit XINFO MarioKart8.wux

# Inspect multiple images:
wit XI Game.nds Title.wad Update.nsp
```

---

## 2. `wit XEXTRACT` / `wit XX`

Unpacks non-Wii containers into a structured, editable directory.

```bash
wit XEXTRACT <source> <dest_dir> [--overwrite] [--test]
# Alias:
wit XX <source> <dest_dir>
```

### Container Extraction Details

#### Nintendo DS / DSi (`.nds`)
Extracts a full ROM tree into `dest_dir/`:
- `header.bin`: Complete 0x200-byte cartridge header.
- `arm9.bin`: Decompressed ARM9 secure core binary.
- `arm7.bin`: ARM7 sub-processor binary.
- `banner.bin`: Title banner icon and multilingual titles.
- `nitro_footer.bin`: Present on cartridges using ARM9 overlay compression.
- `arm9_ovt.bin` / `arm7_ovt.bin`: Binary overlay allocation tables.
- `overlay/`: Individual overlay files named `arm9_XXXX.bin` and `arm7_XXXX.bin`.
- `data/`: Extracted NitroFS directory tree preserving subdirectories and filenames.

#### Wii Installable Title (`.wad`)
Extracts and decrypts all title components into `dest_dir/`:
- `ticket.bin`: Title ticket containing encrypted title keys and console limits.
- `tmd.bin`: Title Metadata detailing content IDs, sizes, types, and SHA-1 hashes.
- `cert.bin`: Title certificate chain.
- `<content_id>.app`: Decrypted content files (e.g., `00000000.app`, `00000001.app`), verified against their TMD SHA-1 checksums.
- `trailer.bin`: Footer/trailer data if present.

#### Wii U Optical Disc (`.wud` / `.wux`)
Decrypts system and update partitions (`SI`, `UP`, `GI`) directly from the image into the destination directory using the disc key and 0x8000 cluster AES-128-CBC decryption.

#### Examples
```bash
# Unpack a Nintendo DS cartridge:
wit XEXTRACT game.nds game.d/

# Unpack a Wii WAD title:
wit XEXTRACT channel.wad channel.d/ --overwrite
```

---

## 3. `wit XCREATE` / `wit XC`

Builds a container from an extracted directory previously generated by `wit XEXTRACT`. The output format is automatically derived from the file extension of the destination path.

```bash
wit XCREATE <source_dir> <dest_file> [--overwrite] [--test]
# Alias:
wit XC <source_dir> <dest_file>
```

### Supported Rebuild Targets

#### Nintendo DS Cartridge (`.nds`)
Rebuilds a valid Nintendo DS ROM from `source_dir/`:
- **File Name Table (FNT) & File Allocation Table (FAT)**: Rebuilt dynamically from `data/`. Files can be added, deleted, or resized.
- **Deterministic Layout**: Directories are assigned IDs depth-first and names are sorted case-insensitively, matching the Nintendo SDK build tools.
- **Header & Overlays**: Recomputes header checksums, table offsets, and file allocations.

#### Wii Installable Title (`.wad`)
Re-encrypts and packages `source_dir/` into a `.wad` archive:
- Re-encrypts each `<id>.app` content file using the title key from `ticket.bin`.
- Recalculates content sizes and SHA-1 hashes and updates `tmd.bin`.
- **Conditional Signing**: If no content files were modified, the original TMD signature is preserved verbatim. If files were modified, the TMD is fake-signed automatically.

#### Wii U "Wii Virtual Console" content (`.nfs`)
Rebuilds the `hif_*.nfs` set from a standard Wii ISO (`source` here is the ISO,
not a directory; `dest` names the first `.nfs` file and its directory receives
the whole set):
- Strips the inner Wii partition AES from every game partition, re-lays the
  sparse `EGGS` mapping, then re-applies the outer per-title AES layer.
- Splits the result into `hif_000000.nfs`, `hif_000001.nfs`, … at 250 MiB.
- Needs the per-title `htk` key (`$WIT_NFS_KEY`, or `htk.bin` / `code/htk.bin`
  near the ISO or output). The Wii common key is built in.
- Also patches `fw.img`'s fakesign hash check in place when one is found
  (`$WIT_NFS_FWIMG`, or `fw.img` / `code/fw.img` nearby); `$WIT_NFS_LEGIT`
  skips it. Port of [nfs2iso2nfs](https://github.com/sabykos/nfs2iso2nfs).

### Examples
```bash
# Rebuild an edited DS game:
wit XCREATE game.d/ modified_game.nds --overwrite

# Rebuild a Wii-VC .nfs set from a Wii ISO:
wit XCREATE game.iso out/hif_000000.nfs --overwrite

# Rebuild an edited Wii WAD:
wit XCREATE channel.d/ modified_channel.wad
```

---

## 4. `wit XCONVERT` / `wit XV`

Converts between container encodings of the same data family, or restores compressed NKit images back into 1:1 retail disc images.

```bash
wit XCONVERT <source> <dest> [--overwrite] [--test]
# Alias:
wit XV <source> <dest>
```

### 1. Wii U Sparse Conversion (`WUD` $\leftrightarrow$ `WUX`)
WUX stores unique sectors once and indexes duplicates via a lookup table, shrinking full 25 GB Wii U disc images down to the actual data size without touching payload data.

- **`WUD` $\to$ `WUX`**: Reads sectors, deduplicates using a SHA-1 hash table, confirms matches byte-for-byte, and outputs a sparse `.wux`.
- **`WUX` $\to$ `WUD`**: Expands sparse sectors back into a full 25 GB uncompressed `.wud` image.
- Both directions are lossless and byte-identical across round-trips.

```bash
# Compress raw WUD to sparse WUX:
wit XCONVERT game.wud game.wux

# Decompress sparse WUX back to raw WUD:
wit XCONVERT game.wux game.wud --overwrite
```

### 2. NKit Restoration (`.nkit.iso` $\to$ Retail `.iso`)
Restores NKit-compressed disc images back to original 1:1 Redump-verified GameCube and Wii ISO images.

#### Restoration Architecture
NKit disc images save space by removing Nintendo's non-zero pseudo-random *junk data* written between files during disc authoring, recording only gap geometry and seeds. `wit XCONVERT` reconstructs this missing data:
1. **Wii Discs**:
   - Reconstructs partition file system tables (FSTs).
   - Regenerates inter-file and partition junk using Nintendo's Lagged Fibonacci Generator (LFG).
   - Recalculates the complete H0, H1, H2, and H3 SHA-1 hash trees.
   - Re-encrypts partition data blocks using the title key from the ticket.
   - Verifies the final restored image against the Redump CRC32 stored in the NKit header.
2. **GameCube Discs**:
   - Computes 68-byte LFG seeds from (Game ID, disc number, 32 KiB block index) and restores inter-file padding.

> [!IMPORTANT]
> **Handling `.nkit.gcz` Files**: Most NKit downloads are distributed with an outer GCZ layer (`.nkit.gcz`). `wit` validates GCZ payloads as standard disc images, which an NKit stream is not. Decompress the outer GCZ container first using any GCZ tool (e.g. Dolphin), then run `wit XCONVERT` on the resulting `.nkit.iso`:
> ```bash
> gcz-decompress game.nkit.gcz game.nkit.iso
> wit XCONVERT   game.nkit.iso  game.iso
> wit VERIFY     game.iso
> ```

---

## 5. `wit RIIVOLUTION` / `wit RIIV` / `wit RII`

Applies Riivolution mod XML packages and external replacement files directly to Wii disc images (`.wbfs`, `.iso`, `.wdf`, `.ciso`, `.wia`) or extracted FST directories.

```bash
wit RIIVOLUTION [options] <source> <mod.xml> [dest]
wit RIIVOLUTION [options] <mod.xml>
# Aliases:
wit RIIV [options] <source> <mod.xml> [dest]
wit RII  [options] <source> <mod.xml> [dest]
```

### Command Options

| Option | Argument | Description |
|---|---|---|
| `--info` | — | Inspects the Riivolution XML and displays sections, options, choices, game IDs, and patch summaries. |
| `--test` | — | Simulates resolution of choices and patches against the source game without writing output. |
| `--choice` / `--select` | `spec` | Selects option choices by name or index (e.g. `--choice "Difficulty=Hard,Costumes=Alt"` or comma-separated list). |
| `--all-choices` | — | Enables all single choices or first available choices for options without defaults. |
| `--default-choices` | — | Uses only the default choices declared in the Riivolution XML. |
| `--interactive` | — | Prompts interactively in the terminal for option choices, 6-character Game ID, and disc Title. |
| `--root` | `dir` | Defines the external replacement files root directory (defaults to mod directory or SD root). |
| `--dol` | `file` | Replaces the base executable with an explicit custom `main.dol`. |
| `--gct` / `--add-section` | `file` | Injects Gecko cheat codes (`.gct` / `.gch`) into `main.dol` using `wstrt`. |
| `--id` | `ID` | Assigns a custom 6-character Disc ID (e.g. `SMNE03`) to ensure independent game save files. |
| `--name` | `title` | Assigns a custom title to the patched game image. |
| `--ignore-regions` | — | Ignores game ID and region filter mismatches declared in the XML `<id>` tags. |
| `--save-xml` | `file` | Saves the resolved/preprocessed Riivolution XML configuration to a file. |
| `--keep-temp` | — | Preserves temporary extracted files after building for inspection. |
| `--overwrite` | — | Overwrites existing destination files or directories. |

### Technical Capabilities
- **Full XML Specification Support**: Implements `<wiidisc>`, `<id>`, `<options>`, `<section>`, `<option>`, `<choice>`, `<patch>`, `<file>`, `<folder>`, `<savegame>`, and `<memory>` tags.
- **DOL Memory Patching**:
  - In-place virtual memory patching with `original` byte verification.
  - Pattern search and replace (`search="true"`) with alignment stride options.
  - **Dynamic Section Creation**: Automatically allocates free DOL text/data section headers and appends payload code to `main.dol` for patches targeting high unmapped memory addresses.
  - **Ocarina Hook Insertion**: Automatically scans for `blr` epilogues and patches branches to hook code.
- **Variable Substitution**: Automatically evaluates `{$__gameid}`, `{$__region}`, `{$__maker}`, and custom parameters defined in `<param>` tags or macros.
- **Flexible Targets**: Can patch an extracted FST directory in place, or build directly into `.wbfs`, `.iso`, `.wdf`, or `.wia`.

### Examples
```bash
# 1. Inspect a Riivolution XML mod:
wit RIIVOLUTION --info /mods/NewerSMBW/riivolution/Newer.xml

# 2. Build a patched WBFS with specific choices and custom Game ID:
wit RIIVOLUTION --choice "Game=Newer Super Mario Bros. Wii,Language=English" \
    --id SMNE03 --name "Newer Super Mario Bros. Wii" \
    clean_game.wbfs /mods/NewerSMBW/riivolution/Newer.xml newer.wbfs

# 3. Interactive configuration mode:
wit RIIVOLUTION --interactive clean_game.wbfs mod.xml patched_game.wbfs

# 4. Patch an extracted FST directory in place:
wit RIIVOLUTION /path/to/extracted_fst /path/to/mod.xml
```

---

## 6. `wit BRAWLBUILDER` / `wit BRAWL`

Native C implementation of the BrawlBuilder modding pipeline for *Super Smash Bros. Brawl*. Builds standalone modded disc images (`.wbfs`, `.iso`, etc.) or extracted FST directories from Gecko-based mods (Project M, Project+, Brawl-, PM Remix, and custom stage/fighter packs), without requiring external C# runtimes, Mono, or .NET.

```bash
wit BRAWLBUILDER [options] <source> [mod_folder] [dest]
wit BRAWLBUILDER [options] <source> [dest] --mod <dir>
# Aliases:
wit BRAWL-BUILDER [options] <source> [mod_folder] [dest]
wit BRAWL         [options] <source> [mod_folder] [dest]
```

### Command Options

| Option | Argument | Description |
|---|---|---|
| `--mod` / `--mod-folder` | `dir` | Specifies the Brawl mod folder (e.g. `pf/` or `projectm/`). |
| `--remove-sse` / `--no-sse` | — | Strips all 840 single-player Subspace Emissary files, reducing image size by ~5 GB. |
| `--banner` | `file` | Replaces the disc banner with a custom `opening.bnr`. |
| `--no-gct-patch` | — | Disables automated compatibility patching of GCT codes. |
| `--no-alt-pad` | — | Disables padding base stage files to match the size of their largest alternate stage. |
| `--offset` | `hex` | Defines the memory offset in `main.dol` for the GCT section (default: `0x80570000`). |
| `--interactive` | — | Prompts interactively for Game ID, Title, and Subspace removal. |
| `--id` | `ID` | Assigns a custom 6-character Game ID (e.g. `PM3601`). |
| `--name` | `title` | Assigns a custom title to the patched image. |
| `--gct` | `file` | Explicitly specifies a custom GCT code file. |
| `--test` | — | Validates configuration and mod assets without modifying files. |
| `--overwrite` | — | Overwrites existing output images. |

### Pipeline Architecture & Gecko Mod Compatibility
- **Automated Mod Root & GCT Discovery**: Automatically locates game files under `pf/` or root, and auto-discovers `RSBE01.gct` under `codes/` or mod root.
- **Automated GCT Compatibility Patches**:
  - *Individual Stock Icons fix*: Prevents crashes on boot.
  - *Alternate Stage Loader loop patch*: Reroutes stage loading from SD card to disc.
  - *Soundbank & SFX DVD loader patch*: Loads custom `.sawnd` audio files from disc instead of SD card.
- **Alternate Stage Preparation & Padding**:
  - Automatically identifies alternate stage definitions (`_[A-Z].pac`) and duplicates the corresponding base stage module (`module/st_*.rel` $\to$ `module/st_*_*.rel`).
  - Zero-pads base stage `.pac` files to match the exact size of their largest alternate stage, ensuring seamless transitions during in-game stage selection.
- **Subspace Emissary Stripping (`--remove-sse`)**:
  - Removes all single-player Subspace cutscenes and assets, shrinking the final disc image from ~7.5 GB to ~2.5 GB.
- **Native DOL Code Injection**:
  - Injects Gecko CodeHandler into a new text section at `0x80001800`.
  - Injects patched GCT codes into a new data section at `0x80570000` (configurable via `--offset`).
  - Patches CodeHandler branch hooks (`0x80200984` and `0x80002778`) and security checks (`0x800042B8` and `0x803E9930`).
  - Automatically patches Brawl's internal disc ID check at `0x805A14B0` & `0x805A14B8` when a custom Game ID is used, completely bypassing the *"Please insert the Super Smash Bros. Brawl Game Disc"* screen.

> [!NOTE]
> `wit BRAWLBUILDER` supports all standard Gecko-based file-patching mods. Mods utilizing **BrawlEx** (e.g. Brawl- versions beyond 2.x.6) rely on dynamic character slot expansion routines requiring physical SD card hardware access and cannot be run from disc-based ISO/WBFS images.

### Examples
```bash
# Build a Project M 3.6 WBFS with Subspace Emissary removed:
wit BRAWLBUILDER --remove-sse \
    --id PM3601 --name "Project M 3.6" \
    Brawl.wbfs /mods/ProjectM/ pm36.wbfs

# Build using an explicit GCT code file and custom banner:
wit BRAWLBUILDER --gct /mods/codes/RSBE01.gct --banner /mods/opening.bnr \
    Brawl.iso /mods/ProjectPlus/ pp.wbfs

# Interactive build mode:
wit BRAWLBUILDER --interactive Brawl.wbfs /mods/ProjectPlus/ pp.wbfs
```

---

## 7. Native Dolphin RVZ Support

Dolphin's `.rvz` format (a derivative of WIA with Zstandard compression, sub-2 MiB chunking, and losslessly packed pseudo-random padding) is supported directly as an input format across standard WIT commands.

### Supported Operations
- **`wit VERIFY`**: Verifies RVZ images against internal H0–H3 hash trees.
- **`wit COPY`**: Converts RVZ directly to raw ISO, WBFS, WDF, or WIA.
- **`wit EXTRACT`**: Extracts file systems directly from RVZ containers.
- **`wit DUMP` / `wit LIST`**: Analyzes partitions and metadata within RVZ files.

### Examples
```bash
# Verify integrity of an RVZ file:
wit VERIFY game.rvz

# Convert Dolphin RVZ to standard uncompressed ISO:
wit COPY --iso --raw game.rvz --dest game.iso

# Convert Dolphin RVZ directly to USB-loader WBFS:
wit COPY game.rvz --dest /wbfs/game.wbfs

# Extract file system directly from an RVZ file:
wit EXTRACT game.rvz --dest game.d/
```
