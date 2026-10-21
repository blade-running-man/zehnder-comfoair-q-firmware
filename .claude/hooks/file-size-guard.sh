#!/usr/bin/env bash
# PostToolUse guard: 350 lines is the ceiling for a hand-written source file.
#
# Why a hook and not an AGENTS.md rule: the rule is only read at the start of a
# session and drifts out of attention over a long one. The hook fires on every
# single write, so the ceiling is enforced at the moment it is crossed rather
# than discovered in review.
#
# It never blocks. It returns additionalContext, so the model is told the file
# is over budget and can split it in the same turn while it still holds the
# design in context -- which is the only cheap moment to do it.
set -euo pipefail

LIMIT=350          # hand-written source
TEST_LIMIT=600     # a Unity suite is one binary per directory, so it splits by
                   # directory, not by file; the looser number reflects that.

payload=$(cat)
file=$(printf '%s' "$payload" | jq -r '.tool_response.filePath // .tool_input.file_path // empty')
[ -n "$file" ] || exit 0
[ -f "$file" ] || exit 0

case "$file" in
  # Generated, vendored or machine-owned. Splitting these is meaningless or wrong.
  *registry_generated.h|*sdkconfig*|*package-lock.json|*/node_modules/*|*/.pio/*|*/dist/*) exit 0 ;;
  *.c|*.h|*.cpp|*.hpp|*.ts|*.tsx|*.py) ;;
  *) exit 0 ;;
esac

lines=$(wc -l < "$file" | tr -d ' ')
limit=$LIMIT
kind="source"
case "$file" in */test/*|*/tests/*|*test_*) limit=$TEST_LIMIT; kind="test" ;; esac

[ "$lines" -le "$limit" ] || jq -n \
  --arg f "$file" --arg l "$lines" --arg lim "$limit" --arg k "$kind" \
  '{
     systemMessage: ("\($f): \($l) lines, over the \($lim)-line ceiling"),
     hookSpecificOutput: {
       hookEventName: "PostToolUse",
       additionalContext: ("\($f) is now \($l) lines; the ceiling for a \($k) file is \($lim). Split it along a seam that already exists (one responsibility per file, public header unchanged) before moving on -- doing it now is cheap, doing it later means re-reading the file. If it was already over the ceiling before this edit, do not rewrite it wholesale: note it and split only the part you are working in.")
     }
   }'
