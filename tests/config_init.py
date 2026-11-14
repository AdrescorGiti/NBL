"""Verify config creation on demand and first launch."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import stat
import subprocess
import sys
from tempfile import TemporaryDirectory

from explorer import done, spawn


def verify(binary):
    template = (Path(__file__).resolve().parents[1] / "config.toml.example").read_bytes()
    with TemporaryDirectory() as temp:
        root = Path(temp)
        xdg = root / "custom settings" / "nested"
        target = xdg / "nbl" / "config.toml"
        env = dict(os.environ, HOME=temp, XDG_CONFIG_HOME=str(xdg))

        def init():
            return subprocess.run([binary, "--init-config"], env=env, check=True,
                                  capture_output=True, text=True)

        subprocess.run([binary, "--check-config"], env=env, check=True, capture_output=True)
        assert not xdg.exists()
        with ThreadPoolExecutor(max_workers=4) as workers:
            list(workers.map(lambda _: init(), range(8)))
        assert target.read_bytes() == template
        assert list(target.parent.iterdir()) == [target]
        assert stat.S_IMODE(target.stat().st_mode) == 0o600
        custom = b"[editor]\ntab_size = 2\n"
        target.write_bytes(custom)
        init()
        assert target.read_bytes() == custom

    with TemporaryDirectory() as temp:
        root = Path(temp)
        target = root / ".config" / "nbl" / "config.toml"
        with spawn(binary, temp, ["-I"], size=(30, 120), XDG_CONFIG_HOME="") as child:
            child.expect_exact("F5 Run")
            child.send("\x18")
            done(child)
        assert not target.exists()
        with spawn(binary, temp, [], size=(30, 120), XDG_CONFIG_HOME="") as child:
            child.expect_exact("F5 Run")
            child.send("\x18")
            done(child)
        assert target.read_bytes() == template
        subprocess.run([binary, "--check-config"], env=dict(os.environ, HOME=temp, XDG_CONFIG_HOME=""),
                       check=True, capture_output=True)


if __name__ == "__main__":
    verify(str(Path(sys.argv[1]).resolve()))
    print("Configuration initialization, concurrent creation, existing settings, and first launch passed")
