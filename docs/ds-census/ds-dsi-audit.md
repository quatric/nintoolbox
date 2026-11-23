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
