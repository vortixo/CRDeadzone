#!/usr/bin/env python3
"""Self-test for tools/validate_menu.py: every invalid fixture must be
rejected, the shipped descriptor must pass. Run: python3 tools/test_validate_menu.py
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
VALIDATOR = HERE / "validate_menu.py"
GOOD = HERE.parent / "deadzone.menu.json"


def base():
    with open(GOOD, encoding="utf-8") as f:
        return json.load(f)


def check_fixture(doc, expect_ok, name):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump(doc, f)
        path = f.name
    r = subprocess.run([sys.executable, str(VALIDATOR), path], capture_output=True, text=True)
    ok = r.returncode == 0
    status = "ok" if ok == expect_ok else "FAIL"
    print(f"{status}: {name} (expected {'pass' if expect_ok else 'fail'}, "
          f"got {'pass' if ok else 'fail'})")
    if ok != expect_ok:
        print(r.stdout, r.stderr)
        return False
    return True


def main():
    good = base()
    cases = []

    d = base()
    d["id"] = "Bad ID!"
    cases.append((d, False, "bad mod id"))

    d = base()
    d["options"][1]["step"] = 4  # default 15: (15-0) % 4 != 0
    cases.append((d, False, "slider off grid"))

    d = base()
    d["options"][1]["id"] = d["options"][2]["id"]
    cases.append((d, False, "duplicate option id"))

    d = base()
    d["options"][3]["default"] = 99
    cases.append((d, False, "choice default not in values"))

    d = base()
    d["options"][13]["default"] = "yes"
    cases.append((d, False, "toggle non-boolean"))

    d = base()
    d["options"][0]["label"] = {"xx-Bad": "nope"}
    cases.append((d, False, "unsupported language key"))

    d = base()
    d["options"] = d["options"] * 20  # 300 options, over the 256 cap (dup ids also fail)
    cases.append((d, False, "too many options"))

    d = base()
    d["options"][3]["choices"] = [{"label": "A", "value": 0}]
    cases.append((d, False, "choice with one entry"))

    results = [check_fixture(good, True, "shipped descriptor")]
    results += [check_fixture(doc, exp, name) for doc, exp, name in cases]
    if all(results):
        print(f"ALL {len(results)} VALIDATOR SELF-TESTS PASSED")
        return 0
    print("VALIDATOR SELF-TESTS FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
