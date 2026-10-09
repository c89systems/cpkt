#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: native_hardening_host_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
guard="$source_dir/scripts/require-native-hardening-host.sh"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

make_uname() {
  local system_name=$1 machine_name=$2
  mkdir -p "$work_dir/bin"
  cat > "$work_dir/bin/uname" <<EOF
#!/usr/bin/env bash
case "\$1" in
  -s) printf '%s\\n' '$system_name' ;;
  -m) printf '%s\\n' '$machine_name' ;;
  *) exit 2 ;;
esac
EOF
  chmod +x "$work_dir/bin/uname"
}

make_uname Linux x86_64
PATH="$work_dir/bin:$PATH" bash "$guard" valgrind

make_uname Darwin x86_64
if output=$(PATH="$work_dir/bin:$PATH" bash "$guard" valgrind 2>&1); then
  printf 'native hardening host guard accepted Darwin x86_64\n' >&2
  exit 1
fi
case "$output" in
  *'native x86_64 Linux-only'*) ;;
  *)
    printf 'native hardening host guard did not explain the non-Linux rejection\n%s\n' "$output" >&2
    exit 1
    ;;
esac

make_uname Linux aarch64
if output=$(PATH="$work_dir/bin:$PATH" bash "$guard" fuzz 2>&1); then
  printf 'native hardening host guard accepted Linux aarch64\n' >&2
  exit 1
fi
case "$output" in
  *'native x86_64 Linux-only'*) ;;
  *)
    printf 'native hardening host guard did not explain the architecture rejection\n%s\n' "$output" >&2
    exit 1
    ;;
esac
