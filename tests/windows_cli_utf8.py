"""Windows real-executable UTF-8 argv regression; no converter-only oracle."""

import argparse
import os
from pathlib import Path
import subprocess
import uuid


def run(executable, *arguments, timeout=15):
    result = subprocess.run(
        [str(executable), *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=timeout,
    )
    return result, result.stdout.decode("utf-8"), result.stderr.decode("utf-8")


def check_invalid_utf16(executable):
    for invalid in ("\ud800", "\udc00"):
        result, out, err = run(executable, invalid)
        if result.returncode != 2 or "Invalid UTF-16 command line" not in err:
            raise AssertionError(f"malformed UTF-16 was not rejected: {result.returncode} {out!r} {err!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("this regression must launch the Windows executable")
    check_invalid_utf16(args.exe)
    name = "\u65e5\u672c \U0001f680 \"252\"-" + uuid.uuid4().hex[:8]
    # No camera is required: use the real parser's unknown-argument diagnostic.
    # --name is consumed first; the following token is an exact argv oracle,
    # not evidence that a camera publisher was created.
    result, out, err = run(args.exe, "--name", name, name)
    if result.returncode != 1 or "Unknown argument: " + name not in err.splitlines():
        raise AssertionError(f"parser did not preserve Unicode argv: {out!r} {err!r}")
    result, out, err = run(args.exe, "--name", "", name)
    if result.returncode != 1 or "Unknown argument: " + name not in err.splitlines():
        raise AssertionError(f"empty argument changed argv positions: {out!r} {err!r}")
    print("PASS Windows Unicode argv parser/status and strict rejection; no camera/GPU claim")


if __name__ == "__main__":
    main()
