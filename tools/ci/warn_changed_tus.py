#!/usr/bin/env python3
"""Compiler warnings for the translation units a change touches (twow-repo#83).

The release build compiles with --no-warnings (CMakeLists.txt, BUILD_ADDITIONAL_FLAGS),
so a green build says nothing about warnings. Turning warnings on globally would bury
every change under the legacy inventory triaged in REF-005, so this script only
recompiles the changed .cpp/.c files, with -fsyntax-only and warnings enabled, and
reports:

  * warnings on lines the change added or modified -> GitHub ::warning annotations
  * all other warnings in changed files             -> counted in the summary only

Non-blocking by design (exit 0 unless the tool itself fails, or --strict is given and
there are warnings on changed lines). Needs compile_commands.json, so configure with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON.

Usage: warn_changed_tus.py --build-dir /build [--base HEAD^1] [--strict]
                           [--summary-file F] [FILE...]
       FILE... overrides the diff (local focused use; all lines count as changed).
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from collections import defaultdict

WARN_FLAGS = ["-Wall", "-Wextra", "-Wno-unused-parameter"]
# Flags that silence warnings; stripped from the recorded compile command.
SILENCERS = {"-w", "--no-warnings"}
SOURCE_RE = re.compile(r"\.(c|cc|cpp|cxx)$")
DIAG_RE = re.compile(r"^(?P<file>[^:\s][^:]*):(?P<line>\d+):(?P<col>\d+): warning: (?P<msg>.*?)(?: \[(?P<opt>-W[^\]]+)\])?$")
HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def git(*args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def changed_lines(src_root, base):
    """{repo-relative path: set(new line numbers)} for added/modified files."""
    out = git("diff", "--unified=0", "--no-color", "--diff-filter=AM", base, "HEAD", cwd=src_root)
    lines, current = defaultdict(set), None
    for row in out.splitlines():
        if row.startswith("+++ "):
            current = row[6:] if row.startswith("+++ b/") else None
        elif current and (m := HUNK_RE.match(row)):
            start, count = int(m.group(1)), int(m.group(2) or 1)
            lines[current].update(range(start, start + count))
    return lines


def load_commands(build_dir):
    with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as fh:
        entries = json.load(fh)
    commands = {}
    for e in entries:
        path = os.path.realpath(os.path.join(e["directory"], e["file"]))
        commands.setdefault(path, e)  # first entry wins; duplicates are identical TUs
    return commands


def warning_command(entry):
    args = entry.get("arguments") or shlex.split(entry["command"])
    # Drop the launcher (ccache) and the output; keep everything that defines the TU.
    if os.path.basename(args[0]) == "ccache":
        args = args[1:]
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in SILENCERS or a in ("-c", "-MD", "-MMD") or a.startswith("-Werror"):
            continue
        out.append(a)
    return out + WARN_FLAGS + ["-fsyntax-only", "-fdiagnostics-color=never"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--src-root", default=os.getcwd())
    ap.add_argument("--base", default="HEAD^1")
    ap.add_argument("--strict", action="store_true", help="exit 1 on warnings on changed lines")
    ap.add_argument("--summary-file", help="also append the markdown summary here")
    ap.add_argument("files", nargs="*")
    opts = ap.parse_args()
    src_root = os.path.realpath(opts.src_root)

    if opts.files:
        changed = {os.path.relpath(os.path.realpath(f), src_root): None for f in opts.files}
    else:
        changed = changed_lines(src_root, opts.base)
    commands = load_commands(opts.build_dir)

    tus = [f for f in sorted(changed) if SOURCE_RE.search(f)]
    compiled, not_built = [], []
    new_warnings, old_warnings = [], defaultdict(int)
    seen = set()
    for rel in tus:
        entry = commands.get(os.path.join(src_root, rel))
        if not entry:
            not_built.append(rel)
            continue
        compiled.append(rel)
        proc = subprocess.run(warning_command(entry), cwd=entry["directory"], capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"::error file={rel}::warning recompile failed (exit {proc.returncode})")
            print(proc.stderr[-4000:], file=sys.stderr)
            return 2
        for row in proc.stderr.splitlines():
            m = DIAG_RE.match(row)
            if not m:
                continue
            path = os.path.relpath(os.path.realpath(os.path.join(entry["directory"], m["file"])), src_root)
            if path not in changed:
                continue  # warnings in unchanged headers belong to the legacy inventory
            key = (path, m["line"], m["col"], m["msg"])
            if key in seen:
                continue  # a changed header seen from several changed TUs
            seen.add(key)
            span = changed[path]
            if span is None or int(m["line"]) in span:
                new_warnings.append((path, m["line"], m["col"], m["msg"], m["opt"] or ""))
            else:
                old_warnings[path] += 1

    for path, line, col, msg, opt in new_warnings:
        print(f"::warning file={path},line={line},col={col},title=compiler warning {opt}::{msg}")

    summary = [
        "### Compiler warnings on changed translation units (twow-repo#83)",
        f"flags: `{' '.join(WARN_FLAGS)}` (release build itself runs with `--no-warnings`)",
        f"- changed TUs compiled: {len(compiled)}",
        f"- changed sources not in this build: {len(not_built)}" + (f" ({', '.join(not_built[:10])})" if not_built else ""),
        f"- **warnings on changed lines: {len(new_warnings)}**",
        f"- other warnings in changed files (legacy, not annotated): {sum(old_warnings.values())}",
    ]
    summary += [f"  - `{p}`: {n}" for p, n in sorted(old_warnings.items())]
    text = "\n".join(summary)
    print(text)
    if opts.summary_file:
        with open(opts.summary_file, "a", encoding="utf-8") as fh:
            fh.write(text + "\n")
    return 1 if opts.strict and new_warnings else 0


if __name__ == "__main__":
    sys.exit(main())
