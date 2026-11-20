# Mii body converter

Turns the Mii body models of the Nintendo Mii editors and Miitomo into cleaned-up `.glb` files with
all their animations, using only `wmdlt` and Python 3.11+ (standard library). It produces the same
kind of output as the mii-body-tools pipeline (BrawlCrate, SPICA, Switch Toolbox, assimp), without
.NET, Mono or a C++ build of assimp.

```
python3 tools/mii-body/convert.py --sources <assets> --out out [target ...]
python3 tools/mii-body/convert.py --list
```

`<assets>` holds one folder per target, as in the mii-body-tools assets:

| Target | Source | Models |
|---|---|---|
| `nigaoe` | Wii Mii Channel | `nigaoe/{male,female}Body.brres` (CHR0 animations inside) |
| `ctr-medit` | 3DS Mii Maker | `ctr-medit/{Male,Female}Body.bcmdl`, `ctr-medit/MiiAnim/*.bcskla` |
| `sugar` | Wii U Mii Maker | `sugar/MiiBodyMiddle.bfres` (MaleBody, FemaleBody), `sugar/MiiAnim.bfres` |
| `miieditn` | Switch Mii Maker | `miieditn/{Male,Female}.bfres` |
| `miiedit-ounce` | Switch 2 Mii Maker | `miiedit-ounce/{Male,Female}.bfres` (decompressed from `.zs`) |
| `arch` | Miitomo | `arch/bodyAll0004/bodyAll0004.Mdl.pod`, `arch/bodyTopslong0046/…`, `arch/*.Anim.pod` |

Each output, `out/<target>/<name>.glb`:

- is scaled to the Wii U body (about 12 units tall, arm span ±7.2);
- has two meshes, pants first and body second (the Miitomo clothing parts are merged into two);
- has one skin that starts at the skin root (`skl_root`, Miitomo `JointScale`), lists parents
  before children, and includes the unweighted bones up to the root, as raylib needs;
- has no shadow mesh and no textures (the applications bring their own materials);
- gives the Wii bones the model's axes, and the Miitomo head bone too;
- cancels the 45 degree plaza lean baked into the Wii animations.

What each recipe does is in `recipes.toml`. The steps only change how the rig is expressed: posed
at any frame, every vertex lands where the raw `wmdlt` output puts it (scaled), apart from the Wii
lean fix.
