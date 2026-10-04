#!/usr/bin/env bash
set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work_root=$(mktemp -d "$repo_root/build/krb5-feature-contract.XXXXXX")
trap 'rm -rf "$work_root"' EXIT
mkdir -p "$work_root/include"
check="$repo_root/cmake/assert_krb5_features.cmake"

cat > "$work_root/include/autoconf.h" <<'EOF'
#define TLS_IMPL_OPENSSL 1
EOF
cat > "$work_root/config.log" <<'EOF'
ac_cv_func_getpwnam_r=yes
ac_cv_func_getpwuid_r=yes
ac_cv_func_gethostbyname_r=yes
ac_cv_func_getservbyname_r=yes
ac_cv_func_gmtime_r=yes
ac_cv_func_localtime_r=yes
EOF

assert_configure() {
  cmake -DCPKT_KRB5_BUILD_DIR="$work_root" \
    -DCPKT_KRB5_REQUIRE_GLIBC_REENTRANT="$1" -P "$check"
}

assert_rejected() {
  if assert_configure "$1" > "$work_root/failure.log" 2>&1; then
    printf 'Kerberos feature gate accepted a missing feature\n' >&2
    exit 1
  fi
}

assert_configure gnu
sed -i '/TLS_IMPL_OPENSSL/d' "$work_root/include/autoconf.h"
assert_rejected gnu
printf '#define TLS_IMPL_OPENSSL 1\n' > "$work_root/include/autoconf.h"

for feature in getpwnam_r getpwuid_r gethostbyname_r getservbyname_r gmtime_r localtime_r; do
  cp "$work_root/config.log" "$work_root/config.backup"
  sed -i "s/ac_cv_func_${feature}=yes/ac_cv_func_${feature}=no/" "$work_root/config.log"
  assert_rejected gnu
  assert_configure musl
  mv "$work_root/config.backup" "$work_root/config.log"
done
