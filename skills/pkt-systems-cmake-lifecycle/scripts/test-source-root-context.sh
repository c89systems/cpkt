#!/usr/bin/env bash
set -euo pipefail
skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
repo_root=${1:?pass the repository root for fixture scratch}
[ "$#" -eq 1 ] && [ -f "$repo_root/CMakeLists.txt" ] || exit 2
[ ! -L "$repo_root/build" ] || exit 2
grep -Fxq '/build/' "$repo_root/.gitignore" || exit 2
mkdir -p "$repo_root/build"
work=$(mktemp -d "$repo_root/build/skill-test-source-root.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
awk '
  $0 == "## Source-root authority" {wanted=1; next}
  wanted && $0 == "```bash" {code=1; next}
  code && $0 == "```" {exit}
  code {print}
' "$skill_dir/references/release.md" > "$work/context.sh"
[ -s "$work/context.sh" ]
fail() { printf 'source-root fixture: %s\n' "$*" >&2; exit 1; }
expect() {
  [ "$(bash "$work/context.sh" "$1")" = "$2" ] || fail "wrong context for $1"
}
checkout="$work/checkout"
mkdir -p "$checkout"
git init -q "$checkout"
printf '/build/\n/VERSION\n' > "$checkout/.gitignore"
printf 'parent payload\n' > "$checkout/parent.txt"
git -C "$checkout" add .gitignore parent.txt
git -C "$checkout" -c user.name=fixture -c user.email=fixture@example.invalid \
  -c commit.gpgsign=false commit -qm 'test: create isolated source-root fixture'
printf '7.7.7\n' > "$checkout/VERSION"
printf 'parent.txt\n' > "$checkout/RELEASE_MANIFEST"
expect "$checkout" git
archive="$checkout/build/extracted source"
mkdir -p "$archive"
printf '1.2.3\n' > "$archive/VERSION"
printf 'payload.txt\n' > "$archive/RELEASE_MANIFEST"
printf 'archive payload\n' > "$archive/payload.txt"
[ "$(git -C "$archive" rev-parse --show-toplevel)" = "$checkout" ] || fail 'ancestor discovery not exercised'
expect "$archive" archive
mkdir -p "$work/plain-archive"
cp "$archive/VERSION" "$archive/RELEASE_MANIFEST" "$work/plain-archive/"
GIT_CEILING_DIRECTORIES="$work" expect "$work/plain-archive" archive
ln -s "$checkout" "$work/checkout-alias"
expect "$work/checkout-alias" git
git -C "$checkout" worktree add -q --detach "$work/worktree"
[ -f "$work/worktree/.git" ] || fail 'worktree .git file not exercised'
expect "$work/worktree" git
for metadata in VERSION RELEASE_MANIFEST; do
  mv "$archive/$metadata" "$work/$metadata"
  if bash "$work/context.sh" "$archive" > "$work/log" 2>&1; then
    fail "missing $metadata borrowed parent Git authority"
  fi
  mv "$work/$metadata" "$archive/$metadata"
done
printf 'source-root context, nested archive and Git worktree fixture passed\n'
