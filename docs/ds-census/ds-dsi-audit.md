# Nintendo DS / DSi file format audit

Every title in the decrypted No-Intro sets of `mcubeds:` (Nintendo - Nintendo DS / Cartridges (Decrypted), Nintendo - Nintendo DSi / Cartridges (Decrypted) and Digital (CDN) (Decrypted)): 10,821 zips, 10,017 of them with a readable game filesystem (the other 36 are BIOS, firmware and NAND dumps, which have no files to list). Each ROM was unpacked with `ndstool -x` and every file was counted by extension and first four bytes. Regions were not deduplicated.

The data is in `ds-dsi-extensions.csv` (one row per extension: titles, files, MB, the commonest magics with their ASCII form, whether `wszst xx` produced output on samples, and a first example). 4966 distinct extensions were found.

## What `wszst xx` does with them

The 1,224 extensions that occur in at least 8 titles were each tried on up to 6 sample files (the commonest magics first): 372 produced output and 836 did not. "Did not" is a coarse test: it includes formats that `xx` leaves raw on purpose or that another nintoolbox command converts (for example plain NCGR/NCLR/NSCR tile data, NSBMD-family models, WAV, TGA, text and XML), and extensions that are only a game's own naming for a standard type, so the list needs the usual second pass (`classify_gaps.py`-style) before it counts as a gap list.

## Biggest groups that gave no output (by number of titles)

| extension | titles | files | MB | commonest magic |
| --- | ---: | ---: | ---: | --- |
| `.bin` | 5370 | 1372697 | 45378.4 | 00000000(....)=79469 |
| `.txt` | 2349 | 311749 | 459.7 | 01000100(....)=27148 |
| `.nscr` | 2249 | 277750 | 643.3 | 5243534e(RCSN)=257700 |
| `.dat` | 2175 | 195292 | 9456.7 | 00000000(....)=23051 |
| `.nbfp` | 1854 | 135381 | 28.6 | e0030000(....)=5362 |
| `.nbfc` | 1765 | 136881 | 1448.3 | 00000000(....)=62922 |
| `.pal` | 939 | 146650 | 64.9 | 09000400(....)=13932 |
| `.plt` | 920 | 19269 | 5.2 | 64737074(dspt)=951 |
| `.wav` | 867 | 52498 | 3709.2 | 52494646(RIFF)=35084 |
| `.ntfp` | 860 | 145551 | 60.2 | e07f0000(....)=7543 |
| `.char` | 822 | 3763 | 49.2 | 00000000(....)=1344 |
| `.nsbta` | 798 | 31631 | 44.0 | 42544130(BTA0)=28120 |
| `.nbfs` | 737 | 91188 | 453.2 | 00000100(....)=48254 |
| `.xml` | 681 | 98474 | 355.9 | 3c3f786d(<?xm)=36520 |
| `.fnt` | 633 | 10413 | 63.1 | 4e464944(NFID)=635 |
| `.bnr` | 591 | 778 | 3.4 | 03010000(....)=12 |
| `.nsbma` | 568 | 26937 | 24.3 | 424d4130(BMA0)=24447 |
| `.raw` | 435 | 77664 | 1126.4 | 01000000(....)=11697 |
| `.bmp` | 408 | 80503 | 955.6 | 43441800(CD..)=4484 |
| `.nmar` | 401 | 8946 | 3.4 | 52414d4e(RAMN)=8751 |
| `.nmcr` | 400 | 8972 | 2.8 | 52434d4e(RCMN)=8727 |
| `.ini` | 396 | 16069 | 33.9 | 5b414f4d([AOM)=6511 |
| `.h` | 370 | 17221 | 33.5 | 2f2f2d2d(//--)=12200 |
| `.db` | 340 | 34658 | 260.1 | 00000000(....)=16790 |
| `.tex` | 319 | 123917 | 1157.7 | 1f8b0800(....)=20440 |
| `.scr` | 304 | 43205 | 164.6 | 00000000(....)=8980 |
| `.csv` | 292 | 71826 | 106.3 | fffe3100(..1.)=11014 |
| `.pak` | 263 | 24159 | 4180.7 | 5245434e(RECN)=6320 |
| `.anm` | 229 | 22347 | 462.2 | 00000100(....)=1072 |
| `.scn` | 228 | 21210 | 133.9 | 00000000(....)=1811 |
| `.bat` | 225 | 873 | 0.4 | 64656c20(del )=171 |
| `.col` | 206 | 18865 | 144.4 | 00000000(....)=1771 |
| `.ahx` | 197 | 436222 | 7981.2 | 80000020(... )=433358 |
| `.dic` | 194 | 8100 | 321.1 | 53445257(SDRW)=5770 |
| `.rdt` | 184 | 742 | 741.9 | 52445401(RDT.)=680 |
| `.str` | 183 | 5835 | 205.9 | 00000000(....)=95 |
| `.spr` | 179 | 26494 | 131.1 | 01010120(... )=7441 |
| `.ani` | 175 | 22515 | 191.9 | 00100000(....)=7368 |
| `.loc` | 173 | 11473 | 37.9 | fffe5b00(..[.)=2446 |
| `.tga` | 172 | 37677 | 592.2 | 00010100(....)=8663 |

The rest of the 852 are in the CSV. Sample files (up to six per extension) are kept outside the repo in the audit folder.

## Triage of the no-output list

Two corrections to the raw "no output" count first. `wszst xx` on a single file with an unknown extension already unpacks a plain Nintendo LZ10/LZ11 stream (the result is written as `<file>.bin`), so most extensions in the list that are really just LZ-wrapped data (`.pak`, `.scn`, `.cbin`, `.msd`, `.saf`, `.anim`, `.set`, `.mdl`, `.bg`, `.lua`, `.jpg` ...) are decoded one layer deep; what is inside them is what needs triage. And the first probe used the first sample files of each extension instead of the commonest magic, so the table above uses the second probe.

Of the 836 extensions without output, 186 are named by an `is_ext` gate in an extractor (usually because the sample failed that extractor's checks, so they need a second look), and 152 more have a readable four-letter magic that no extractor mentions. Those 152 are listed below, most titles first (extension, titles, files, MB, magics with counts). Each one is a game-specific format; none of them has been reverse engineered yet except where marked.

| extension | titles | files | MB | magic |
| --- | ---: | ---: | ---: | --- |
| `.nmar` | 401 | 8946 | 3.4 | [('RAMN', 8751)] |
| `.nmcr` | 400 | 8972 | 2.8 | [('RCMN', 8727), ('SDPC', 66)] |
| `.ncl` | 149 | 10266 | 9.3 | [('NCCL', 10239)] |
| `.ncg` | 141 | 8850 | 209.9 | [('NCCG', 8830)] |
| `.sbin` | 136 | 629 | 252.5 | [('TAMS', 213)] |
| `.aap` | 118 | 29671 | 17.0 | [('JBOA', 29671)] |
| `.msg` | 100 | 27235 | 118.9 | [('0GSM', 5811)] |
| `.apl` | 77 | 5320 | 18.0 | [('D2FP', 3897), ('VNFP', 1423)] |
| `.def` | 76 | 3779 | 4.2 | [('H19S', 160), ('H20S', 160), ('H14S', 160)] |
| `.nbsq` | 62 | 5931 | 13.4 | [('NBSQ', 5843), ('33"3', 2)] |
| `.nce` | 61 | 4184 | 16.7 | [('NCOB', 4184)] |
| `.lst` | 61 | 2609 | 65.2 | [('..\\S', 708), ('.\\to', 190)] |
| `.dss` | 61 | 7506 | 167.8 | [('DSS0', 588)] |
| `.ngr` **done (LMPK/LMP2)** | 59 | 23684 | 242.2 | [('LMP2', 21508), ('LMPK', 1522), ('NGR1', 654)] |
| `.texture` | 55 | 30348 | 109.2 | [('""""', 448)] |
| `.gif` | 53 | 21929 | 176.5 | [('GIF8', 21817)] |
| `.sent` | 52 | 52 | 0.3 | [('NUOS', 52)] |
| `.dsf` | 51 | 7851 | 127.7 | [('DSF0', 4204)] |
| `.fon` | 50 | 205 | 2.8 | [('FDFT', 109), ('BCFG', 9)] |
| `.dsa` | 48 | 1726 | 253.5 | [('DSA1', 135)] |
| `.dsm` | 40 | 2475 | 14.9 | [('DSM1', 500)] |
| `.sft` | 39 | 225 | 1.6 | [('SKYF', 95)] |
| `.5pl` | 39 | 9641 | 2.0 | [('NTPL', 9641)] |
| `.fmv` | 38 | 1312 | 2200.3 | [('FMV!', 1271)] |
| `.urf` | 37 | 195 | 1599.9 | [('KTUN', 195)] |
| `.bncd` | 36 | 94 | 2.1 | [('JNCD', 94)] |
| `.skl` | 35 | 3218 | 3.1 | [('*OZD', 1422)] |
| `.lay` | 34 | 2574 | 31.7 | [('LAY!', 1473)] |
| `.emt` | 34 | 931 | 0.2 | [('SDPC', 809)] |
| `.textbin` | 33 | 210 | 1.0 | [('TXTB', 210)] |
| `.quo` | 30 | 120 | 14.5 | [('SERQ', 120)] |
| `.ocp` | 29 | 1185 | 24.4 | [('#OCP', 883)] |
| `.ds` | 29 | 750 | 116.0 | [('CRUS', 222)] |
| `.clb` | 28 | 1436 | 5.0 | [('CLBD', 988)] |
| `.bpa` | 28 | 1337 | 25.2 | [('#BPA', 75)] |
| `.dscol` | 27 | 698 | 27.6 | [('SDPC', 333), ('TSED', 297)] |
| `.5tx` | 27 | 4818 | 49.3 | [('NTTX', 4818)] |
| `.3d` | 27 | 598 | 124.9 | [('NS3D', 598)] |
| `.adb` | 26 | 28 | 1.8 | [('PAM_', 5)] |
| `.evt` | 25 | 3533 | 43.0 | [('SC01', 1916)] |
| `.cbin` | 25 | 14065 | 32.2 | [('DATB', 1722)] |
| `.bsf` | 25 | 516 | 0.3 | [('SDPC', 450)] |
| `.tix` | 24 | 2355 | 28.5 | [('VTIX', 2349)] |
| `.dau` | 24 | 673 | 231.7 | [('DAUP', 559)] |
| `.ntfpack` | 22 | 77785 | 195.3 | [('SDPC', 77641)] |
| `.an3g` | 22 | 3088 | 29.4 | [('G3NA', 3088)] |
| `.psc` | 21 | 10943 | 30.4 | [('psas', 8575)] |
| `.naf` | 20 | 3361 | 42.4 | [('NAF!', 3330), ('NAF ', 31)] |
| `.msd` | 20 | 2541 | 92.6 | [('MSDA', 2276)] |
| `.mob` | 20 | 2218 | 34.2 | [('<13,', 547)] |
| `.mcs` | 20 | 20 | 0.0 | [('X:\\p', 18), ('x:/p', 2)] |
| `.hex` | 20 | 20 | 0.5 | [(':100', 20)] |
| `.event` | 20 | 1097 | 1.7 | [('<13,', 65)] |
| `.tbb` | 19 | 942 | 2.9 | [('TBB1', 942)] |
| `.saf` | 19 | 16709 | 131.1 | [('SAF0', 16026), ('SAF ', 311)] |
| `.hash` | 19 | 62 | 0.1 | [('5f22', 3), ('5853', 3)] |
| `.dtx` | 19 | 968 | 3.9 | [('DSTX', 717)] |
| `.wsnd` | 18 | 1776 | 43.3 | [('WSND', 1776)] |
| `.mesh` | 18 | 1001 | 7.0 | [('M3DF', 495)] |
| `.itpr` | 18 | 852 | 0.1 | [('RPTI', 852)] |
| `.icxr` | 18 | 935 | 0.1 | [('RXCI', 935)] |
| `.ffn` | 18 | 190 | 5.8 | [('FntF', 190)] |
| `.dsir` | 18 | 11732 | 141.6 | [('BLDT', 6585)] |
| `.dscr` | 18 | 12241 | 3.7 | [('BLDT', 6373)] |
| `.dsbr` | 18 | 11692 | 33.4 | [('BLDT', 8211)] |
| `.dsar` | 18 | 7327 | 29.8 | [('BLDT', 4095)] |
| `.fmf` | 17 | 494 | 11.2 | [('FMF_', 294)] |
| `.bpd` | 17 | 54 | 0.2 | [('#BPD', 39)] |
| `.bb` | 17 | 366 | 75.3 | [('BKBK', 2), ('[2,4', 2)] |
| `.bad` | 17 | 32 | 1.0 | [('BPAD', 28)] |
| `.an3a` | 17 | 711 | 26.5 | [('A3NA', 711)] |
| `.tim2` | 16 | 1650 | 34.6 | [('2ITD', 1650)] |
| `.ohs` | 16 | 1192 | 4.8 | [('*OZD', 764)] |
| `.objs` | 16 | 625 | 31.8 | [('SJBO', 146), ('OBIS', 65)] |
| `.psf` | 15 | 87 | 0.2 | [('BCFG', 75)] |
| `.lev` | 15 | 1276 | 24.5 | [('SDPC', 552), ('LEV!', 156)] |
| `.edat` | 14 | 5443 | 14.5 | [('DtnE', 5433)] |
| `.bsd` | 14 | 803 | 0.3 | [('DSND', 45)] |
| `.xtl` | 13 | 311 | 0.0 | [('1,fa', 200), ('2,hi', 81), ('1,hi', 15)] |
| `.xds` | 13 | 773 | 3.2 | [('XDS!', 773)] |
| `.fa` | 13 | 37 | 572.3 | [('GFSA', 37)] |
| `.da` | 13 | 91 | 0.5 | [('#ART', 75)] |
| `.ptl` | 12 | 16 | 0.1 | [('*OZD', 16)] |
| `.pbg` | 12 | 1629 | 16.4 | [('D2KP', 430), ('PBG ', 5)] |
| `.ozt` | 12 | 14 | 4.2 | [('*OZD', 14)] |
| `.olt` | 12 | 92 | 4.5 | [('*OZD', 92)] |
| `.odx` | 12 | 85 | 0.0 | [('*OZD', 85)] |
| `.kdb` | 12 | 18 | 2.3 | [('YKBH', 2)] |
| `.itrr` | 12 | 40 | 0.0 | [('RRTI', 40)] |
| `.bcf` | 12 | 1559 | 8.4 | [('BCF!', 1559)] |
| `.ancl` | 12 | 3243 | 0.5 | [('ANCL', 3243)] |
| `.txa` | 11 | 643 | 2.0 | [('*OZD', 64)] |
| `.spp` | 11 | 270 | 1233.2 | [('KMSP', 250)] |
| `.pth` | 11 | 2822 | 1.2 | [('HTAP', 804)] |
| `.ph` | 11 | 11 | 0.3 | [('2D_0', 9)] |
| `.mxa` | 11 | 3013 | 30.2 | [('mxa ', 2141), ('lzm ', 520)] |
| `.icg` | 11 | 164 | 3.9 | [('""""', 2)] |
| `.gmk` | 11 | 656 | 1.4 | [('[GUI', 183)] |
| `.dssa` | 11 | 1696 | 18.2 | [('DSSA', 1696)] |
| `.crsb` | 11 | 736 | 0.2 | [('CRSB', 736)] |
| `.bga` | 11 | 1512 | 18.8 | [('BGA1', 76)] |
| `.atv` | 11 | 1697 | 2.0 | [('lzm ', 1598)] |
| `.zcb` | 10 | 400 | 0.8 | [('BLCM', 400)] |
| `.zc2` | 10 | 10 | 0.0 | [('BLCM', 10)] |
| `.visnd` | 10 | 5012 | 34.3 | [('VSND', 5012)] |
| `.s3db` | 10 | 17 | 82.1 | [('SQLi', 17)] |
| `.pchr` | 10 | 6991 | 32.5 | [('RHCP', 6991)] |
| `.lxml` | 10 | 4343 | 33.2 | [('<lay', 3562), ('BPCK', 775)] |
| `.gfnt` | 10 | 4725 | 31.8 | [('GFNT', 1678)] |
| `.file` | 10 | 21 | 0.0 | [('st06', 4)] |
| `.dialog` | 10 | 216 | 0.3 | [('SDPC', 215)] |
| `.cib` | 10 | 10 | 0.0 | [('ZCIB', 10)] |
| `.tpr` | 9 | 27 | 0.6 | [('CRPT', 27)] |
| `.sct` | 9 | 1251 | 53.1 | [('SFCT', 1251)] |
| `.s3d` | 9 | 211 | 3.7 | [('SH3D', 48)] |
| `.osc` | 9 | 4451 | 36.5 | [('OCSC', 4451)] |
| `.ooa` | 9 | 8766 | 2.9 | [('OOAD', 8766)] |
| `.ocg` | 9 | 439 | 22.0 | [('OCCG', 439)] |
| `.oce` | 9 | 2607 | 13.2 | [('OCOB', 2607)] |
| `.oab` | 9 | 279 | 1.6 | [('OABC', 279)] |
| `.md2` | 9 | 76 | 10.8 | [('IDP2', 76)] |
| `.ilb` | 9 | 45 | 0.0 | [('ZILB', 45)] |
| `.hpc` | 9 | 9 | 0.2 | [('HPC@', 9)] |
| `.dr` | 9 | 651 | 2.3 | [('#ART', 75)] |
| `.csv_sgn` | 9 | 27 | 0.0 | [('1;0;', 9), ('0;1;', 9), ('0;0;', 6)] |
| `.bhio` | 9 | 18 | 0.2 | [('DNFH', 18)] |
| `.bcm` | 9 | 461 | 3.3 | [('@BCM', 286)] |
| `.an3t` | 9 | 1203 | 4.9 | [('T3NA', 1203)] |
| `.an3m` | 9 | 1203 | 6.6 | [('M3NA', 1203)] |
| `.ucg` | 8 | 678 | 6.8 | [('UNCG', 678)] |
| `.uce` | 8 | 176 | 0.1 | [('UCEL', 176)] |
| `.sbmpic` | 8 | 550 | 37.8 | [('SOLC', 550)] |
| `.rca` | 8 | 1519 | 1.3 | [('BRCA', 1519)] |
| `.que` | 8 | 1244 | 12.0 | [('#QUE', 20)] |
| `.opl` | 8 | 787 | 0.1 | [('*OZD', 787)] |
| `.nmdp` | 8 | 31 | 0.2 | [('NMDP', 31)] |
| `.nmc` | 8 | 18 | 0.1 | [('NCMC', 18)] |
| `.nbf` | 8 | 1978 | 26.9 | [('NBF!', 8)] |
| `.nax2` | 8 | 94 | 53.4 | [('2XAN', 94)] |
| `.lya` | 8 | 426 | 0.1 | [('lyad', 426)] |
| `.lwrld` | 8 | 8 | 3.4 | [('LWRL', 8)] |
| `.ic` | 8 | 812 | 14.6 | [('DSIC', 812)] |
| `.gava` | 8 | 189 | 1.4 | [('GAVA', 189)] |
| `.ebp` | 8 | 8 | 0.2 | [('PMOC', 5), ('BPNZ', 1)] |
| `.cga` | 8 | 2902 | 96.9 | [('BCGA', 2902)] |
| `.cea` | 8 | 2902 | 9.4 | [('BCEA', 2902)] |
| `.bnm` | 8 | 3279 | 64.4 | [('BNMM', 2433)] |
| `.benm` | 8 | 8 | 4.3 | [('EMPC', 8)] |
| `.bcfg` | 8 | 20 | 0.1 | [('BCFG', 3)] |
| `.acgr` | 8 | 17842 | 35.5 | [('RGCA', 7919)] |

### Done in this pass

`.ngr` (59 titles, 23,684 files): `LMPK` is `u32 unpacked size` plus a zlib stream and `LMP2` is a raw LZMA stream with its 13-byte header in front (properties, dictionary size, `u64` unpacked size); both unpack to exactly the stored size on every sample. `NGR1` files (the third magic in the extension) are a different format and are not handled.

### Notes for the next pass

An extension is not an engine: `.dss`, `.dsf`, `.dsa` and `.dsm` each carry their own magic (`DSS0`, `DSF0`, `DSA\0`, `DSM1`) in only three or four unrelated titles (Pipe Mania, Puzzle Quest, LEGO Ninjago, FIFA), so they are one-off formats, not a shared engine. The ranking that matters is by (extension, magic) and number of distinct games. Top of that ranking (formats nintoolbox leaves untouched), most games first:

| extension | magic | games | files |
| --- | --- | ---: | ---: |
| `.nscr` | `RCSN` | 1530 | 257700 |
| `.wav` | `RIFF` | 489 | 35084 |
| `.nsbta` | `BTA0` | 431 | 28120 |
| `.nsbma` | `BMA0` | 301 | 24447 |
| `.nmcr` | `RCMN` | 273 | 8727 |
| `.nmar` | `RAMN` | 272 | 8751 |
| `.xml` | `<?xm` | 245 | 36520 |
| `.ncl` | `NCCL` | 114 | 10239 |
| `.h` | `//--` | 112 | 12200 |
| `.sbdl` | `#def` | 103 | 144 |
| `.ncg` | `NCCG` | 94 | 8830 |
| `.dat` | `S41J` | 92 | 143 |
| `.aap` | `JBOA` | 82 | 29671 |
| `.atb` | `SLBT` | 81 | 29465 |
| `.ini` | `[Las` | 75 | 196 |
| `(none)` | `TTLP` | 74 | 70782 |
| `.nsc` | `NCSC` | 65 | 6920 |
| `.swd` | `swdl` | 59 | 9168 |
| `.nce` | `NCOB` | 50 | 4184 |
| `.nbfc` | `wwww` | 49 | 157 |
| `.ogg` | `OggS` | 47 | 39253 |
| `.fnt` | `NFID` | 43 | 635 |
| `.h` | `#ifn` | 43 | 2101 |
| `.ani` | `ANIM` | 42 | 4028 |
| `.dic` | `BPSD` | 41 | 54 |
| `(none)` | `MOEG` | 41 | 138096 |
| `(none)` | `NARC` | 41 | 11809 |
| `.txt` | `dumm` | 40 | 167 |
| `.sent` | `NUOS` | 40 | 52 |
| `.sed` | `sedl` | 40 | 4104 |
| `(none)` | `PMB` | 40 | 15116 |
| `.atk` | `TKLT` | 39 | 3379 |
| `.apl` | `D2FP` | 39 | 3897 |
| `.til` | `RLE!` | 38 | 12131 |
| `.set` | `PACK` | 37 | 905 |
| `.fnt` | `PACK` | 37 | 296 |
| `.nbfc` | `DDDD` | 36 | 345 |
| `(none)` | `LXET` | 35 | 52040 |
| `.nbsa` | `WAVE` | 35 | 2750 |
| `.ngr` | `LMP2` | 34 | 21508 |
| `.ngr` | `NGR1` | 34 | 654 |
| `.adpcm` | `RIFF` | 34 | 7810 |
| `.dat` | `NINT` | 34 | 70 |
| `.h` | `#def` | 34 | 157 |

Reading it: `RCSN`, `BTA0`, `BMA0`, `RCMN`, `RAMN`, `NCCL`/`NCCG`/`NCOB`/`NCSC` and `swdl`/`sedl` are standard or near-standard NITRO-System / Procyon resources (check what `wszst` already converts through other commands before writing anything); `JBOA`, `SLBT`, `D2FP`, `TKLT`, `TTLP`, `MOEG`, `LXET` and `PMB` are one publisher's engine (the Battle of Giants family, about 80 games and 300,000 files) and the best real reverse-engineering target; `PACK` is done (see FORMATS.md); `NGR1` is the second format in `.ngr`.

### Battle of Giants family: findings, not yet decoded

One 2D engine shared by about 80 titles (Battle of Giants, 100 All-Time Favorites, Chessmaster, BFF TV, Hugo ...). Every resource is a tagged file; the tag decides the kind:

| magic | extension | what it is (from samples) |
| --- | --- | --- |
| `JBOA` | `.aap` | not content: a manifest. Header (`00 08`, 2 flag bytes, `01 00`), a u32 length plus the source path (`2d/animation/.../x.atb`), then one `{u32 1, u32 length, path}` entry per referenced resource (palettes under `2d/palette/`). |
| `D2FP` / `TKLT` | `.apl` / `.atk` | tilekit and tile data for minimap and tile maps; the source path is stored in the header (`/tilekit/minimap/mmap_10_0303.atk`); `TKLT` carries `2d/palette/...` and then raw 4-bit tile rows. |
| `SLBT` | `.atb` | animation/sprite bank. Header ends with a u32 compressed size (header length varies, 0x15 or 0x21 bytes) and the payload looks like a custom LZ stream (an offset table of `f0 xx` pairs stepping by 0x12 in the one sample read closely); the compression scheme is not identified. |
| `LXET` | `texel` | `LXET`, u16 byte count (0x0800 or 0x1000) and u16 0, then 16-bit RGB555 (magenta `1f fc` as key color) or 8-bit indexed pixels, 8 trailing bytes; dimensions not stored. |
| `TTLP` | `palette` | `TTLP`, u16 byte count (0x0200 for 256 colors), u16 0, then BGR555 colors. |
| `MOEG` | | `MOEG`, u32 size, then small tile-map / geometry records. |
| `NUOS` | `.sent` | sound-entry table. |

Correction on `SLBT` (re-checked on the 100 All-Time Favorites `card_back.atb`, the Battle of Giants `attachement_gold.atb` and the Prince of Persia `vizir_animation.atb`, whose magic is `DTBT`): the u32 at offset 0x1d equals file size minus header length exactly (3783 - 0x21 = 0xea6, 626 - 0x15 = 0x25d), so it is the payload length, not a compressed size, and nothing here shows an LZ stream. The `DTBT` payload reads as plain 4-bit tile rows (`11 99 a9 45 44 54 0f ...`) and the `attachement_gold` payload starts with an offset table (`f0 25 f0 37 f0 49 ...`, step 0x12). So `SLBT` is probably an uncompressed container of offset tables plus tile data; the missing piece is the record layout, not a decompressor. Update: the payload is one plain Nintendo LZ10 stream (`10 <u24 size>` at the start; header is `0x11 + 4 * count` bytes with the count at offset 9, unpacked size at offset 4). All three `SLBT` samples unpack to exactly the stored size and use the whole payload, and `wszst xx` now writes the unpacked data as `.bin` (commit after `47085104`). The unpacked data is tile pixels: `attachement_gold` is 64 4 bit tiles (a 4 bit sheet shows recognizable gold bars), `card_back` is four 0x800 byte chunks of 8 bit tiles that look like 4 frames of a 4x8 tile icon. The header has no bit depth (it comes from the palette in the sibling data), so `wszst xx` writes both `.4bpp.ncgr` and `.8bpp.ncgr` sheets. Sprite cell layout, animation and palette binding are not decoded, and `DTBT` is not handled.

The `JBOA` manifests would only give a source file list, so nothing was written. A useful extractor needs the `SLBT` compression scheme (most likely found by reading the engine's decoder in one of the ROMs) and then ties the `TTLP` palettes and `LXET` textures to images.
