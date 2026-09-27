#!/usr/bin/env python3
"""perfmon_init_reachable -- is the performance monitor's Init() still reachable?

PerformanceMonitor::Init() is the only writer of mapsData, and
PerformanceMonitor::start() returns nullptr for any (mapId, instanceId) that has
no bucket. So a single misplaced call site switches the entire bot performance
monitor off, silently, with `.perfmon toggle` still reporting "enabled".

That is exactly what had happened. The only call to Init() sat near the bottom of
RandomPlayerbotMgr::UpdateAIInternal, which means below:

  * `if (!sPlayerbotAIConfig.randomBotAutologin || !sPlayerbotAIConfig.enabled)
     return;`
  * both `return`s of the `if (sPlayerbotAIConfig.persistentActiveRosterEnabled)`
    branch

On a server running a persistent active roster -- which is the configuration the
platform's tick-latency gate depends on -- the call was unreachable. mapsData
stayed empty for the life of the process, every probe in the bot tree handed back
nullptr, and PrintStats returned at its first line.

Nothing else in the tree catches that. The code compiles, links, runs, and
produces a monitor that reports "enabled" and measures nothing;
perfmon_collection_tests proves the collector works, which it always did.

So this guard pins the call site: within
RandomPlayerbotMgr::UpdateAIInternal, Init() must be called before the function's
first `return`, and the map-less bucket -- mapsData[0][0], where FullTick and
every PERF_MON_RNDBOT probe land -- must be registered explicitly rather than
left to depend on whether map 0 happens to be loaded.

Python rather than `cmake -P` because the check needs brace matching to find the
end of one function body, and CMake's regex engine is the wrong tool for that.
"""

import argparse
import pathlib
import re
import sys

FUNCTION = "void RandomPlayerbotMgr::UpdateAIInternal"
INIT_CALL = "sPerformanceMonitor.Init("


def strip_comments(text):
    """Blank out // and /* */ comments, preserving length so offsets stay valid.

    Offsets have to survive because the whole check is "does A appear before B",
    and the file's own explanatory comments mention both Init() and returns.
    Replacing each comment character with a space keeps every later index exactly
    where it was.
    """
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j == -1 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif text[i] in '"\'':
            quote = text[i]
            i += 1
            while i < n and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            i += 1
    return "".join(out)


def function_body(text, signature):
    """Return (start, end) offsets of the braced body following `signature`."""
    start = text.find(signature)
    if start == -1:
        return None
    brace = text.find("{", start)
    if brace == -1:
        return None
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return (brace, i)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir", required=True)
    args = parser.parse_args()

    source = pathlib.Path(args.module_dir) / "src" / "playerbot" / "RandomPlayerbotMgr.cpp"
    if not source.is_file():
        print(f"FAIL: {source} does not exist", file=sys.stderr)
        return 1

    raw = source.read_text(encoding="utf-8", errors="replace")
    code = strip_comments(raw)

    span = function_body(code, FUNCTION)
    if span is None:
        print(
            f"FAIL: could not find the body of {FUNCTION} in {source}.\n"
            "       If it was renamed, this guard has to be pointed at the new "
            "name -- do not delete it: Init() being reachable is the difference "
            "between a working bot performance monitor and one that reports "
            '"enabled" while collecting nothing.',
            file=sys.stderr,
        )
        return 1

    begin, end = span
    body = code[begin:end]
    raw_body = raw[begin:end]

    failures = []

    init_at = body.find(INIT_CALL)
    if init_at == -1:
        failures.append(
            f"{FUNCTION} never calls {INIT_CALL}...). It is the only writer of "
            "PerformanceMonitor::mapsData; without it start() returns nullptr "
            "for every probe and the monitor collects nothing at all."
        )
    else:
        return_match = re.search(r"\breturn\b", body)
        if return_match and return_match.start() < init_at:
            before = body[: return_match.start()].count("\n")
            init_line = body[:init_at].count("\n")
            first_line = raw[:begin].count("\n") + 1
            failures.append(
                f"{FUNCTION} returns (line {first_line + before}) before it calls "
                f"{INIT_CALL}...) (line {first_line + init_line}).\n"
                "       Every early return in this function -- the "
                "randomBotAutologin/enabled gate and both returns of the "
                "persistentActiveRosterEnabled branch -- skips the call, and a "
                "skipped Init() leaves mapsData empty for the life of the "
                "process. Move the Init() calls above the first return."
            )

    if not re.search(r"sPerformanceMonitor\.Init\(\s*0\s*,\s*0\s*\)", body):
        failures.append(
            "the map-less bucket sPerformanceMonitor.Init(0, 0) is not "
            f"registered in {FUNCTION}.\n"
            "       PlayerbotAIBase::UpdateAI times \"FullTick\" and every "
            "PERF_MON_RNDBOT probe uses the start() overload that defaults "
            "mapId and instanceId to 0, so they all land in mapsData[0][0]. "
            "The loop over sMapMgr.Maps() only creates it if map 0 happens "
            "to be instantiated, and PrintStats(perTick) divides by the "
            "FullTick count."
        )

    if failures:
        print("perfmon_init_reachable: FAILED", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("perfmon_init_reachable: ok")
    print(f"  {FUNCTION} calls {INIT_CALL}...) before its first return")
    print("  the map-less bucket (0, 0) is registered explicitly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
