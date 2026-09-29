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
