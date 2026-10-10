"""Optional smoke test of supplied NTSC-U disc assets, through native C readers."""
import pathlib
import subprocess
import sys
import tempfile

validator = str(pathlib.Path(sys.argv[1]).resolve())
assets = str(pathlib.Path(sys.argv[2]).resolve())

# NTSC-U indices from namespace_LOAD.h and LOAD_GetBigfileIndex. VRAM precedes
# each LEV; shared MPK is BI_SHAREDMPKVRM+1, 1P Crash pack BI_1PARCADEPACK.
for arguments, expected in [
    (['disc-mpk', assets, '259'], 'MPK OK'),
    (['disc-mpk', assets, '260'], 'MPK OK'),
    (['disc-lev', assets, '1'], 'LEV OK'),
    (['disc-lev', assets, '25'], 'LEV OK'),
    (['disc-lev-ptr', assets, '201', '202'], 'LEV OK'),
    (['disc-mpk-vram', assets, '259', '258'], 'MPK OK'),
    (['disc-mpk-vram', assets, '260', '258'], 'MPK OK'),
    (['disc-lev-vram', assets, '1', '0'], 'LEV OK'),
    (['disc-lev-ptr-vram', assets, '201', '202', '200'], 'LEV OK'),
]:
    result = subprocess.run([validator, *arguments], capture_output=True, text=True)
    if result.returncode != 0 or expected not in result.stdout:
        raise SystemExit(f"Retail validation failed: {' '.join(arguments)}\n{result.stdout}{result.stderr}")
    if 'Animation data OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach animation/frame checks.')
    if 'Vertex streams OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach vertex decompression checks.')
    if 'Draw commands OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach triangle/color/texture checks.')
    if 'Local transforms OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach local vertex transforms.')
    if 'Matrix probes OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach Q12 matrix probes.')
    if 'Projection probes OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach camera projection probes.')
    if arguments[0].endswith('-vram') and ('VRAM OK:' not in result.stdout or 'Texture pixels OK:' not in result.stdout):
        raise SystemExit('Retail validation did not reach VRAM loading and texture sampling.')
    if arguments[0].startswith('disc-lev') and 'Instance draw OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach authored instance transforms.')
    if 'Draw pipeline OK:' not in result.stdout:
        raise SystemExit('Retail validation did not reach the connected frame/triangle/projection pipeline.')
    if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
        raise SystemExit(result.stderr)
    print(result.stdout, end='')

# Local instance groups use one camera/depth target and retain authored placement.
with tempfile.TemporaryDirectory(prefix='ctr-scene-') as directory:
    for mode, indices, expected, selected in [
        ('disc-scene-near', ['1', '258,0', '0', '6'], 6, [0, 22, 3, 14, 15, 27]),
        ('disc-scene', ['1', '258,0', '0', '1'], 1, [0]),
        ('disc-scene-ptr', ['201', '202', '258,200', '0', '13'], 13, list(range(13))),
        ('disc-scene-ptr-near', ['201', '202', '258,200', '12', '1'], 1, [12]),
        ('disc-scene-terrain', ['1', '258,0', '0', '6'], 6, [0, 22, 3, 14, 15, 27]),
        ('disc-scene-ptr-terrain', ['201', '202', '258,200', '12', '1'], 1, [12]),
    ]:
        output = pathlib.Path(directory) / f'{mode}.ppm'
        result = subprocess.run([validator, mode, assets, *indices, str(output), 'iso'],
                                capture_output=True, text=True)
        if result.returncode != 0 or f'rendered {expected} instances' not in result.stdout:
            raise SystemExit(f'Retail scene failed:\n{result.stdout}{result.stderr}')
        actual = [int(line.split()[2].rstrip(':')) for line in result.stdout.splitlines()
                  if line.startswith('Scene instance ')]
        if actual != selected:
            raise SystemExit(f'Unexpected scene selection: {actual} != {selected}')
        if mode.endswith('-terrain'):
            terrain = [line for line in result.stdout.splitlines() if line.startswith('Terrain OK:')]
            if len(terrain) != 1 or int(terrain[0].split()[2]) <= 0:
                raise SystemExit('Retail scene did not visit any terrain quad blocks.')
            if int(terrain[0].split()[8]) <= 0:
                raise SystemExit('Retail terrain produced no fragments.')
        image = output.read_bytes()
        header = b'P6\n512 512\n255\n'
        if not image.startswith(header) or len(image) != len(header) + 512 * 512 * 3:
            raise SystemExit('Invalid scene RGB payload.')
        if image[len(header):] == bytes([24, 28, 36]) * (512 * 512):
            raise SystemExit('Scene contains only background.')
        if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
            raise SystemExit(result.stderr)
        print(result.stdout, end='')

# These assert bounded image production and ordered uploads, not retail parity.
with tempfile.TemporaryDirectory(prefix='ctr-preview-') as directory:
    images = {}
    for view in ['front', 'side', 'top', 'iso']:
        output = pathlib.Path(directory) / f'crash-{view}.ppm'
        result = subprocess.run([validator, 'disc-preview', assets, '260', '258,0',
                                 '36', '0', 'auto', '0', str(output), view], capture_output=True, text=True)
        if result.returncode != 0 or 'Preview OK:' not in result.stdout:
            raise SystemExit(f'Retail preview failed:\n{result.stdout}{result.stderr}')
        first, second = 'VRAM upload 0: index 258', 'VRAM upload 1: index 0'
        if first not in result.stdout or second not in result.stdout or result.stdout.index(first) >= result.stdout.index(second):
            raise SystemExit('VRAM upload order changed.')
        image = output.read_bytes()
        prefix = b'P6\n512 512\n255\n'
        if not image.startswith(prefix) or len(image) != len(prefix) + 512 * 512 * 3:
            raise SystemExit('Invalid preview dimensions or RGB payload.')
        if image[len(prefix):] == bytes([24, 28, 36]) * (512 * 512):
            raise SystemExit('Preview contains only background.')
        if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
            raise SystemExit(result.stderr)
        images[view] = image
        print(result.stdout, end='')
    if len(set(images.values())) != 4:
        raise SystemExit('Distinct camera views produced duplicate images.')
    # A bad later upload cannot publish a partially rendered file.
    output = pathlib.Path(directory) / 'bad-upload.ppm'
    result = subprocess.run([validator, 'disc-preview', assets, '260', '258,999999',
                             '36', '0', 'auto', '0', str(output)], capture_output=True, text=True)
    if result.returncode != 1 or output.exists():
        raise SystemExit('Failed upload stack published an image.')

with tempfile.TemporaryDirectory(prefix='ctr-motion-') as directory:
    listing = subprocess.run([validator, 'disc-animations', assets, '260', '36', '0'], capture_output=True, text=True)
    if listing.returncode != 0 or 'reverse logical=7 stored=4 interpolated=1' not in listing.stdout:
        raise SystemExit('Crash animation metadata mismatch.')
    prefix = pathlib.Path(directory) / 'reverse'
    result = subprocess.run([validator, 'disc-sequence', assets, '260', '258,0',
                             '36', '0', '1', '0', '7', str(prefix), 'side'], capture_output=True, text=True)
    if result.returncode != 0 or result.stdout.count('Preview OK:') != 7 or 'shared across 7 frames' not in result.stdout:
        raise SystemExit(f'Retail sequence failed:\n{result.stdout}{result.stderr}')
    images = [pathlib.Path(f'{prefix}-{index:06d}.ppm').read_bytes() for index in range(7)]
    header = b'P6\n512 512\n255\n'
    if any(not image.startswith(header) or len(image) != len(header) + 512 * 512 * 3 for image in images) or len(set(images)) < 2:
        raise SystemExit('Invalid or motionless retail sequence.')
    if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
        raise SystemExit(result.stderr)
    print(result.stdout, end='')

# Real material selectors, animated frames, BSP traversal and camera updates.
with tempfile.TemporaryDirectory(prefix='ctr-runtime-') as directory:
    for mode, indices in [
        ('disc-scene-textured', ['1', '258,0', '0', '6']),
        ('disc-scene-ptr-textured', ['201', '202', '258,200', '12', '1']),
        ('disc-scene-runtime', ['1', '258,0', '0', '6', '3']),
        ('disc-scene-runtime', ['25', '258,24', '0', '6', '3']),
        ('disc-scene-ptr-runtime', ['201', '202', '258,200', '12', '1', '3']),
    ]:
        output = pathlib.Path(directory) / f"{mode}-{indices[0]}"
        result = subprocess.run([validator, mode, assets, *indices, output, 'iso'], capture_output=True, text=True)
        if result.returncode or 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
            raise SystemExit(f'Retail material/runtime failure: {result.stdout}{result.stderr}')
        paths = [pathlib.Path(f'{output}-{i:06d}.ppm') for i in range(3)] if mode.endswith('runtime') else [output]
        images = [path.read_bytes() for path in paths]
        if any(not image.startswith(b'P6\n512 512\n255\n') or len(image) != 786447 for image in images):
            raise SystemExit('Malformed retail runtime image.')
        if mode.endswith('runtime') and (result.stdout.count('Runtime frame OK:') != 3 or len(set(images)) < 2):
            raise SystemExit('Retail camera/animation sequence did not advance.')
        if mode.endswith('textured') and 'Terrain material OK:' not in result.stdout:
            raise SystemExit('Retail terrain did not reach material consumer.')
        print(result.stdout, end='')

# Opt-in midpoint subdivision reaches the retail GPU/raster consumer, retains
# bounded output, and cannot overwrite a previously published sequence.
with tempfile.TemporaryDirectory(prefix='ctr-subdivision-') as directory:
    prefix = pathlib.Path(directory) / 'dingo'
    command = [validator, 'disc-scene-runtime', assets, '1', '258,0', '0', '6', '3', str(prefix), 'iso', 'subdiv=1']
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode or result.stdout.count('subdivision 1 ->') != 3:
        raise SystemExit(f'Retail subdivision failed: {result.stdout}{result.stderr}')
    paths = [pathlib.Path(f'{prefix}-{i:06d}.ppm') for i in range(3)]
    images = [path.read_bytes() for path in paths]
    if any(len(image) != 786447 or not image.startswith(b'P6\n512 512\n255\n') for image in images) or len(set(images)) < 2:
        raise SystemExit('Malformed or motionless subdivision preview.')
    if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
        raise SystemExit(result.stderr)
    retry = subprocess.run(command, capture_output=True, text=True)
    if retry.returncode != 1 or [path.read_bytes() for path in paths] != images:
        raise SystemExit('Subdivision sequence overwrote existing output.')
    invalid = pathlib.Path(directory) / 'invalid'
    command[-3] = str(invalid)
    command[-1] = 'subdiv=4'
    rejected = subprocess.run(command, capture_output=True, text=True)
    if rejected.returncode != 2 or list(pathlib.Path(directory).glob('invalid-*')):
        raise SystemExit('Invalid subdivision depth published an output.')
    print(result.stdout, end='')
