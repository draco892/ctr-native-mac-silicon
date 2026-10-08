#!/usr/bin/env python3
"""Embed a bounded native PPM sequence as a self-contained macOS HTML viewer."""
import argparse
import base64
import html
import json
import pathlib
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--fps', type=int, default=15, choices=range(1, 61))
    args = parser.parse_args()
    if args.output.suffix.lower() != '.html':
        parser.error('output must have an .html suffix')
    prefix = args.prefix.resolve()
    pattern = re.compile(re.escape(prefix.name) + r'-(\d{6})\.ppm')
    files = sorted((int(match.group(1)), file) for file in prefix.parent.iterdir()
                   if (match := pattern.fullmatch(file.name)))
    if not files or len(files) > 256:
        parser.error('expected 1..256 exported frames for this prefix')
    if any(b[0] != a[0] + 1 for a, b in zip(files, files[1:])):
        parser.error('logical frame indices must be consecutive')
    frames = []
    with tempfile.TemporaryDirectory(prefix='ctr-sequence-html-') as directory:
        for index, file in files:
            data = file.read_bytes()
            header = b'P6\n512 512\n255\n'
            if not data.startswith(header) or len(data) != len(header) + 512 * 512 * 3:
                parser.error(f'invalid native RGB frame: {file}')
            png = pathlib.Path(directory) / f'{index}.png'
            subprocess.run(['sips', '-s', 'format', 'png', str(file), '--out', str(png)],
                           check=True, capture_output=True)
            frames.append({'index': index, 'url': 'data:image/png;base64,' +
                           base64.b64encode(png.read_bytes()).decode('ascii')})
    template = '''<!doctype html><html lang="it"><meta charset="utf-8">
<title>Anteprima animazione CTR</title><style>
body{font:16px system-ui;background:#181c24;color:#e9edf5;max-width:600px;margin:32px auto;padding:16px}
img{width:100%;max-width:512px;display:block}input{width:100%}button{padding:8px 16px}
small{color:#b9c1d0}</style><h1>__TITLE__</h1>
<p>Anteprima diagnostica, camera fissa. La velocità scelta non rappresenta il timing del gioco.</p>
<img id="image" alt="Frame animazione di Crash"><p><button id="play">Riproduci</button>
<span id="label" aria-live="polite"></span></p><input id="seek" type="range" min="0" value="0" aria-label="Frame">
<small>__COUNT__ frame, __FPS__ frame/s. Texture, luci e materiali restano diagnostici.</small>
<script>
const frames=__FRAMES__,fps=__FPS__,image=document.getElementById('image'),
seek=document.getElementById('seek'),label=document.getElementById('label'),button=document.getElementById('play');
let current=0,timer=null;seek.max=frames.length-1;
function show(){image.src=frames[current].url;seek.value=current;label.textContent='Frame logico '+frames[current].index;}
function stop(){clearInterval(timer);timer=null;button.textContent='Riproduci';}
button.onclick=()=>{if(timer){stop();return;}button.textContent='Pausa';timer=setInterval(()=>{current=(current+1)%frames.length;show();},1000/fps);};
seek.oninput=()=>{stop();current=Number(seek.value);show();};
document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();});show();
</script></html>'''
    document = template.replace('__TITLE__', html.escape(prefix.name)).replace('__COUNT__', str(len(frames)))
    document = document.replace('__FPS__', str(args.fps)).replace('__FRAMES__', json.dumps(frames))
    args.output.write_text(document, encoding='utf-8')
    print(f'Viewer OK: {len(frames)} frames -> {args.output.resolve()}')


if __name__ == '__main__':
    main()
