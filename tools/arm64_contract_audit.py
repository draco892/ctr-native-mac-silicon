"""Inventory host-width diagnostics and scratchpad call sites; not a build pass."""
import argparse
import collections
import json
import pathlib
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--syntax-log', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    diagnostics = []
    pattern = re.compile(r'^(.*?):(\d+):(\d+): (warning|error): (.*)$')
    for line in args.syntax_log.read_text().splitlines():
        match = pattern.match(line)
        if not match:
            continue
        path, number, column, severity, message = match.groups()
        if 'static assertion' in message:
            category = 'layout_guard'
        elif any(flag in message for flag in ('-Wpointer-to-int-cast', '-Wvoid-pointer-to-int-cast', '-Wint-to-pointer-cast')):
            category = 'pointer_integer_cast'
        elif severity == 'error':
            category = 'other_error'
        else:
            continue
        diagnostics.append(dict(path=path.removeprefix('./'), line=int(number), column=int(column), category=category, message=message))
    scratch = []
    for directory in ('game', 'platform', 'include'):
        for path in sorted((root / directory).rglob('*')):
            if path.suffix not in ('.c', '.h'):
                continue
            for number, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
                if ('CTR_SCRATCHPAD_PTR(' in line or 'CTR_SCRATCHPAD_ADDR_PTR(' in line) and not line.lstrip().startswith(('#define', '//')):
                    scratch.append(dict(path=str(path.relative_to(root)), line=number, source=line.strip()))
    counts = collections.Counter(item['category'] for item in diagnostics)
    report = dict(
        scope='C17 CTR_NATIVE ARM64 syntax diagnostics plus source scratchpad inventory',
        limitations='Syntax errors intentionally include active retail layout guards. Cast warnings need semantic review: retail IDs/tokens and wire offsets must remain 32 bit. Scratch inventory includes PS1 fallback branches; it does not prove absence of overlaps.',
        counts=dict(counts), diagnostics=diagnostics, scratchpad_call_sites=scratch,
    )
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Audit: {dict(counts)}, {len(scratch)} scratchpad call sites -> {args.output.resolve()}')


if __name__ == '__main__':
    main()
