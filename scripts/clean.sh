#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
action=${1:-clean}
if [ "$#" -gt 0 ]; then shift; fi
group=${GROUP:-all}
path=
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --group) group=$2 ;; --path) path=$2 ;; *) cpkt_fail "unknown cleanup option: $1" ;; esac
  shift 2
done
cpkt_check_group "$group"
export GROUP="$group"

remove_generated() {
  local path=$1 parent
  case "$path" in
    "$cpkt_root/build"|"$cpkt_root/build/"*|"$cpkt_root/.cache/"*|"$cpkt_root/.cache"|"$cpkt_root/dist"|"$cpkt_root"/package-assertions-*) ;;
    "$cpkt_root"/scripts/*|"$cpkt_root"/tests/*|"$cpkt_root"/tools/*|"$cpkt_root"/cmake/*)
      [ "${path##*/}" = __pycache__ ] || cpkt_fail "cleanup source path is not a Python cache: $path" ;;
    *) cpkt_fail "cleanup path is not generated repository state: $path" ;;
  esac
  case "$path/" in *'/../'*|*'/./'*) cpkt_fail 'cleanup path contains traversal' ;; esac
  parent=$path
  while [ "$parent" != "$cpkt_root" ]; do
    [ ! -L "$parent" ] || cpkt_fail "cleanup path has a symlink ancestor: $parent"
    parent=${parent%/*}
  done
  rm -rf -- "$path"
}
case "$action" in
  graph|stage)
    case "$path" in "$cpkt_root/build/"*) ;; *) cpkt_fail 'graph/stage cleanup must remain under build/' ;; esac
    remove_generated "$path"
    exit 0 ;;
  clean-dist)
    [ "$group" = all ] || cpkt_fail 'dist cleanup requires GROUP=all'
    remove_generated "$cpkt_root/dist"
    exit 0 ;;
  clean) ;;
  *) cpkt_fail "unsupported cleanup operation: $action" ;;
esac
if [ "$group" = all ]; then
  remove_generated "$cpkt_root/build"
  remove_generated "$cpkt_root/.cache"
  remove_generated "$cpkt_root/dist"
  for entry in "$cpkt_root"/package-assertions-*; do remove_generated "$entry"; done
  for directory in scripts tests tools cmake; do
    [ -d "$cpkt_root/$directory" ] || continue
    while IFS= read -r -d '' entry; do remove_generated "$entry"; done \
      < <(find "$cpkt_root/$directory" -type d -name __pycache__ -prune -print0)
  done
else
  for target in "$cpkt_root/build/"*; do
    case "$(basename -- "$target")" in control|verification|package-stage) continue ;; esac
    remove_generated "$target/$group"
  done
  for base in verification package-stage; do
    for target in "$cpkt_root/build/$base/"*; do remove_generated "$target/$group"; done
  done
  while IFS= read -r component; do
    directory=$("$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" -DCPKT_INFO=component-directory "-DCPKT_DEPENDENCY=$component" -P "$cpkt_root/cmake/lifecycle-info.cmake")
    for target in "$cpkt_root/.cache/deps/"*; do remove_generated "$target/$directory"; done
    for target in "$cpkt_root/.cache/deps-build/"*; do remove_generated "$target/$directory"; done
    for target in "$cpkt_root/.cache/dependency-contracts/"*; do
      remove_generated "$target/$component.inputs"
      remove_generated "$target/$component.complete"
    done
  done < <(cpkt_info components)
fi
