#!/usr/bin/env bash
#
# Assert UPSTREAM.lock against the real upstream.
#
# This was inline shell inside the `upstream-freshness` job in
# .github/workflows/ci.yml. It is a file now for one reason: the guard it
# implements had been green for the life of the fork over a fork_point that was
# 512 commits stale, and a guard nobody can run cannot be watched failing. It is
# runnable on a developer machine exactly as CI runs it:
#
#   tools/ci/check-upstream-lock.sh
#
# It needs network access to upstream -- it fetches the tracked branch itself
# rather than trusting whatever a previous fetch left in refs/remotes. Without
# that fetch none of the ancestry questions below can be answered, so a failed
# fetch is a failure, never a skip.
#
# Every failure path ends in a `::error file=UPSTREAM.lock::` annotation naming
# the key that is wrong and what to do about it, and the script exits non-zero.
# It fails closed: a missing key, an unparseable value, an unreachable upstream
# or an ambiguous merge base are all errors, not passes.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

LOCK=UPSTREAM.lock

if [ ! -f "$LOCK" ]; then
  echo "::error::$LOCK not found at the repository root ($PWD)" >&2
  exit 1
fi

fail() {
  # GitHub renders this as a file annotation; locally it is just a line of text
  # that still names the file and the key.
  echo "::error file=$LOCK::$*" >&2
}

field() {
  # awk rather than sed: every line in UPSTREAM.lock is `key = value` with
  # whitespace around the `=`, which awk's default field splitting reads
  # directly -- no regex full of backslashes to be mangled on its way through
  # YAML and two shells.
  awk -v k="$1" '$1 == k && $2 == "=" { print $3; exit }' "$LOCK"
}

UPSTREAM_URL=$(field upstream_url)
UPSTREAM_BRANCH=$(field upstream_branch)
FORK_POINT=$(field fork_point)
TRACK_BRANCH=$(field upstream_tracking_branch)
TRACK_COMMIT=$(field upstream_tracking_commit)
MAX_DRIFT=$(field max_drift_commits)

# A renamed, deleted or commented-out key reads as the empty string, which would
# otherwise turn every assertion below into a comparison of two empty strings --
# i.e. into a pass. Named individually so the message says which one.
# Shell variable -> the key it came from, so a message names what to edit rather
# than what the script happens to call it.
declare -A KEYOF=(
  [UPSTREAM_URL]=upstream_url
  [UPSTREAM_BRANCH]=upstream_branch
  [FORK_POINT]=fork_point
  [TRACK_BRANCH]=upstream_tracking_branch
  [TRACK_COMMIT]=upstream_tracking_commit
  [MAX_DRIFT]=max_drift_commits
)

for v in UPSTREAM_URL UPSTREAM_BRANCH FORK_POINT TRACK_BRANCH TRACK_COMMIT MAX_DRIFT; do
  if [ -z "${!v}" ]; then
    fail "${KEYOF[$v]} is missing from $LOCK (expected a line \`<key> = <value>\`; keys are upstream_url, upstream_branch, fork_point, upstream_tracking_branch, upstream_tracking_commit, max_drift_commits)"
    exit 1
  fi
done

# Full 40-hex only. An abbreviated hash would still resolve through rev-parse
# but would never string-compare equal to a merge base printed in full, so it
# would fail with a confusing message instead of an honest one.
for v in FORK_POINT TRACK_COMMIT; do
  val=${!v}
  if [ ${#val} -ne 40 ] || [ -n "${val//[0-9a-f]/}" ]; then
    fail "${KEYOF[$v]} must be a full lowercase 40-character hex commit id, got '$val'"
    exit 1
  fi
done

case "$MAX_DRIFT" in
  ''|*[!0-9]*) fail "max_drift_commits must be a non-negative integer, got '$MAX_DRIFT'"; exit 1 ;;
esac

# The fork side of every comparison below. HEAD, not origin/main, and the choice
# matters:
#
#   * On `pull_request` GitHub checks out the merge of the pull request into its
#     base, so HEAD is the tree that would exist if this pull request landed.
#     That is the thing worth asserting about. Measuring origin/main instead
#     would make the guard unpassable for the one pull request that legitimately
#     changes the answer -- a real upstream merge moves the merge base and must
#     update fork_point in the same commit, and origin/main does not contain
#     that merge until afterwards. The guard would then demand the old value on
#     the way in and the new value forever after, which is a guard that has to
#     be disabled to do the work it exists to protect.
#   * On `push: main`, on the weekly schedule and on a local run, HEAD is the
#     branch itself, so this is the origin/main assertion the stricter reading
#     asked for.
OURS=$(git rev-parse HEAD)
OURS_NAME=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo HEAD)

echo "upstream        : $UPSTREAM_URL"
echo "branch          : $UPSTREAM_BRANCH"
echo "fork point      : $FORK_POINT"
echo "tracking branch : $TRACK_BRANCH"
echo "tracking commit : $TRACK_COMMIT"
echo "max drift       : $MAX_DRIFT"
echo "our side        : $OURS ($OURS_NAME)"

git remote add upstream "$UPSTREAM_URL" 2>/dev/null \
  || git remote set-url upstream "$UPSTREAM_URL"

# Network required. Everything after this line is an ancestry question about
# upstream history, and an unfetched upstream cannot answer one -- so this is an
# error with the URL and branch in it, not a silent "nothing to check".
git fetch --quiet upstream "+refs/heads/$UPSTREAM_BRANCH:refs/remotes/upstream/$UPSTREAM_BRANCH" || {
  fail "cannot fetch $UPSTREAM_BRANCH from $UPSTREAM_URL. Nothing below can be decided without it; check upstream_url and upstream_branch in $LOCK and the runner's network access."
  exit 1
}

git fetch --quiet origin "+refs/heads/$TRACK_BRANCH:refs/remotes/origin/$TRACK_BRANCH" || {
  fail "branch '$TRACK_BRANCH' does not exist on origin"
  exit 1
}

TIP=$(git rev-parse "refs/remotes/upstream/$UPSTREAM_BRANCH")
HEAD_OF_TRACKING=$(git rev-parse "refs/remotes/origin/$TRACK_BRANCH")
echo "live tip        : $TIP"
echo "$TRACK_BRANCH is: $HEAD_OF_TRACKING"

# A hash recorded in the file but absent from the object store is not a
# disagreement CI can reason about; say so rather than letting merge-base die
# with `Not a valid object name`.
for v in FORK_POINT TRACK_COMMIT; do
  val=${!v}
  if ! git cat-file -e "$val^{commit}" 2>/dev/null; then
    fail "${KEYOF[$v]} ($val) is not a commit in this repository, even after fetching $UPSTREAM_BRANCH. Either the hash is wrong or it belongs to a history nothing here points at."
    exit 1
  fi
done

status=0

# 1. The branch and the record agree.
if [ "$HEAD_OF_TRACKING" != "$TRACK_COMMIT" ]; then
  fail "$TRACK_BRANCH is at $HEAD_OF_TRACKING but $LOCK records $TRACK_COMMIT. Move the branch and the record together."
  status=1
fi

# Checks 2-4 are about the REF, so they are measured against the branch head and
# not against the string in the file. Measuring the recorded value instead makes
# them unreachable in the case that matters: somebody moves upstream-tracking to
# a dead branch and does not touch UPSTREAM.lock, check 1 fires, and 2-4 sit
# there checking a commit that is still correct. Verified by watching exactly
# that happen.

# 2. The tracked ref is on the branch we claim to track. This is the assertion
#    the task asked for, and on its own it is weaker than it looks -- see 3.
if ! git merge-base --is-ancestor "$HEAD_OF_TRACKING" "$TIP"; then
  fail "$TRACK_BRANCH ($HEAD_OF_TRACKING) is NOT an ancestor of $UPSTREAM_BRANCH ($TIP). It is not on the branch this fork tracks."
  status=1
fi

# 3. ...and it is newer than our own fork point. Every dead upstream branch
#    (main, dev, 1181dev, challenges, shop, 1181-rogue-fixes) is an ancestor of
#    the live branch and would sail through check 2; what gives them away is
#    that they are all older than the fork point.
if ! git merge-base --is-ancestor "$FORK_POINT" "$HEAD_OF_TRACKING"; then
  fail "$TRACK_BRANCH ($HEAD_OF_TRACKING) is behind this fork's own fork point $FORK_POINT -- that is the signature of one of upstream's dead branches, not of the branch we track."
  status=1
fi

# 4. Staleness, as distinct from wrongness.
if git merge-base --is-ancestor "$HEAD_OF_TRACKING" "$TIP"; then
  DRIFT=$(git rev-list --count "$HEAD_OF_TRACKING..$TIP")
  echo "drift           : $DRIFT commits behind $UPSTREAM_BRANCH"
  if [ "$DRIFT" -gt "$MAX_DRIFT" ]; then
    fail "tracked commit is $DRIFT commits behind $UPSTREAM_BRANCH (limit $MAX_DRIFT). Fetch upstream, move $TRACK_BRANCH, update $LOCK."
    status=1
  elif [ "$DRIFT" -gt 0 ]; then
    echo "::notice::tracked commit is $DRIFT commits behind $UPSTREAM_BRANCH (limit $MAX_DRIFT)."
  fi
fi

# 5-7. fork_point itself. Checks 1-4 read fork_point only as a floor for the
# tracking branch, which is why this file could record a commit 512 behind the
# real merge base and stay green for the life of the fork: a stale floor is
# still a floor. The header of UPSTREAM.lock calls fork_point "the answer to
# what does our delta apply to", and nothing asserted that answer until here.
#
# The merge base is the definition of that answer, so check 7 is equality with
# it and not merely an ancestor test. That is the strong form, and it is
# deliberately the strong form:
#
#   * Ancestry alone is the trap we already fell into. 61a8269 was an ancestor
#     of both sides and of the tracking branch, so every ancestor test passed
#     while the recorded value understated by 512 commits. Wrong in the benign
#     direction is still wrong, and every "we are N commits behind" figure
#     computed from it is wrong by construction -- that has happened twice in
#     this repository's short history.
#   * Equality is stable, which is the usual objection and it does not hold.
#     merge-base(ours, upstream) does not move when upstream advances; it moves
#     only when our side absorbs upstream commits. So the ordinary case --
#     upstream pushes daily, we do nothing -- leaves this check green, and
#     staleness of that kind is check 4's job, measured against a branch and a
#     budget. Check 7 goes red exactly when somebody merges upstream and forgets
#     this file, which is the whole point.
#   * Checks 5 and 6 are not redundant with 7. They fail with a precise message
#     for the two ways the value can be nonsense (a commit off our history, a
#     commit off upstream's) instead of leaving the reader to work out from a
#     hash mismatch which side is at fault.
MERGE_BASES=$(git merge-base --all "$OURS" "$TIP")
MB_COUNT=$(printf '%s\n' "$MERGE_BASES" | grep -c . || true)

echo "merge base(s)   : $(printf '%s' "$MERGE_BASES" | tr '\n' ' ')"

# 5. fork_point is on our history.
if ! git merge-base --is-ancestor "$FORK_POINT" "$OURS"; then
  fail "fork_point ($FORK_POINT) is not an ancestor of $OURS_NAME ($OURS). Our delta cannot be applied on top of a commit we do not contain. Re-derive it: git merge-base HEAD upstream/$UPSTREAM_BRANCH"
  status=1
fi

# 6. fork_point is on upstream's tracked branch.
if ! git merge-base --is-ancestor "$FORK_POINT" "$TIP"; then
  fail "fork_point ($FORK_POINT) is not an ancestor of upstream/$UPSTREAM_BRANCH ($TIP). It is not a commit on the branch this fork claims to be based on. Re-derive it: git merge-base HEAD upstream/$UPSTREAM_BRANCH"
  status=1
fi

# 7. ...and it is the merge base, not merely somewhere below it.
if [ "$MB_COUNT" -ne 1 ]; then
  # Criss-cross history: no single commit is "the" base, so a single recorded
  # hash cannot be right. Fail closed and make a human look.
  fail "git merge-base --all HEAD upstream/$UPSTREAM_BRANCH returned $MB_COUNT candidates ($(printf '%s' "$MERGE_BASES" | tr '\n' ' ')). fork_point records one hash and cannot describe this history; resolve the criss-cross before trusting $LOCK."
  status=1
elif [ "$MERGE_BASES" != "$FORK_POINT" ]; then
  if git merge-base --is-ancestor "$FORK_POINT" "$MERGE_BASES"; then
    # The benign direction: the recorded value is behind the real base. Say by
    # how far, because that number is what every "we are N behind" claim derived
    # from this file was wrong by.
    HOW="$(git rev-list --count "$FORK_POINT..$MERGE_BASES") commits further on"
  else
    HOW="and the recorded value is not even an ancestor of it"
  fi
  fail "fork_point is $FORK_POINT but git merge-base $OURS_NAME upstream/$UPSTREAM_BRANCH is $MERGE_BASES ($HOW). Set fork_point = $MERGE_BASES in $LOCK, and record the command in the comment above it."
  status=1
fi

if [ "$status" -eq 0 ]; then
  echo "$LOCK agrees with $UPSTREAM_URL $UPSTREAM_BRANCH"
fi
exit $status
