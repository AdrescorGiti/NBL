"""Mentor checks source snapshots without running them."""
from pathlib import Path
from tempfile import TemporaryDirectory
import sys

from coding import F10
from explorer import spawn, done

with TemporaryDirectory() as temp:
    root = Path(temp)
    binary = str(Path(sys.argv[1]).resolve())
    for name, source, diagnostic in [
        ("broken.py", "open('executed', 'w')\nx = (\n", "opening delimiter"),
        ("broken.c", "int main(void) { return missing; }\n", "declaration"),
        ("broken.cpp", "int main() { return missing; }\n", "declaration"),
        ("broken.bash", "#!/bin/bash\ntouch executed\necho \"oops\n", "closing quote"),
    ]:
        path = root / name
        path.write_text(source)
        cfg = root / "nbl"
        cfg.mkdir(exist_ok=True)
        (cfg / "nblrc").write_text("unset checkonopen\n")
        with spawn(binary, temp, [name], size=(32, 125)) as child:
            child.expect_exact("F10 Check")
            child.send(F10)
            child.expect_exact("Mentor")
            child.expect_exact(diagnostic)
            child.send("\x18")
            done(child)
        assert not (root / "executed").exists()
print("Mentor diagnoses Bash, Python, C and C++ without running the buffer")
