#!/usr/bin/env python3
"""Measure PTY startup and resident memory using identical editor sessions."""

import argparse
import os
import pathlib
import statistics
import tempfile
import time

import pexpect


def trial(editor, directory, check_editing):
    sample = pathlib.Path(directory, "probe.sh")
    sample.write_text("#!/bin/sh\nprintf '%s\\n' BASELINE_READY\n", encoding="utf-8")
    env = dict(os.environ, LC_ALL="C.UTF-8", TERM="xterm-256color", HOME=directory,
               XDG_CONFIG_HOME=directory)
    start = time.monotonic_ns()
    with pexpect.spawn(editor, ["-I", str(sample)], cwd=directory, env=env,
                       dimensions=(30, 120), timeout=4) as editor_pty:
        editor_pty.expect_exact(b"BASELINE_READY")
        ready_ms = (time.monotonic_ns() - start) / 1_000_000
        stat = pathlib.Path(f"/proc/{editor_pty.pid}/status").read_text()
        rss_kib = int(next(row.split()[1] for row in stat.splitlines()
                           if row.startswith("VmRSS:")))
        if check_editing:
            editor_pty.send("X")
            editor_pty.send("\x1bu")
            editor_pty.expect_exact(b"Undid")
            editor_pty.send("\x1be")
            editor_pty.expect_exact(b"Modified")
            editor_pty.send("\x17")
            editor_pty.expect_exact(b"Search")
            editor_pty.send("BASELINE_READY\r")
            editor_pty.send("\x0f")
            editor_pty.expect_exact(b"Write to File")
            editor_pty.send("\r")
            editor_pty.expect_exact(b"Wrote")
        editor_pty.send("\x18")
        editor_pty.expect(pexpect.EOF)
        editor_pty.close()
        assert editor_pty.exitstatus == 0
    if check_editing:
        assert sample.read_text().startswith("X#!/bin/sh")
    return ready_ms, rss_kib


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("editor", type=lambda path: str(pathlib.Path(path).resolve()))
    parser.add_argument("--runs", type=int, default=15)
    args = parser.parse_args()
    assert args.runs > 0
    with tempfile.TemporaryDirectory() as directory:
        trial(args.editor, directory, True)
        results = [trial(args.editor, directory, False) for _ in range(args.runs)]
    startup, rss = zip(*results)
    print(f"{args.editor}: PTY startup median {statistics.median(startup):.2f} ms "
          f"(min {min(startup):.2f}, max {max(startup):.2f}); "
          f"RSS median {statistics.median(rss):.0f} KiB; {args.runs} runs")
