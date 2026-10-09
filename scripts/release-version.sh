#!/usr/bin/env bash
set -euo pipefail
root=${1:-$(dirname -- "$0")/..}
root=$(CDPATH= cd -- "$root" && pwd -P)
top=$(git -C "$root" rev-parse --show-toplevel 2>/dev/null) || top=
if [ -n "$top" ]; then top=$(CDPATH= cd -- "$top" && pwd -P); fi
if [ "$top" = "$root" ]; then
  version=
  while IFS= read -r tag; do
    [[ "$tag" =~ ^v[0-9]+[.][0-9]+[.][0-9]+$ ]] || continue
    [ -z "$version" ] || { printf 'ambiguous exact release tags\n' >&2; exit 1; }
    [ "$(git -C "$root" cat-file -t "refs/tags/$tag")" = commit ] || { printf 'release tag must be lightweight\n' >&2; exit 1; }
    [ -z "$(git -C "$root" symbolic-ref -q "refs/tags/$tag" || true)" ] || { printf 'symbolic release tag is unsupported\n' >&2; exit 1; }
    version=${tag#v}
  done < <(git -C "$root" tag --points-at HEAD --list 'v[0-9]*.[0-9]*.[0-9]*')
  if [ -n "${CPKT_RELEASE_VERSION_OVERRIDE:-}" ]; then version=$CPKT_RELEASE_VERSION_OVERRIDE; fi
  version=${version:-0.0.0}
else
  [ -f "$root/VERSION" ] && [ -f "$root/RELEASE_MANIFEST" ] || { printf 'source archive version/manifest missing\n' >&2; exit 1; }
  IFS= read -r version < "$root/VERSION"
fi
[[ "$version" =~ ^[0-9]+[.][0-9]+[.][0-9]+$ ]] || { printf 'invalid release version\n' >&2; exit 1; }
printf '%s\n' "$version"
