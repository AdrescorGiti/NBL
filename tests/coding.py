#!/usr/bin/env python3
"""Exercise the coding controls through xterm key and mouse reports."""
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from explorer import done, spawn
from mouse import event

F5, F6, F7, F8, F9, F10, F11, F12 = [
    f"\x1b[{code}~" for code in (15, 17, 18, 19, 20, 21, 23, 24)
]


def run_script(binary, root):
    script = root / "example ; script.sh"
    script.write_text("#!/bin/sh\nprintf 'REQUEST> '\nread -r reply\nprintf 'REPLY_%s\\n' \"$reply\"\n")
    with spawn(binary, str(root), [str(script)], PATH=str(root), size=(30, 120)) as child:
        child.expect_exact("F5 Run")
        child.send(F5)
        child.expect_exact("REQUEST>")
        child.send("works\r")
        found = set()
        while len(found) != 2:
            found.add(child.expect_exact(["REPLY_works", "Exit 0"]))
        child.send(F6)
        child.send("\x18")
        done(child)


def unsaved_and_interrupt(binary, root):
    script = root / "unsaved.sh"
    script.write_text("#!/bin/sh\nprintf 'ON_DISK\\n'\n")
    with spawn(binary, str(root), ["+", str(script)], size=(32, 120)) as child:
        child.expect_exact("F5 Run")
        child.send("printf 'UNSAVED_READY\\n'\rwhile :; do sleep 1; done")
        child.send(F5)
        child.expect_exact("Terminal *")
        child.expect_exact("UNSAVED_READY")
        child.send(F8)
        child.send(F6)
        child.send(F6)
        child.expect_exact("Terminal *")
        child.send("\x03")
        child.expect(r"(Signal 2|Exit 130)")
        child.send(F6)
        child.send("\x18")
        child.expect_exact("Save modified buffer?")
        child.send("n")
        done(child)
    assert script.read_text() == "#!/bin/sh\nprintf 'ON_DISK\\n'\n"


def errors(binary, root):
    script = root / "broken.sh"
    script.write_text("#!/bin/sh\n: > CREATED\nthen\n")
    with spawn(binary, str(root), [str(script)], PATH=str(root),
               term="xterm-direct", size=(30, 120)) as child:
        child.expect_exact("Problems")
        child.expect(r"\x1b\[38:2::255:82:99m(?:\x1b\[[0-9:;]*m)*[^\x1b]*Error")
        child.expect_exact("syntax error")
        child.send(F5)
        child.expect_exact("Run blocked")
        child.send(F12)
        child.send("\x18")
        done(child)
    assert not (root / "CREATED").exists()


def selection(binary, root):
    sample = root / "drag.txt"
    sample.write_text("start COPY_THIS end\n")
    with spawn(binary, str(root), [str(sample)], term="xterm-direct") as child:
        child.expect_exact("COPY_THIS")
        event(child, 0, 10, 2)
        event(child, 32, 15, 2)
        event(child, 32, 19, 2)
        event(child, 0, 19, 2, "m")
        child.send(F9)
        child.expect_exact("\x1b]52;c;Q09QWV9USElT\x07")
        child.send("\x18")
        done(child)
    assert sample.read_text() == "start COPY_THIS end\n"


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    cases = {"run": run_script, "unsaved": unsaved_and_interrupt,
             "errors": errors, "selection": selection}
    for name in sys.argv[2:] or cases:
        with TemporaryDirectory() as directory:
            cases[name](binary, Path(directory))
        print(f"Coding workflow {name} passed")
