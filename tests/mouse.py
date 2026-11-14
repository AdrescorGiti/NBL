#!/usr/bin/env python3
"""Send xterm SGR mouse input through ncurses and verify editor actions."""
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from explorer import done, spawn


def event(child, button, x, y, suffix="M"):
    child.send(f"\x1b[<{button};{x};{y}{suffix}")


def click(child, x, y):
    event(child, 0, x, y)
    event(child, 0, x, y, "m")


def verify(binary, root):
    project = root / "project"
    project.mkdir()
    for index in range(90):
        (project / f"{index:03}.txt").write_text(f"FILE_{index:03}\n" + "alpha\nbeta\n" * 50)
    with spawn(binary, str(root), [str(project)], term="xterm-direct", size=(26, 112)) as child:
        child.expect_exact("Explorer")
        child.expect_exact("001.txt")
        event(child, 65, 97, 10)
        child.expect_exact("4-20")
        click(child, 97, 6)
        child.expect_exact("FILE_004")
        child.expect_exact("\x1b[?25h")
        click(child, 7, 4)
        child.expect_exact("beta")
        child.send("X\x0f")
        child.expect_exact("Write to File")
        child.send("\r")
        child.expect_exact("Wrote")
        event(child, 65, 20, 8)
        child.expect_exact("beta")
        child.send("\x18")
        done(child)
    lines = (project / "004.txt").read_text().splitlines()
    assert lines[2] == "beXta", lines[:4]


if __name__ == "__main__":
    with TemporaryDirectory() as directory:
        verify(str(Path(sys.argv[1]).resolve()), Path(directory))
    print("SGR mouse wheel, explorer click, editor click, and file write passed")
