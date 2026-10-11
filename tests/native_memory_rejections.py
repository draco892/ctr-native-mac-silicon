"""Run trapping allocator cases in child processes without stopping CTest."""

import os
import signal
import subprocess
import sys


def main() -> int:
    executable = sys.argv[1]
    modes = (
        "rounded-low", "rounded-high", "negative-low", "negative-high",
        "overflow-low", "overflow-high", "realloc-capacity",
        "negative-pack", "unalignable-pack",
    )
    for mode in modes:
        result = subprocess.run(
            [executable, mode], capture_output=True, text=True, timeout=10,
        )
        if os.name == "posix":
            rejected = result.returncode in (-signal.SIGILL, -signal.SIGTRAP, -signal.SIGABRT)
        else:
            rejected = result.returncode not in (0, 2)
        if not rejected:
            print(f"{mode}: expected allocator trap, got {result.returncode}", file=sys.stderr)
            print(result.stdout + result.stderr, file=sys.stderr)
            return 1
    print(f"All {len(modes)} invalid allocations were rejected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
