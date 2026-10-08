#!/usr/bin/env bash
set -euo pipefail

skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
release_ref="$skill_dir/references/release.md"
local_ci_ref="$skill_dir/references/local-ci.md"
operability_ref="$skill_dir/references/operability.md"

# Wording guards validate policy routing. Isolated fixtures exercise the documented
# ref and publication commands; current-checkout refs and remote state stay intact.

fail() {
  printf 'test-release-tag-contract: %s\n' "$*" >&2
  exit 1
}

require_text() {
  local file=$1 text=$2 description=$3
  grep -Fq -- "$text" "$file" || fail "$file is missing lifecycle contract: $description"
}

require_text "$release_ref" \
  'Tag-mutating version and manifest contract checks must live behind the focused `make lifecycle-version-contract` target.' \
  'tag-mutating checks have a focused make target'
require_text "$release_ref" \
  'Make exposes the release targets with simple prerequisites and short recipes. Readable Bash scripts own procedural sequencing, validation and cleanup; CMake supplies build, test, install and package operations.' \
  'Make exposes targets, Bash sequences release work, and CMake owns build operations'
require_text "$release_ref" \
  '`make release` is the only standard release-flow target that runs this check, and it must run it before `clean`, `release-pipeline`, `release-matrix`, `package-verify`, checksum generation, or artifact production.' \
  'make release runs the tag contract before late release work'
require_text "$release_ref" \
  'Do not wire tests that create, delete, or otherwise mutate git tags into `test`, `test-all`, `prerelease`, `release-pipeline`, `release-matrix`, `package-verify`, or any other late release-flow command.' \
  'late release-flow commands exclude tag-mutating checks'
require_text "$release_ref" '## Temporary test-tag ownership' 'canonical ownership section'
require_text "$release_ref" 'the current `HEAD`' 'active-checkout version discovery'
require_text "$release_ref" 'this focused tag test does not configure CMake.' 'focused pre-clean scope'
require_text "$release_ref" 'git update-ref --no-deref --create-reflog' 'direct lightweight test tag with explicit reflog'
require_text "$release_ref" 'reject annotated or signed tag objects.' 'lightweight-only version tags'
require_text "$release_ref" 'skip temporary-tag creation.' 'already-tagged HEAD behavior'
require_text "$release_ref" 'nonce-bearing intent before exclusive creation' 'interrupted-creation recovery'
require_text "$release_ref" 'Authenticate the zero-to-object Git reflog entry' 'creation authentication'
require_text "$release_ref" 'intent alone does not prove tag ownership.' 'intent cannot authorize deletion'
require_text "$release_ref" 'Fail on an unowned pre-existing tag' 'foreign-tag refusal'
require_text "$release_ref" 'Automatic recovery and trap cleanup use compare-and-delete against the recorded object' 'owned-object deletion'
require_text "$release_ref" 'preserving changed or unowned refs.' 'changed-ref preservation'
require_text "$release_ref" 'cleanup restores the original version.' 'version restoration'
require_text "$release_ref" 'tag removal and branch rewind follow [failed local release recovery]' 'separate real-release approval'

for reference in "$local_ci_ref" "$operability_ref"; do
  require_text "$reference" \
    '[temporary test-tag ownership](release.md#temporary-test-tag-ownership)' \
    'routing to canonical ownership policy'
done

repo_root=${1:?pass the repository root for fixture scratch}
[ "$#" -eq 1 ] && [ -f "$repo_root/CMakeLists.txt" ] || exit 2
[ ! -L "$repo_root/build" ] || exit 2
grep -Fxq '/build/' "$repo_root/.gitignore" || exit 2
mkdir -p "$repo_root/build"
work=$(mktemp -d "$repo_root/build/skill-test-tag.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
awk '
  $0 == "## Temporary test-tag ownership" {wanted=1; next}
  wanted && $0 == "```bash" {code=1; next}
  code && $0 == "```" {exit}
  code {print}
' "$release_ref" > "$work/create.sh"
[ -s "$work/create.sh" ] || fail 'missing documented ref-creation example'
awk '
  $0 == "### Owned temporary-tag cleanup" {wanted=1; next}
  wanted && $0 == "```bash" {code=1; next}
  code && $0 == "```" {exit}
  code {print}
' "$release_ref" > "$work/cleanup.sh"
[ -s "$work/cleanup.sh" ] || fail 'missing documented ref-cleanup example'
git -c init.defaultBranch=fixture init -q "$work/repo"
cd "$work/repo"
git config user.name fixture
git config user.email fixture@example.invalid
git config core.hooksPath /dev/null
git config commit.gpgSign false
git config tag.gpgSign true
git config core.logAllRefUpdates false
git commit -qm 'test(lifecycle-skill): create isolated tag fixture' --allow-empty
test_tag_ref=refs/tags/v99.99.99
test_tag_object=$(git rev-parse HEAD)
nonce="${work##*/}"
export test_tag_ref test_tag_object nonce
printf '%s %s %s\n' "$test_tag_ref" "$test_tag_object" "$nonce" > "$work/intent"
bash "$work/create.sh"
[ "$(git cat-file -t "$test_tag_ref")" = commit ] || fail 'created tag is not lightweight'
log=".git/logs/$test_tag_ref"
grep -Eq "^0+ $test_tag_object " "$log" || fail 'missing zero-to-object reflog creation'
grep -Fq $'\t'"lifecycle-version-contract:$nonce" "$log" || fail 'nonce absent from creation reflog'
if bash "$work/create.sh" > "$work/log" 2>&1; then fail 'create-only command replaced an existing ref'; fi
[ "$(git rev-parse "$test_tag_ref")" = "$test_tag_object" ] || fail 'existing ref changed'
[ "$(wc -l < "$log")" -eq 1 ] || fail 'failed creation changed reflog'
git commit -qm 'test(lifecycle-skill): simulate changed tag ownership' --allow-empty
other=$(git rev-parse HEAD)
git update-ref "$test_tag_ref" "$other" "$test_tag_object"
if bash "$work/cleanup.sh" > "$work/log" 2>&1; then
  fail 'cleanup deleted a changed ref'
fi
[ "$(git rev-parse "$test_tag_ref")" = "$other" ] || fail 'changed ref was not preserved'
test_tag_object=$other bash "$work/cleanup.sh"
for target in refs/heads/missing refs/heads/unowned; do
  if [ "$target" = refs/heads/unowned ]; then git update-ref "$target" "$test_tag_object"; fi
  git symbolic-ref "$test_tag_ref" "$target"
  if bash "$work/create.sh" > "$work/log" 2>&1; then fail 'creation accepted a symbolic tag'; fi
  if bash "$work/cleanup.sh" > "$work/log" 2>&1; then
    fail 'cleanup accepted a symbolic tag'
  fi
  [ "$(git symbolic-ref "$test_tag_ref")" = "$target" ] || fail 'symbolic tag changed'
  if [ "$target" = refs/heads/missing ]; then
    if git show-ref --verify "$target" > "$work/log" 2>&1; then fail 'dangling tag created a branch'; fi
  else
    [ "$(git rev-parse "$target")" = "$test_tag_object" ] || fail 'symbolic tag mutated its branch'
  fi
  git symbolic-ref --delete "$test_tag_ref"
done

# Execute the documented publication argv against a local CLI fixture only.
awk '
  $0 == "## GitHub release destination" {wanted=1; next}
  wanted && $0 == "```bash" {code=1; next}
  code && $0 == "```" {exit}
  code {print}
' "$release_ref" > "$work/publish.sh"
[ -s "$work/publish.sh" ] || fail 'missing documented publication example'
release_repository=github.com/fixture/release
release_tag=v1.2.3
release_uploads=("$work/payload with spaces.tar.gz" "$work/CHECKSUMS")
export GH_REPO=fixture/unrelated
tag_available=1
gh() {
  [ "$#" -eq 8 ] && [ "$1" = release ] && [ "$2" = create ] &&
    [ "$3" = "$release_tag" ] && [ "$4" = --repo ] &&
    [ "$5" = "$release_repository" ] && [ "$6" = --verify-tag ] &&
    [ "$7" = "${release_uploads[0]}" ] && [ "$8" = "${release_uploads[1]}" ] || return 2
  [ "$tag_available" -eq 1 ] || return 1
  printf 'published\n' >> "$work/publications"
}
source "$work/publish.sh"
[ "$(wc -l < "$work/publications")" -eq 1 ] || fail 'publication fixture did not receive the verified destination'
tag_available=0
if source "$work/publish.sh"; then fail 'publication accepted a missing remote tag'; fi
[ "$(wc -l < "$work/publications")" -eq 1 ] || fail 'missing-tag failure published'
printf 'release ownership, symbolic-ref refusal and publication argv fixtures passed\n'
