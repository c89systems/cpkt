#!/usr/bin/env bash
set -euo pipefail

skill_names=(pkt-systems-cmake-lifecycle codex-review-and-fix-loop)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
codex_home=${CODEX_HOME:-"${HOME:?HOME must be set}/.codex"}
skills_root="$codex_home/skills"
validator="$skills_root/.system/skill-creator/scripts/quick_validate.py"
skill_sources=()
skill_destinations=()
staging_dirs=()
backup_dirs=()
replaced=()
install_complete=0

cleanup() {
  status=$?

  if [ "$install_complete" -eq 0 ]; then
    for ((index=${#skill_names[@]}-1; index>=0; index--)); do
      destination=${skill_destinations[$index]:-}
      backup=${backup_dirs[$index]:-}
      if [ "${replaced[$index]:-0}" -eq 1 ]; then
        rm -rf -- "$destination"
      fi
      if [ -n "$backup" ] && [ -d "$backup" ]; then
        mv -- "$backup" "$destination"
      fi
    done
  fi

  for staging_dir in "${staging_dirs[@]}"; do
    if [ -n "$staging_dir" ] && [ -d "$staging_dir" ]; then
      rm -rf -- "$staging_dir"
    fi
  done

  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

validate_skill() {
  skill_path=$1
  if ! python3 "$validator" "$skill_path"; then
    fail "Codex skill validation failed for: $skill_path"
  fi
}

[ -f "$validator" ] || fail "Codex skill validator not found: $validator"

for index in "${!skill_names[@]}"; do
  skill_name=${skill_names[$index]}
  skill_sources[$index]="$script_dir/$skill_name"
  skill_destinations[$index]="$skills_root/$skill_name"
  staging_dirs[$index]=
  backup_dirs[$index]=
  replaced[$index]=0

  [ -d "${skill_sources[$index]}" ] || fail "Skill source directory not found: ${skill_sources[$index]}"
  validate_skill "${skill_sources[$index]}"
  if [ -L "${skill_destinations[$index]}" ]; then
    fail "Refusing to replace symlinked skill destination: ${skill_destinations[$index]}"
  fi
  if [ -e "${skill_destinations[$index]}" ] && [ ! -d "${skill_destinations[$index]}" ]; then
    fail "Skill destination is not a directory: ${skill_destinations[$index]}"
  fi
done

mkdir -p "$skills_root"
for index in "${!skill_names[@]}"; do
  skill_name=${skill_names[$index]}
  staging_dirs[$index]=$(mktemp -d "$skills_root/.${skill_name}.staging.XXXXXXXXXX")
  cp -R "${skill_sources[$index]}/." "${staging_dirs[$index]}/"
  validate_skill "${staging_dirs[$index]}"
done

for index in "${!skill_names[@]}"; do
  skill_name=${skill_names[$index]}
  destination=${skill_destinations[$index]}
  if [ -d "$destination" ]; then
    backup_dirs[$index]=$(mktemp -d "$skills_root/.${skill_name}.backup.XXXXXXXXXX")
    rmdir "${backup_dirs[$index]}"
    mv -- "$destination" "${backup_dirs[$index]}"
  fi

  replaced[$index]=1
  mv -- "${staging_dirs[$index]}" "$destination"
  staging_dirs[$index]=

  if ! diff -qr "${skill_sources[$index]}" "$destination"; then
    fail "Installed skill does not match repository source: $destination"
  fi
done

install_complete=1
for index in "${!skill_names[@]}"; do
  if [ -n "${backup_dirs[$index]}" ]; then
    rm -rf -- "${backup_dirs[$index]}"
  fi
  printf 'Installed %s to %s\n' "${skill_names[$index]}" "${skill_destinations[$index]}"
done
