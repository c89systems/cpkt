#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  printf 'usage: %s <source-dir> <cmake>\n' "$0" >&2
  exit 2
fi

source_dir=$1
cmake_command=$2
mkdir -p "$source_dir/build"
work_dir=$(mktemp -d "$source_dir/build/darwin-install-name-normalization.XXXXXXXX")
trap '"$cmake_command" -E remove_directory "$work_dir"' EXIT HUP INT TERM

stage_dir=$work_dir/stage/usr/lib
installed_dir=$work_dir/install/lib
mkdir -p "$stage_dir" "$installed_dir" "$work_dir/bin"
printf 'fixture\n' > "$stage_dir/libdemo.3.3.dylib"
printf 'bundle\n' > "$stage_dir/libplugin.dylib"
ln -s libdemo.3.3.dylib "$stage_dir/libdemo.dylib"
"$cmake_command" -E copy_directory "$stage_dir" "$installed_dir"
if [[ -L "$installed_dir/libdemo.dylib" ]]; then
  printf 'fixture did not reproduce a dereferenced alias\n' >&2
  exit 1
fi

cat > "$work_dir/bin/otool" <<'SH'
#!/usr/bin/env sh
case "$1" in
  -L)
    printf '%s:\n\t/usr/lib/libdemo.dylib (compatibility version 3.0.0, current version 3.3.0)\n' "$2"
    ;;
  -D)
    if [ "${2##*/}" = libplugin.dylib ]; then
      printf '%s:\n' "$2"
    else
      printf '%s:\n/usr/lib/libdemo.3.3.dylib\n' "$2"
    fi
    ;;
  -hv)
    printf 'MH_MAGIC_64 ARM64 ALL BUNDLE 16\n'
    ;;
  *) exit 2 ;;
esac
SH
cat > "$work_dir/bin/install_name_tool" <<'SH'
#!/usr/bin/env sh
printf '%s\n' "$*" >> "$CPKT_TEST_INSTALL_NAME_LOG"
SH
chmod +x "$work_dir/bin/otool" "$work_dir/bin/install_name_tool"

CPKT_TEST_INSTALL_NAME_LOG="$work_dir/install-name.log" \
  "$cmake_command" \
    -DCPKT_DARWIN_LIBRARY_DIR="$installed_dir" \
    -DCPKT_DARWIN_STAGE_LIBRARY_DIR="$stage_dir" \
    -DCPKT_DARWIN_INSTALL_NAME_TOOL="$work_dir/bin/install_name_tool" \
    -DCPKT_DARWIN_OTOOL="$work_dir/bin/otool" \
    -P "$source_dir/cmake/normalize_darwin_dylib_install_names.cmake"

if [[ ! -L "$installed_dir/libdemo.dylib" ]] ||
   [[ "$(readlink "$installed_dir/libdemo.dylib")" != libdemo.3.3.dylib ]]; then
  printf 'Darwin library alias was not restored as a canonical symlink\n' >&2
  exit 1
fi
if ! grep -Fq -- "-id @rpath/libdemo.3.3.dylib $installed_dir/libdemo.3.3.dylib" \
    "$work_dir/install-name.log" ||
   ! grep -Fq -- "-change /usr/lib/libdemo.dylib @rpath/libdemo.dylib $installed_dir/libdemo.3.3.dylib" \
    "$work_dir/install-name.log"; then
  cat "$work_dir/install-name.log" >&2
  printf 'Darwin canonical install identity or alias load rewrite was lost\n' >&2
  exit 1
fi
if grep -Fq -- "-id @rpath/libplugin.dylib" "$work_dir/install-name.log"; then
  printf 'Darwin loadable bundle must not receive a dylib install ID\n' >&2
  exit 1
fi
