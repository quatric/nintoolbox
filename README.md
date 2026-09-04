# Wiimms ISO Tools

    ****************************************
    *    __            __ _ ___________    *
    *    \ \          / /| |____   ____|   *
    *     \ \        / / | |    | |        *
    *      \ \  /\  / /  | |    | |        *
    *       \ \/  \/ /   | |    | |        *
    *        \  /\  /    | |    | |        *
    *         \/  \/     |_|    |_|        *
    *                                      *
    *           Wiimms ISO Tools           *
    *         https://wit.wiimm.de/        *
    *                                      *
    ****************************************

»Wiimms ISO Tools« is a set of command line tools to extract,
modify and create Wii and GameCube ISO images and WBFS containers.
Development started in 2009.
See https://wit.wiimm.de/ for more details, documentation and downloads.

## About this fork

`wiimms-iso-tools-plus` is a fork of
[Wiimm/wiimms-iso-tools](https://github.com/Wiimm/wiimms-iso-tools) that widens
the range of container formats WIT understands, so that a single tool can open
them instead of shelling out to a different program per format.  Upstream
behaviour is unchanged; everything here is additive.

### Format & compression support

Wii and GameCube disc images keep going through WIT's normal commands. The
other containers have nothing in common with a Wii disc, so instead of forcing
them into that pipeline they get their own four commands — `XINFO`, `XEXTRACT`,
`XCREATE` and `XCONVERT` — which never touch the disc code.

| Format | Category | Decode | Encode | Notes |
|---|---|---|---|---|
| 3DS CCI / CIA | Disc/package | 🔍 | ⛔ | Identified by `XINFO`, not yet unpacked |
| BrawlBuilder | Mod / Disc Patching | ✅ | ✅ | Brawl/PW4/PMEX code-mod pipeline: PatchFile injection, Gecko codes, module relocation via `wit BRAWLBUILDER` |
| ISO / WDF / CISO / WBFS / WIA / GCZ / FST | Disc image | ✅ | ✅ | Upstream WIT commands |
| NDS | Disc image | ✅ | ✅ | DS/DSi; full file system, both CPU binaries, overlays and banner via `XINFO`, `XEXTRACT` and `XCREATE` |
| NKit (`.nkit.iso`) | Disc image | ✅ | ⛔ | Restore via `XCONVERT`; Wii is byte exact against the header CRC32; GameCube is implemented but still unverified against a real sample |
| Riivolution | Mod / Disc Patching | ✅ | ✅ | Full XML spec: file/folder replacement, DOL memory patching, dynamic sections, variable substitution, multi-choice selection via `wit RIIVOLUTION` |
| RVZ | Disc image | ✅ | ⛔ | Dolphin's WIA derivative; all normal WIT read commands; Zstandard, sub-2 MiB chunks and losslessly packed pseudo-random padding |
| Switch XCI / NSP | Disc/package | 🔍 | ⛔ | Identified by `XINFO`, not yet unpacked |
| WAD | Installable title | ✅ | ✅ | Wii; contents decrypted and re-encrypted via `XINFO`, `XEXTRACT` and `XCREATE`; TMD re-signed only when something changed |
| WUX / WUD | Disc image | ✅ | ✅ | Wii U; container conversion via `XINFO` and `XCONVERT`; file-system unpacking needs the per-disc key and is not implemented |

✅ supported · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented

### RVZ

RVZ images are detected automatically wherever a WIA is accepted — `wit VERIFY`,
`wit DUMP`, `wit EXTRACT`, `wit COPY` and friends all work:

```
wit VERIFY  game.rvz
wit COPY --iso --raw game.rvz --dest game.iso
wit EXTRACT game.rvz --dest game.d/
```

Reading RVZ needs Zstandard; `./setup.sh` detects `zstd.h` automatically and the
build falls back to a clear "not supported" error if it is missing.  Writing RVZ
is not implemented — use `--wia` for a compressed output format.

The implementation follows Dolphin's
[WiaAndRvz.md](https://github.com/dolphin-emu/dolphin/blob/master/docs/WiaAndRvz.md).
It has been checked against Redump RVZ images for both Wii (every partition hash
including H3 verifies, so the decoded image is byte-identical to the original
disc) and GameCube.

### Wii U (WUX / WUD)

WUX is a sparse container: the payload sectors are stored verbatim, but
identical sectors are stored once and referenced repeatedly, which is what
makes a 25 GB disc image manageable.  Converting between the two encodings
needs no key, because it does not touch the payload at all:

```
wit XCONVERT game.wux game.wud
wit XCONVERT game.wud game.wux
wit XINFO    game.wux
```

The round trip is byte-identical in both directions, including images whose
size is not a multiple of the sector size.  Reading has been checked against a
retail Redump WUX; writing dedupes with a hash table and confirms every
candidate by comparing the stored bytes, so a hash collision can cost time but
never corrupt the output.

### Nintendo DS

```
wit XINFO    game.nds
wit XEXTRACT game.nds game.d/
wit XCREATE  game.d/ new.nds
```

`XEXTRACT` writes the header, both CPU binaries (plus the nitro footer when the
cartridge has one), the overlay tables, the banner, the overlays as
`overlay/arm9_NNNN.bin`, and the file system under `data/`.  `XCREATE` rebuilds
the FNT and FAT from the directory, so files may be added, removed or resized.

Rebuilding assigns directory ids depth first and orders names case
insensitively, which is what the original build tools did: repacking a retail
cartridge unchanged reproduces its FNT byte for byte and every file keeps its
exact size and content.  The image is not byte-identical, because retail images
place file data in a build specific physical order and often leave a large gap
before it; the rebuilt image is compact and in file id order.

### Wii WAD

```
wit XINFO    title.wad
wit XEXTRACT title.wad title.d/
wit XCREATE  title.d/ new.wad
```

Contents are decrypted with the title key from the ticket and written as
`<content id>.app`; each one is checked against its TMD hash, which is the only
thing that can tell you the key was right.  `XCREATE` re-encrypts them, updates
the TMD sizes and hashes, and fake signs the TMD — but only if a content
actually changed, so extracting and repacking an untouched WAD gives back the
original file byte for byte and a genuinely signed title keeps its signature.

### NKit

NKit shrinks a GameCube/Wii ISO by throwing away the disc's *junk data* — the
pseudo-random padding the SDK writes between files — and keeping just enough
per-gap metadata to regenerate it later.  `XCONVERT` restores the original
image:

```
wit XCONVERT game.nkit.iso game.iso
```

GameCube and Wii images are told apart automatically by the disc magic the NKit
header still carries.  The Wii path rebuilds each partition's file system,
regenerates the junk, re-derives the whole H0–H3 hash tree and re-encrypts every
group with the ticket's title key, then checks the finished image against the
CRC32 stored in the NKit header — so a successful restore is a byte exact one,
not merely a plausible one.  Restoring an image whose update partition was
removed at conversion time is refused, because putting it back needs a copy of
that partition this tool has no source for.

Note that `.nkit.gcz` — the form most NKit images are distributed in — is an
NKit stream wrapped in Dolphin's GCZ container, and `wit` cannot unwrap it.
`wit`'s GCZ reader decompresses the container and then validates the payload as
a GC/Wii disc, which an NKit stream deliberately is not, so it is rejected with
`WRONG FILE TYPE / GameCube or Wii ISO image expected` before any copy happens;
no flag (`--raw` included) changes that, because the rejection is in source
detection, not in the copy.  Strip the GCZ layer with any plain GCZ
decompressor first, then convert the `.nkit.iso`:

```
gcz-decompress game.nkit.gcz game.nkit.iso   # any GCZ tool; Dolphin's format, zlib blocks
wit XCONVERT   game.nkit.iso  game.iso
wit VERIFY     game.iso
```

### Riivolution ISO Builder (`wit RIIVOLUTION`)

`wit RIIVOLUTION` applies Riivolution mod XML packages and external replacement files directly to Wii disc images (WBFS, ISO, WDF, CISO, WIA) or extracted FST directories. It produces standalone patched disc images or patches FST folders in place, without needing SD card emulation or external python/batch scripts.

```bash
# Inspect a Riivolution mod XML (displays sections, options, choices, and patches)
wit RIIVOLUTION --info /path/to/mod.xml

# Test resolution of choices and patches against a base game without making changes
wit RIIVOLUTION --test game.wbfs /path/to/mod.xml

# Build a patched WBFS or ISO
wit RIIVOLUTION game.wbfs /path/to/mod.xml patched_game.wbfs
wit RIIVOLUTION game.iso  /path/to/mod.xml patched_game.iso

# Select custom options and choices
wit RIIVOLUTION --choice "Difficulty=Hard,Costumes=Alt" game.wbfs mod.xml patched.wbfs

# Enable all available choices or use XML defaults
wit RIIVOLUTION --all-choices game.wbfs mod.xml patched.wbfs
wit RIIVOLUTION --default-choices game.wbfs mod.xml patched.wbfs

# Interactive configuration mode (prompts for options, Game ID, and Title)
wit RIIVOLUTION --interactive game.wbfs mod.xml patched.wbfs

# Patch an extracted FST directory in place
wit RIIVOLUTION /path/to/extracted_fst /path/to/mod.xml

# Set custom Disc ID and Title for independent save files
wit RIIVOLUTION --id SMNE03 --name "Newer Super Mario Bros. Wii" game.wbfs mod.xml newer.wbfs

# Provide a custom or patched main.dol executable
wit RIIVOLUTION --dol /path/to/custom_main.dol game.wbfs mod.xml patched.wbfs

# Inject GCT / Gecko cheat codes into main.dol using wstrt
wit RIIVOLUTION --gct codes.gct game.wbfs mod.xml patched.wbfs
wit RIIVOLUTION --add-section codes.gct game.wbfs mod.xml patched.wbfs
```

#### Supported Riivolution Features
- **Full Riivolution XML Specification**: Full parsing of `<wiidisc>`, `<id>`, `<options>`, `<section>`, `<option>`, `<choice>`, `<patch>`, `<file>`, `<folder>`, `<savegame>`, and `<memory>` tags.
- **Patched DOL Handling & Executable Replacement**:
  - Automatically imports external DOL executables declared in `<file>` or `<folder>` tags (e.g. `disc="/sys/main.dol"` or `disc="main.dol"`).
  - Automatically discovers standalone `main.dol` in mod or SD root directories if present.
  - Supports explicit CLI executable overrides via `--dol <file>`.
- **GCT Cheat Code Injection (`--gct` / `--add-section`)**:
  - Seamlessly integrates with Wiimms SZS Tool (`wstrt`) to inject Gecko Code Tables (`.gct`) directly into `main.dol` as a new executable section.
  - Automatically detects `<gameid>.gct` or `codes/<gameid>.gct` in the mod folder if present.
- **DOL Executable Memory Patching**:
  - In-place virtual memory patching with `original` byte verification.
  - Pattern search and replace (`search="true"`) with configurable alignment strides.
  - Dynamic section creation: automatically allocates free DOL text/data section headers and appends payload code to `main.dol` for patches targeting high unmapped memory addresses.
  - Ocarina hook insertion: automatically scans for `blr` epilogues and patches branches to hook code.
- **Variable Substitution**: Automatic substitution of `{$__gameid}`, `{$__region}`, `{$__maker}`, and custom parameters defined in `<param>` tags or `<macros>`.
- **Filesystem Flexibility**:
  - Direct file replacements and folder merges with `resize`, `create`, `offset`, `fileoffset`, and `length` support.
  - Automatic external file resolution handling standard SD card folder structures (`/riivolution/...` and mod root folders).
  - Case-insensitive path lookup matching Wii filesystem naming conventions.
- **Container Formats**: Outputs directly to any format WIT supports (`.iso`, `.wbfs`, `.wdf`, `.ciso`, `.wia`, or extracted `.fst` folder).

### BrawlBuilder ISO Builder (`wit BRAWLBUILDER`)

`wit BRAWLBUILDER` (aliases `BRAWL-BUILDER`, `BRAWL`) is a native C implementation of the BrawlBuilder pipeline for Super Smash Bros. Brawl. It builds standalone modded disc images (`.wbfs`, `.iso`, etc.) or extracted FST directories from Gecko-based Brawl mods (such as Project M, Project+, Brawl-, PM Remix, and custom stage/fighter packs), without requiring external C# runtimes, Mono, or .NET.

```bash
# Test Brawl mod configuration without modifying files
wit BRAWLBUILDER --test brawl.wbfs /path/to/mod_folder/ output.wbfs

# Build a modded Brawl WBFS image
wit BRAWLBUILDER brawl.wbfs /path/to/mod_folder/ output.wbfs

# Interactive configuration mode (prompts for Game ID, Title, and Subspace removal)
wit BRAWLBUILDER --interactive brawl.wbfs /path/to/mod_folder/ output.wbfs

# Shrink final image by removing Subspace Emissary files (~5 GB saved)
wit BRAWLBUILDER --remove-sse brawl.wbfs /path/to/mod_folder/ output.wbfs

# Assign custom Game ID and Title (automatically patches Brawl's disc check)
wit BRAWLBUILDER --id PM3601 --name "Project M 3.6" brawl.wbfs /path/to/mod/ pm36.wbfs

# Explicitly specify GCT codes file and custom opening banner
wit BRAWLBUILDER --gct codes/RSBE01.gct --banner opening.bnr brawl.wbfs mod/ output.wbfs
```

#### Pipeline & Gecko Mod Compatibility
- **Automatic Mod Root & GCT Detection**: Intelligently locates game files under `pf/` or root, and auto-discovers `RSBE01.gct` under `codes/` or mod root.
- **GCT Compatibility Patching**:
  - Individual Stock Icons fix (prevents crashes on boot).
  - Alternate Stage Loader loop patch (reroutes stage loading from SD to disc).
  - Soundbank & SFX DVD loader patch (loads custom `.sawnd` audio files from disc instead of SD card).
- **Alternate Stage Preparation & Padding**:
  - Automatically identifies alternate stage definitions (`_[A-Z].pac`) and duplicates the corresponding base stage module (`module/st_*.rel` -> `module/st_*_*.rel`).
  - Pads base stage `.pac` files with zero-byte padding to match the size of their largest alternate stage, ensuring seamless in-game stage transitions.
  - Automatically cleans up redundant duplicate modules.
- **Subspace Emissary Removal (`--remove-sse`)**:
  - Strips all 840 single-player Subspace Emissary assets and cutscenes, drastically reducing final image size from ~7.5 GB to ~2.5 GB.
- **Native DOL Code Injection**:
  - Injects Gecko CodeHandler into a new text section at `0x80001800`.
  - Injects the patched GCT codes into a new data section at `0x80570000` (configurable via `--offset`).
  - Patches CodeHandler branch hooks (`0x80200984` and `0x80002778`) and security checks (`0x800042B8` and `0x803E9930`).
  - Automatically patches Brawl's internal disc ID check at `0x805A14B0` & `0x805A14B8` when a custom Game ID is used, bypassing the *"Please insert the Super Smash Bros. Brawl Game Disc"* screen.

> [!NOTE]
> Like BrawlBuilder, `wit BRAWLBUILDER` supports standard Gecko-based file-patching mods. Mods utilizing **BrawlEx** (e.g. Brawl- versions beyond 2.x.6) rely on dynamic character slot expansion routines requiring physical SD card hardware access and are not compatible with disc-based ISO loading.

### Anti-Piracy Patch Database (`wit PATCH`)

`wit PATCH` (alias `PAT`) applies known, documented anti-piracy neutralization patches to a Wii disc image (WBFS, ISO, WDF, CISO, WIA) or extracted FST directory. It reuses the same DOL memory-patch engine as `wit RIIVOLUTION` internally, so every patch is a verified, sourced set of RAM pokes rather than a guessed offset.

```bash
# List the known patch database, including unimplemented entries and why
wit PATCH --patch-list

# Apply all patches that match the source game's Disc ID automatically
wit PATCH game.wbfs patched.wbfs

# Select a specific patch by key
wit PATCH --patch metafortress game.iso patched.iso

# Test resolution without writing anything
wit PATCH --test game.wbfs

# Patch an extracted FST directory in place
wit PATCH /path/to/extracted_fst
```

#### Patch Database Status
- **Kirby's Return to Dream Land / Kirby's Adventure Wii — `metafortress`** (`SUKE01`/`SUKP01`/`SUKJ01`, USA/Europe/Japan): **Implemented.** Neutralizes the "MetaFortress" decoy-level anti-piracy trap using the 1391-1392 memory-poke patch set ported verbatim from Dolphin Emulator's own `GameSettings/SUK*.ini` "Bypass Metafortress [crediar]" patch, a community-verified, widely-distributed Gecko-style fix.
- **Wii "Error #001"**: **Not implemented.** Error 001 is raised by the IOS/DI trust-chain check outside of any per-game `main.dol` code, so there is no single disc-image byte patch that fixes it for a given game — the documented community fix is a correctly-signed cIOS / Trucha Bug fix at the system level, out of scope for a disc-image patch database.
- **Wii "Error #002"**: **Not implemented.** Error 002 is triggered by an IOS-version mismatch resolved system-side (loading the correct/patched IOS), not by a single fixed offset shared across games. No verified per-game address could be sourced, so no patch was added rather than guessing one.
- **New Super Mario Bros. Wii anti-piracy screen**: **Not implemented.** The BCA-check neutralization is not distributed as a standard Gecko code or Dolphin GameSettings patch, so no sourced, verified address/opcode set could be found for it; it needs dedicated reverse engineering per game revision before it can be added.

Run `wit PATCH --patch-list` at any time for the live, in-tool version of this table, including full source citations for each entry.

---

## Documentation & Guides

- **[Command Reference & New Tools Guide](docs/COMMANDS.md)**: Complete guide to all new commands (`XINFO`, `XEXTRACT`, `XCREATE`, `XCONVERT`, `RIIVOLUTION`, `BRAWLBUILDER`), options, and supported container formats.
- **[Official Wiimms ISO Tools Documentation](https://wit.wiimm.de/)**: Original WIT command reference, parameters, and documentation.

---

<dl>
<dt>Note:</dt>
<dd>
This is only a copy of Wiimms private SVN repository.
Only official releases are exported to <i>GitHub</i>.
Therefor merge requests can not imported directly and must be included manually.
</dd>

<dt>License:</dt>
<dd>
This program is free software;
you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation;
either version 2 of the License, or (at your option) any later version.

See file project/gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt for details.
</dd>
</dl>

*Wiimm, 2020-08-22*
