"""NBL 0.3 configuration, editing and language regressions through a PTY."""
from pathlib import Path
import os
import subprocess
import sys
from tempfile import TemporaryDirectory

from coding import F5, F6, F11, F12
from explorer import done, spawn

F4 = "\x1bOS"


def config(binary, root):
    path = root / "nbl" / "config.toml"
    path.parent.mkdir()
    path.write_text('[theme]\nbackground = "#142536"\n[editor]\ntab_size = 0x04\n'
                    '[explorer]\nposition = "left"\n')
    env = dict(os.environ, XDG_CONFIG_HOME=str(root))

    def check():
        return subprocess.run([binary, "--check-config"], env=env, text=True, capture_output=True)

    assert check().returncode == 0
    with spawn(binary, str(root), [], term="xterm-direct") as child:
        child.expect_exact("\x1b[48:2::20:37:54m")
        path.write_text('[theme]\nbackground = "#314253"\n[explorer]\nwidth = 31\n')
        child.send(F4)
        child.expect_exact("\x1b[48:2::49:66:83m")
        path.write_text("[editor]\ntab_size = false\n")
        child.send(F4)
        child.expect_exact("editor.tab_size")
        child.send("\x18")
        done(child)

    bad = check()
    assert bad.returncode != 0 and "editor.tab_size" in bad.stderr
    path.write_text('[theme]\naccent = "#123456"\naccent = "#222222"\n')
    assert check().returncode != 0
    with spawn(binary, str(root), []) as child:
        child.expect_exact("config.toml")
        child.send("\x18")
        done(child)


def pairs(binary, root):
    script = root / "pairs.py"
    script.write_text("MANUAL)\n")
    with spawn(binary, str(root), [str(script)]) as child:
        child.expect_exact("F5 Run")
        child.send('print(["Hello"])\r')
        child.send("(")
        child.send("\x7f")
        child.send("\x1bu\x1be")
        child.send("value = [({})]\r")
        child.send("# no pair (\r")
        child.send("if True:\rpass\r")
        child.send("\x13")
        child.expect_exact("Wrote")
        child.send("\x18")
        done(child)
    assert script.read_text() == 'print(["Hello"])\nvalue = [({})]\n# no pair (\nif True:\n    pass\n    MANUAL)\n', script.read_text()


def pair_provenance(binary, root):
    sample = root / "provenance.py"
    sample.write_text(")\n")
    with spawn(binary, str(root), [str(sample)]) as child:
        child.expect_exact("F5 Run")
        child.send("(")
        child.send("\x1bu")
        child.expect_exact("Undid")
        child.send("\x1be")
        child.send("))")
        child.send("\x13")
        child.expect_exact("Wrote")
        child.send("\x18")
        done(child)
    assert sample.read_text() == "()))\n", sample.read_text()

    sample.write_text("literal = '''text\n")
    with spawn(binary, str(root), ["+", str(sample)]) as child:
        child.expect_exact("F5 Run")
        child.send("[( text\r'''\rliteral = \"a\\\"b\"\r")
        child.send("\x13")
        child.expect_exact("Wrote")
        child.send("\x18")
        done(child)
    assert sample.read_text() == "literal = '''text\n[( text\n'''\nliteral = \"a\\\"b\"\n\n", repr(sample.read_text())

    path = root / "nbl" / "config.toml"
    path.parent.mkdir(exist_ok=True)
    path.write_text("[editor]\nauto_close_brackets = false\n")
    sample.write_text("")
    with spawn(binary, str(root), [str(sample)]) as child:
        child.expect_exact("F5 Run")
        child.send("(\x13")
        child.expect_exact("Wrote")
        child.send("\x18")
        done(child)
    assert sample.read_text() == "(\n"


def languages(binary, root):
    scripts = {
        "interactive.py": ('who = input("NAME> ")\nprint("HI_" + who)\n', "NAME> "),
        "native.c": ('#include <stdio.h>\nint main(void) { puts("C_OK"); }\n', "C_OK"),
        "native.cpp": ('#include <iostream>\nint main() { std::cout << "CPP_OK\\n"; }\n', "CPP_OK"),
    }
    for filename, (source, output) in scripts.items():
        (root / filename).write_text(source)
        with spawn(binary, str(root), [filename], size=(30, 120)) as child:
            child.expect_exact("F5 Run")
            child.send(F5)
            child.expect_exact("Terminal")
            if filename.endswith(".py"):
                child.expect_exact(output)
                child.send("Ada\r")
                output = "HI_Ada"
            seen = set()
            while len(seen) < 2:
                seen.add(child.expect_exact([output, "Exit 0"]))
            child.send(F6)
            child.send("\x18")
            done(child)
    broken = root / "broken.c"
    broken.write_text("int main(void) {\n    return missing;\n}\n")
    with spawn(binary, str(root), [broken.name]) as child:
        child.expect_exact("F5 Run")
        child.send(F5)
        child.expect_exact("Problems")
        child.expect_exact("undeclared")
        child.send(F11)
        child.send("\x03")
        child.expect(r"line +2/")
        child.send(F12)
        child.send("\x18")
        done(child)


if __name__ == "__main__":
    cases = {"config": config, "pairs": pairs, "provenance": pair_provenance, "languages": languages}
    for name in sys.argv[2:] or cases:
        with TemporaryDirectory() as directory:
            cases[name](str(Path(sys.argv[1]).resolve()), Path(directory))
        print(f"NBL 0.3 {name} passed")
