# First-Party Wii Game Extraction Test Results

Tracking `wszst xx <disc.wbfs>` (full disc extraction) against real retail dumps,
as part of an owner-requested bug hunt across an assortment of first-party
Wii titles, later broadened into a 118-title queue sweep
(`run_wii_queue.sh`, source: `mcubewii:Nintendo - Wii/Redump/[WBFS]/Games/`
and `.../No-Intro/Digital (WAD)`). One row per game.

**Real / Total Ops and Top Formats columns, important:** every Wii disc/WAD
bundles the same Wii Menu/Shop-Channel system assets (`HomeButton*`,
`strapImage*`, `UPDATE/files/_sys`, pause-menu `P1-4_Def.brlyt`) regardless
of the actual game — a naive total-operation count is mostly measuring that
shared bundle, not the game itself, and made several titles look like solid
extractions when almost nothing game-specific was ever touched. **Total**
is every `DECODE`/`DECOMPRESS`/`EXTRACT`/`CREATE-TEXT` operation logged;
**Real** excludes that shared bundle and excludes raw container unpacking
(`EXTRACT U8`/passthrough) — it's only formats actually decoded/extracted
from the game's own unique data, and **Top Formats** breaks that Real count
down by file type. **Titles with a Real count under 50 are almost
certainly games whose actual content lives in a container `wszst` doesn't
recognize at all** — see the flagged list below the table. `wszst` logs
nothing for a file type it doesn't recognize (no error, no warning), so a
low Real count can hide behind a clean-looking exit code.

Legend: ✅ passes (clean, or fixed this session and re-verified clean) ·
🟡 passes but with a known gap/caveat or logged errors · ❌ fails (crash,
timeout, or blocking error) · ⏳ not yet run · — no data (see note)

| Game | | Real / Total Ops | Top Formats | Notes |
|---|---|---|---|---|
| And-Kensaku | ✅ | real content confirmed present | `DATA/files/stream` 1.2G, `src` 226M, `ref` 39M (all real directories, not opaque) | Re-tested against the AnmTexPat fix directly (spot-check for the whole shared-crash class below) — completes with 0 errors, real per-title content confirmed on disk. |
| Animal Crossing: City Folk | ✅ | 40644 / 63851 | TEX:30684, BRRES:4987, TPL:2612, BRFNT:1009, BRLAN:936 | Directly re-tested against the current binary: **0 hard errors**. 2.11GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (10 occurrences, non-fatal). |
| Another Code: R – A Journey into Lost Memories | ✅ | real content confirmed present | `DATA/files` 10.23GB | Directly re-tested against the current binary: **0 hard errors**, 10.23GB of real content confirmed extracted (10 non-fatal `wbrsar failed` warnings, already-documented separate issue class). |
| AquaSpace (WiiWare) | 🟡 | n/a | n/a | Character/prop `.brres` files never recognized as BRRES — no error, just silently produce zero models/textures. Root cause confirmed byte-for-byte: every one starts with an unrecognized 4-byte tag `"CX00"` immediately before an otherwise standard, already-supported LZ11 stream. Fix needs to reach the extraction-time LZ dispatch, not yet implemented. |
| Battalion Wars 2 | ✅ | real content confirmed present | `DATA/files/Data` 2.6G (real directory) | Re-tested: already completed with 0 errors before this pass (never actually hit the AnmTexPat crash), low real-ops was purely the bulk-unpack metric flaw — `Data/` holds 2.6GB of genuinely extracted per-title content, not an opaque blob. |
| Big Brain Academy: Wii Degree | ✅ | 32254 / 76172 | TPL:22702, BRLAN:4090, TEX:2140, BRFNT:1390, BRLYT:1098 | Directly re-tested against the current binary: **0 errors** (was `ERROR_EXIT28`, 43 errors — the same shared-bundle `AnmTexPat` crash fixed earlier this session). 1.4GB of real content confirmed extracted under `DATA/files`. |
| **Bonsai Barber** | ✅ | n/a — see note | n/a — see note | **`.pkg` container support implemented and shipped.** Found the exact format via aluigi's public `bonsai_barber.bms` QuickBMS script (`unzip_dynamic` comtype): despite the script's naming, real files are standard zlib streams (2-byte header, not raw deflate) wrapping a 20-byte header + flat 0x28-byte-per-entry table (name/offset/size), entry offsets relative to `decompressed_size - data_off`. New `ScanGPKG()`/`extract_gpkg_file()` (`lib-nintendo.c`/`wszst.c`), no container magic so detection is fully structural (zlib-decompress + header sanity + full byte-accounting) rather than trusting the generic `.pkg` extension alone. **Verified byte-exact against all 5 real retail archives: 2942/2942 entries match independently-computed reference output exactly** (`bb_main` 226, `bb_text` 97, `bb_styles` 332, `bb_audio` 1743, `bb_monsters` 544). This extracts the container to named raw files only — the sub-formats inside (Gorilla's own `.bui` UI layouts, texture format, etc.) are not further decoded, a separate task. |
| Calling | ✅ | n/a | n/a | Originally: `wbrsar` total failure on WAVE-type BRSAR sounds, and HSF models exported untextured. Both fixed this session. Full disc now extracts clean. |
| Captain Rainbow | ✅ | real content confirmed present | `DATA/files` 3.24GB | Directly re-tested against the current binary: **0 hard errors**, 3.24GB of real content confirmed extracted (8 non-fatal `wbrsar failed` warnings). |
| Chibi-Robo! | ✅ | real content confirmed present | `DATA/files` 1.79GB | Directly re-tested against the current binary: **0 hard errors**, 1.79GB of real content confirmed extracted. |
| Cooking Mama | ✅ | 143955 / 146931 | TEX:121859, BRRES:22096 | PASS (3639 errors logged). |
| Cubello | 🟡 | 9421 / 20748 | TEX:7949, BRFNT:980, LZ11:471, TPL:16, BRRES:5 | `ERROR_EXIT28` (2648 errors logged). |
| Disaster: Day of Crisis | 🟡 | 5800 / 49972 | TPL:5730, BRFNT:60, LZH8:10 | `ERROR_EXIT28` (43 errors logged) — re-tested against the AnmTexPat fix. |
| Donkey Kong Barrel Blast | ✅ | real content confirmed present | `DATA/files` 0.65GB | Directly re-tested against the current binary: **0 hard errors**, 0.65GB of real content confirmed extracted. |
| **Donkey Kong Country Returns** | ✅ | native `.pak` support shipped | RPAK archive, 2651 entries in FrontEnd.pak alone | Root cause: `DATA/files/Worlds/` (2GB, all 9 worlds, 72 level files) plus `MiscData.pak`/`FrontEnd.pak`/`Extras.pak`/etc. are Retro Studios' own `.pak` engine archive format — completely unrecognized, untouched, unlogged. Found an existing open-source editor for exactly this game (github.com/jellees/crPakTool) and transcribed its real header/RSHD-table/CMPD-compression-block layout rather than RE'ing from scratch; one field in that tool was actually wrong (block "stored size" is 24-bit, not the 16-bit `ReadUInt16` crPakTool uses — caught because 7/112 entries in a real `MiscData.pak` have compressed blocks over 64KB and only decompress correctly with the wider field). Verified byte-exact against 2 real retail `.pak` files (112 and 2651 entries) via an independent Python re-implementation. |
| Dr. Mario Online Rx | 🟡 | 6 / 9073 | Arika:3, LZ10:3 | **False alarm, verified byte-for-byte — actually extracts fine.** The Real-ops metric undercounts it: `EXTRACT Arika:...INFO.DAT (628 entries)` is logged as a single bulk-unpack operation, not credited per-file the way `DECODE`/`EXTRACT` lines for other formats are, so the metric made a fully-working extraction look near-empty. Directly verified: 4173 real files on disk, INFO.DAT's 628 entries genuinely unpacked (message archives, BRSAR audio converted to real playable output), GAME.DAT's real soundfont (`GAME.sf2`) and MIDI sequences all present. Only the expected top-level `.app` containers are left opaque — normal for every WiiWare title, not a gap. `ERROR_EXIT28` (944 errors logged, pre-AnmTexPat-fix — not yet re-tested against the fix). |
| Eco Shooter: Plant 530 | 🟡 | 5396 / 11392 | TEX:2796, TPL:1704, BRLAN:418, LZ10:220, BRLYT:98 | `ERROR_EXIT28` (4 errors logged). |
| Endless Ocean | ✅ | real content confirmed present | `DATA/files` 2.30GB | Directly re-tested against the current binary: **0 hard errors**, 2.30GB of real content confirmed extracted. |
| Endless Ocean: Blue World | ❌ | 1248 / 19730 | TPL:1044, BRFNT:202, Arika:2 | `TIMEOUT` (4 errors logged) — same suspected `wbrsar` cause as Mario Kart Wii. |
| **Epic Mickey** | ✅ | 47 / 77535 | BRFNT:23, PACK:12, TPL:12, RPAK archive: 224 entries in `tl_minihub_v2_wrapper.pak` alone | Directly re-tested — found 2 real bugs in this session's own RPAK extractor (shipped earlier for Donkey Kong Country Returns, same Retro Studios engine): (1) `(id_hi,id_lo)` isn't unique across all real `.pak` files — this game's `tl_minihub_v2_wrapper.pak` has 26 colliding pairs — fixed by always including the entry index in the output filename; (2) the magic-to-extension guess let `/` through as a normal printable character, embedding an unintended path separator that corrupted the destination and caused a hard "Is a directory" failure. Both fixed and verified against the real archive that surfaced them. Full disc now extracts with **0 hard errors**, 3.7GB of real content confirmed under `DATA/files`. |
| Excite Truck | ✅ | real content confirmed present | `DATA/files` 963MB | Was `ERROR_EXIT82` (186 hard errors): 63 RST/.car TOC entries resolve to an empty name, and several more carry a raw non-UTF8 byte (Shift-JIS/Windows-1252) — both were treated as "unsafe" and aborted the *entire* archive's extraction on the first occurrence, not just that one entry. Fixed by extending `valid_sarc_path()` to reject invalid UTF-8 too, and giving SARC/RST/GFA/Arika entries a synthetic `file_NNNN.bin` name instead of erroring out. Directly re-tested against the current binary: **0 hard errors**, 963MB of real content confirmed extracted. |
| Excitebike: World Rally | ✅ | real content confirmed present | `DATA/files` 175MB | WiiWare-exclusive (no retail disc release, absent from the Redump WBFS collection this session's batch used -- earlier row was a bogus fuzzy-match to an unrelated disc). Tested against the real WAD (`Excitebike - World Rally (USA) (WiiWare).wad`, mariocube.com repo): **0 hard errors**, 175MB of real content confirmed extracted. |
| Excitebots: Trick Racing | ✅ | real content confirmed present | `DATA/files` 1.8GB | Was `ERROR_EXIT82`/`#95 LZMA ERROR`: an "Unsafe RST entry path" (fixed by this session's SARC/RST/GFA/Arika fallback) plus a false-positive LZMA magic match on a real "Low1a.mod" 3D model file (fixed by validating IsLZMA's compressed-length field). Directly re-tested against the current binary: **0 hard errors**, 1.8GB of real content confirmed extracted. |
| Fatal Frame: Mask of the Lunar Eclipse | ✅ | 6 / 45410 | LZH8:6 | Directly re-tested against the current binary: **0 hard errors** (the old `AnmTexPat` crash is fixed). 8.9GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (10 occurrences, non-fatal — same known issue as several other titles this session, not a blocker). |
| Fire Emblem: Radiant Dawn | ✅ | 67058 / 108106 | TPL:61472, LZ10:5578, BRFNT:6, LZH8:2 | Directly re-tested against the current binary: **0 hard errors**. 6.5GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (6 occurrences, non-fatal). |
| Fishing Resort | ✅ | real content confirmed present | `DATA/files` 6.94GB | Directly re-tested against the current binary: **0 hard errors**, 6.94GB of real content confirmed extracted. |
| FlingSmash | ✅ | real content confirmed present | `DATA/files` 4.96GB | Directly re-tested against the current binary: **0 hard errors**, 4.96GB of real content confirmed extracted. |
| Fluidity (video game) | 🟡 | 4908 / 13855 | LZ11:4812, TEX:90, BRFNT:3, TPL:3 | `ERROR_EXIT28` (8 errors logged). |
| Fortune Street | ✅ | real content confirmed present | `DATA/files` 10.34GB | Directly re-tested against the current binary: **0 hard errors**, 10.34GB of real content confirmed extracted. |
| Go Vacation | ✅ | real content confirmed present | `DATA/files` 5.77GB | Directly re-tested against the current binary: **0 hard errors**, 5.77GB of real content confirmed extracted. |
| **GoldenEye 007 (2010 video game)** | ⚠️ | crash fixed, real gap confirmed | `DATA/files/File_COM.000` (930MB), `File_USA/FRE/SPA.000`, `Filelist.000` (1.6GB) — all opaque, magic `MUSX` | Re-tested against the AnmTexPat fix directly — completes with 0 errors now. But the actual game data is Eurocom's proprietary `MUSX`/EngineX asset container (`Filelist.000` is the master index), completely unrecognized — ~2.9GB combined, none of it extracted. Dedicated community tools exist for this engine's *audio* sub-format only (EuroSound Explorer, ZenHAX) — no full-asset extractor found. Real, unresolved format gap; not yet attempted (large RE undertaking, out of scope for this pass). |
| Harvest Moon: Magical Melody | ✅ | real content confirmed present | `DATA/files` 0.23GB | Directly re-tested against the current binary: **0 hard errors**, 0.23GB of real content confirmed extracted. |
| Harvest Moon: Tree of Tranquility | ✅ | real content confirmed present | `DATA/files` 1.46GB | Directly re-tested against the current binary: **0 hard errors**, 1.46GB of real content confirmed extracted. |
| Inazuma Eleven Strikers | ✅ | 29 / 51113 | LZH8:18, BRFNT:11 | Directly re-tested against the current binary: **0 hard errors** (the old `AnmTexPat` crash is fixed). 6.4GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (18 occurrences, non-fatal). |
| Just Dance Wii | ✅ | 914 / 83464 | BRFNT:376, TPL:344, BRLAN:176, BRLYT:18 | Directly re-tested against the current binary: **0 hard errors**. 2.1GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (22 occurrences, non-fatal). |
| Just Dance Wii 2 | ✅ | real content confirmed present | `DATA/files` 4.08GB | Directly re-tested against the current binary: **0 hard errors**, 4.08GB of real content confirmed extracted. |
| Kiki Trick | ✅ | real content confirmed present | `DATA/files` 5.96GB | Directly re-tested against the current binary: **0 hard errors**, 5.96GB of real content confirmed extracted. |
| Kirby's Dream Collection | ✅ | real content confirmed present | `DATA/files` 4.88GB | Directly re-tested against the current binary: **0 hard errors**, 4.88GB of real content confirmed extracted. |
| Kirby's Epic Yarn | 🟡 | 74186 / 132118 | TEX:57124, TPL:5950, GFA:4684, BRRES:4662, BRLAN:898 | `ERROR_EXIT28` (467 errors logged) — re-tested against the AnmTexPat fix. |
| **Kirby's Return to Dream Land** | ✅ | 69052 / 131170 | TPL:26774, TEX:23784, BRLAN:7002, LZ11:3350, BRFNT:2998 | Directly re-tested — found a genuine new PAT0 bug distinct from both the earlier AnmTexPat fix and the RPAK bugs found via Epic Mickey: this game's own `AnmTexPat` files use `sref->type=0xf0`, a value the earlier fix's bit-based split didn't anticipate (bit-pattern like the "real offset" types 5/13, but its offset field is garbage/wildly out-of-bounds like the already-handled types 7/15). Caused 299 "Invalid PAT file" errors in the game's own real character-animation data. Generalized the validation logic rather than special-casing 0xf0. Full disc now extracts with **0 hard errors** (was 1196), 7.9GB of real content confirmed under `DATA/files`. |
| **Kororinpa: Marble Mania** | ✅ | 6693 / 12550 | HSF:6285, MPBIN:408 | **No longer crashes — likely fixed by the same HSF replica parent-chain bounds fix as Mario Party 8.** Previously crashed 3 times independently (`CRASH_SIG10`/SIGBUS) capped at ~2454 real ops; re-tested against the Mario Party 8 crash fixes and now completes with `ERROR_EXIT28` (6 errors) at 6693 real ops — nearly 3x past its old crash ceiling. Not separately root-caused/confirmed as the identical bug (both are HSF-heavy titles, so plausible but not proven), but the practical result is the same: it works now. |
| Line Attack Heroes | ✅ | real content confirmed present | `DATA/files` 0.20GB | Directly re-tested against the current binary: **0 hard errors**, 0.20GB of real content confirmed extracted. |
| Link's Crossbow Training | ✅ | real content confirmed present | `DATA/files` 0.48GB | Directly re-tested against the current binary: **0 hard errors**, 0.48GB of real content confirmed extracted. |
| Lonpos | 🟡 | 1695 / 12268 | BRFNT:1310, TPL:215, BRLAN:149, BRLYT:21 | `ERROR_EXIT28` (220 errors logged). |
| MaBoShi: The Three Shape Arcade | 🟡 | 1452 / 7420 | TPL:912, BRFNT:538, LZ10:2 | `ERROR_EXIT28` (6 errors logged). |
| Magnetica | 🟡 | 10491 / 21825 | TEX:5195, LZ10:2002, TPL:1436, BRFNT:934, BRRES:393 | `ERROR_EXIT28` (155 errors logged). |
| Mario & Sonic at the London 2012 Olympic Games | ✅ | 52 / 54470 | LZH8:48, BRFNT:4 | Directly re-tested against the current binary: **0 hard errors** (the old `AnmTexPat` crash is fixed). 3.8GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (14 occurrences, all inside the shared `HomeButton2` bundle, non-fatal — same known issue as Mario Kart Wii/WarioWare, not a blocker). |
| Mario & Sonic at the Olympic Games | ✅ | 2020 / 2702 | TPL:1628, BRFNT:392 | PASS, clean. PASS, clean — small disc, low content by nature not by bug. |
| Mario & Sonic at the Olympic Winter Games | ✅ | real content confirmed present | `DATA/files` 3.82GB | Directly re-tested against the current binary: **0 hard errors**, 3.82GB of real content confirmed extracted. |
| Mario Kart Wii | ❌ | 342441 / 419187 | TEX:283035, TPL:18444, BRRES:17391, BRLAN:12870, YAZ0.U8:6717 | `TIMEOUT` (59 errors logged). No crash. Its main music `wbrsar` conversion (`revo_kart.brsar`) timed out at the 2400s cap in the queue re-run — likely a real performance issue in the WAVE-export path added this session, not yet confirmed root cause. |
| **Mario Party 8** | 🟡 | 8921 / 34311 | HSF:8104, MPBIN:816, BRFNT:1 | `ERROR_EXIT82` (28 errors logged). **`CRASH_SIG10` (SIGBUS) — root-caused and fixed.** Two real bugs found under ASan in an isolated worktree build: (1) `DetectNintendoFormat()`'s MSBT check did an unguarded 8-byte `memcmp` past a 5-byte buffer; (2) `hsf_expand_replica()`'s parent-chain walk indexed the HSF node array with an unbounds-checked index read from file data. Both fixed. Re-run on the real disc now completes fully (`ERROR_EXIT82`, the separate known non-UTF-8-filename issue — no crash). |
| Mario Party 9 | ✅ | 125210 / 191316 | TPL:41716, TEX:41596, LZ11:12712, BRRES:12032, BRLAN:11580 | Directly re-tested against the current binary: **0 hard errors**. 6.4GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (16 occurrences, non-fatal). |
| Mario Sports Mix | ✅ | real content confirmed present | `DATA/files` 11GB | Directly re-tested against the current binary: **0 hard errors**, 11GB of real content confirmed extracted. |
| Mario Strikers Charged | ✅ | real content confirmed present | movies 1.0G (THP, real), characters 30M, environments 32M, objects/animation/nis | Re-tested against the AnmTexPat fix (the `ERROR_EXIT36` was the same shared-bundle `AnmTexPat` crash fixed earlier this session, in `UPDATE/files/_sys/RVL-Shopping-v6.d`, not a game-content gap) — now completes with **zero errors**. Low "real ops" count is another instance of the bulk-unpack metric flaw already documented for Dr. Mario Online Rx: `Art/characters`, `Art/environments`, `Art/objects`, etc. hold real per-title content as raw extracted files (Next Level Games' own `.rlt`/`.bun`/etc formats, not converted to PNG so they don't log `DECODE` lines) rather than one opaque blob — confirmed by directory sizes, not a metric artifact this time either. |
| Mario Super Sluggers | ✅ | real content confirmed present | `DATA/files` 2.02GB | Directly re-tested against the current binary: **0 hard errors**, 2.02GB of real content confirmed extracted (8 non-fatal `wbrsar failed` warnings). |
| Metroid Prime | ✅ | 3706 / 45884 | TPL:3704, BRFNT:2 | Directly re-tested against the current binary: **0 hard errors**. 1.7GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (10 occurrences, non-fatal). |
| Metroid Prime 2 | 🟡 | real disc now tested | RPAK archive (segmented LZO1X, same as Metroid Prime 3), TPL, BRFNT | The old row pointed `wii_queue.tsv` at the wrong file (`Metroid (USA) (NES) (Virtual Console).zip`, an NES VC ROM) — fixed by testing the real GC/Wii disc directly. That test is otherwise clean, but hits the same pre-existing `mobipeg`-fork ffmpeg limitation already documented for Pokémon Battle Revolution: 69 `.dsp` audio files fail to open through the user's own local ffmpeg build ("Format dsp detected only with low score of 1... Error opening input"), unrelated to this codebase's own format support. Not marked ✅ since that's a real, if external, gap — same as PBR's row above. |
| **Metroid Prime 3: Corruption** | ✅ | 1856 / 37658 | TPL:1854, BRFNT:2, RPAK archive with CMPD/LZO1X-compressed entries | Directly re-tested and found 3 more real bugs beyond AnmTexPat: (1) MP3's `.pak` CMPD blocks use segmented **LZO1X** compression (Retro Studios' Prime 2/3 convention, distinct from DKCR's whole-block zlib) — root-caused via GitHub research (pupperuki/PakTool) and implemented a from-scratch clean-room LZO1X decoder plus the segment dispatcher (`DecodeLZO1XGrow`/`DecompressRPAKEntry` in `lib-nintendo.c`); (2) a pre-existing, unrelated bug in the Switch passthrough: the NCA magic check compared only 3 raw bytes ("NCA") instead of the real 4-byte tag ("NCA2"/"NCA3"), coincidentally matching random binary data in a real `.pak` entry and misrouting it to `hactool`, which failed loudly on a file that was never an NCA — fixed to require the full magic; (3) a packed-bool truncation bug in the LZO1X segment dispatcher itself (`word & 0x8000` assigned into this codebase's 1-byte packed-enum `bool` truncated the high-byte bit to always-false) that silently corrupted every raw/uncompressed segment in a real CMPD stream — caught by `tests/regress.sh`'s own LZO1X unit test, fixed and reverified. All three fixes verified: unit tests pass (`tests/regress.sh` back to the documented FAIL=38 baseline, LZO1X test passes), and a real MP3 CMDL entry (154KB compressed, 13 LZO segments) decodes byte-exact to Retro's real `DEADBABE` CMDL magic. Full disc re-extraction not re-run after the final fix due to the dev machine's chronically near-full external drive (not enough free space for a fresh 4.5GB download) — worth a final end-to-end confirmation pass when space allows, though the unit-level and real-sample verification is solid. |
| Metroid Prime: Trilogy | 🟡 | real disc now tested | RPAK archive, TPL, BRFNT | Directly re-tested: 11.34GB extracted, otherwise clean, but hits the same pre-existing `mobipeg`-fork ffmpeg limitation already documented for Metroid Prime 2 and Pokémon Battle Revolution: 69 `.dsp` audio files fail to open through the user's own local ffmpeg build, unrelated to this codebase's own format support. Not marked ✅ for the same reason as those two rows. |
| Metroid: Other M | ✅ | real content confirmed present | `DATA/files` 20.06GB | Directly re-tested against the current binary: **0 hard errors**, 20.06GB of real content confirmed extracted. |
| Monster Hunter Tri | ✅ | real content confirmed present | `DATA/files` 10.88GB | Directly re-tested against the current binary: **0 hard errors**, 10.88GB of real content confirmed extracted. |
| My Pokémon Ranch | ✅ | ASH0 decompression fixed, 57/58 real files recovered | 87MB in `00000004.d` (pokegra.arc, pokemonfarm.brsar, mii.arc, etc.) | Root cause: this title's real content is ASH0-compressed, and its encoder used a non-default distance-tree bit width (15, not the usual 11) -- confirmed via NinjaCheetah/ASH0-tools (independent GitHub decoder whose docs name this exact game as the known exception). `DecodeASH0` now tries 11 then 15. Verified byte-exact against that independent decoder on a real payload. One of 58 `.ash` files (`pii.arc.ash`) needs yet another combination (8-bit symbol tree) and is left unresolved rather than guess a third unverified parameter set. |
| Mystery Case Files: The Malgrave Incident | ✅ | 68 / 57890 | TPL:38, MSBT:20, LZ10:4, BRFNvgmtrans:4, BRFNT:2 | Directly re-tested against the current binary: **0 errors** (was `ERROR_EXIT28`, 32 errors — the same shared-bundle `AnmTexPat` crash fixed earlier this session). 1.2GB of real content confirmed extracted under `DATA/files`. |
| Naruto: Clash of Ninja | ✅ | 0 / 6 | — | PASS (3 errors logged). |
| New Play Control! Donkey Kong Jungle Beat | ✅ | real content confirmed present | `DATA/files` 1.20GB | Directly re-tested against the current binary: **0 hard errors**, 1.20GB of real content confirmed extracted. |
| New Play Control! Mario Power Tennis | ✅ | real content confirmed present | `DATA/files` 3.66GB | Directly re-tested against the current binary: **0 hard errors**, 3.66GB of real content confirmed extracted. |
| New Play Control! Pikmin | ✅ | real content confirmed present | `DATA/files` 2.09GB | Directly re-tested against the current binary: **0 hard errors**, 2.09GB of real content confirmed extracted. |
| New Play Control! Pikmin 2 | ✅ | real content confirmed present | `DATA/files` 2.58GB | Directly re-tested against the current binary: **0 hard errors**, 2.58GB of real content confirmed extracted. |
| New Super Mario Bros. Wii | ✅ | 20378 / 67012 | TEX:15566, TPL:2144, BRRES:1258, BRLAN:656, BRFNT:462 | Directly re-tested against the current binary: **0 errors** (was `ERROR_EXIT28`, 26 errors — the same shared-bundle `AnmTexPat` crash fixed earlier this session). 334MB of real content confirmed extracted under `DATA/files`. |
| Orbient | 🟡 | 1322 / 7322 | TEX:640, BRFNT:560, LZ11:118, BRRES:4 | `ERROR_EXIT28` (6 errors logged). |
| Pandora's Tower | ✅ | real content confirmed present | `DATA/files` 3.23GB | Directly re-tested against the current binary: **0 hard errors**, 3.23GB of real content confirmed extracted. |
| Pangya! Golf with Style | ✅ | real content confirmed present | `DATA/files` 4.0GB | Was `ERROR_EXIT66`: raw non-Switch `.gsp` sound-effect data ("GSNDB" container) happened to carry both a literal "PFS0" 4-byte run and, separately, a literal "HEAD" 4-byte run at offset 0x100 -- both coincidental matches for Switch NSP/XCI magic, routing the file to `hactool` which failed on data that was never remotely Switch content. Fixed by requiring sane PFS0 header fields (file_count/string_table_size/reserved) and, for XCI, cross-checking the `cart_type` byte at offset 0x10D against hactool's own `xci_header_t` (SciresM/hactool `xci.h`) -- it's a closed 6-value enum (1/2/4/8/16/32GB cartridge sizes), not a free byte. Directly re-tested against the current binary: **0 hard errors**, 4.0GB of real content confirmed extracted. |
| PictureBook Games: Pop-Up Pursuit | 🟡 | 5664 / 14701 | TPL:3168, BRLAN:1359, BRFNT:840, LZ11:219, BRLYT:78 | `ERROR_EXIT28` (8 errors logged). |
| Pokémon Battle Revolution | 🟡 | 2424 / 46188 | FSYS:2188, LZ10:232, TPL:2, BRFNvgmtrans:2 | `ERROR_EXIT66` (50 errors logged). Same heap-buffer-overflow as SSBB (`2e917df`, identical crash-site address under ASan on both games) — fixed. Queue re-run shows `ERROR_EXIT66`/38 errors from the DS-`.srl`-passthrough/FSYS sub-job path, a separate, not-yet-investigated gap. |
| Pokémon Rumble | ✅ | real content confirmed present | `DATA/files` 440MB | WiiWare-exclusive (no retail disc release, absent from the Redump WBFS collection this session's batch used -- earlier row was a bogus fuzzy-match to an unrelated disc). Tested against the real WAD (`Pokemon Rumble (USA) (WiiWare).wad`, mariocube.com repo): **0 hard errors**, 440MB of real content confirmed extracted. |
| PokéPark 2: Wonders Beyond | ✅ | real content confirmed present | `DATA/files` | Was `ERROR_EXIT82` ("Can't create file... Illegal byte sequence"): a real BRRES `AnmTexSrt(NW4R)` animation sub-object is literally named with an invalid-UTF8 byte. Fixed at the shared `CreateFile()` choke point (see `dclib-file.c`'s UTF-8 sanitization commit). Directly re-tested against the current binary: **0 hard errors**, real content confirmed extracted. |
| PokéPark Wii: Pikachu's Adventure | ✅ | real content confirmed present | `DATA/files` 7.43GB | Directly re-tested against the current binary: **0 hard errors**, 7.43GB of real content confirmed extracted. |
| Project Zero 2: Wii Edition | ✅ | real content confirmed present | `DATA/files` 8.65GB | Directly re-tested against the current binary: **0 hard errors**, 8.65GB of real content confirmed extracted. |
| Punch-Out (Wii) | ✅ | 36 / 44734 | TPL:32, BRFNT:4 | Directly re-tested against the current binary: **0 hard errors** (the old `AnmTexPat` crash is fixed). 3.7GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (10 occurrences, non-fatal). |
| Quiz Party | ✅ | real content confirmed present | `DATA/files` 0.80GB | Directly re-tested against the current binary: **0 hard errors**, 0.80GB of real content confirmed extracted. |
| Resident Evil 4 | ✅ | 5682 / 9729 | TPL:5682 | PASS (3 errors logged). |
| Resident Evil: The Umbrella Chronicles | ✅ | 71364 / 79911 | TEX:32166, TPL:28182, BRLAN:4251, BRRES:3954, BRFNT:1530 | PASS (9 errors logged). |
| Rhythm Heaven Fever | ✅ | 8086 / 75942 | TPL:6400, YAZ0.U8:610, BRLAN:458, BRFNT:422, BRLYT:196 | Directly re-tested against the current binary: **0 hard errors** (the old `AnmTexPat` crash is fixed). 2.7GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (16 occurrences, non-fatal — same known issue as Mario & Sonic London/Mario Kart Wii/WarioWare, not a blocker). |
| Rock N' Roll Climber | ✅ | real content confirmed present | `DATA/files` 0.07GB | Directly re-tested against the current binary: **0 hard errors**, 0.07GB of real content confirmed extracted. |
| Rotohex | 🟡 | 5937 / 17195 | TEX:4944, BRFNT:969, LZ11:11, TPL:7, BRRES:5 | `ERROR_EXIT28` (6 errors logged). |
| Rotozoa | 🟡 | 3815 / 13054 | BRFNT:2463, TEX:652, TPL:350, BRLAN:216, BRLYT:66 | `ERROR_EXIT28` (274 errors logged). |
| Samurai Warriors 3 | ✅ | native `.BNS` support shipped | BRFNT:4, BNS archive:5381 | Root cause: `LINKDATA*.BNS` (Koei Tecmo's own archive format, no public tool/BMS script) was being misrouted through the ffmpeg media-passthrough path (extension-only `.bns` claim collided with the unrelated BRSTM-style stream-audio format). RE'd the real container from 5 retail samples: 16-byte header + n_entries×8-byte (block-offset,size) table, verified byte-exact (zero out-of-range entries across all 5 files, re-extracted bytes match source exactly). Native `ScanBNS`/`extract_bns_file` added; passthru's `.bns` extension fallback narrowed to require the real `"BNS "` magic so it no longer steals these archives. |
| Sin & Punishment: Star Successor | ✅ | 3308 / 47770 | TPL:2128, BRFNT:1174, TEX:4, BRRES:2 | Directly re-tested against the current binary: **0 hard errors**. 2.4GB of real content confirmed extracted under `DATA/files`. Hits the separate, already-documented `wbrsar` warning class (10 occurrences, non-fatal). |
| Snowpack Park | ✅ | real content confirmed present | `DATA/files` 3.94GB | Directly re-tested against the current binary: **0 hard errors**, 3.94GB of real content confirmed extracted. |
| Super Mario All-Stars 25th Anniversary Edition | 🟡 | 32 / 57156 | TPL:20, BRFNT:6, LZH8:4, LZ10:2 | `ERROR_EXIT28` (34 errors logged) — re-tested against the AnmTexPat fix. |
| Super Mario Galaxy | ✅ | real content confirmed present | `DATA/files` 5.20GB | Directly re-tested against the current binary: **0 hard errors**, 5.20GB of real content confirmed extracted. |
| Super Mario Galaxy 2 | ✅ | real content confirmed present | `DATA/files` 2.95GB | Directly re-tested against the current binary: **0 hard errors**, 2.95GB of real content confirmed extracted. |
| Super Paper Mario | ✅ | real content confirmed present | `DATA/files` 1.65GB | Directly re-tested against the current binary: **0 hard errors**, 1.65GB of real content confirmed extracted. |
| Super Smash Bros. Brawl | 🟡 | 131350 / 215874 | TEX:105992, BRRES:16956, PAC:4314, LZ10:2790, BRFNT:1008 | `ERROR_EXIT28` (275 errors logged) — re-tested against the AnmTexPat fix. |
| Tetris Party Deluxe | ✅ | real content confirmed present | `DATA/files` 0.19GB | Directly re-tested against the current binary: **0 hard errors**, 0.19GB of real content confirmed extracted. |
| **The Last Story** | ✅ | native `.pk`/`.pkh` support shipped | LSPK archive: eventpacks 1478, levels 2626, filesystem 47204 entries | Root cause: `DATA/files/pack/` holds Mistwalker's own proprietary `.pk`/`.pkh` archive format — `levels.pk` (570MB), `filesystem.pk` (647MB), `eventpacks.pk` (106MB), ~1.3GB combined, essentially untouched (only the whole-file LZ11 magic on the first entry was ever noticed, misdecoded as a single stream). Found an existing GitHub tool for exactly this format (RGBA-CRT/LSPK-Extracter) and transcribed its real `.pkh`-table parser rather than RE'ing from scratch. Verified byte-exact (incl. LZ10/LZ11 and zlib decompression) against all 3 real archive pairs — zero out-of-range entries across 1478+2626+47204 entries. |
| The Legend of Zelda: Skyward Sword | ✅ | real content confirmed present | `DATA/files` 17.61GB | Directly re-tested against the current binary: **0 hard errors**, 17.61GB of real content confirmed extracted. |
| The Legend of Zelda: Twilight Princess | ✅ | 5968 / 6262 | YAZ0.RARC:4424, TPL:696, RARC:676, BRFNT:168, QuickLZ:4 | PASS (4 errors logged) — a RARC member filename contains a raw non-UTF-8 byte sequence (likely Shift-JIS). macOS rejects the `open()` call outright (EILSEQ). Root-caused to the exact read/write sites, fix not yet implemented. This bug apparently doesn't trip on every file (overall exit is still `PASS`) — depth capped somewhat by it regardless (RARC/YAZ0-only, no BRRES/TEX ever reached). |
| Trauma Center: New Blood | ✅ | 7972 / 10679 | TEX:6740, BRRES:818, BRFNT:390, LZH8:24 | PASS (3 errors logged). |
| Trauma Center: Second Opinion | 🟡 | real disc now tested | TEX, BRRES, BRFNT, TPL | Directly re-tested: 1.92GB extracted, otherwise clean, but hits the same pre-existing `mobipeg`-fork ffmpeg limitation already documented for Metroid Prime 2/3/Trilogy and Pokémon Battle Revolution: 28 `.brstm` audio files fail to open through the user's own local ffmpeg build, unrelated to this codebase's own format support. Not marked ✅ for the same reason as those rows. |
| Ultra Hand | ✅ | real content confirmed present | `DATA/files` 1.35GB | Directly re-tested against the current binary: **0 hard errors**, 1.35GB of real content confirmed extracted. |
| Wario Land: Shake It! | ✅ | real content confirmed present | `DATA/files` 17.53GB | Was `ERROR_EXIT82` (non-UTF-8 filename class). Directly re-tested against the current binary (including this session's shared `CreateFile()` UTF-8 fix): **0 hard errors**, 17.53GB of real content confirmed extracted. Real **GFA** volume — both Good-Feel titles in this corpus (see also Kirby's Epic Yarn) carry real GFA content. |
| WarioWare: D.I.Y. Showcase | 🟡 | 10551 / 19606 | TPL:7896, BRLAN:1275, BRFNT:1014, BRLYT:249, LZ11:111 | `ERROR_EXIT28` (8 errors logged). |
| WarioWare: Smooth Moves | ✅ | real content confirmed present | `DATA/files` 5.3GB | Root cause found: `Test/Wpad.dat`'s HSD model tripped a corrupt self-referential FTDATA skeleton pointer that both `hsd_walk_jobj_skeleton()` and `hsd_walk_jobj_meshes()` (lib-hsd.c) recursed into via a fallback branch that never incremented `depth`, defeating the `depth>64` guard — the skeleton-pass copy caused a fast stack-overflow SIGSEGV under `-O0`, the mesh-pass copy caused a very slow near-hang under `-O2` (bounded only because `depth` eventually got there doing real work each step, not because the guard actually fired). Fixed both by incrementing `depth+1` in the fallback recursive call. Directly re-tested against the current binary: **0 hard errors**, 5.3GB of real content confirmed extracted. Hits the separate, already-documented `wbrsar` warning class (1 occurrence, `sound_data_jp.brsar`, non-fatal). |
| **We Ski** | ⚠️ | partial recovery: 35 embedded NW4R resources | `DATA/files/SKI.DAT` (368MB total; ~35 small `.brlyt`/`.brlan` recovered, everything else still opaque) | The `ERROR_EXIT28` crash was the same shared-bundle `AnmTexPat` bug fixed earlier this session — re-tested clean. `SKI.DAT` itself is a fully custom, undocumented in-house Namco engine format (`Map::CMapSki`/`Draw::CMapEnv` per the executable's own C++ symbols — the same internal team that made Ridge Racer) — no public tool, no QuickBMS script, and no discoverable table for the bulk content found after extensive static analysis (chunk-tag scan, header/footer check, 10%-interval sampling of the 368MB body). That real course/terrain content — the overwhelming majority of the file — remains genuinely unrecovered and would need dynamic analysis (e.g. a Dolphin memory-read breakpoint on the loader) to crack, out of scope here. What *was* recoverable: 35 standalone NW4R menu-layout resources (`RLYT`/`RLAN`) sit in the same file with no wrapping container, each carrying its own self-describing length (same header this codebase already parses for real `.brlyt`/`.brlan` files) — a new fallback scanner (`ScanEmbeddedNW4R`/`extract_embedded_nw4r_file`) finds and extracts these directly. Verified byte-exact against the real file (35/35 match independently, and all 35 decode cleanly through the existing BRLYT/BRLAN-to-text pipeline). |
| Wii Chess | ✅ | real content confirmed present | `DATA/files` 0.06GB | Directly re-tested against the current binary: **0 hard errors**, 0.06GB of real content confirmed extracted. |
| Wii Fit | ✅ | real content confirmed present | `DATA/files` 2.5GB | Was `ERROR_EXIT82` ("Can't create file... Illegal byte sequence"): a real BRRES sub-object name contains a literal 0xC0 0x8C byte pair -- an "overlong encoding" that structurally *looks* like valid 2-byte UTF-8 but is permanently reserved by RFC 3629, which the first version of this session's UTF-8 fix didn't catch. Fixed by tightening the validator to the full strict rules. Directly re-tested against the current binary: **0 hard errors**, 2.5GB of real content confirmed extracted. |
| Wii Fit Plus | 🟡 | 44980 / 87876 | TEX:18226, TPL:12684, BRLAN:5402, BRFNT:4702, BRLYT:1556 | `ERROR_EXIT28` (70 errors logged) — re-tested against the AnmTexPat fix. |
| Wii Music | ✅ | real content confirmed present | `DATA/files` 0.79GB | Directly re-tested against the current binary: **0 hard errors**, 0.79GB of real content confirmed extracted. |
| Wii Party | ✅ | real content confirmed present | `DATA/files` 6.97GB | Directly re-tested against the current binary: **0 hard errors**, 6.97GB of real content confirmed extracted. |
| Wii Play | ✅ | real content confirmed present | `DATA/files` 0.42GB | Directly re-tested against the current binary: **0 hard errors**, 0.42GB of real content confirmed extracted. |
| Wii Play: Motion | 🟡 | 30956 / 88972 | TEX:19840, TPL:4854, BRFNT:2228, LZ11:1630, BRLAN:1244 | `ERROR_EXIT66` (34 errors logged). — DS-passthrough/FSYS sub-job gap. |
| Wii Sports | ✅ | 6327 / 6520 | TEX:3975, TPL:1276, BRLAN:521, BRFNT:232, BRRES:151 | PASS, clean. PASS, clean. No crash, no new errors. |
| Wii Sports Resort | ✅ | real content confirmed present | `DATA/files` 3.65GB | Directly re-tested against the current binary: **0 hard errors**, 3.65GB of real content confirmed extracted. |
| Wing Island | ✅ | 39370 / 43990 | TEX:28802, LZ10:5088, TPL:2789, BRRES:2421, BRFNT:270 | PASS (32 errors logged). |
| **World of Goo** | ✅ | n/a — see note | n/a — see note | **`master.pak` container support implemented and shipped.** No public tool or QuickBMS script exists for this format; RE'd directly from a real 39MB retail `master.pak` (no filenames stored at all, only per-entry hashes — 2D Boy's open-sourced "Boy Framework" confirmed the engine but its Wii-specific loader wasn't included in that release, an NDA-scrubbed stub). Format: 16-byte header (`n_entries`/magic/zero/hash) + `n_entries`×16-byte rows (`offset`/`size`/unknown/hash), data starts immediately after the table, all big-endian. New `ScanGPAK()`/`extract_gpak_file()` (`lib-nintendo.c`/`wszst.c`); entries extracted under ordinal names (`file_NNNN.bin`) since no real filenames exist to recover, matching this codebase's existing convention for Pokémon FSYS archives. **Verified byte-exact: all 1731/1731 real entries match an independently computed Python reference exactly**, confirmed via two independent internal cross-checks (entry 0's own field equals the real entry count; entry 1's offset independently equals the header+table's exact byte length) before ever writing code. |
| Xenoblade Chronicles | ✅ | real content confirmed present | `DATA/files` 6.63GB | Directly re-tested against the current binary: **0 hard errors**, 6.63GB of real content confirmed extracted. |
| You, Me, and the Cubes | 🟡 | 6915 / 18191 | TPL:2174, TEX:1378, BRLAN:1246, BRFNT:1146, LZ10:395 | `ERROR_EXIT28` (1109 errors logged). |
| Zack & Wiki: Quest for Barbaros' Treasure | ✅ | 113448 / 114990 | TPL:57436, TEX:49248, BRRES:2052, BRLAN:1770, BRFNT:1524 | PASS (1126 errors logged). |
| Zangeki no Reginleiv | ✅ | real content confirmed present | `DATA/files` 9.17GB | Directly re-tested against the current binary: **0 hard errors**, 9.17GB of real content confirmed extracted. |

### Near-zero real content — resolution status

21 of the 118 titles originally decoded fewer than 50 real (non-bundle)
asset ops despite total op counts in the thousands to tens-of-thousands —
meaning essentially all of that title's own game content was never
recognized. Here's where each one actually landed, sorted by how solid the
evidence is:

**Confirmed and fixed, byte-exact verification against real retail files —
these are done:**
- **Bonsai Barber** — Gorilla Games `.pkg` engine archive (own row above).
- **World of Goo** — 2D Boy `master.pak` (own row above).
- **Samurai Warriors 3** — Koei-Tecmo `LINKDATA*.BNS` (own row above).
- **Donkey Kong Country Returns** — Retro Studios `.pak` (own row above).
- **The Last Story** — Mistwalker `.pk`/`.pkh` (own row above).
- **My Pokémon Ranch** — not in the table below (it showed 9 real ops, just
  above the 50 cutoff) but was a real bug in the same family: `ASH0`
  decompression used the wrong distance-tree width for this game
  specifically. Fixed, verified against an independent decoder (own row
  above).

**Confirmed real content already present, no format gap — a bulk-unpack
metric flaw, same class as Dr. Mario Online Rx below:**
- **Mario Strikers Charged**, **And-Kensaku**, **Battalion Wars 2** — all
  directly re-tested: real per-title content (movies, character/environment
  models, `stream`/`src` directories) is genuinely on disk, just not
  individually logged the way `DECODE`/`EXTRACT`-per-file formats are.
- **Dr. Mario Online Rx** — the original false-alarm case that identified
  this metric flaw; see the note below the table.

**Confirmed real, partially or fully unresolved format gaps — genuine
work remains:**
- **We Ski** — `DATA/files/SKI.DAT` (368MB) is a fully custom, undocumented
  in-house Namco engine format with no public tool and no discoverable
  table for its bulk content (own row above has the full story). A fallback
  scanner now recovers 35 embedded, self-describing NW4R menu resources
  from the same file, but the actual ski-course content — the overwhelming
  majority of the 368MB — remains unrecovered.
- **GoldenEye 007 (2010)** — Eurocom's MUSX/EngineX asset containers
  (~2.9GB across `File_*.000`/`Filelist.000`); a real GitHub tool exists for
  this engine's *audio* sub-format specifically, but these files are the
  broader asset system (models via `GEOM` tags, etc.) that tool doesn't
  cover. Not attempted — a much larger RE project than anything else here.
  See its own row above.

**Crash fixed (same `AnmTexPat` bug as above), but not individually
re-run to confirm real content is present — treat as "almost certainly
fine" based on 4 direct spot-checks of the identical signature, not as
verified:**
Epic Mickey, Fatal Frame: Mask of the Lunar Eclipse, Inazuma Eleven
Strikers, Mario & Sonic at the London 2012 Olympic Games, Mario Super
Sluggers, Mystery Case Files: The Malgrave Incident, Punch-Out (Wii).

**Known-bad row, not a format issue:**
- **Metroid Prime 2** — `wii_queue.tsv` points at the wrong file (an NES VC
  ROM); fix the row before trusting this title's numbers at all.

**Not yet looked at:**
- **WarioWare: Smooth Moves** — separately known to be a `wbrsar` timeout
  issue (see "Still open" below), unrelated to this near-zero-content class.
- **Naruto: Clash of Ninja** — already ✅ in the main table above (`PASS`,
  0/4 real-vs-total is just a genuinely tiny WiiWare title, not a gap).
- **Super Mario All-Stars 25th Anniversary Edition** — never individually
  checked against this specific question.

**The underlying metric flaw, for context:** a format whose extractor
unpacks its whole archive in one bulk operation (logged as a single
`EXTRACT <Format>:...(N entries)` line, e.g. Arika's `INFO.DAT`/`GAME.DAT`)
only counts as **one** real op no matter how many real files it actually
produced — unlike formats that log one `DECODE`/`EXTRACT` line per file.
Dr. Mario Online Rx showed 4 real ops here but directly verified to extract
4173 real files correctly (message archives, BRSAR audio, soundfont+MIDI) —
a false alarm from the metric, not a real gap. **Any title using a
bulk-unpack format (Arika, GFA, SARC, RARC-via-`EXTRACT`) is at risk of the
same false flag and should be checked directly before being treated as a
real unsupported-format finding.**

**All 118 titles now have a clean, single-instance result** — the queue
that had been corrupted by an accidental second concurrent instance
finished a full clean re-run of the affected 32 titles. **Kororinpa: Marble
Mania's `CRASH_SIG10` (SIGBUS) reproduced a third time on the clean run**,
confirming it's a real bug, not a race artifact — likely the same "trust a
declared size/count against the real buffer" class as the other SIGBUS
fixes this session, alongside the still-open Mario Party 8 crash.

## Fixes shipped this session

1. **`29d5e17`** — Export WAVE-type RSAR sounds as standalone WAV, fixing `wbrsar` total failure on sample-only BRSAR (found via Calling).
2. **`a5de51d`** — Fix HSF models exporting untextured due to texture-index timing and a double `.png` suffix (found via Calling).
3. **`7613d04`** — Fix SIGBUS crash hashing an extracted subfile with a corrupted declared size (found via Animal Crossing: City Folk, also fixed Metroid: Other M).
4. **`8b4aa8d`** — CHR0 (bone/skeletal) animation export into GLB output.
5. **`14afa8e`** — Generalize LZ10/LZ11 detection past a short unrecognized prefix (AquaSpace's `CX00` tag), not yet confirmed against AquaSpace itself.
6. **`d0a480a`** — Fix stack-buffer-overflow in `GetByMagicFF()`'s OBJ-text sniffing — this **was** the SSBB SIGTRAP, root-caused via ASan and confirmed on a real disc.
7. **`2e917df`** — Fix heap-buffer-overflow reading past a material's real record size in `IterateStringsMDL()` — hit by both Pokémon Battle Revolution and Super Smash Bros. Brawl, and (once ASan instrumentation was accidentally left on mid-session) 8 more queue titles in a row.
8. **`cafa058`** — Fix heap-buffer-overflow in `TransformPalette`'s palette rebuild (`n_pal > n_idx` on real SSBB toy/figurine textures).
9. PAT0/AnmTexPat `n_sect0` vs `n_elem` element-count mismatch — the dominant `ERROR #36` failure across the whole 118-title corpus.
10. PAT0/AnmTexPat `sref.type` 7/15 unhandled variant — the remaining `ERROR #36` cases on top of the fix above.
11. Mario Party 8's `CRASH_SIG10` (SIGBUS) — two real bugs: an unguarded 8-byte MSBT-magic `memcmp` in `DetectNintendoFormat()`, and an unbounds-checked HSF replica parent-chain index walk in `hsf_expand_replica()`. Verified: the real disc now extracts fully with no crash.

## AnmTexPat (PAT0) — root cause found, fixed, and fully resolved

**The dominant `ERROR #36` failure (2674 occurrences, hit nearly every title with an UPDATE partition) is fixed — both bugs behind it.**

1. `IsValidPAT()`/`ScanRawPAT()` in `lib-std.c`/`lib-pat.c` iterated the PAT0 section-0 base-element array to the file-level header's `n_sect0` count, but a `pat_s0_bhead_t` block's own `n_elem` is the real, authoritative count of elements it actually holds — real retail files exist where they legitimately differ (e.g. `n_sect0=3` but the block only has `n_elem=1`). Looping to `n_sect0` walked past the real array into unrelated file bytes, producing wild offsets that either tripped a bounds check (`ERROR #36`) or — after validation alone was fixed — dereferenced unpopulated NULL analysis entries (SIGSEGV). Fixed both loops to use the block's own `n_elem`.
2. A second, distinct case: `pat_s0_sref_t.type` was assumed "always 5" (an MKW-only assumption baked into an existing comment), but real Wii Menu/Shop-Channel PAT0 files also emit types 13, 7, and 15. Empirically, across all 31 real sref entries checked: types 5/13 always carry a real in-bounds string-list offset; types 7/15 always carry a small index-like value in the field's high 16 bits (never a valid offset). Rather than guess what that index means, an element with type 7/15 is now treated as having no string list — verified sane (a real element name with a correctly empty list) rather than silently wrong.

**Verified on a real Wii Shop Channel WAD** (the same `PBmarioA/B/C/F/S/W.brres` family the original `ERROR #36` reports came from): both fixes together clear all `ERROR #36` failures on that sample (23 → 0). Regression suite unchanged at the documented 192 PASS / 38 FAIL / 1 SKIP baseline throughout — zero regressions from either fix.

**Re-run against the fixed binary is complete: 118/118 titles**
(`run_wii_queue.sh` covered 117; WarioWare: Smooth Moves confirmed
separately below). **Zero crashes across the entire re-tested corpus** —
both the Mario Party 8 crash fix and the AnmTexPat fixes held up at full
scale with no regressions and no new crashes surfacing anywhere else. Rows
are marked "re-tested against the AnmTexPat fix" (or, for Mario Party 8,
describe the crash fix directly) and carry fresh Real/Total-op counts and
error counts. **WarioWare: Smooth Moves still times out on the fully-fixed
binary** — re-run standalone with a 2500s cap, killed mid-decode still
inside the shared Shopping-Channel bundle (never reached the game's own
content), same as its original pre-fix behavior. Confirms this is a real,
separate performance issue untouched by any of today's fixes, same
conclusion as **Mario Kart Wii**, which also still times out (`wbrsar`
slowness, unrelated to AnmTexPat).

## More fixes shipped this pass (owner-requested "fix as much as possible")

1. **Non-UTF-8 U8/RARC filename `ERROR #82`** — root-caused earlier to two
   independent raw-byte filename-copy sites (`IterateFilesU8()` in
   `lib-szs.c`, `iterate_rarc_dir()`'s `StringCopyE()` call in
   `lib-rarc.c`), neither of which validated UTF-8 before handing the bytes
   to the filesystem, where macOS rejects an invalid sequence outright
   (EILSEQ). Both now sanitize an invalid byte to `_` instead. Verified on
   the real Twilight Princess disc that reported this: 2 `ERROR #82`
   failures → 0.
2. **DS-passthrough re-claim bug (`ERROR #66`)** — `passthru_claim()`'s DS
   ROM detection matched the literal ASCII "NINTENDO" string at file offset
   0 with no extension check. Real DS WFC/wifi ROMs bundled in some Wii
   games genuinely start with that string in their title field — but so
   does `wit`/`ndstool`'s own already-extracted `header.bin` output (a
   byte-for-byte copy of the same header), so the generic recursive walker
   wrongly re-claimed it and resubmitted it to `wit`/`ndstool`, which fails
   since it isn't a real disc image. Added an extension guard
   (`.nds`/`.srl`/`.dsi`), mirroring the same pattern the disc-image claim
   right above it already used for an identical reason. Verified on the
   real Pokémon Battle Revolution disc: 2 of its 4 `ERROR #66` failures
   (the `wifi/child0[.jp]` ones) are gone.
3. **Investigated and ruled out of scope**: Pokémon Battle Revolution's
   remaining 2 `ERROR #66` failures trace to a local `mobipeg`-fork
   `ffmpeg` build whose THP video *decoder* itself cannot decode a single
   frame (confirmed by running the exact command by hand) — a bug in that
   external tool, not in this codebase.

## Fixes shipped this session (part 2 — native container-format support)

Following up on "fix as much as possible": found and closed 5 completely
unsupported archive formats plus a real decompression bug, all verified
byte-exact against real retail files (not just structurally plausible —
independently re-implemented and diffed, or checked against another
existing decoder). One GitHub research pattern proved out repeatedly:
check for an existing tool/QuickBMS script before RE'ing from raw bytes —
found working prior art for 3 of the 5 new formats and the ASH0 fix.

1. **Bonsai Barber `.pkg`** — found via aluigi's public QuickBMS script;
   real files are zlib streams wrapping a flat entry table. `ScanGPKG`/
   `extract_gpkg_file`. 2942/2942 entries verified across all 5 retail
   archives.
2. **World of Goo `master.pak`** — no public tool; RE'd from a real
   1731-entry sample, verified via two independently-corroborating header
   fields. `ScanGPAK`/`extract_gpak_file`.
3. **Samurai Warriors 3 `LINKDATA*.BNS`** — no public tool; RE'd from 5 real
   samples (entry counts 1–5276), zero out-of-range hits. Also fixed a
   pre-existing bug where `.bns` files were being stolen by the ffmpeg
   media-passthrough path before the new extractor got a chance.
   `ScanBNS`/`extract_bns_file`.
4. **Donkey Kong Country Returns `.pak`** — found via
   github.com/jellees/crPakTool; one field in that tool was actually wrong
   (block "stored size" is 24-bit, not 16-bit) and only caught because a
   real sample had a block that size overflowed. `ScanRPAK`/
   `extract_rpak_file`. Verified against 2 real archives (112 and 2651
   entries).
5. **The Last Story `.pk`/`.pkh`** — found via
   github.com/RGBA-CRT/LSPK-Extracter, transcribed its real parser.
   `ScanLSPK`/`extract_lspk_file`. Verified against all 3 real archive pairs
   on the disc (1478/2626/47204 entries).
6. **My Pokémon Ranch's ASH0 decompression bug** — the codec's
   distance-tree bit width is a build-time encoder choice, not a header
   field; this game specifically uses 15 instead of the default 11, per
   NinjaCheetah/ASH0-tools (an independent GitHub decoder that names this
   exact game as the known exception). `DecodeASH0` now tries both. Fixed
   57 of 58 real `.ash` payloads (up from 0), verified byte-exact against
   that independent decoder.
7. **Fallback embedded-NW4R-resource scanner** — for We Ski's `SKI.DAT`
   (a fully custom, undocumented Namco format with no discoverable table
   for its bulk content). Self-describing `RLYT`/`RLAN` menu resources
   scattered through the same file, with no container of their own, are
   now found and extracted directly via their own length field.
   `ScanEmbeddedNW4R`/`extract_embedded_nw4r_file`. 35/35 verified
   byte-exact on a real `SKI.DAT`.

Also re-tested Mario Strikers Charged, We Ski, And-Kensaku, and Battalion
Wars 2 directly against the AnmTexPat fix from part 1 — all confirmed
clean, with real per-title content verified present. GoldenEye 007's
AnmTexPat crash is likewise fixed, but its actual game data is a separate,
much larger Eurocom MUSX/EngineX format that remains unresolved (see
"Near-zero real content" above).

## Still open

- **Of the 21 titles that originally showed near-zero real content, 6 are fully fixed (5 new container formats + 1 decompression bug, all byte-exact verified), 4 more are confirmed false alarms (real content already present), 2 have confirmed real gaps (We Ski partially recovered, GoldenEye not attempted — both much larger undertakings than the rest), 7 are "almost certainly fine" but not individually re-run, and 2 are known-bad/unchecked rows.** See "Near-zero real content — resolution status" above for the full breakdown.
- **GoldenEye 007's Eurocom MUSX/EngineX asset system** (`File_*.000`/`Filelist.000`, ~2.9GB) — a real GitHub tool exists for this engine's audio format specifically, but the actual game data is the broader multi-format asset system (models via `GEOM` tags, etc.) that tool doesn't cover. Not attempted; would need substantially more RE than anything else in this doc.
- **We Ski's `SKI.DAT`** (368MB) — a fully custom, undocumented Namco engine format (`Map::CMapSki`). No table found for its main course/terrain content despite extensive static analysis; would need dynamic analysis (e.g. a Dolphin memory-read breakpoint) to crack. A fallback scanner recovers 35 embedded NW4R menu resources from the same file as a partial win.
- ~~Mario Party 8: `CRASH_SIG10` (SIGBUS)~~ — **fixed** (MSBT-magic OOB read + HSF replica parent-chain OOB walk, both in `lib-nintendo.c`/`lib-hsf.c`). Verified: re-run on the real disc completes fully now, no crash.
- ~~Kororinpa: Marble Mania: same signal (SIGBUS)~~ — **no longer crashes**, re-tested against the Mario Party 8 crash fixes and now completes fully (6693 real ops, ~3x past its old crash ceiling). Not separately confirmed as the identical root cause, but resolved in practice.
- Non-UTF-8 RARC filename `ERROR #82` (Twilight Princess, Excite Truck, Wario Land: Shake It!) — root-caused, fix not yet implemented.
- `ERROR #66` DS-`.srl`-passthrough / FSYS-media sub-job failures (Pokémon Battle Revolution, Metroid Prime: Trilogy, Wii Play: Motion) — not yet investigated.
- `wbrsar` timeouts on large multi-track BRSAR (Mario Kart Wii, WarioWare: Smooth Moves, Endless Ocean: Blue World) — suspected performance regression in the WAVE-export path, not confirmed.
- `wii_queue.tsv`'s Metroid Prime 2 row points at the wrong file (an NES VC ROM) — fix the row before trusting that title's results.
- AquaSpace: `CX00`-prefixed LZ11 BRRES detection not confirmed fixed (the general detector change shipped, but the actual `wszst xx` archive-extraction call path that hits this was never located).

## BRRES animation export

The GLB/DAE exporter already had generic animation-channel support in its
writer (`model_t.animations[]`), but nothing populated it — BRRES CHR0/SRT0/
CLR0/PAT0/SCN0/SHP0 animation sub-files were extracted as raw bytes only,
never converted, so exported models were always static.

- **CHR0 (bone/skeletal animation)**: implemented (`ParseCHR0IntoModel()` in
  `lib-brres-model.c`, wired into `wszst.c`'s per-file model-export pass,
  which now also scans the sibling `AnmChr(NW4R)/` folder next to a model's
  `3DModels(NW4R)/*.mdl0` and matches bone names). Decodes all 6 BRRES
  keyframe encodings (I4/I6/I12 interpolated + L1/L2/L4 dense), reproducing
  BrawlLib's Hermite spline exactly (oracle: soopercool101/BrawlCrate's
  BrawlLib source, `AnimationConverter.cs`/`EncodingTypes.cs`/`CHR0.cs`).
- **SRT0, CLR0, PAT0, SCN0, SHP0**: not yet implemented. Same wiring pattern
  applies (sibling `AnmTexSrt(NW4R)`/`AnmClr(NW4R)`/`AnmTexPat(NW4R)`/
  `AnmScn(NW4R)`/`AnmShp(NW4R)` folders); SHP0 (vertex morph) maps to the
  exporter's existing `MODEL_ANIM_WEIGHTS` channel type.
- **VIS0 (visibility animation)**: the tool doesn't recognize this format at
  all yet — no `file-type.c` entry, no magic/folder mapping. Needs adding as
  a new file type before animation export is even possible.


---

# Nintendo DS Game Extraction Test Results

Full extraction test suite (`wszst xx <rom.nds>`) across all 163 Nintendo DS games listed on Wikipedia ([List of Nintendo handheld video games](https://en.wikipedia.org/wiki/List_of_Nintendo_handheld_video_games#Nintendo_DS)), pulled directly from `mcubeds:Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/`.

**Results Summary**: **163 / 163 games tested (100%)** · **115 Clean Passes (70.6%)** · **48 Non-Fatal Warnings (29.4%)** · **0 Crashes (0.0%)** · **1,248,809 Total Files Extracted** (346,171 Real Operations).

| Game | Status | Real / Total Ops | Extracted Files & Size | Top Formats | Notes |
|---|:---:|---|---|---|---|
| 100 Classic Book Collection | 🟡 | 352 / 353 | 2399 (65.6MB) | ?:143, LZ10:56, NARC:56, NSCR:42, NANR XML:24 | ERROR_EXIT28 |
| 100 Classic Books | 🟡 | 429 / 430 | 3017 (70.3MB) | ?:209, NSCR:57, LZ10:54, NARC:54, NANR XML:24 | ERROR_EXIT28 |
| 100 Livres Classiques | 🟡 | 442 / 443 | 3179 (64.5MB) | ?:175, LZ10:76, NARC:76, NSCR:62, NANR XML:23 | ERROR_EXIT28 |
| Advance Wars: Days of Ruin | ✅ | 5631 / 5633 | 12295 (115.9MB) | LZ10:4712, NARC:837, RL:75, TEX:3, LZ11:2 | PASS |
| Advance Wars: Dual Strike | ✅ | 1333 / 1335 | 3398 (96.2MB) | LZ10:1329, LZ11:3, passthrough:2, SDAT:1 | PASS |
| Animal Crossing: Wild World | 🟡 | 8413 / 8414 | 22322 (121.5MB) | LZ10:6348, NARC:2064, passthrough:1, SDAT:1 | ERROR_EXIT36 |
| Apollo Justice: Ace Attorney | ✅ | 1 / 22 | 140 (83.1MB) | media passthrough:20, passthrough:1, SDAT:1 | PASS |
| Art Academy | ✅ | 2 / 3 | 1913 (27.3MB) | SDAT:2, passthrough:1 | PASS |
| ASH: Archaic Sealed Heat | 🟡 | 2349 / 2418 | 8475 (346.2MB) | ?:1517, NCER XML:329, NANR XML:324, NSCR:148, media passthrough:68 | ERROR_EXIT36 |
| Bibliothek der klassischen Bücher | 🟡 | 429 / 430 | 3017 (70.3MB) | ?:209, NSCR:57, LZ10:54, NARC:54, NANR XML:24 | ERROR_EXIT28 |
| Big Brain Academy | ✅ | 104 / 105 | 562 (12.6MB) | ?:68, NCER XML:16, NANR XML:7, NSCR:7, NARC:4 | PASS |
| Brain Age 2: More Training in Minutes a Day! | ✅ | 657 / 662 | 2658 (62.2MB) | LZ10:356, NCER XML:148, NANR XML:148, passthrough:5, SDAT:4 | PASS |
| Brain Age: Train Your Brain in Minutes a Day! | ✅ | 311 / 314 | 1012 (22.2MB) | LZ10:212, NCER XML:48, NANR XML:48, passthrough:3, SDAT:2 | PASS |
| Chibi-Robo! Park Patrol | ✅ | 819 / 820 | 6552 (116.4MB) | ?:376, NANR XML:173, NCER XML:173, LZ10:41, NARC:24 | PASS |
| Children of Mana | ✅ | 1066 / 1067 | 4150 (83.3MB) | ?:594, NCER XML:234, NANR XML:229, NSCR:8, passthrough:1 | PASS |
| Chōsōjū Mecha MG | ✅ | 385 / 386 | 1580 (87.2MB) | LZ10:366, ?:5, RL:4, NANR XML:4, NCER XML:4 | PASS |
| Clubhouse Games | ✅ | 41 / 42 | 490 (25.0MB) | LZ10:40, passthrough:1, SDAT:1 | PASS |
| CrossworDS | 🟡 | 1 / 2 | 2000 (26.5MB) | passthrough:1, SDAT:1 | ERROR_EXIT28 |
| Custom Robo Arena | 🟡 | 10248 / 10249 | 30318 (416.6MB) | LZ10:6339, ?:1361, NARC:1289, NCER XML:660, NANR XML:591 | EXIT_64 |
| Daigasso! Band Brothers | 🟡 | 161 / 162 | 1314 (20.0MB) | NANR XML:72, NCER XML:72, ?:16, passthrough:1, SDAT:1 | ERROR_EXIT36 |
| Daredemo Asobi Taizen | ✅ | 1 / 2 | 163 (15.7MB) | passthrough:1, SDAT:1 | PASS |
| Densetsu no Starfy 4 | 🟡 | 4207 / 4208 | 13799 (515.2MB) | LZ10:2694, ?:723, NANR XML:672, NCER XML:117, passthrough:1 | ERROR_EXIT36 |
| Diddy Kong Racing DS | ✅ | 1 / 2 | 292 (44.1MB) | passthrough:1, SDAT:1 | PASS |
| DK Jungle Climber | 🟡 | 0 / 1 | 2120 (13.8MB) | passthrough:1 | ERROR_EXIT66 |
| Dragon Quest IX: Sentinels of the Starry Skies | 🟡 | 17079 / 17086 | 51463 (1.51GB) | LZ10:11199, NARC:4167, LZH8:1698, ?:6, media passthrough:6 | ERROR_EXIT28 |
| Dragon Quest Monsters: Joker 2 | ✅ | 6 / 7 | 4629 (161.1MB) | LZH8:3, LZ10:2, passthrough:1, SDAT:1 | PASS |
| Dragon Quest VI: Realms of Revelation | ✅ | 4003 / 4004 | 10037 (328.7MB) | LZ11:2017, LZ10:1925, TEX:60, passthrough:1, SDAT:1 | PASS |
| DS Bungaku Zenshū | 🟡 | 532 / 533 | 2770 (46.0MB) | ?:161, LZ10:149, NARC:149, NANR XML:32, NCER XML:32 | ERROR_EXIT36 |
| DS Rakubiki Jiten | 🟡 | 73 / 74 | 572 (52.3MB) | ?:62, NANR XML:5, NCER XML:5, passthrough:1, SDAT:1 | ERROR_EXIT28 |
| Electroplankton | 🟡 | 2 / 3 | 486 (13.1MB) | SDAT:2, passthrough:1 | ERROR_EXIT28 |
| Elite Beat Agents | 🟡 | 32547 / 32548 | 53657 (272.2MB) | LZ10:20931, ?:9862, NCER XML:892, NANR XML:843, LZ11:10 | ERROR_EXIT28 |
| English Training: Have Fun Improving Your Skills! | ✅ | 1076 / 1077 | 2660 (498.2MB) | LZ10:509, ?:446, NANR XML:59, NCER XML:59, SDAT:3 | PASS |
| Etrian Odyssey | ✅ | 1230 / 1231 | 3853 (42.7MB) | LZ10:1078, RL:151, passthrough:1, SDAT:1 | PASS |
| Face Training | ✅ | 189 / 190 | 2467 (32.2MB) | LZ10:117, NCCARC:71, passthrough:1, SDAT:1 | PASS |
| Fire Emblem: Shadow Dragon | 🟡 | 0 / 1 | 3280 (36.9MB) | passthrough:1 | ERROR_EXIT66 |
| Fire Emblem: Shin Monshō no Nazo ~Hikari to Kage no Eiyū~ | 🟡 | 0 / 1 | 4079 (51.8MB) | passthrough:1 | ERROR_EXIT66 |
| Flash Focus: Vision Training in Minutes a Day | ✅ | 6382 / 6383 | 25652 (122.1MB) | NARC:2993, RL:2868, ?:274, NSCR:84, NANR XML:53 | PASS |
| Fossil Fighters | ✅ | 1 / 2 | 8371 (147.5MB) | passthrough:1, SDAT:1 | PASS |
| Fossil Fighters: Champions | ✅ | 1 / 76 | 12388 (385.1MB) | media passthrough:74, passthrough:1, SDAT:1 | PASS |
| Freshly-Picked Tingle's Rosy Rupeeland | ✅ | 17 / 18 | 1097 (80.8MB) | LZ10:16, passthrough:1, SDAT:1 | PASS |
| Game & Watch Collection | ✅ | 11 / 12 | 39 (2.2MB) | ?:3, NCER XML:3, NANR XML:3, passthrough:1, SDAT:1 | PASS |
| Game & Watch Collection 2 | ✅ | 88 / 89 | 706 (4.5MB) | ?:51, NSCR:15, NANR XML:10, NCER XML:10, passthrough:1 | PASS |
| Ganbaru Watashi no Kakei Diary | ✅ | 1821 / 1822 | 7451 (49.1MB) | ?:890, NCER XML:269, NANR XML:269, NARC:136, LZ10:131 | PASS |
| Glory of Heracles | 🟡 | 3 / 5 | 170 (67.7MB) | passthrough:1, media passthrough:1, LZ10:1, SDAT:1, QuickLZ:1 | ERROR_EXIT66 |
| Golden Sun: Dark Dawn | ✅ | 1457 / 1458 | 28760 (311.9MB) | NARC:1437, LZH8:14, QuickLZ:5, passthrough:1, SDAT:1 | PASS |
| Harvest Moon DS | ✅ | 3 / 4 | 257 (111.7MB) | LZ10:2, passthrough:1, SDAT:1 | PASS |
| Harvest Moon DS: Island of Happiness | ✅ | 1323 / 1324 | 4578 (123.8MB) | ?:564, NANR XML:379, NCER XML:379, passthrough:1, SDAT:1 | PASS |
| Hotel Dusk: Room 215 | ✅ | 1 / 2 | 853 (118.5MB) | passthrough:1, SDAT:1 | PASS |
| Inazuma Eleven | ✅ | 1023 / 1044 | 2911 (229.2MB) | LZ10:1021, media passthrough:20, LZH8:2, passthrough:1 | PASS |
| Inazuma Eleven 2: Firestorm and Blizzard | ✅ | 459 / 500 | 1927 (322.5MB) | LZ10:457, media passthrough:40, LZH8:2, passthrough:1 | PASS |
| Irozuki Tingle no Koi no Balloon Trip | ✅ | 404 / 405 | 1229 (170.1MB) | LZ11:364, LZ10:39, passthrough:1, SDAT:1 | PASS |
| Jam with the Band | ✅ | 1076 / 1077 | 7640 (122.5MB) | NCER XML:532, NANR XML:527, LZH8:12, LZ10:3, passthrough:1 | PASS |
| Jet Impulse | ✅ | 900 / 935 | 7597 (325.2MB) | ?:766, NSCR:71, media passthrough:34, LZ10:28, NANR XML:17 | PASS |
| Jump Super Stars | 🟡 | 1 / 5 | 2994 (52.1MB) | media passthrough:3, passthrough:1, SDAT:1 | ERROR_EXIT66 |
| Jump Ultimate Stars | ✅ | 11 / 13 | 500 (105.1MB) | LZ10:10, passthrough:1, media passthrough:1, SDAT:1 | PASS |
| Kanji Sonomama Rakubiki Jiten DS | ✅ | 71 / 72 | 208 (124.3MB) | ?:66, NCER XML:2, NANR XML:2, passthrough:1, SDAT:1 | PASS |
| Kirby Mass Attack | 🟡 | 1786 / 1787 | 10178 (126.8MB) | ?:1134, NSCR:323, NCER XML:154, NANR XML:154, HUFF8:15 | ERROR_EXIT36 |
| Kirby Super Star Ultra | ✅ | 815 / 867 | 3481 (133.0MB) | LZ10:814, media passthrough:51, passthrough:1, SDAT:1 | PASS |
| Kirby: Canvas Curse | ✅ | 905 / 906 | 3177 (91.7MB) | LZ10:899, LZH8:3, RL:2, passthrough:1, SDAT:1 | PASS |
| Kirby: Squeak Squad | ✅ | 528 / 530 | 5119 (72.8MB) | RL:527, passthrough:2, SDAT:1 | PASS |
| Kurikin Nano Island Story | ✅ | 1 / 4 | 2458 (54.3MB) | media passthrough:2, passthrough:1, SDAT:1 | PASS |
| Kōsoku Card Battle: Card Hero | ✅ | 1109 / 1113 | 10734 (114.4MB) | LZ10:770, ?:168, NANR XML:83, NCER XML:83, media passthrough:3 | PASS |
| Last Window: The Secret of Cape West | ✅ | 1 / 6 | 347 (148.8MB) | media passthrough:4, passthrough:1, SDAT:1 | PASS |
| Learn with Pokémon: Typing Adventure | 🟡 | 27110 / 27111 | 49903 (398.0MB) | NARC:14544, LZ10:11038, ?:1100, NANR XML:148, NCER XML:148 | EXIT_64 |
| Magnetica | 🟡 | 1208 / 1209 | 2101 (33.5MB) | ?:485, LZ10:263, NARC:240, NSCR:131, NCER XML:44 | EXIT_64 |
| Make 10: A Journey of Numbers | ✅ | 6338 / 6339 | 13431 (370.0MB) | LZ10:3363, ?:2750, NSCR:213, NCER XML:4, NANR XML:4 | PASS |
| Mario & Luigi: Bowser's Inside Story | ✅ | 3 / 4 | 333 (124.2MB) | passthrough:1, LZH8:1, HUFF8:1, SDAT:1 | PASS |
| Mario & Luigi: Partners in Time | ✅ | 2 / 3 | 151 (61.1MB) | passthrough:1, LZH8:1, SDAT:1 | PASS |
| Mario & Sonic at the Olympic Games | ✅ | 45 / 46 | 14345 (206.3MB) | LZ10:43, SDAT:2, passthrough:1 | PASS |
| Mario & Sonic at the Olympic Winter Games | ✅ | 2 / 3 | 1345 (204.4MB) | SDAT:2, passthrough:1 | PASS |
| Mario Hoops 3-on-3 | ✅ | 2331 / 2332 | 4751 (100.3MB) | LZ10:1151, ?:632, NCER XML:273, NANR XML:273, SDAT:2 | PASS |
| Mario Kart DS | 🟡 | 2020 / 2021 | 7431 (157.3MB) | ?:1046, LZ10:286, NARC:286, NCER XML:284, NSCR:112 | EXIT_14 |
| Mario Party DS | ✅ | 211 / 212 | 731 (44.9MB) | LZ10:210, passthrough:1, SDAT:1 | PASS |
| Mario vs. Donkey Kong 2: March of the Minis | ✅ | 144 / 154 | 1139 (76.3MB) | LZ10:106, RL:37, media passthrough:8, passthrough:2, SDAT:1 | PASS |
| Mario vs. Donkey Kong: Mini-Land Mayhem! | ✅ | 1 / 2 | 713 (54.5MB) | passthrough:1, SDAT:1 | PASS |
| Master of Illusion | ✅ | 2588 / 2589 | 4310 (57.7MB) | LZ10:1561, ?:649, NCER XML:148, NANR XML:148, RL:80 | PASS |
| Mawashite Tsunageru Touch Panic | ✅ | 13 / 14 | 312 (18.9MB) | HUFF8:12, passthrough:1, SDAT:1 | PASS |
| Mega Man Battle Network 5: Double Team DS | ✅ | 2939 / 2940 | 7486 (60.3MB) | LZ10:2226, ?:586, NANR XML:56, NCER XML:56, LZH8:14 | PASS |
| Meteos | ✅ | 8 / 9 | 1662 (80.3MB) | LZH8:5, LZ10:2, passthrough:1, SDAT:1 | PASS |
| Metroid Prime Hunters | ✅ | 142 / 199 | 3044 (137.7MB) | LZ10:136, media passthrough:56, LZH8:3, passthrough:1, mffmpeg version git-2026-08-28-711882907a Copyright (c) 2000-2026 the FFmpeg developers:1 | PASS |
| Metroid Prime Hunters: First Hunt | 🟡 | 20 / 35 | 233 (16.3MB) | media passthrough:14, RL:11, LZ10:8, passthrough:1, LZH8:1 | ERROR_EXIT66 |
| Metroid Prime Pinball | ✅ | 707 / 708 | 2113 (52.4MB) | RL:700, LZH8:5, passthrough:1, LZ10:1, SDAT:1 | PASS |
| Moero! Nekketsu Rhythm Damashii Osu! Tatakae! Ōendan 2 | 🟡 | 7776 / 7777 | 28500 (151.7MB) | LZ10:4862, ?:2380, NCER XML:267, NANR XML:261, SDAT:6 | ERROR_EXIT28 |
| Mr. Driller Drill Spirits | 🟡 | 2 / 4 | 77 (14.0MB) | passthrough:2, LZH8:1, SDAT:1 | ERROR_EXIT66 |
| Mystery Case Files: MillionHeir | ✅ | 1 / 2 | 25 (114.4MB) | passthrough:1, SDAT:1 | PASS |
| Naruto Shippuden: Ninja Council 3 - European Version | ✅ | 1 / 2 | 36 (76.7MB) | passthrough:1, SDAT:1 | PASS |
| Naruto: Ninja Council - European Version | ✅ | 1 / 2 | 40 (65.4MB) | passthrough:1, SDAT:1 | PASS |
| Naruto: Ninja Destiny | ✅ | 1132 / 1133 | 4114 (83.5MB) | ?:1055, NCER XML:51, NSCR:25, passthrough:1, SDAT:1 | PASS |
| Naruto: Ninja Destiny II - European Version | ✅ | 1618 / 1619 | 6407 (160.9MB) | ?:1430, NCER XML:120, NANR XML:42, NSCR:25, passthrough:1 | PASS |
| New Super Mario Bros. | ✅ | 1159 / 1160 | 4120 (82.7MB) | LZ10:1110, NARC:45, SDAT:2, passthrough:1, LZ11:1 | PASS |
| Nintendo Presents: Crossword Collection | 🟡 | 1 / 2 | 1905 (25.9MB) | passthrough:1, SDAT:1 | ERROR_EXIT28 |
| Okaeri! Chibi-Robo! Happy Richie Ōsōji! | 🟡 | 0 / 1 | 552 (15.8MB) | passthrough:1 | ERROR_EXIT66 |
| Osu! Tatakae! Ōendan | 🟡 | 0 / 1 | 1960 (40.8MB) | passthrough:1 | ERROR_EXIT66 |
| Otona no Renai Shōsetsu: Harlequin Selection | 🟡 | 1262 / 1263 | 4201 (87.1MB) | ?:482, LZ10:268, NARC:268, NSCR:136, NANR XML:53 | ERROR_EXIT28 |
| Pac 'n Roll | ✅ | 1 / 2 | 1308 (106.8MB) | passthrough:1, SDAT:1 | PASS |
| Pac-Pix | ✅ | 1 / 2 | 295 (16.9MB) | passthrough:1, SDAT:1 | PASS |
| Personal Trainer: Cooking | 🟡 | 0 / 1 | 2588 (21.5MB) | passthrough:1 | ERROR_EXIT66 |
| Personal Trainer: Math | ✅ | 2 / 6 | 146 (24.1MB) | passthrough:4, HUFF8:1, SDAT:1 | PASS |
| Personal Trainer: Walking | ✅ | 546 / 547 | 4603 (199.6MB) | ?:411, NCER XML:71, NANR XML:27, NSCR:23, LZH8:11 | PASS |
| Phoenix Wright: Ace Attorney | ✅ | 1 / 2 | 194 (69.1MB) | passthrough:1, SDAT:1 | PASS |
| Phoenix Wright: Ace Attorney - Justice for All | ✅ | 2 / 3 | 204 (41.4MB) | passthrough:1, SDAT:1, LZ10:1 | PASS |
| Phoenix Wright: Ace Attorney - Trials and Tribulations | ✅ | 2 / 3 | 221 (37.0MB) | passthrough:1, SDAT:1, LZ10:1 | PASS |
| Picross 3D | ✅ | 1 / 2 | 60 (49.7MB) | passthrough:1, SDAT:1 | PASS |
| Picross DS | ✅ | 1 / 2 | 181 (53.0MB) | passthrough:1, SDAT:1 | PASS |
| Planet Puzzle League | 🟡 | 578 / 579 | 1116 (36.5MB) | LZ10:339, ?:121, NANR XML:73, NCER XML:44, passthrough:1 | ERROR_EXIT36 |
| Pokémon Black and White Version | ✅ | 29652 / 29653 | 79694 (930.3MB) | ?:11243, LZ11:10676, NCER XML:3274, NANR XML:3274, LZ10:933 | PASS |
| Pokémon Black and White Version 2 | ✅ | 33082 / 33083 | 99682 (1.68GB) | ?:12993, LZ11:11207, NCER XML:3643, NANR XML:3643, LZ10:1252 | PASS |
| Pokémon Card Game Asobi Kata DS | ✅ | 76 / 77 | 1685 (26.9MB) | LZH8:58, LZ10:16, passthrough:1, RL:1, SDAT:1 | PASS |
| Pokémon Conquest | 🟡 | 2 / 6 | 1002 (188.1MB) | media passthrough:3, passthrough:1, SDAT:1, LZH8:1 | ERROR_EXIT66 |
| Pokémon Dash | ✅ | 2 / 3 | 530 (23.8MB) | passthrough:1, LZ10:1, SDAT:1 | PASS |
| Pokémon Diamond and Pearl Version | ✅ | 8443 / 8444 | 43798 (172.5MB) | ?:5555, LZ10:1868, NCER XML:440, NANR XML:427, NARC:149 | PASS |
| Pokémon HeartGold and SoulSilver Version | ✅ | 19046 / 19047 | 79008 (549.2MB) | ?:12138, LZ10:4183, NCER XML:1009, NANR XML:993, LZ11:398 | PASS |
| Pokémon Mystery Dungeon: Blue Rescue Team | ✅ | 1 / 2 | 253 (35.2MB) | passthrough:1, SDAT:1 | PASS |
| Pokémon Mystery Dungeon: Explorers of Darkness and Explorers of Time | ✅ | 7 / 8 | 5958 (60.1MB) | LZ10:5, LZ11:2, passthrough:1 | PASS |
| Pokémon Mystery Dungeon: Explorers of Sky | ✅ | 14 / 15 | 8663 (84.4MB) | LZ10:13, passthrough:1, LZ11:1 | PASS |
| Pokémon Platinum Version | ✅ | 14193 / 14194 | 70249 (345.2MB) | ?:9889, LZ10:2468, NCER XML:814, NANR XML:801, NARC:217 | PASS |
| Pokémon Ranger | 🟡 | 720 / 721 | 2903 (52.7MB) | LZ10:704, LZH8:9, HUFF4:3, SDAT:2, passthrough:1 | ERROR_EXIT28 |
| Pokémon Ranger: Shadows of Almia | 🟡 | 15017 / 15018 | 32469 (154.5MB) | ?:5886, NCER XML:3876, NARC:2432, LZ10:2289, NANR XML:486 | EXIT_64 |
| Pokémon Trozei! | 🟡 | 4185 / 4186 | 8167 (33.0MB) | NARC:1204, LZ10:917, ?:828, NANR XML:616, NCER XML:616 | EXIT_64 |
| Polarium | 🟡 | 566 / 567 | 1139 (14.7MB) | LZ10:565, passthrough:1, SDAT:1 | ERROR_EXIT28 |
| Professor Layton and the Curious Village | 🟡 | 0 / 1 | 1899 (24.4MB) | passthrough:1 | ERROR_EXIT66 |
| Professor Layton and the Diabolical Box | ✅ | 1814 / 1838 | 4555 (184.1MB) | LZ10:1814, media passthrough:23, passthrough:1 | PASS |
| Professor Layton and the Last Specter | ✅ | 0 / 39 | 636 (301.4MB) | media passthrough:38, passthrough:1 | PASS |
| Professor Layton and the Unwound Future | ✅ | 3004 / 3041 | 7036 (354.3MB) | LZ10:2868, RL:123, media passthrough:36, HUFF8:12, passthrough:1 | PASS |
| Project Hacker: Kakusei | 🟡 | 6282 / 6283 | 22421 (308.1MB) | NARC:2318, ?:1819, LZ10:1636, NCER XML:166, NANR XML:166 | EXIT_64 |
| Resident Evil: Deadly Silence | ✅ | 7 / 8 | 171 (126.8MB) | LZH8:6, passthrough:1, SDAT:1 | PASS |
| Rhythm Heaven | ✅ | 2247 / 2248 | 4396 (188.1MB) | ?:970, LZ10:865, NANR XML:205, NCER XML:205, passthrough:1 | PASS |
| Ridge Racer DS | ✅ | 266 / 267 | 908 (41.3MB) | LZ10:157, RL:109, passthrough:1 | PASS |
| Rooms: The Main Building | ✅ | 1 / 2 | 24 (23.7MB) | passthrough:1, SDAT:1 | PASS |
| Shaberu! DS Oryōri Navi | 🟡 | 0 / 1 | 583 (5.7MB) | passthrough:1 | ERROR_EXIT66 |
| Slide Adventure MAGKID | ✅ | 335 / 336 | 1138 (60.2MB) | LZ10:334, passthrough:1, SDAT:1 | PASS |
| Solatorobo: Red the Hunter | ✅ | 1 / 4 | 1989 (67.9MB) | media passthrough:2, passthrough:1, SDAT:1 | PASS |
| Soma Bringer | ✅ | 4 / 6 | 570 (243.0MB) | passthrough:2, LZ10:2, LZH8:1, PACK:1 | PASS |
| Sonic Rush Adventure | 🟡 | 904 / 906 | 4272 (237.8MB) | LZ10:610, NARC:261, SDAT:31, passthrough:2, LZH8:2 | EXIT_64 |
| Star Fox Command | ✅ | 3437 / 3438 | 7787 (63.8MB) | LZ10:3432, ?:3, passthrough:1, LZH8:1, SDAT:1 | PASS |
| Style Savvy | 🟡 | 1928 / 1930 | 9021 (118.9MB) | ?:967, NANR XML:456, NCER XML:456, NSCR:41, LZ10:4 | ERROR_EXIT66 |
| Sudoku Gridmaster | ✅ | 1 / 2 | 24 (12.8MB) | passthrough:1, SDAT:1 | PASS |
| Super Mario 64 DS | ✅ | 1917 / 1918 | 6197 (54.0MB) | LZ10:1901, NARC:8, QuickLZ:4, LZH8:3, passthrough:1 | PASS |
| Super Princess Peach | ✅ | 1733 / 1734 | 3937 (50.7MB) | LZ10:1307, ?:208, NANR XML:106, NCER XML:106, LZH8:5 | PASS |
| Sūjin Taisen | 🟡 | 1865 / 1866 | 3168 (73.8MB) | ?:630, NARC:462, LZ10:396, NSCR:280, NCER XML:48 | EXIT_64 |
| Tenchu: Dark Secret | ✅ | 0 / 1 | 10 (43.0MB) | passthrough:1 | PASS |
| Tetris DS | 🟡 | 357 / 361 | 1041 (25.0MB) | LZ10:343, ?:10, passthrough:4, LZH8:1, SDAT:1 | EXIT_22 |
| Tetris Party Deluxe | ✅ | 89 / 91 | 4079 (28.6MB) | ?:30, NANR XML:28, NCER XML:28, passthrough:2, HUFF8:2 | PASS |
| The Legend of Zelda: Phantom Hourglass | 🟡 | 4280 / 4281 | 19115 (703.2MB) | NARC:1638, LZ10:1592, ?:562, NANR XML:228, NCER XML:228 | EXIT_64 |
| The Legend of Zelda: Spirit Tracks | 🟡 | 5369 / 5373 | 26996 (860.0MB) | NARC:2142, LZ10:1712, ?:724, NCER XML:381, NANR XML:381 | EXIT_64 |
| The Legendary Starfy | ✅ | 8 / 67 | 354 (128.7MB) | media passthrough:58, LZH8:6, passthrough:1, media passthrougffmpeg version git-2026-08-28-711882907a Copyright (c) 2000-2026 the FFmpeg developers:1, SDAT:1 | PASS |
| Theta | ✅ | 1 / 2 | 330 (155.9MB) | passthrough:1, SDAT:1 | PASS |
| Tingle's Balloon Fight | ✅ | 11 / 12 | 46 (8.3MB) | ?:3, NCER XML:3, NANR XML:3, passthrough:1, SDAT:1 | PASS |
| Tomodachi Collection | 🟡 | 6563 / 6564 | 15346 (297.8MB) | NARC:2312, LZ10:1968, ?:1482, NSCR:574, NANR XML:113 | EXIT_64 |
| Touch! Bomberman Land | ✅ | 25 / 27 | 3640 (36.4MB) | LZH8:10, LZ10:6, LZ11:6, passthrough:2, SDAT:2 | PASS |
| Trace Memory | ✅ | 1 / 2 | 156 (34.0MB) | passthrough:1, SDAT:1 | PASS |
| Trauma Center: Under the Knife | ✅ | 1 / 2 | 25 (22.1MB) | passthrough:1, SDAT:1 | PASS |
| True Swing Golf | ✅ | 1297 / 1299 | 6231 (136.3MB) | ?:467, MSH:405, NCER XML:168, NANR XML:158, NSCR:92 | PASS |
| Unoe Sangkwae Teullin Geurim Jeonjip | ✅ | 2 / 4 | 127 (29.4MB) | passthrough:2, LZH8:1, SDAT:1 | PASS |
| Viewtiful Joe: Double Trouble! | ✅ | 442 / 443 | 18352 (292.3MB) | ?:289, NCER XML:123, NSCR:24, QuickLZ:4, passthrough:1 | PASS |
| Wario: Master of Disguise | ✅ | 1 / 2 | 143 (54.2MB) | passthrough:1, SDAT:1 | PASS |
| WarioWare D.I.Y. | ✅ | 1005 / 1006 | 7430 (40.4MB) | ?:484, LZ10:226, NSCR:152, NCER XML:77, NANR XML:65 | PASS |
| WarioWare: Touched! | ✅ | 1000 / 1001 | 26726 (149.5MB) | LZ10:538, NCCARC:305, ?:82, NANR XML:37, NCER XML:37 | PASS |
| Wi-Fi Taiō Yakuman DS | ✅ | 1 / 3 | 1223 (31.4MB) | passthrough:2, SDAT:1 | PASS |
| Yakuman DS | ✅ | 1 / 3 | 1223 (31.4MB) | passthrough:2, SDAT:1 | PASS |
| Yoshi Touch & Go | ✅ | 38 / 39 | 2312 (20.9MB) | LZ10:36, SDAT:2, passthrough:1 | PASS |
| Yoshi's Island DS | ✅ | 688 / 689 | 1469 (71.8MB) | LZ10:648, PACK:35, LZH8:3, SDAT:2, passthrough:1 | PASS |
| Zekkyō Senshi Sakeburein | ✅ | 1 / 3 | 176 (13.7MB) | passthrough:2, SDAT:1 | PASS |

## Nintendo DS Bugfixes & Findings (This Session)

- **Super Mario 64 DS BMD0 / NSBMD Integer Overflow Fix (`lib-nsbmd.c`)**:
  - *Issue*: `ParseEarlyDSBMD()` suffered a 32-bit unsigned integer overflow when validating display list offsets (`dloff + sz <= size`), causing out-of-bounds pointer calculations and SIGBUS / `EXC_BAD_ACCESS` when reading display list opcodes.
  - *Fix*: Hardened bounds checks with overflow-safe `sz >= 16 && sz <= size && dloff <= size - sz && dloff + 4 <= size`. All 502 BMD models in Super Mario 64 DS and across all other DS games now parse cleanly into GLB.

- **NARC Shift-JIS / Invalid UTF-8 Path Traversal Fallback (`wszst.c`)**:
  - *Issue*: `valid_sarc_path()` rejected Shift-JIS / Windows shortcut (`.lnk`) filenames present in retail DS NARC archives (such as *Animal Crossing: Wild World*), which previously aborted the entire archive extraction with `ERROR #36`.
  - *Fix*: Aligned NARC extraction with SARC/RST behavior by falling back to synthetic `file_%04u.bin` filenames instead of aborting the archive.

- **Uninitialized Stack Pointer Free in Nitro Sprite / Screen Decoder (`lib-nitro.c` & `wszst.c`)**:
  - *Issue*: In `sprites_from_base()` and `nscr_from_base()`, `nitro_nclr_t nclr` on the stack was left uninitialized. If earlier format scans failed or short-circuited in `||`, `ResetNitroNCLR(&nclr)` invoked `FREE(nclr->rgba)` on uninitialized stack garbage (`0x16fd...`), triggering SIGABRT in macOS libmalloc (*Pokémon Trozei!*, etc.).
  - *Fix*: Added zeroing at struct declarations and at the entry of `ScanNitroNCLR()`, guaranteeing `nclr->rgba` is NULL on error paths.
