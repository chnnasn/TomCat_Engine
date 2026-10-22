#!/usr/bin/env python3
"""Stage a native linux-x64 Player and its private .NET runtime, with SHA-256 manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def newest(root):
    versions = [p for p in root.iterdir() if p.is_dir() and re.fullmatch(r"10\.\d+\.\d+", p.name)]
    if not versions:
        raise RuntimeError(f"No stable .NET 10 runtime under {root}")
    return max(versions, key=lambda p: tuple(map(int, p.name.split('.'))))


def notices(root, build):
    parts = ['TomCat Engine - Third-Party Notices']
    # Consume the same checked-in notice catalog as the Windows release tools.
    catalog = (root / 'Scripts/ReleaseArtifactTools.ps1').read_text(encoding='utf-8-sig')
    entries = re.findall(r"Name = '([^']+)';[^\n]*Notice = '([^']*)'; Mode = '([^']+)'", catalog)
    if not entries:
        raise RuntimeError('The release notice catalog could not be read')
    for name, relative, mode in entries:
        if mode == 'None':
            continue
        text = (root / relative.replace('\\', '/')).read_text(encoding='utf-8-sig')
        if mode == 'FirstComment':
            text = re.search(r'/\*.*?\*/', text, re.S).group()
        elif mode == 'StbLicense':
            marker = text.index('This software is available under 2 licenses')
            text = text[text.rindex('/*', 0, marker):]
        elif mode != 'Full':
            raise RuntimeError(f'Unknown notice mode: {mode}')
        parts += [name, text.strip()]
    parts += ['SPIRV-Cross', (build / 'dependencies/spirv_cross/LICENSE').read_text(encoding='utf-8')]
    for package in ('libshaderc-dev', 'glslang-dev', 'spirv-tools'):
        path = Path('/usr/share/doc') / package / 'copyright'
        if path.is_file():
            parts += [package, path.read_text(encoding='utf-8')]
    return '\n\n'.join(parts) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/linux'))
    parser.add_argument('--dotnet-root', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    player = build / 'bin/Player/TomCatPlayer'
    with player.open('rb') as executable:
        header = executable.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\x3e\x00':
        raise RuntimeError('Expected a Linux ELF64 Player')
    version = subprocess.check_output([str(player), '--version'], text=True)
    values = dict(re.findall(r'([\w-]+)=([^\s]+)', version))
    dotnet = args.dotnet_root or Path(shutil.which('dotnet') or '').resolve().parent
    fxr = newest(dotnet / 'host/fxr')
    runtime = newest(dotnet / 'shared/Microsoft.NETCore.App')
    destination = build / 'bin/Editor/Packages/PlayerTemplates/linux-x64'
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and (destination.is_symlink() or not (destination / 'template.json').is_file()):
        raise RuntimeError(f'Refusing to replace a non-template directory: {destination}')
    stage = Path(tempfile.mkdtemp(prefix='.linux-template-', dir=destination.parent))
    try:
        def copy(source, relative):
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target, follow_symlinks=True)

        copy(player, 'TomCatPlayer')
        for name in ('TomCat.Managed.dll', 'TomCat.ScriptHost.dll',
                     'TomCat.ScriptHost.runtimeconfig.json', 'TomCat.ScriptHost.deps.json'):
            copy(build / 'bin/Editor/Managed' / name, Path('Managed') / name)
        copy(fxr / 'libhostfxr.so', Path('dotnet/host/fxr') / fxr.name / 'libhostfxr.so')
        shutil.copytree(runtime, stage / 'dotnet/shared/Microsoft.NETCore.App' / runtime.name)
        for name in ('Texture.glsl', 'FlatColor.glsl'):
            copy(root / 'Editor/TomCatInut/Packages/Shaders' / name, Path('Packages/Shaders') / name)
        # The .NET distribution also carries notices inside its runtime directory.
        (stage / 'THIRD_PARTY_NOTICES.txt').write_text(notices(root, build), encoding='utf-8')
        files = []
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                with path.open('rb') as stream:
                    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
                files.append({'Path': path.relative_to(stage).as_posix(), 'SHA256': digest})
        manifest = {'SchemaVersion': int(values['template']), 'EngineBuildID': values['build'],
                    'TcpakVersion': int(values['tcpak']), 'PlayerAbiVersion': int(values['abi']), 'Files': files}
        (stage / 'template.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        backup = None
        if destination.exists():
            backup = stage.with_name(stage.name + '-previous')
            destination.rename(backup)
        try:
            stage.rename(destination)
        except BaseException:
            if backup:
                backup.rename(destination)
            raise
        if backup:
            shutil.rmtree(backup)
        print(f'Linux Player template: {destination} ({len(files)} verified files)')
    finally:
        if stage.exists():
            shutil.rmtree(stage)


if __name__ == '__main__':
    main()
