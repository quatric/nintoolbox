# -*- mode: python ; coding: utf-8 -*-
import os

# Bundle every CLI tool this repo builds (project/bin/*) plus every
# companion passthrough tool staged in extra_tools/ (wit, mobipeg, sharpii,
# nsz, makerom, ...), same set the CI GUI jobs add via --add-binary. Both
# directories are optional here so this spec still works for a bare local
# build that only has project/bin/wszst.
binaries = []
for src_dir in ('project/bin', 'extra_tools'):
    if os.path.isdir(src_dir):
        for name in sorted(os.listdir(src_dir)):
            path = os.path.join(src_dir, name)
            if os.path.isfile(path) and os.access(path, os.X_OK):
                binaries.append((path, '.'))

datas = [('logo.png', '.')]
if os.path.isdir('extra_tools/share'):
    datas.append(('extra_tools/share', 'share'))

# Bundled data files resolved next to the running tool by lib-passthru.c
# (seeddb.bin for ctrtool, prod.keys/title.keys for the Switch tools -- see
# resolve_bundled_tool()/locate_switch_key()): flat via --contents-directory .
# so the lookup that checks ProgramDirectory() first finds them.
for name in ('seeddb.bin', 'prod.keys', 'title.keys'):
    path = os.path.join('project', 'third_party', name)
    if os.path.isfile(path):
        datas.append((path, '.'))

a = Analysis(
    ['nintoolbox.py'],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='nintoolbox',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['logo.icns'],
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='nintoolbox',
)
app = BUNDLE(
    coll,
    name='nintoolbox.app',
    icon='logo.icns',
    bundle_identifier='net.quatric.nintoolbox',
    version='1.1',
)
