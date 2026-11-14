#!/usr/bin/env python3
"""Exercise the project window and buffer switching through a real PTY."""
import os
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import time

import pexpect


def spawn(binary, directory, files, term="xterm-256color", size=(26, 112), **overrides):
    env = dict(os.environ, LC_ALL="C.UTF-8", TERM=term,
               HOME=directory, XDG_CONFIG_HOME=directory)
    env.update(overrides)
    env.pop("NO_COLOR", None)
    return pexpect.spawn(binary, files, cwd=directory, env=env, dimensions=size,
                         encoding="utf-8", codec_errors="replace", timeout=4)


def done(child):
    child.expect(pexpect.EOF)
    child.close()
    assert child.exitstatus == 0, (child.exitstatus, child.signalstatus)


def keyboard(binary, folder):
    project = folder / "Проект"
    project.mkdir()
    (project / "docs").mkdir()
    (project / "docs" / "read me.txt").write_text("Русский текст\n")
    (project / "alpha.txt").write_text("ORIGINAL\n")
    (project / ".hidden").touch()
    (project / "loop").symlink_to(project, target_is_directory=True)

    with spawn(binary, str(folder), [str(project)]) as child:
        child.expect_exact("Explorer")
        child.expect_exact("alpha.txt")
        child.send("\x1b2\r")
        child.expect_exact("read me.txt")
        child.send("\x1bOB\r")
        child.expect_exact("Русский текст")
        child.send("\x1b2\x1bOB\r")
        child.expect_exact("ORIGINAL")
        child.send("\x1b5")
        child.expect_exact(".hidden")
        child.setwinsize(12, 51)
        child.expect_exact("ORIGINAL")
        time.sleep(0.2)
        child.setwinsize(30, 120)
        child.expect_exact("Explorer")
        time.sleep(0.2)
        child.send("\x1b1")
        time.sleep(0.15)
        child.send("\x1b1")
        child.expect_exact("Explorer")
        child.send("\x18")
        child.expect_exact("Русский текст")
        child.send("\x18")
        done(child)


if __name__ == "__main__":
    with TemporaryDirectory() as directory:
        keyboard(str(Path(sys.argv[1]).resolve()), Path(directory))
    print("Explorer keyboard, Unicode, paths with spaces, buffers, and resize passed")
