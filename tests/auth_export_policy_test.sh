#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 9 ]]; then
  printf 'usage: %s <gssapi|sasl> <shared-library> <allowlist> <elf|macho> <nm> <compiler> <sysroot> <repo-root> <staged-dependency-root>\n' "$0" >&2
  exit 2
fi

provider=$1
library=$2
allowlist=$3
format=$4
symbol_tool=$5
compiler=$6
sysroot=$7
repo_root=$8
dependency_root=$9
if [[ ! -f "$library" || ! -f "$allowlist" ]]; then
  printf 'auth export input is missing: %s or %s\n' "$library" "$allowlist" >&2
  exit 2
fi

case "$format" in
  macho)
    actual=$("$symbol_tool" -gU "$library" | awk '{ print $NF }' | sort -u)
    expected=$(awk '/^cpkt_/ { print "_" $0 }' "$allowlist" | sort -u)
    ;;
  elf)
    actual=$("$symbol_tool" -D --defined-only "$library" |
      awk '{ print $NF }' | sed '/^_init$/d; /^_fini$/d' | sort -u)
    expected=$(awk '/^cpkt_/ { print }' "$allowlist" | sort -u)
    ;;
  *)
    printf 'unsupported auth symbol format: %s\n' "$format" >&2
    exit 2
    ;;
esac
if [[ "$actual" != "$expected" ]]; then
  printf '%s exact export catalog mismatch\nexpected:\n%s\nactual:\n%s\n' \
    "$provider" "$expected" "$actual" >&2
  exit 1
fi

if [[ "$provider" == gssapi ]]; then
  headers=("$repo_root/include/cpkt/gssapi.h")
  sentinel=cpkt_gss_ext_invalid
elif [[ "$provider" == sasl ]]; then
  headers=("$repo_root/include/cpkt/sasl.h"
           "$repo_root/include/cpkt/sasl_plugin.h")
  sentinel=cpkt_sasl_plugin_utils_cleanup
else
  printf 'unknown auth provider: %s\n' "$provider" >&2
  exit 2
fi
while IFS= read -r symbol; do
  [[ -n "$symbol" ]] || continue
  if ! rg -q "\\b${symbol}[[:space:]]*\\(" "${headers[@]}"; then
    printf '%s export has no callable public C89 declaration: %s\n' \
      "$provider" "$symbol" >&2
    exit 1
  fi
done < "$allowlist"

if [[ "$format" == elf ]]; then
  if "$symbol_tool" -D --undefined-only "$library" |
      awk '{ print $NF }' | rg -q '(^|@)(cpkt_|pslog_)'; then
    printf '%s has a private or unexpected facade import\n' "$provider" >&2
    exit 1
  fi
fi

mkdir -p "$repo_root/build"
scratch=$(mktemp -d "$repo_root/build/auth-export-policy.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
if [[ "$format" == elf ]]; then
  "$symbol_tool" -D --undefined-only "$library" |
    awk '{ print $NF }' | sed 's/@.*$//' |
    rg '^(gss|GSS|krb5|sasl|_sasl|prop_|auxprop_)' |
    sort -u > "$scratch/provider-imports" || true
  : > "$scratch/provider-definitions"
  for dependency in "$dependency_root"/krb5/install/lib/*.so* \
                    "$dependency_root"/cyrus-sasl/install/lib/*.so*; do
    [[ -f "$dependency" ]] || continue
    "$symbol_tool" -D --defined-only "$dependency" |
      awk '{ print $NF }' | sed 's/@.*$//' >> "$scratch/provider-definitions"
  done
  sort -u "$scratch/provider-definitions" -o "$scratch/provider-definitions"
  comm -23 "$scratch/provider-imports" "$scratch/provider-definitions" \
    > "$scratch/unresolved-provider-imports"
  if [[ -s "$scratch/unresolved-provider-imports" ]]; then
    printf '%s imports symbols absent from staged shared providers:\n' "$provider" >&2
    cat "$scratch/unresolved-provider-imports" >&2
    exit 1
  fi
fi
if [[ "$format" == macho ]]; then
  "$symbol_tool" -u "$library" | awk '{ print $NF }' |
    rg '^_(gss|GSS|krb5|sasl|_sasl|prop_|auxprop_)' |
    sort -u > "$scratch/provider-imports" || true
  : > "$scratch/provider-definitions"
  for dependency in "$dependency_root"/krb5/install/lib/*.dylib \
                    "$dependency_root"/cyrus-sasl/install/lib/*.dylib; do
    [[ -f "$dependency" ]] || continue
    "$symbol_tool" -gU "$dependency" |
      awk '{ print $NF }' >> "$scratch/provider-definitions"
  done
  sort -u "$scratch/provider-definitions" -o "$scratch/provider-definitions"
  comm -23 "$scratch/provider-imports" "$scratch/provider-definitions" \
    > "$scratch/unresolved-provider-imports"
  if [[ -s "$scratch/unresolved-provider-imports" ]]; then
    printf '%s imports symbols absent from staged Mach-O providers:\n' "$provider" >&2
    cat "$scratch/unresolved-provider-imports" >&2
    exit 1
  fi
fi
args=("$compiler" -std=c89 -Wall -Wextra -Wpedantic -pedantic-errors -Werror)
if [[ -n "$sysroot" ]]; then
  args+=("--sysroot=$sysroot")
fi
link_args=()
for dependency_dir in "$dependency_root"/*/install/lib; do
  [[ -d "$dependency_dir" ]] || continue
  link_args+=("-L$dependency_dir")
  if [[ "$format" == elf ]]; then
    link_args+=("-Wl,-rpath-link,$dependency_dir")
  fi
done
if [[ "$provider" == gssapi ]]; then
  cat > "$scratch/public.c" <<'EOF'
#include <cpkt/gssapi.h>
int main(void) { return cpkt_gss_status_is_error(CPKT_GSS_COMPLETE); }
EOF
  cat > "$scratch/private.c" <<'EOF'
#include <cpkt/gssapi.h>
extern cpkt_gss_status cpkt_gss_ext_invalid(cpkt_gss_status *);
int main(void) { cpkt_gss_status minor = 0; return (int)cpkt_gss_ext_invalid(&minor); }
EOF
else
  cat > "$scratch/public.c" <<'EOF'
#include <cpkt/sasl.h>
int main(void) { return cpkt_sasl_error_string(CPKT_SASL_OK, 0, 0) == 0; }
EOF
  cat > "$scratch/private.c" <<'EOF'
extern void cpkt_sasl_plugin_utils_cleanup(void);
int main(void) { cpkt_sasl_plugin_utils_cleanup(); return 0; }
EOF
fi
if ! output=$("${args[@]}" -I"$repo_root/include" "$scratch/public.c" \
    "$library" "${link_args[@]}" -o "$scratch/public" 2>&1); then
  printf '%s public-link control failed:\n%s\n' "$provider" "$output" >&2
  exit 1
fi
if output=$("${args[@]}" -I"$repo_root/include" "$scratch/private.c" "$library" \
    "${link_args[@]}" \
    -o "$scratch/private" 2>&1); then
  printf '%s private symbol unexpectedly linked: %s\n' "$provider" "$sentinel" >&2
  exit 1
fi
if [[ "$output" != *"$sentinel"* ]]; then
  printf '%s private-link check failed for an unrelated reason:\n%s\n' \
    "$provider" "$output" >&2
  exit 1
fi
