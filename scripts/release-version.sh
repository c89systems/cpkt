#!/usr/bin/env bash
set -eu

repo_root=${1:-}
if [ -z "$repo_root" ]; then
  repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fi
repo_root=$(CDPATH= cd -- "$repo_root" && pwd)

git_top_level=
if git_top_level=$(git -C "$repo_root" rev-parse --show-toplevel 2>/dev/null); then
  git_top_level=$(CDPATH= cd -- "$git_top_level" && pwd)
fi

if [ "$git_top_level" = "$repo_root" ]; then
  version_tag=
  candidate_tags=$(
    git -C "$repo_root" tag --points-at HEAD --list 'v[0-9]*.[0-9]*.[0-9]*' 2>/dev/null |
      sed -n '/^v[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$/p' |
      sort -V
  )
  for candidate_tag in $candidate_tags; do
    candidate_type=$(git -C "$repo_root" cat-file -t "$candidate_tag")
    if [ "$candidate_type" = "commit" ]; then
      version_tag=$candidate_tag
    fi
  done

  if [ -z "$version_tag" ]; then
    printf '0.0.0\n'
  else
    printf '%s\n' "${version_tag#v}"
  fi
  exit 0
fi

if [ -f "$repo_root/VERSION" ]; then
  version=$(sed -n '1{s/[[:space:]]*$//;p;q;}' "$repo_root/VERSION")
  if [ -z "$version" ]; then
    printf 'failed to resolve version: %s/VERSION is empty\n' "$repo_root" >&2
    exit 1
  fi
  printf '%s\n' "$version"
  exit 0
fi

printf 'failed to resolve version: %s is neither a git worktree nor a source archive with VERSION\n' "$repo_root" >&2
exit 1
