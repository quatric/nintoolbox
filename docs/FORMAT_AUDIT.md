# Retail format audit

Retail samples retrieved with rclone were checked against independent Python
record parsers and byte-for-byte extraction comparisons. No game assets are
included in this repository. These results establish support for the sampled
files, not every revision or title that might use a similar extension.

| Sample | Coverage | Result |
| --- | --- | --- |
| Jump Super Stars (Japan), Nintendo DS | 745 ALAR type-2 archives; 2,528 members | Every member name and payload matched |
| Jump Ultimate Stars (Japan), Nintendo DS | 283 ALAR type-2 and 32 type-3 archives; 6,787 members | Every member name and payload matched |
| Jump Ultimate Stars compressed members | 3,249 DSCP-wrapped streams | Native decompression matched an independent LZ10 decoder byte for byte |
| Pikmin (USA), GameCube | 20 ARC/DIR pairs; 1,169 members | Interleaved directory records, aligned names, and Shift-JIS names |
| Pikmin standalone textures | 11 TXE files | Decoded through wimgt, including textures with an omitted payload size |

The ALAR fix restores all members instead of extracting only the first, and
associates type-3 names with their own records. Both extractors validate the
complete directory before writing, preserve empty members, reject unsafe paths,
propagate write failures, and honor `--recurse=0`. Known DSCP and CX00 wrappers
are accepted by LZ10/LZ11 decoding; CX00 coverage is synthetic.

Pikmin's ARC/DIR reader and writer now use interleaved records. The writer is
available through the C API and tested with synthetic fixtures; CLI creation is
not wired up. TXE loading is extension-gated because the format has no magic.

## Remaining uncertainty

The sampled Jump Stars files use ALAR rather than establishing retail STPK
support. The Pikmin disc listing contains ARC/DIR pairs and no `.pvol` files.
STPK and PVOL retain synthetic round-trip coverage, but their former game
attributions are unverified. Extraction of proprietary Jump Stars member data
does not imply semantic decoding of every contained image, animation, or model
format. Recompression and ALAR creation were not established by this audit.

## Reproduce

Retrieve the matching images from your configured rclone remote, extract the
NitroFS trees with `ndstool`, and extract Pikmin's ARC/DIR and TXE files with a
disc tool capable of reading RVZ. The source object names used were:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Jump Super Stars (Japan).zip
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Jump Ultimate Stars (Japan).zip
Nintendo - GameCube/Redump/[RVZ]/Games/Pikmin (USA).rvz
```

Run from the repository root, substituting your extracted directories:

```sh
make -C project -j8 all test-alar test-pikarc
python3 -m unittest discover -s tests -p 'test_*cli.py'
python3 tests/audit_retail_corpus.py /path/to/jump-super/files --output /tmp/super.json
python3 tests/audit_retail_corpus.py /path/to/jump-ultimate/files --output /tmp/ultimate.json
python3 tests/audit_retail_corpus.py /path/to/pikmin --format pikmin --output /tmp/pikmin.json
wimgt DECODE /path/to/texture.txe -d /tmp/texture.png
```

The corpus checker writes a JSON report with source hashes, member counts,
diagnostics, and mismatches. It snapshots the executable and uses temporary
output directories, leaving the supplied corpus untouched. Regression fixtures
are generated independently and contain no retail assets. Native parser tests
also support AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
make -C project test-alar ALAR_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
make -C project test-pikarc PIKARC_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

SHA-256 of the extracted DS ROMs and the original Pikmin RVZ:

```text
Jump Super Stars.nds    819a2325536c3f5b5fdbf6d77d924e5d30b686ba8741f0a9893e9dbc4bb57877
Jump Ultimate Stars.nds a9c9bf89e6d99548b7c87e822b217c3fb74ef25186535b06193a6fb73d0d6d27
Pikmin.rvz             239946db4d46fb29b90245a7602f27f83e3b5c90dc98510b15a2fd191f70b76f
```

## Professor Layton audio and archives

Additional rclone samples:

```text
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Professor Layton and the Curious Village (USA, Australia).zip
Nintendo - Nintendo DS/No-Intro/Cartridges (Decrypted)/Professor Layton and the Diabolical Box (USA).zip
```

| Sample | Coverage | Result |
| --- | --- | --- |
| Curious Village | 59 SADL IMA streams: 42 mono, 17 stereo | Every PCM sample, channel count, sample rate and length matches vgmstream |
| Diabolical Box | 359 SADL Procyon streams: 312 mono, 47 stereo | Every PCM sample, channel count, sample rate and length matches vgmstream |
| Both games | 4 looped streams | Original loop bounds preserved in WAV `smpl` chunks |
| Diabolical Box | 74 LZ10-wrapped PCK2 archives; 8,160 members | All payloads match independent decompression and directory parsing |

SADL conversion now accepts retail lowercase magic, decodes 16-byte channel
blocks with independent predictor state, honors the stored initial IMA state,
and handles Procyon ADPCM instead of treating every payload as IMA. Both sample
rates are covered by retail samples. The 0xC0 header offset and uppercase magic
compatibility are covered by synthetic tests; all sampled streams use 0x100.
Malformed channels, codec flags, offsets, block lengths, IMA indices, and loops
are rejected before writing output. Decoding emits one pass through the stream,
with loop metadata rather than repeated audio.

The audio reference is [vgmstream commit 7dc938fa](https://github.com/vgmstream/vgmstream/tree/7dc938fa2f210943b37c7b6511852b516ef432ab),
particularly `src/meta/sadl.c`, `src/coding/ima_decoder.c`, and
`src/coding/nds_procyon_decoder.c`. The upstream [copyright and permission
notice](licenses/vgmstream.txt) is retained. Native conversion does not require
vgmstream; only the optional corpus checker uses it.

PCK2 uses sequential records with separate header, record and payload sizes.
The extractor validates all records and names before writing and supports raw
PCK2, LZ10-wrapped PLZ, empty members, dry runs and `--recurse=0`. The sample
members contain 4,755 scripts, 1,963 data files, 1,380 text files, 20 models, and
42 motion/animation files. Extraction does not establish semantic decoding of
every member type. PCK2 creation is not implemented.

```sh
wszst DECOMPRESS stream.SAD -d stream.wav --no-passthrough
wszst EXTRACT archive.plz -d archive-out --recurse=0 --no-passthrough
python3 tests/audit_sadl_corpus.py /path/to/curious/files /path/to/diabolical/files \
  --reference /path/to/vgmstream-cli --output /tmp/sadl-report.json
python3 tests/audit_retail_corpus.py /path/to/diabolical/files \
  --format pck2 --output /tmp/pck2-report.json
make -C project test-sadl SADL_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
make -C project test-pck2 PCK2_TEST_CFLAGS='-O1 -g -fsanitize=address,undefined'
```

Extracted ROM SHA-256:

```text
Curious Village.nds 0d6c06e010f4228605f08f934bf30d994e9986898ddc68a5260f6937c41bda57
Diabolical Box.nds  604e962ad3eda65a637c6cace2a521d99133501b59c9cac150d7face11cba3f7
```

A Shift-JIS filename in Curious Village blocked the local `ndstool` build on
macOS. The audit instead read NitroFS directory/FAT records independently and
decoded names as UTF-8 or Shift-JIS. That external extraction limitation remains.
No retail game assets are included in this repository.
