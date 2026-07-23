#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
output=${1:-"${repo_root}/images/cpm/p2file.flp"}

build_dir="${repo_root}/build"
cli=${P2000C_CLI:-}
ipl="${repo_root}/tools/ipldump/IPLDUMP.BIN"
system_disk="${repo_root}/images/cpm/system.flp"
asm_com="${repo_root}/media/files/core/ASM.COM"
load_com="${repo_root}/media/files/core/LOAD.COM"
source_asm="${script_dir}/P2FILE.ASM"
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
    cmake -S "${repo_root}" -B "${build_dir}" \
      -DP2000C_BUILD_APP=OFF
  fi
  cmake --build "${build_dir}" --target p2000c_cli --config Release
  find_cli || true
fi

for required_file in \
  "${cli}" "${ipl}" "${system_disk}" "${asm_com}" "${load_com}" \
  "${source_asm}" "${media_module_dir}/build_media.py"; do
  if [[ ! -f "${required_file}" ]]; then
    printf 'Missing required file: %s\n' "${required_file}" >&2
    exit 1
  fi
done

work_dir="$(mktemp -d)"
trap 'rm -rf -- "${work_dir}"' EXIT
work_disk="${work_dir}/p2file.flp"
build_log="${work_dir}/p2file-build.log"

PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="${media_module_dir}" python3 -c \
  'from pathlib import Path
import sys
from build_media import build_floppy
build_floppy(Path(sys.argv[1]), [Path(value) for value in sys.argv[2:]], None)' \
  "${work_disk}" "${asm_com}" "${load_com}" "${source_asm}"

if ! "${cli}" \
  --ipl "${ipl}" \
  --floppy-a "${system_disk}" \
  --floppy-b "${work_disk}" \
  --write-through \
  --wait-cycles 1000000000 \
  --wait-for 'A>' \
  --send 'B:\rASM P2FILE\r' \
  --wait-for 'END OF ASSEMBLY' \
  --send 'LOAD P2FILE\r' \
  --wait-for 'FIRST ADDRESS' \
  --send 'P2FILE\r' \
  --wait-for 'P2FILE: READING DRIVE A:' \
  --wait-for 'P2FILE: READING DRIVE B:' \
  --wait-for 'DRIVE B:    6 FILES' \
  --wait-for 'Q QUIT' \
  --wait-for 'P2FILE  .COM     5K' \
  --send 'Q' \
  --wait-for 'B>' \
  --output text >"${build_log}" 2>&1; then
  printf 'P2FILE compilation failed. Emulator output follows:\n' >&2
  cat "${build_log}" >&2
  exit 1
fi

mkdir -p -- "$(dirname -- "${output}")"
cp -- "${work_disk}" "${output}"
printf 'Created %s\n' "${output}"
printf 'Files: ASM.COM LOAD.COM P2FILE.ASM P2FILE.PRN P2FILE.HEX P2FILE.COM\n'
