#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: run_afl_fuzz_crash_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
fake_repo="$work_dir/repo"
fake_afl_root="$work_dir/afl"
mkdir -p "$fake_repo/scripts" "$fake_afl_root/bin" "$work_dir/seeds" "$work_dir/bin"
cp "$source_dir/scripts/run-afl-fuzz.sh" "$fake_repo/scripts/run-afl-fuzz.sh"
cp "$source_dir/scripts/require-native-hardening-host.sh" "$fake_repo/scripts/require-native-hardening-host.sh"

cat > "$fake_repo/scripts/cpkt-aflpp.sh" <<EOF
#!/usr/bin/env bash
printf 'export CPKT_AFLPP_ROOT=%q\n' '$fake_afl_root'
EOF
chmod +x "$fake_repo/scripts/cpkt-aflpp.sh"

cat > "$fake_afl_root/bin/afl-fuzz" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
output_dir=
while [[ $# -gt 0 ]]; do
  case "$1" in
    -o) output_dir=$2; shift 2 ;;
    *) shift ;;
  esac
done
case "${CPKT_FAKE_AFL_FINDING_KIND:-crashes}" in
  crashes)
    mkdir -p "$output_dir/default/crashes"
    : > "$output_dir/default/crashes/id:000000,sig:11,src:000000,time:0,execs:1,op:havoc,rep:1"
    ;;
  hangs)
    mkdir -p "$output_dir/default/hangs"
    : > "$output_dir/default/hangs/id:000000,src:000000,time:0,execs:1,op:havoc,rep:1"
    ;;
  none)
    mkdir -p "$output_dir/default/crashes" "$output_dir/default/hangs"
    ;;
  *)
    exit 2
    ;;
esac
EOF
chmod +x "$fake_afl_root/bin/afl-fuzz"

cat > "$work_dir/bin/uname" <<'EOF'
#!/usr/bin/env bash
case "$1" in
  -s) printf '%s\n' Linux ;;
  -m) printf '%s\n' x86_64 ;;
  *) exit 2 ;;
esac
EOF
chmod +x "$work_dir/bin/uname"

cat > "$work_dir/fuzz-target" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "$work_dir/fuzz-target"
: > "$work_dir/seeds/seed"

assert_recorded_finding_fails() {
  finding_kind=$1
  expected_message=$2
  output=

  if output=$(CPKT_FAKE_AFL_FINDING_KIND="$finding_kind" PATH="$work_dir/bin:$PATH" bash "$fake_repo/scripts/run-afl-fuzz.sh" smoke "$work_dir/fuzz-target" "$work_dir/seeds" 2>&1); then
    printf 'AFL++ gate accepted recorded %s finding\n' "$finding_kind" >&2
    exit 1
  fi
  case "$output" in
    *"$expected_message"*'AFL++ findings retained in:'*) ;;
    *)
      printf 'AFL++ gate did not report the recorded %s finding and retained findings\n%s\n' "$finding_kind" "$output" >&2
      exit 1
      ;;
  esac
}

assert_recorded_finding_fails crashes 'AFL++ recorded a crashing input'
assert_recorded_finding_fails hangs 'AFL++ recorded a hanging input'

if ! find "$work_dir" -type f -path '*/crashes/id:*' -print -quit | grep -q .; then
  printf 'AFL++ crash gate removed the recorded crashing input\n' >&2
  exit 1
fi

if ! find "$work_dir" -type f -path '*/hangs/id:*' -print -quit | grep -q .; then
  printf 'AFL++ hang gate removed the recorded hanging input\n' >&2
  exit 1
fi
