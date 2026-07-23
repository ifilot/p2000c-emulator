#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
output=${1:-"${repo_root}/images/cpm/p2edit.flp"}

build_dir="${repo_root}/build"
cli=${P2000C_CLI:-}
ipl="${repo_root}/tools/ipldump/IPLDUMP.BIN"
system_disk="${repo_root}/images/cpm/system.flp"
asm_com="${repo_root}/media/files/core/ASM.COM"
load_com="${repo_root}/media/files/core/LOAD.COM"
source_asm="${script_dir}/P2EDIT.ASM"
stress_source="${repo_root}/LICENSE"
media_module_dir="${repo_root}/tools/build_media"

find_cli() {
  local candidate
  for candidate in \
    "${build_dir}/p2000c_cli" \
    "${build_dir}/p2000c_cli.exe" \
    "${build_dir}/Release/p2000c_cli.exe"; do
    if [[ -x "${candidate}" ]]; then
      cli=${candidate}
      return 0
    fi
  done
  return 1
}

if [[ -z "${cli}" ]] && ! find_cli; then
  if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
    cmake -S "${repo_root}" -B "${build_dir}" -DP2000C_BUILD_APP=OFF
  fi
  cmake --build "${build_dir}" --target p2000c_cli --config Release
  find_cli || true
fi

for required_file in \
  "${cli}" "${ipl}" "${system_disk}" "${asm_com}" "${load_com}" \
  "${source_asm}" "${stress_source}" "${media_module_dir}/build_media.py"; do
  if [[ ! -f "${required_file}" ]]; then
    printf 'Missing required file: %s\n' "${required_file}" >&2
    exit 1
  fi
done

work_dir="$(mktemp -d)"
trap 'rm -rf -- "${work_dir}"' EXIT
work_disk="${work_dir}/p2edit.flp"
build_log="${work_dir}/p2edit-build.log"
stress_file="${work_dir}/STRESS.TXT"
edit_actions=()
for key in H E L L O ' ' F R O M ' ' P 2 E D I T '\r' \
           S E C O N D ' ' L I N E; do
  if [[ "${key}" == '\r' ]]; then
    # A line break changes the document structure and intentionally redraws.
    edit_actions+=(--send "${key}" --run 3000000)
  else
    edit_actions+=(--send "${key}" --run 1000000)
  fi
done
stress_actions=()
stress_marker=
viewport_marker=
left_marker=
for ((index = 0; index < 96; ++index)); do
  if ((index < 40)); then
    stress_key=A
    stress_marker+=A
    left_marker+=A
  elif ((index < 80)); then
    stress_key=B
    viewport_marker+=B
    if ((index < 64)); then
      stress_marker+=B
    fi
  else
    stress_key=C
    viewport_marker+=C
  fi
  if ((index == 0)); then
    # Opening the gap moves the existing 35 KiB suffix once.
    stress_actions+=(--send "${stress_key}" --run 20000000)
  else
    stress_actions+=(--send "${stress_key}" --run 1000000)
  fi
done
# The CLI keyboard model commits the final queued printable key when the next
# key arrives, so the pre-navigation screen contains the first 95 characters.
viewport_marker=${viewport_marker%?}

cp -- "${stress_source}" "${stress_file}"

PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="${media_module_dir}" python3 -c \
  'from pathlib import Path
import sys
from build_media import build_floppy
build_floppy(Path(sys.argv[1]), [Path(value) for value in sys.argv[2:]], None)' \
  "${work_disk}" "${asm_com}" "${load_com}" "${source_asm}" "${stress_file}"

if ! "${cli}" \
  --ipl "${ipl}" \
  --floppy-a "${system_disk}" \
  --floppy-b "${work_disk}" \
  --write-through \
  --fast-storage \
  --wait-cycles 1000000000 \
  --wait-for 'A>' \
  --send 'B:\rASM P2EDIT\r' \
  --wait-for 'END OF ASSEMBLY' \
  --run 5000000 \
  --send 'LOAD P2EDIT\r' \
  --wait-for 'FIRST ADDRESS' \
  --run 5000000 \
  --send 'P2EDIT B:TEST.TXT\r' \
  --wait-for 'New file' \
  --run 5000000 \
  "${edit_actions[@]}" \
  --send '\x0f' \
  --wait-for 'Wrote B:TEST.TXT' \
  --run 5000000 \
  --send '\x18' \
  --wait-for 'B>' \
  --send 'TYPE TEST.TXT\r' \
  --wait-for 'HELLO FROM P2EDIT' \
  --wait-for 'SECOND LINE' \
  --run 3000000 \
  --send 'P2EDIT B:STRESS.TXT\r' \
  --wait-for 'File read' \
  --run 3000000 \
  --send '\x01' \
  --run 1000000 \
  "${stress_actions[@]}" \
  --wait-for 'Col 96' \
  --wait-for "${viewport_marker}" \
  --send '\x01' \
  --run 5000000 \
  --wait-for "${left_marker}" \
  --send '\x16' \
  --run 5000000 \
  --wait-for 'Ln 20/' \
  --wait-for '^X Exit  ^N New' \
  --send '\x19' \
  --run 5000000 \
  --wait-for 'Ln 1/' \
  --send '\x0f' \
  --wait-for 'Wrote B:STRESS.TXT' \
  --run 5000000 \
  --send '\x18' \
  --wait-for 'B>' \
  --send 'TYPE STRESS.TXT\r' \
  --wait-for "${stress_marker}" \
  --wait-for '<https://www.gnu.org/licenses/why-not-lgpl.html>.' \
  --wait-for 'B>' \
  --send 'ERA STRESS.TXT\r' \
  --wait-for 'B>ERA STRESS.TXT' \
  --run 5000000 \
  --output text >"${build_log}" 2>&1; then
  printf 'P2EDIT compilation or smoke test failed. Emulator output follows:\n' >&2
  sed -n '1,260p' "${build_log}" >&2
  exit 1
fi

mkdir -p -- "$(dirname -- "${output}")"
cp -- "${work_disk}" "${output}"
printf 'Created %s\n' "${output}"
printf 'Files: ASM.COM LOAD.COM P2EDIT.ASM P2EDIT.PRN P2EDIT.HEX P2EDIT.COM TEST.TXT\n'
