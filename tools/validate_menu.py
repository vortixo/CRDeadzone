#!/usr/bin/env python3
"""Validate a CRModMenu descriptor (deadzone.menu.json) against the interface
rules from AUTHOR_GUIDE_EN.md. Used locally and in CI (ubuntu job).

Usage: python3 tools/validate_menu.py deadzone.menu.json
"""

import json
import re
import sys

ID_RE = re.compile(r"^[a-z][a-z0-9_-]*$")
LANGS = {"zh-Hans", "zh-Hant", "en"}
ERRORS = []


def err(msg):
    ERRORS.append(msg)


def check_text(value, where):
    if isinstance(value, str):
        return
    if isinstance(value, dict):
        for k in value:
            if k not in LANGS:
                err(f"{where}: unsupported language key '{k}'")
        return
    err(f"{where}: label/description must be string or language object")


def check_grid(opt_id, default, min_v, max_v, step):
    for name, v in (("default", default), ("min", min_v), ("max", max_v), ("step", step)):
        if not isinstance(v, (int, float)):
            err(f"option '{opt_id}': {name} must be a number")
            return
    if step <= 0:
        err(f"option '{opt_id}': step must be > 0")
        return
    if min_v > max_v:
        err(f"option '{opt_id}': min > max")
        return
    if not (min_v <= default <= max_v):
        err(f"option '{opt_id}': default outside [min, max]")
        return
    # (default - min) must sit on the step grid (integer-exact check).
    if isinstance(default, int) and isinstance(min_v, int) and isinstance(step, int):
        if (default - min_v) % step != 0:
            err(f"option '{opt_id}': default not on step grid")
    else:
        n = round((default - min_v) / step)
        if abs((min_v + n * step) - default) > 1e-9:
            err(f"option '{opt_id}': default not on step grid")


def main(path):
    with open(path, encoding="utf-8") as f:
        try:
            d = json.load(f)
        except json.JSONDecodeError as e:
            print(f"FAIL: invalid JSON: {e}")
            return 1

    if d.get("schemaVersion") != 1:
        err("schemaVersion must be 1")
    mod_id = d.get("id", "")
    if not isinstance(mod_id, str) or not ID_RE.match(mod_id) or len(mod_id) > 48:
        err("mod id must match ^[a-z][a-z0-9_-]*$ and be <= 48 chars")
    if "name" not in d:
        err("missing mod name")
    else:
        check_text(d["name"], "mod name")
    if "version" in d and not isinstance(d["version"], str):
        err("version must be a string")
    if "dll" in d:
        dlls = d["dll"]
        if not isinstance(dlls, list) or not dlls:
            err("dll must be a non-empty list")
        else:
            for name in dlls:
                if not isinstance(name, str) or not re.match(r"^[\w\-.]+\.(dll|asi)$", name, re.I):
                    err(f"bad dll entry '{name}'")

    opts = d.get("options")
    if not isinstance(opts, list) or not (1 <= len(opts) <= 256):
        err("options must be a list of 1-256 entries")
        return 1 if ERRORS else 0

    seen = set()
    for o in opts:
        oid = o.get("id", "")
        if not isinstance(oid, str) or not ID_RE.match(oid) or len(oid) > 48:
            err(f"bad option id '{oid}'")
            continue
        if oid in seen:
            err(f"duplicate option id '{oid}'")
        seen.add(oid)
        otype = o.get("type")
        if "label" not in o:
            err(f"option '{oid}': missing label")
        else:
            check_text(o["label"], f"option '{oid}' label")
        if "description" in o:
            check_text(o["description"], f"option '{oid}' description")

        if otype == "toggle":
            if not isinstance(o.get("default"), bool):
                err(f"option '{oid}': toggle default must be boolean")
        elif otype == "slider":
            for k in ("default", "min", "max", "step"):
                if k not in o:
                    err(f"option '{oid}': slider missing '{k}'")
            if all(k in o for k in ("default", "min", "max", "step")):
                check_grid(oid, o["default"], o["min"], o["max"], o["step"])
        elif otype == "choice":
            choices = o.get("choices", [])
            if not isinstance(choices, list) or not (2 <= len(choices) <= 32):
                err(f"option '{oid}': choice needs 2-32 choices")
                continue
            values = [c.get("value") for c in choices if isinstance(c, dict)]
            if len(set(values)) != len(values):
                err(f"option '{oid}': duplicate choice values")
            if o.get("default") not in values:
                err(f"option '{oid}': default not among choice values")
            for c in choices:
                if not isinstance(c, dict) or "label" not in c or "value" not in c:
                    err(f"option '{oid}': malformed choice entry")
                    continue
                check_text(c["label"], f"option '{oid}' choice label")
                if "pad" in c and not isinstance(c["pad"], int):
                    err(f"option '{oid}': pad must be int")
        elif otype == "heading":
            pass
        elif otype in ("key", "row", "action"):
            pass  # validated by the menu itself; not used here
        else:
            err(f"option '{oid}': unknown type '{otype}'")

    if ERRORS:
        print("FAIL:")
        for e in ERRORS:
            print(f"  - {e}")
        return 1
    print(f"OK: {path} ({len(opts)} options, mod id '{mod_id}')")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "deadzone.menu.json"))
