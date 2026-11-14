#!/usr/bin/env python3
"""Check completion candidates and verify the resulting buffer on disk."""
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from explorer import done, spawn


def verify(binary, root):
    script = root / "example.sh"
    script.write_text("#!/bin/sh\nPROJECT_VALUE=42\n")
    (root / "some file.txt").touch()
    command = root / "nbl-demo-cmd"
    command.write_text("#!/bin/sh\nexit 0\n")
    command.chmod(0o755)

    with spawn(binary, str(root), ["+", str(script)], PATH=str(root)) as child:
        child.expect_exact("PROJECT_VALUE")
        for prefix in [
                "whil", "echo $PROJ",
                "nbl-demo-c", "cat ./some"]:
            child.send(prefix)
            child.send("\x1d")
            child.expect_exact("Completed")
            child.send("\r")
        child.send("\x13")
        child.expect_exact("Wrote")
        child.send("\x18")
        done(child)
    text = script.read_text()
    assert "\nwhile\necho $PROJECT_VALUE\nnbl-demo-cmd\ncat ./some\\ file.txt\n" in text, text


if __name__ == "__main__":
    with TemporaryDirectory() as directory:
        verify(str(Path(sys.argv[1]).resolve()), Path(directory))
    print("Shell keyword, variable, PATH executable and escaped filename completion passed")
