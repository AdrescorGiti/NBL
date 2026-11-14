#!/usr/bin/env python3
"""Check unsaved POSIX syntax, safe input, optional ShellCheck, and save hooks."""
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from explorer import done, spawn


def verify(binary, root, tool):
    name = root / "script ; touch NBL-RAN.sh"
    name.write_text("#!/bin/sh\nif true; then\n  touch NBL-RAN\n")
    with spawn(binary, str(root), [str(name)], PATH=str(root)) as child:
        child.expect_exact("touch NBL-RAN")
        child.send("\x1b?")
        child.expect_exact("sh -n")
        child.expect_exact("syntax error")
        child.send("\x1b*")
        child.send("\x03")
        child.expect(r"line +2/")
        child.send("\x18")
        done(child)
    assert not (root / "NBL-RAN").exists()

    name.write_text("#!/bin/sh\necho $value\ntouch NBL-RAN\n")
    with spawn(binary, str(root), [str(name)], PATH=str(root)) as child:
        child.expect_exact("touch NBL-RAN")
        child.send("\x1b?")
        child.expect_exact("ShellCheck unavailable")
        child.send("\x18")
        done(child)
    assert not (root / "NBL-RAN").exists()

    if tool:
        cfg = root / "nbl" / "nblrc"
        cfg.parent.mkdir(exist_ok=True)
        cfg.write_text("unset checkonopen\nset checkonsave\n")
        with spawn(binary, str(root), [str(name)], PATH=str(tool.parent)) as child:
            child.expect_exact("touch NBL-RAN")
            child.send("\x13")
            child.expect(r"SC(?:2086|2154)")
            child.send("\x18")
            done(child)
        assert not (root / "NBL-RAN").exists()


if __name__ == "__main__":
    with TemporaryDirectory() as directory:
        verify(str(Path(sys.argv[1]).resolve()), Path(directory),
               Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
    print("Shell diagnostics, save hook, missing tool and safe snapshot passed")
