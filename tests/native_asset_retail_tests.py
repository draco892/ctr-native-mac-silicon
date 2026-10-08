"""Optional smoke test of supplied NTSC-U disc assets, through native C readers."""
import pathlib
import subprocess
import sys

validator = str(pathlib.Path(sys.argv[1]).resolve())
assets = str(pathlib.Path(sys.argv[2]).resolve())

# NTSC-U indices from namespace_LOAD.h and LOAD_GetBigfileIndex. VRAM precedes
# each LEV; shared MPK is BI_SHAREDMPKVRM+1, 1P Crash pack BI_1PARCADEPACK.
for arguments, expected in [
    (['disc-mpk', assets, '259'], 'MPK OK'),
    (['disc-mpk', assets, '260'], 'MPK OK'),
    (['disc-lev', assets, '1'], 'LEV OK'),
    (['disc-lev-ptr', assets, '201', '202'], 'LEV OK'),
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
    if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
        raise SystemExit(result.stderr)
    print(result.stdout, end='')
