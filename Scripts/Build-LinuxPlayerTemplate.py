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
        # Runtime distribution includes its own license notices. Keep the engine's too.
        notices = root / 'THIRD_PARTY_NOTICES.txt'
        if notices.is_file():
            copy(notices, notices.name)
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
