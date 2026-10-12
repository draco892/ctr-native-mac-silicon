"""Exercise the real executable's config loader and SDL binding validation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
sample = Path(sys.argv[2]).resolve()


def run(args, cwd, success, contains):
    result = subprocess.run([exe, *args], cwd=cwd, capture_output=True, text=True, timeout=15)
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == success, output
    assert contains in output, output


with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    # In the standard source layout, default discovery must use the repository
    # base rather than assets/ or the caller's working directory.
    if (sample.parent / "assets/ctr-u.bin").exists() and Path(exe).parent.parent == sample.parent:
        run(["--check-config"], root, True, str(sample) + ": loaded")
    run(["--config", str(sample), "--check-config"], root, True, "800x600 windowed, max_fps=30, keyboard.cross=C")
    valid = {
        "video": {"width": 1280, "height": 720, "fullscreen": True, "max_fps": 20},
        "input": {"keyboard": {"cross": "Y", "l3": "none"},
                  "controller": {"cross": "b", "r3": "none", "axis_left_x": "axis:-leftx"}},
    }
    target = root / "custom settings.json"
    target.write_text(json.dumps(valid))
    run(["--config", target.name, "--check-config"], root, True, "1280x720 fullscreen, max_fps=20, keyboard.cross=Y")
    for settings, message in [
        ({"input": {"keyboard": {"cross": "not a key"}}}, "Unknown keyboard key"),
        ({"input": {"controller": {"cross": "not a button"}}}, "Invalid controller binding"),
        ({"input": {"controller": {"axis_left_x": "a"}}}, "Invalid controller binding"),
        ({"video": {"max_fps": 0}}, "allowed range"),
        ({"video": {"fullscreen": "true"}}, "Expected true or false"),
    ]:
        target.write_text(json.dumps(settings))
        run(["--config", str(target), "--check-config"], root, False, message)
    target.write_text('{"video": {"width": 800,}}')
    run(["--config", str(target), "--check-config"], root, False, "Unexpected JSON token")
    target.write_text(" " * 16385)
    run(["--config", str(target), "--check-config"], root, False, "exceeds 16 KiB")
    run(["--config", "absent.json", "--check-config"], root, False, "File not found")
    run(["--config"], root, False, "requires one file path")
    run(["--config", "--check-config"], root, False, "requires one file path")
    run(["--config", str(sample), "--config", str(sample)], root, False, "requires one file path")
    run(["--version"], root, True, "CTR Native ")

print("native config CLI tests passed")
