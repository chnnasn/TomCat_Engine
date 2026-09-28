"""Export application-view GIF excerpts from the original OBS recording.

Usage: python docs/portfolio/export.py SOURCE.mp4 --ffmpeg PATH_TO_FFMPEG
Add --manifest docs/portfolio/2026-09-20/capture.json for the current capture.
Requires Python 3.11+ and an FFmpeg build with libx264, palettegen and paletteuse.
Removes the bottom 40-pixel Windows taskbar; preserves application content.
No screen reconstruction, cursor synthesis, or image overlays.
"""
import argparse
import json
import hashlib
import subprocess
import tempfile
from pathlib import Path

CUTS = {
    # Keep the action and a brief readable hold; omit idle intervals between actions.
    '01-hub-project': [
        (23.8, 26.6, 1.5), (31.8, 34.1, 1.5), (44.0, 46.4, 1.5),
        (52.0, 54.2, 1.5), (64.9, 68.0, 1.5), (142.4, 144.2, 1.5),
        (153.7, 156.1, 1.5), (160.7, 163.5, 1.5), (190.7, 197.0, 2),
    ],
    '02-sprite-transform': [
        (246.3, 248.6, 1.5), (256.7, 258.9, 1.5), (263.7, 265.6, 1.5),
        (269.2, 273.1, 2), (281.5, 284.0, 1.5),
        (400.0, 401.4, 1.5), (406.9, 409.4, 1.5),
        (423.5, 424.9, 1.5), (428.5, 431.0, 1.5),
        (440.7, 442.4, 1.5), (452.1, 455.0, 1.5),
    ],
    '03-physics-authoring': [
        (467.0, 469.0, 1.5), (474.3, 477.0, 1.5),
        (488.0, 490.0, 1.5), (494.0, 497.0, 1.5),
        (508.1, 510.3, 1.5), (516.5, 519.3, 1.5),
        (531.0, 533.5, 1.5), (539.4, 541.5, 1.5),
        (551.2, 553.4, 1.5), (561.3, 564.0, 1.5),
        (583.0, 587.0, 2), (610.4, 613.0, 1.5),
        (629.0, 632.0, 1.5), (648.8, 652.0, 1.5),
        (660.5, 663.0, 1.5), (670.0, 673.0, 1.5),
    ],
    # Preserve the complete fall/contact/settling interval at the source speed.
    '04-play-pause-step-stop': [
        (693.1, 697.3, 1), (703.1, 705.0, 1.5),
        (712.0, 714.0, 1.5), (723.5, 726.3, 1),
    ],
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--manifest', type=Path, help='Dated capture manifest; output beside it')
    args = parser.parse_args()
    out = Path(__file__).resolve().parent
    config = {'clips': CUTS, 'screenshots': {'editor-fullscreen': 686}}
    if args.manifest:
        config = json.loads(args.manifest.read_text(encoding='utf-8'))
        out = args.manifest.resolve().parent
        with args.source.open('rb') as source:
            actual = hashlib.file_digest(source, 'sha256').hexdigest()
        if actual.lower() != config['source_sha256'].lower():
            parser.error('Source SHA-256 does not match the capture manifest')
    crop = config.get('crop', '1280:680:0:0')
    fps = config.get('fps', 25)
    colors = config.get('gif_max_colors', 256)
    dither = config.get('gif_dither', 'bayer')
    if not isinstance(colors, int) or not 4 <= colors <= 256:
        parser.error('GIF palette size must be between 4 and 256')
    if dither not in ('bayer', 'none'):
        parser.error('GIF dither must be bayer or none')
    for name in [*config['clips'], *config.get('screenshots', {})]:
        if not name or Path(name).name != name or any(c in name for c in '/\\:'):
            parser.error('Output names must be plain filenames')
    for cuts in config['clips'].values():
        for start, end, speed in cuts:
            if not 0 <= start < end or speed <= 0:
                parser.error('Cuts require 0 <= start < end and speed > 0')
    out.mkdir(parents=True, exist_ok=True)
    def run(*options):
        subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', *map(str, options)], check=True)
    with tempfile.TemporaryDirectory(prefix='tomcat-capture-') as folder:
        temp = Path(folder)
        for name, cuts in config['clips'].items():
            parts = []
            for index, (start, end, speed) in enumerate(cuts):
                part = temp / f'{name}-{index}.mp4'
                run('-ss', start, '-t', end-start, '-i', args.source, '-an',
                    '-vf', f'crop={crop},setpts=(PTS-STARTPTS)/{speed},fps={fps}',
                    '-c:v', 'libx264', '-preset', 'fast', '-crf', 16, part)
                parts.append(part)
            listing = temp / 'concat.txt'
            listing.write_text(''.join(f"file '{p.as_posix()}'\n" for p in parts), encoding='utf-8')
            run('-f', 'concat', '-safe', 0, '-i', listing,
                '-filter_complex', f'[0:v]split[a][b];[a]palettegen=stats_mode=diff:max_colors={colors}[p];[b][p]paletteuse=dither={dither}:bayer_scale=3:diff_mode=rectangle',
                '-loop', 0, out / f'{name}.gif')
            print(f'Exported {name}.gif', flush=True)
        for name, time in config.get('screenshots', {}).items():
            run('-ss', time, '-i', args.source, '-vf', f'crop={crop}',
                '-frames:v', 1, out / f'{name}.png')

if __name__ == '__main__':
    main()
