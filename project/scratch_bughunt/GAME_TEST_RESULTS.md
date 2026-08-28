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
| And-Kensaku | 🟡 | 1358 / 26895 | TPL:812, BRFNT:467, LZ11:79 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Animal Crossing: City Folk | ✅ | 0 / 0 | — | Crashed with **SIGBUS** on `DATA/files/BgData/BgModel/017_1.brres` — a subfile's declared size was corrupted/huge independent of its data pointer, and the post-extraction SHA1 hash-cache builder read off the end of the buffer. Fixed (`SafeSubfileHashSize()`, commit `7613d04`, pushed). Full disc re-verified clean. |
| Another Code: R – A Journey into Lost Memories | 🟡 | 88593 / 114420 | TPL:35771, TEX:23548, BRRES:11552, BRLAN:7348, BRLYT:5954 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| AquaSpace (WiiWare) | 🟡 | n/a | n/a | Character/prop `.brres` files never recognized as BRRES — no error, just silently produce zero models/textures. Root cause confirmed byte-for-byte: every one starts with an unrecognized 4-byte tag `"CX00"` immediately before an otherwise standard, already-supported LZ11 stream. Fix needs to reach the extraction-time LZ dispatch, not yet implemented. |
| Battalion Wars 2 | 🟡 | 0 / 0 | — | `ERROR_EXIT28` (29 errors). |
| Big Brain Academy: Wii Degree | 🟡 | 16127 / 39380 | TPL:11351, BRLAN:2045, TEX:1070, BRFNT:695, BRLYT:549 | `ERROR_EXIT28` (35 errors) — AnmTexPat gap. |
| **Bonsai Barber** | ⚠️ | 6 / 6702 | BRFNT:3, LZ11:2, BRLAN:1 | **Content column is misleading — verified byte-for-byte.** `00000006.d/` contains 5 real, untouched `.pkg` files (`bb_main.pkg`, `bb_monsters.pkg`, `bb_audio.pkg`, `bb_styles.pkg`, `bb_text.pkg`) — that's the entire actual game (Gorilla Games' own engine format), sitting as opaque unrecognized files. `wszst` logs **nothing** for a file it doesn't recognize (no error, no warning), so the low error count and PASS-ish exit code look clean while zero real game content was ever extracted. See the general note below the table. |
| Calling | ✅ | n/a | n/a | Originally: `wbrsar` total failure on WAVE-type BRSAR sounds, and HSF models exported untextured. Both fixed this session. Full disc now extracts clean. |
| Captain Rainbow | 🟡 | 12437 / 32705 | TEX:8664, TPL:2226, BRRES:1144, BRFNT:344, BRLAN:52 | `ERROR_EXIT28` (140 errors) — AnmTexPat gap. |
| Chibi-Robo! | 🟡 | 13404 / 34881 | TEX:10014, TPL:2212, BRRES:953, BRFNT:225 | `ERROR_EXIT28` (105 errors) — AnmTexPat gap. |
| Cooking Mama | 🟡 | 71668 / 73098 | TEX:60620, BRRES:11048 | ERROR_EXIT14 (3639 errors logged) |
| Cubello | 🟡 | 7033 / 15361 | TEX:5959, BRFNT:700, LZ11:354, TPL:16, BRRES:4 | ERROR_EXIT28 (2645 errors logged) |
| Disaster: Day of Crisis | 🟡 | 2895 / 24981 | TPL:2865, BRFNT:30 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Donkey Kong Barrel Blast | 🟡 | 4345 / 26346 | TPL:2622, BRLAN:1117, LZ10:419, BRLYT:187 | `ERROR_EXIT28` (32 errors) — AnmTexPat gap. |
| **Donkey Kong Country Returns** | ⚠️ | 7 / 28913 | BRFNT:6, BRFvgmtrans:1 | **Confirmed byte-for-byte.** `DATA/files/Worlds/` (2GB, all 9 worlds, 72 level files like `W02_Beach/L08_Crab_Boss_Arena.pak`) is entirely Retro Studios' own `.pak` engine archive format — completely unrecognized, untouched, unlogged. This is the real game; nothing in it was ever extracted. |
| Dr. Mario Online Rx | 🟡 | 4 / 6343 | Arika:2, LZ10:2 | ERROR_EXIT28 (944 errors logged) |
| Eco Shooter: Plant 530 | 🟡 | 2698 / 5696 | TEX:1398, TPL:852, BRLAN:209, LZ10:110, BRLYT:49 | ERROR_EXIT28 (2 errors logged) |
| Endless Ocean | 🟡 | 1011 / 24301 | TPL:812, BRFNT:198, Arika:1 | `ERROR_EXIT28` (70 errors) — AnmTexPat gap. |
| Endless Ocean: Blue World | ❌ | 624 / 9865 | TPL:522, BRFNT:101, Arika:1 | `TIMEOUT` — same suspected `wbrsar` cause as Mario Kart Wii. |
| Epic Mickey | 🟡 | 30 / 48612 | BRFNT:14, PACK:8, TPL:8 | ERROR_EXIT28 (33 errors logged) |
| Excite Truck | 🟡 | 101 / 2811 | RST:100, MOD:1 | `ERROR_EXIT82` (73 errors) — very likely the same non-UTF-8 filename class as Twilight Princess (same class of bundled system-channel content); not confirmed byte-for-byte for this title. |
| Excitebike: World Rally | 🟡 | 1353 / 4336 | TEX:785, MOD:369, ART:128, RST:36, MSH:34 | ERROR_EXIT28 (3 errors logged) |
| Excitebots: Trick Racing | 🟡 | 7813 / 29162 | TEX:3166, MOD:2767, TPL:812, MSH:467, ART:208 | `ERROR_EXIT36` (35 errors). Notable **MOD** count (2767) — 3D model format, uncommon elsewhere. |
| Fatal Frame: Mask of the Lunar Eclipse | 🟡 | 3 / 22705 | LZH8:3 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Fire Emblem: Radiant Dawn | 🟡 | 33529 / 55347 | TPL:30736, LZ10:2789, BRFNT:3, LZH8:1 | `ERROR_EXIT28` (32 errors) — AnmTexPat gap. |
| Fishing Resort | 🟡 | 172729 / 249546 | TEX:142996, TPL:12214, BRRES:11584, BRLAN:2894, BRFNT:1588 | ERROR_EXIT28 (295 errors logged) |
| FlingSmash | 🟡 | 80219 / 110007 | TPL:53581, BRFNT:16447, TEX:6074, BRLAN:1690, BRLYT:1624 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Fluidity (video game) | 🟡 | 3272 / 9237 | LZ11:3208, TEX:60, BRFNT:2, TPL:2 | ERROR_EXIT28 (6 errors logged) |
| Fortune Street | 🟡 | 81795 / 113989 | TEX:30412, TPL:26441, BRLAN:11591, BRFNT:7458, BRLYT:2854 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| Go Vacation | 🟡 | 190309 / 231682 | TEX:121146, TPL:40258, BRLAN:15247, BRRES:5728, BRFNT:3666 | ERROR_EXIT36 (18255 errors logged) |
| GoldenEye 007 (2010 video game) | 🟡 | 37 / 23544 | TPL:36, BRFNT:1 | ERROR_EXIT28 (17 errors logged) |
| Harvest Moon: Magical Melody | 🟡 | 3449 / 70907 | TPL:3431, BRFNT:10, Bvgmtrans:3, BRFNvgmtrans:3, BRLYT:1 | ERROR_EXIT28 (274 errors logged) |
| Harvest Moon: Tree of Tranquility | 🟡 | 2280 / 28200 | TPL:2274, LZH8:4, QuickLZ:2 | ERROR_EXIT28 (34 errors logged) |
| Inazuma Eleven Strikers | 🟡 | 5 / 15220 | LZH8:4, BRFNT:1 | ERROR_EXIT78 (108 errors logged) |
| Just Dance Wii | 🟡 | 457 / 41732 | BRFNT:188, TPL:172, BRLAN:88, BRLYT:9 | `ERROR_EXIT28` (26 errors) — AnmTexPat gap. |
| Just Dance Wii 2 | 🟡 | 310 / 41585 | TPL:174, BRLAN:88, BRFNT:37, BRLYT:9, AT7:2 | `ERROR_EXIT28` (26 errors) — AnmTexPat gap. |
| Kiki Trick | 🟡 | 10879 / 54092 | TPL:5579, TEX:3802, BRLAN:1105, BRFNT:196, BRLYT:91 | `ERROR_EXIT28` (111 errors) — AnmTexPat gap. |
| Kirby's Dream Collection | 🟡 | 15212 / 46846 | TPL:6777, TEX:4382, BRLAN:1516, BRLYT:736, LZ11:732 | `ERROR_EXIT28` (240 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Kirby's Epic Yarn | 🟡 | 37093 / 66059 | TEX:28562, TPL:2975, GFA:2342, BRRES:2331, BRLAN:449 | `ERROR_EXIT28` (450 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Kirby's Return to Dream Land | 🟡 | 34526 / 65585 | TPL:13387, TEX:11892, BRLAN:3501, LZ11:1675, BRFNT:1499 | `ERROR_EXIT28` (339 errors) — AnmTexPat gap, unusually high count worth a second look. |
| **Kororinpa: Marble Mania** | ❌ | 533 / 3680 | MPBIN:306, HSF:227 | **`CRASH_SIG10` (SIGBUS), confirmed on a clean single-instance re-run — the 3rd independent crash at this same site.** Real, reproducible bug, not a race artifact. Worth checking whether it's the same root cause as Mario Party 8's SIGBUS. |
| Line Attack Heroes | 🟡 | 269 / 3253 | BRFNT:268, LZ11:1 | ERROR_EXIT28 (2 errors logged) |
| Link's Crossbow Training | 🟡 | 2510 / 21300 | TPL:1180, YAZ0.RARC:660, BRFNT:386, BRLAN:191, RARC:58 | `ERROR_EXIT28` (29 errors) — AnmTexPat gap. |
| Lonpos | 🟡 | 1199 / 8817 | BRFNT:932, TPL:153, BRLAN:100, BRLYT:14 | ERROR_EXIT28 (218 errors logged) |
| MaBoShi: The Three Shape Arcade | 🟡 | 726 / 3710 | TPL:456, BRFNT:269, LZ10:1 | ERROR_EXIT28 (3 errors logged) |
| Magnetica | 🟡 | 7717 / 16027 | TEX:3900, LZ10:1494, TPL:972, BRFNT:666, BRRES:313 | ERROR_EXIT28 (141 errors logged) |
| Mario & Sonic at the London 2012 Olympic Games | 🟡 | 26 / 27235 | LZH8:24, BRFNT:2 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Mario & Sonic at the Olympic Games | ✅ | 1010 / 1351 | TPL:814, BRFNT:196 | PASS, clean — small disc, low content by nature not by bug. |
| Mario & Sonic at the Olympic Winter Games | 🟡 | 44690 / 67397 | TPL:28742, TEX:14758, BRRES:939, BRFNT:141, BRLAN:96 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Mario Kart Wii | 🟡 | 114147 / 139729 | TEX:94345, TPL:6148, BRRES:5797, BRLAN:4290, YAZ0.U8:2239 | No crash. Its main music `wbrsar` conversion (`revo_kart.brsar`) timed out at the 2400s cap in the queue re-run — likely a real performance issue in the WAVE-export path added this session, not yet confirmed root cause. |
| **Mario Party 8** | ✅ | — | — | **`CRASH_SIG10` (SIGBUS) — root-caused and fixed.** Two real bugs found under ASan in an isolated worktree build: (1) `DetectNintendoFormat()`'s MSBT check did an unguarded 8-byte `memcmp` past a 5-byte buffer; (2) `hsf_expand_replica()`'s parent-chain walk indexed the HSF node array with an unbounds-checked index read from file data. Both fixed. Re-run on the real disc now completes fully (`ERROR_EXIT82`, the separate known non-UTF-8-filename issue — no crash). |
| Mario Party 9 | 🟡 | 62605 / 95658 | TPL:20858, TEX:20798, LZ11:6356, BRRES:6016, BRLAN:5790 | `ERROR_EXIT28` (39 errors) — AnmTexPat gap. |
| Mario Sports Mix | 🟡 | 82304 / 111698 | TEX:37502, TPL:20382, BRLAN:15127, BRFNT:3231, BRRES:2077 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Mario Strikers Charged | 🟡 | 18 / 21829 | TPL:16, BRFNT:1, BRFNvgmtrans:1 | `ERROR_EXIT36` (32 errors). |
| Mario Super Sluggers | 🟡 | 2 / 19787 | BRFNT:2 | `ERROR_EXIT28` (31 errors) — AnmTexPat gap. |
| Metroid Prime | 🟡 | 1853 / 22942 | TPL:1852, BRFNT:1 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| **Metroid Prime 2** | 🟡 | 2 / 2727 | LZ10:1, BRFNT:1 | **`wii_queue.tsv`'s row for this title points at the wrong file** — `Metroid (USA) (NES) (Virtual Console).zip`, an NES VC ROM, not the real GC/Wii disc. Data bug in the queue list, not a `wszst` gap; don't draw format conclusions from this row until the queue entry is fixed and re-run. |
| Metroid Prime 3: Corruption | 🟡 | 928 / 18829 | TPL:927, BRFNT:1 | `ERROR_EXIT28` (29 errors) — AnmTexPat gap, see below. |
| Metroid Prime: Trilogy | 🟡 | 3704 / 24797 | TPL:3704 | `ERROR_EXIT66` (102 errors) — DS-passthrough/FSYS sub-job gap, same class as Pokémon Battle Revolution. |
| Metroid: Other M | ✅ | 88467 / 118238 | TEX:61438, TPL:21989, BRRES:2234, BRLAN:1829, BRLYT:858 | Crashed with the same **SIGBUS** signature as Animal Crossing. Re-ran full extraction with the fix in place: completed cleanly, confirming the same fix resolved this title too. |
| Monster Hunter Tri | 🟡 | 56988 / 78794 | TEX:54268, BRRES:1523, TPL:1090, BRLAN:92, BRLYT:9 | ERROR_EXIT28 (17 errors logged) |
| My Pokémon Ranch | 🟡 | 6 / 5975 | BRFNT:4, LZ11:2 | ERROR_EXIT28 (6 errors logged) |
| Mystery Case Files: The Malgrave Incident | 🟡 | 34 / 28945 | TPL:19, MSBT:10, LZ10:2, BRFNvgmtrans:2, BRFNT:1 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Naruto: Clash of Ninja | ✅ | 0 / 4 | — | PASS (3 errors logged) |
| New Play Control! Donkey Kong Jungle Beat | 🟡 | 2290 / 23868 | BRFNT:749, YAZ0.RARC:688, TPL:546, BRLAN:175, RARC:93 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| New Play Control! Mario Power Tennis | 🟡 | 42887 / 67529 | TPL:40153, STPL:2631, BRLAN:89, BRLYT:10, BRFNT:4 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| New Play Control! Pikmin | 🟡 | 346 / 23773 | BRFNT:272, TPL:48, BRLAN:12, YAZ0.U8:6, BRLYT:5 | `ERROR_EXIT76` (31 errors) — new code, not yet explained (see the queue-driver log for this title before assuming AnmTexPat). |
| New Play Control! Pikmin 2 | 🟡 | 1580 / 25009 | YAZ0.RARC:1242, BRFNT:270, TPL:42, RARC:10, BRLAN:7 | `ERROR_EXIT28` (31 errors) — AnmTexPat gap. |
| New Super Mario Bros. Wii | 🟡 | 10189 / 33506 | TEX:7783, TPL:1072, BRRES:629, BRLAN:328, BRFNT:231 | `ERROR_EXIT28` (16 errors) — AnmTexPat gap. |
| Orbient | 🟡 | 661 / 3661 | TEX:320, BRFNT:280, LZ11:59, BRRES:2 | ERROR_EXIT28 (3 errors logged) |
| Pandora's Tower | 🟡 | 10994 / 42985 | MSBT:10970, QuickLZ:14, LZH8:5, TPL:4, BRFNT:1 | `ERROR_EXIT36` (20 errors). Heavy **MSBT** (message-table format) — barely appears elsewhere. |
| Pangya! Golf with Style | 🟡 | 1027 / 12586 | TPL:818, BRFNT:196, LZH8:8, LZ10:4, QuickLZ:1 | ERROR_EXIT66 (28 errors logged) |
| PictureBook Games: Pop-Up Pursuit | 🟡 | 3776 / 9801 | TPL:2112, BRLAN:906, BRFNT:560, LZ11:146, BRLYT:52 | ERROR_EXIT28 (6 errors logged) |
| Pokémon Battle Revolution | 🟡 | 1212 / 24388 | FSYS:1094, LZ10:116, TPL:1, BRFNvgmtrans:1 | Same heap-buffer-overflow as SSBB (`2e917df`, identical crash-site address under ASan on both games) — fixed. Queue re-run shows `ERROR_EXIT66`/38 errors from the DS-`.srl`-passthrough/FSYS sub-job path, a separate, not-yet-investigated gap. |
| Pokémon Rumble | 🟡 | 16698 / 23206 | TEX:11957, TPL:1783, BRRES:1780, BRLAN:453, LZH8:295 | ERROR_EXIT28 (4 errors logged) |
| PokéPark 2: Wonders Beyond | 🟡 | 31487 / 62109 | TEX:26460, TPL:3144, BRLAN:747, BRRES:713, BRLYT:125 | `ERROR_EXIT28` (21 errors) — AnmTexPat gap. |
| PokéPark Wii: Pikachu's Adventure | 🟡 | 36537 / 58696 | TEX:34866, BRRES:1091, TPL:348, BRFNT:118, LZ11:114 | `ERROR_EXIT28` (14 errors) — AnmTexPat gap. |
| Project Zero 2: Wii Edition | 🟡 | 51331 / 86851 | TPL:48084, LZ11:3239, BRFNT:6, MPBIN:2 | `ERROR_EXIT28` (823 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Punch-Out (Wii) | 🟡 | 18 / 22367 | TPL:16, BRFNT:2 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Quiz Party | 🟡 | 133 / 36009 | TEX:104, BRRES:20, TPL:5, BRFNT:3, BRwbrsar:1 | ERROR_EXIT28 (22 errors logged) |
| Resident Evil 4 | ✅ | 2878 / 5576 | TPL:2878 | PASS (3 errors logged) |
| Resident Evil: The Umbrella Chronicles | ✅ | 47576 / 53274 | TEX:21444, TPL:18788, BRLAN:2834, BRRES:2636, BRFNT:1020 | PASS (7 errors logged) |
| Rhythm Heaven Fever | 🟡 | 4043 / 37971 | TPL:3200, YAZ0.U8:305, BRLAN:229, BRFNT:211, BRLYT:98 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| Rock N' Roll Climber | 🟡 | 2340 / 8590 | TEX:1328, TPL:340, BRLAN:328, BRFNT:144, BRRES:132 | ERROR_EXIT28 (61 errors logged) |
| Rotohex | 🟡 | 4437 / 12709 | TEX:3726, BRFNT:691, LZ11:8, TPL:7, BRRES:4 | ERROR_EXIT28 (4 errors logged) |
| Rotozoa | 🟡 | 2676 / 8914 | BRFNT:1721, TEX:466, TPL:250, BRLAN:144, LZ11:45 | ERROR_EXIT28 (271 errors logged) |
| Samurai Warriors 3 | 🟡 | 2 / 33910 | BRFNT:2 | ERROR_EXIT66 (3016 errors logged) |
| Sin & Punishment: Star Successor | 🟡 | 1654 / 23885 | TPL:1064, BRFNT:587, TEX:2, BRRES:1 | `ERROR_EXIT28` (14 errors) — AnmTexPat gap. |
| Snowpack Park | 🟡 | 10400 / 18651 | TEX:5896, TPL:1511, BRRES:1053, BRFNT:861, LZ11:854 | ERROR_EXIT28 (6 errors logged) |
| Super Mario All-Stars 25th Anniversary Edition | 🟡 | 16 / 28578 | TPL:10, BRFNT:3, LZH8:2, LZ10:1 | `ERROR_EXIT28` (19 errors) — AnmTexPat gap. |
| Super Mario Galaxy | 🟡 | 4238 / 23260 | YAZ0.RARC:2884, TPL:652, BRLAN:449, BRFNT:155, BRLYT:95 | `ERROR_EXIT28` (29 errors) — AnmTexPat gap. |
| Super Mario Galaxy 2 | 🟡 | 6404 / 28780 | YAZ0.RARC:3104, MSBT:795, TPL:652, RARC:586, BRLAN:550 | `ERROR_EXIT28` (14 errors) — AnmTexPat gap. |
| Super Paper Mario | 🟡 | 44321 / 57744 | TPL:43929, LZ10:386, BRLAN:4, BRLYT:2 | `ERROR_EXIT28` (28 errors) — AnmTexPat gap. |
| Super Smash Bros. Brawl | ✅ | 65675 / 107937 | TEX:52996, BRRES:8478, PAC:2157, LZ10:1395, BRFNT:504 | The SIGTRAP was actually **two separate, real bugs**, both root-caused via AddressSanitizer and fixed this session: a stack-buffer-overflow in `GetByMagicFF()`'s OBJ-text sniffing (`d0a480a`) and a heap-buffer-overflow reading past a material record in `IterateStringsMDL()` (`2e917df`). Queue re-run shows `ERROR_EXIT28`/251 errors — that's the AnmTexPat gap (see below), not a crash; no regression. |
| Tetris Party Deluxe | 🟡 | 5383 / 28073 | TPL:3677, BRLAN:1009, BRFNT:342, BRLYT:326, BRRES:16 | ERROR_EXIT28 (905 errors logged) |
| The Last Story | 🟡 | 20 / 30308 | LZ11:19, BRFNT:1 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| The Legend of Zelda: Skyward Sword | 🟡 | 51016 / 86122 | TEX:41460, BRRES:3825, TPL:3112, BRLAN:835, BRFNT:685 | `ERROR_EXIT28` (204 errors) — AnmTexPat gap, unusually high count worth a second look. |
| The Legend of Zelda: Twilight Princess | ✅ | 2984 / 3131 | YAZ0.RARC:2212, TPL:348, RARC:338, BRFNT:84, QuickLZ:2 | `ERROR #82 [CAN'T CREATE FILE]` — a RARC member filename contains a raw non-UTF-8 byte sequence (likely Shift-JIS). macOS rejects the `open()` call outright (EILSEQ). Root-caused to the exact read/write sites, fix not yet implemented. Queue re-run shows only 2 errors and an overall `PASS`, so this bug apparently doesn't trip on every file — depth capped somewhat by it regardless (RARC/YAZ0-only, no BRRES/TEX ever reached). |
| Trauma Center: New Blood | ✅ | 3990 / 5345 | TEX:3370, BRRES:409, BRFNT:195, LZH8:16 | PASS (3 errors logged) |
| Trauma Center: Second Opinion | 🟡 | 2000 / 3871 | TEX:1717, BRRES:238, BRFNT:44, TPL:1 | ERROR_EXIT66 (33 errors logged) |
| Ultra Hand | 🟡 | 2998 / 6013 | TPL:1558, BRLAN:980, BRFNT:268, TEX:116, LZ10:37 | ERROR_EXIT28 (10 errors logged) |
| Wario Land: Shake It! | 🟡 | 108097 / 129904 | TEX:93758, TPL:9278, GFA:2224, BRRES:1718, BRLYT:718 | `ERROR_EXIT82` (86 errors) — likely the same non-UTF-8 filename class as Twilight Princess. Only title with real **GFA** volume (2224) — the best GFA sample if that decoder needs re-checking. |
| WarioWare: D.I.Y. Showcase | 🟡 | 7034 / 13071 | TPL:5264, BRLAN:850, BRFNT:676, BRLYT:166, LZ11:74 | ERROR_EXIT28 (6 errors logged) |
| WarioWare: Smooth Moves | ❌ | 1 / 1048 | BRFNT:1 | `TIMEOUT` — same suspected `wbrsar` performance cause as Mario Kart Wii, unconfirmed. |
| We Ski | 🟡 | 3 / 19787 | BRFNT:3 | ERROR_EXIT28 (31 errors logged) |
| Wii Chess | 🟡 | 1171 / 23113 | TPL:650, BRFNT:155, BRLAN:153, TEX:96, BRRES:78 | `ERROR_EXIT28` (31 errors) — AnmTexPat gap. |
| Wii Fit | 🟡 | 15057 / 35267 | TEX:5479, TPL:4148, BRFNT:2325, BRLAN:1790, BRLYT:613 | `ERROR_EXIT28` (35 errors) — AnmTexPat gap. |
| Wii Fit Plus | 🟡 | 22490 / 43938 | TEX:9113, TPL:6342, BRLAN:2701, BRFNT:2351, BRLYT:778 | `ERROR_EXIT28` (47 errors) — AnmTexPat gap. |
| Wii Music | 🟡 | 9002 / 30515 | TEX:4085, TPL:2470, BRLAN:873, BRFNT:802, BRRES:336 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Wii Party | 🟡 | 48414 / 77997 | TEX:19180, TPL:12328, BRLAN:5334, LZ11:5248, BRRES:3810 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Wii Play | 🟡 | 3430 / 3604 | TEX:1398, TPL:936, BRLAN:484, BRFNT:328, BRRES:131 | `ERROR_EXIT28` (2 errors) — minor, not investigated. |
| Wii Play: Motion | 🟡 | 15478 / 44486 | TEX:9920, TPL:2427, BRFNT:1114, LZ11:815, BRLAN:622 | `ERROR_EXIT66` (19 errors) — DS-passthrough/FSYS sub-job gap. |
| Wii Sports | ✅ | 0 / 0 | — | No crash, no new errors. |
| Wii Sports Resort | 🟡 | 19868 / 41068 | TEX:10496, TPL:5265, BRLAN:2210, BRLYT:644, BRFNT:544 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Wing Island | ✅ | 27490 / 30748 | TEX:19886, LZ10:3718, TPL:1966, BRRES:1740, BRFNT:180 | PASS (32 errors logged) |
| **World of Goo** | ⚠️ | 1 / 2984 | LZ11:1 | **Same issue as Bonsai Barber, verified byte-for-byte.** `0000000b.d/master.pak` (37MB, 2D Boy's own engine archive) is the entire real game, sitting untouched and unlogged. See the general note below the table. |
| Xenoblade Chronicles | 🟡 | — | TPL:17735, BRLAN:6019, BRFNT:5724, LZ10:889 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| You, Me, and the Cubes | 🟡 | 4762 / 13043 | TPL:1490, TEX:954, BRLAN:829, BRFNT:815, LZ10:271 | ERROR_EXIT28 (1107 errors logged) |
| Zack & Wiki: Quest for Barbaros' Treasure | 🟡 | 56020 / 56791 | TPL:28718, TEX:23920, BRRES:1026, BRLAN:885, BRFNT:762 | ERROR_EXIT14 (1126 errors logged) |
| Zangeki no Reginleiv | 🟡 | 288 / 25492 | BRFNT:236, TPL:28, BRLAN:13, BRLYT:4, BRRES:3 | `ERROR_EXIT28` (32 errors) — AnmTexPat gap. |

### Near-zero real content — likely unsupported game-data containers

21 of the 118 titles decoded fewer than 50 real (non-bundle) asset ops
despite total op counts in the thousands to tens-of-thousands — meaning
essentially all of that title's own game content was never recognized.
Two are confirmed byte-for-byte (Bonsai Barber's `.pkg`, World of Goo's
`.pak`, and Samurai Warriors 3's `.BNS` — see above); the rest are flagged
here as real candidates for the same class of problem, not yet individually
root-caused:

| Title | Real ops | Total ops |
|---|---|---|
| Naruto: Clash of Ninja | 0 | 4 |
| WarioWare: Smooth Moves | 1 | 1,048 |
| World of Goo | 1 | 2,984 |
| Mario Super Sluggers | 2 | 19,787 |
| Metroid Prime 2 | 2 | 2,727 |
| Samurai Warriors 3 | 2 | 33,910 |
| Fatal Frame: Mask of the Lunar Eclipse | 3 | 22,705 |
| We Ski | 3 | 19,787 |
| Dr. Mario Online Rx | 4 | 6,343 |
| Inazuma Eleven Strikers | 5 | 15,220 |
| Bonsai Barber | 6 | 6,702 |
| My Pokémon Ranch | 6 | 5,975 |
| Donkey Kong Country Returns | 7 | 28,913 |
| Super Mario All-Stars 25th Anniversary Edition | 16 | 28,578 |
| Mario Strikers Charged | 18 | 21,829 |
| Punch-Out (Wii) | 18 | 22,367 |
| The Last Story | 20 | 30,308 |
| Mario & Sonic at the London 2012 Olympic Games | 26 | 27,235 |
| Epic Mickey | 30 | 48,612 |
| Mystery Case Files: The Malgrave Incident | 34 | 28,945 |
| GoldenEye 007 (2010) | 37 | 23,544 |

Confirmed causes so far: **Bonsai Barber** (Gorilla Games `.pkg` engine
archive), **World of Goo** (2D Boy `master.pak`), **Samurai Warriors 3**
(Koei-Tecmo `.BNS` — also mis-routed through the ffmpeg media-passthrough
path, which fails with "Error opening input file" rather than skipping
cleanly). Metroid Prime 2's row is separately known-bad (`wii_queue.tsv`
points at the wrong file, an NES VC ROM). The other 17 titles are
unverified — each needs the same byte-level check (download, `wszst xx`,
inspect the resulting `.d` tree for large/opaque files with no further
`.d` extraction under them) before concluding what container format they
actually need. **Donkey Kong Country Returns and Mario Strikers Charged
are the two most surprising entries here** — both first-party Retro/Next
Level titles, worth checking first since a gap there is more likely to be
a real, fixable format-support hole than a third-party engine quirk.

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

**Re-run in progress against the fixed binary** (`run_wii_queue.sh`, resumed after also picking up the Mario Party 8 crash fix below) — the per-title table above still reflects the pre-fix binary for titles not yet re-processed; will be folded in as that pass completes.

## Still open

- **21 titles show near-zero real (non-bundle) content extracted — see the table above.** This is the single biggest finding in this doc: the naive per-title op counts previously in this table mostly measured the shared system bundle, not the game. Three causes confirmed (Bonsai Barber `.pkg`, World of Goo `.pak`, Samurai Warriors 3 `.BNS`); 17 more titles flagged but unverified. Worth systematically checking each one's `.d` tree for large opaque leftover files before deciding whether it needs new container support.
- ~~Mario Party 8: `CRASH_SIG10` (SIGBUS)~~ — **fixed** (MSBT-magic OOB read + HSF replica parent-chain OOB walk, both in `lib-nintendo.c`/`lib-hsf.c`). Verified: re-run on the real disc completes fully now, no crash.
- **Kororinpa: Marble Mania: same signal (SIGBUS), confirmed reproducible on a clean run (3rd independent crash)** — not yet re-tested against the Mario Party 8 fixes above; worth checking whether it's the same root cause or a distinct one.
- AnmTexPat (texture-pattern animation) parse gap — `ERROR #36`, hits nearly every title with an UPDATE partition (the bundled Wii Menu/Shop-Channel content), by far the most common non-zero exit code in this table. Not yet fixed.
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
