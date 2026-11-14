#!/usr/bin/env python3
"""PTY scenarios for large inputs, filesystem failures, and limited terminals."""
from pathlib import Path
import os
import signal
import sys
from tempfile import TemporaryDirectory

import pexpect
from explorer import done, spawn


def verify(binary, root):
    tree = root / "large"
    tree.mkdir()
    for number in range(6500):
        (tree / f"{number + 2:05}.txt").touch()
    with spawn(binary, str(root), [str(tree)]) as child:
        child.expect_exact("Explorer")
        child.send("\x1b2\x1bOF")
        child.expect_exact("Load more")
        child.send("\r")
        child.expect_exact("4095")
        child.send("\x18")
        done(child)

    edges = root / "edge-cases"
    edges.mkdir()
    (edges / "000-denied").mkdir(mode=0)
    (edges / "001-cycle").symlink_to(edges, target_is_directory=True)
    with spawn(binary, str(root), [str(edges)]) as child:
        child.expect_exact("001-cycle")
        child.send("\x1b2\r")
        if os.geteuid() != 0:
            child.expect_exact("Permission denied")
        child.send("\x1bOB\r")
        child.expect_exact("Explorer opens regular files inside the project")
        child.send("\x18")
        done(child)
    (edges / "000-denied").chmod(0o700)

    large_file = root / "large.txt"
    large_file.write_text("START\n" + "abcdefghij\n" * 150_000 + "КОНЕЦ_測試\n")
    with spawn(binary, str(root), [str(large_file)], term="vt100", size=(16, 52)) as child:
        child.expect_exact("START")
        child.send("\x17")
        child.expect_exact("Search")
        child.send("КОНЕЦ_測試\r")
        child.expect(r"150002[\s\x1b]")
        child.send("\x18")
        done(child)

    sample = root / "unsaved.txt"
    sample.write_text("UNCHANGED\n")
    with spawn(binary, str(root), [str(sample)], size=(18, 80)) as child:
        child.expect_exact("UNCHANGED")
        child.send("edit\x18")
        child.expect_exact("Save modified buffer?")
        child.send("\x03")
        child.expect_exact("Cancelled")
        child.send("\x18")
        child.expect_exact("Save modified buffer?")
        child.send("n")
        done(child)
    assert sample.read_text() == "UNCHANGED\n"

    with spawn(binary, str(root), [str(sample)], size=(18, 80)) as child:
        child.expect_exact("UNCHANGED")
        child.send("safe\x0f")
        child.expect_exact("Write to File")
        sample.write_text("EXTERNAL UPDATE\n")
        child.send("\r")
        child.expect_exact("continue saving?")
        child.send("n\x18")
        child.expect_exact("Save modified buffer?")
        child.send("n")
        done(child)
    assert sample.read_text() == "EXTERNAL UPDATE\n"

    with spawn(binary, str(root), [str(sample)], size=(18, 80)) as child:
        child.expect_exact("EXTERNAL UPDATE")
        child.kill(signal.SIGTERM)
        child.expect_exact("\x1b[?1049l")
        child.expect(pexpect.EOF)


if __name__ == "__main__":
    with TemporaryDirectory() as directory:
        verify(str(Path(sys.argv[1]).resolve()), Path(directory))
    print("Large tree/file, permissions, links, VT100, unsaved changes, external edits and SIGTERM passed")
