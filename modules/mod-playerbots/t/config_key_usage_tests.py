#!/usr/bin/env python3
"""Fail when a PlayerBot config key is loaded into a member nothing ever reads.

WHAT THIS PREVENTS
------------------
`PlayerbotAIConfig::Initialize` loads ~283 keys into members. A member that is
assigned and then never read is a config key that appears in the shipped .conf,
is documented, can be set by an operator, and does nothing at all. There is no
compile error -- the assignment is a real use of the variable -- no warning, and
no test. The only symptom is an operator changing a setting and observing no
effect, which reads as "the feature is broken" rather than "the key is dead".

`AiPlayerbot.RandomBotLoginAtStartup` has been in that state long enough to have
its own issue (twow-repo#8). This test exists so the seventh one is caught by CI
rather than by someone eventually wondering.

WHY AN ALLOWLIST RATHER THAN DELETION
-------------------------------------
Most of these are upstream's. ADR-0040 keeps our core delta upstream-shaped so
it survives a rebase, and deleting upstream members is exactly the kind of edit
that produces conflicts forever. Resolving them -- implement or remove -- is
twow-repo#8's job and a decision, not a cleanup. The allowlist records the
current state so that NEW dead keys are what fails.

The allowlist is checked in BOTH directions. An entry that is no longer dead is
also an error, because an allowlist nobody prunes stops describing anything and
starts hiding things.

WHAT IT DELIBERATELY DOES NOT DO
--------------------------------
It does not compare code defaults against the .conf templates, and it does not
find keys present in a conf that no code reads. Both are real and worth having;
they need the platform's config tree, which core cannot see. This half lives
here because it is entirely answerable from core's own sources.

Run:  python3 config_key_usage_tests.py --module-dir <modules/mod-playerbots>
"""

import argparse
import pathlib
import re
import sys

# Members assigned from a config read and read nowhere else, as of 2026-09-08.
# Each needs a reason, and the reason should say what resolving it would mean.
ALLOWED = {
    "randomBotLoginAtStartup":
        "twow-repo#8 tracks this one explicitly. Upstream's; either log bots in "
        "at startup or drop the key.",
    "botsSilent":
        "Upstream. A global mute that never reached the chat paths.",
    "disableBotOptimizations":
        "Upstream. Would gate the bot update-throttling paths; nothing reads it.",
    "randomBotBracketCount":
        "Upstream. Level-bracket count for random bot distribution, unused since "
        "the bracket logic it belonged to was replaced.",
    "randomGearTabardsUnobtainable":
        "Upstream. Gear-roll filter for tabards; the roll does not consult it.",
    "RandombotsWalkingRPGInDoors":
        "Upstream. Indoor variant of the walking-RPG toggle; only the outdoor one "
        "is read.",
}

# `member = config.GetIntDefault("Key", ...)` at the start of a line.
#
# Anchored at line start on purpose: a LOCAL is written `int rProb = config.Get...`,
# with a type in front, and locals are not what this test is about. An earlier
# draft matched them and then had to guess whether the next line was a use.
ASSIGN = re.compile(
    r'^\s*(\w+)\s*=\s*(?:config|sConfig)\.Get(\w+)Default\(\s*"([^"]+)"', re.M)


def declaration_pattern(name):
    """Matches `uint32 foo;` but never `bar[i] = foo;`.

    The character class excludes '[', ']' and '=' deliberately. A previous
    version used `.*` for the type, which happily matched
    `classRaceProbability[cls][race] = rProb;` as a declaration and so reported a
    variable used on the very next line as dead. A guard that cries wolf is worse
    than no guard, because it gets switched off.
    """
    return re.compile(
        r'^\s*[A-Za-z_][A-Za-z0-9_:<>,\s\*&]*\s+' + re.escape(name) + r'\s*;\s*(//.*)?$')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module-dir", required=True)
    args = ap.parse_args()

    src = pathlib.Path(args.module_dir) / "src" / "playerbot"
    config_cpp = src / "PlayerbotAIConfig.cpp"
    if not config_cpp.is_file():
        print("FAIL: %s not found -- the scan would pass having read nothing" % config_cpp)
        return 2

    assignments = ASSIGN.findall(config_cpp.read_text(encoding="utf-8", errors="replace"))
    if not assignments:
        # An empty result must fail. It is the one outcome that looks like a pass
        # and proves nothing -- a rename of GetIntDefault would produce it.
        print("FAIL: no config assignments matched; the pattern has gone stale")
        return 2

    sources = [p for p in src.rglob("*") if p.suffix in (".cpp", ".h")]
    lines_by_file = {p: p.read_text(encoding="utf-8", errors="replace").split("\n")
                     for p in sources}

    dead = {}
    for member, _kind, key in assignments:
        decl = declaration_pattern(member)
        word = re.compile(r'\b' + re.escape(member) + r'\b')
        uses = 0
        for lines in lines_by_file.values():
            for line in lines:
                if not word.search(line):
                    continue
                if "Default(" in line and "=" in line:
                    continue   # the assignment being examined
                if decl.match(line):
                    continue   # the member declaration
                uses += 1
        if uses == 0:
            dead[member] = key

    failures = []

    for member, key in sorted(dead.items()):
        if member not in ALLOWED:
            failures.append(
                "%s (%s) is loaded and never read.\n"
                "    An operator can set this key and nothing happens. Either read it, "
                "remove it, or add it to ALLOWED with a reason." % (key, member))

    for member in sorted(ALLOWED):
        if member not in dead:
            failures.append(
                "%s is in the allowlist but IS read now.\n"
                "    Remove it from ALLOWED -- a stale allowlist hides the next one."
                % member)

    if failures:
        print("Config keys loaded but never read:\n")
        for f in failures:
            print("  - %s\n" % f)
        return 1

    print("config key usage OK (%d keys scanned, %d known-dead allowlisted)"
          % (len(assignments), len(ALLOWED)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
