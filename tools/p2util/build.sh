#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
output=${1:-"${repo_root}/images/cpm/p2util.flp"}

build_dir="${repo_root}/build"
cli=${P2000C_CLI:-}
ipl="${repo_root}/tools/ipldump/IPLDUMP.BIN"
system_disk="${repo_root}/images/cpm/system.flp"
asm_com="${repo_root}/media/files/core/ASM.COM"
load_com="${repo_root}/media/files/core/LOAD.COM"
media_module_dir="${repo_root}/tools/build_media"
p2file_ref=${P2FILE_REF:-master}
p2edit_ref=${P2EDIT_REF:-master}
p2file_url=${P2FILE_SOURCE_URL:-"https://raw.githubusercontent.com/ifilot/p2000c-file/${p2file_ref}/src/P2FILE.ASM"}
p2edit_url=${P2EDIT_SOURCE_URL:-"https://raw.githubusercontent.com/ifilot/p2000c-editor/${p2edit_ref}/src/P2EDIT.ASM"}

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
  "${media_module_dir}/build_media.py"; do
  if [[ ! -f "${required_file}" ]]; then
    printf 'Missing required file: %s\n' "${required_file}" >&2
    exit 1
  fi
done
if ! command -v curl >/dev/null 2>&1; then
  printf 'Missing required command: curl\n' >&2
  exit 1
fi

work_dir="$(mktemp -d)"
trap 'rm -rf -- "${work_dir}"' EXIT
work_disk="${work_dir}/p2util.flp"
build_log="${work_dir}/p2util-build.log"
p2file_source="${work_dir}/P2FILE.ASM"
p2edit_source="${work_dir}/P2EDIT.ASM"

curl --fail --location --silent --show-error --retry 3 \
  --output "${p2file_source}" "${p2file_url}"
curl --fail --location --silent --show-error --retry 3 \
  --output "${p2edit_source}" "${p2edit_url}"

python3 - "${p2file_source}" "${p2edit_source}" <<'PY'
from pathlib import Path
import sys

for value in sys.argv[1:]:
    path = Path(value)
    data = path.read_bytes()
    body = data[:-1]
    if not data.endswith(b"\x1a"):
        raise SystemExit(f"{path.name}: missing trailing CP/M 1Ah marker")
    if b"\r\n" not in body or body.count(b"\n") != body.count(b"\r\n"):
        raise SystemExit(f"{path.name}: expected CP/M CR/LF line endings")
PY

PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="${media_module_dir}" python3 -c \
  'from pathlib import Path
import sys
from build_media import build_floppy
build_floppy(Path(sys.argv[1]), [Path(value) for value in sys.argv[2:]], None)' \
  "${work_disk}" "${asm_com}" "${load_com}" \
  "${p2file_source}" "${p2edit_source}"

if ! "${cli}" \
  --ipl "${ipl}" \
  --floppy-a "${system_disk}" \
  --floppy-b "${work_disk}" \
  --write-through \
  --fast-storage \
  --wait-cycles 1000000000 \
  --wait-for 'A>' \
  --send 'B:\rASM P2FILE\r' \
  --wait-for 'END OF ASSEMBLY' \
  --run 5000000 \
  --send 'LOAD P2FILE\r' \
  --wait-for 'B>LOAD P2FILE' \
  --wait-for 'FIRST ADDRESS' \
  --run 5000000 \
  --send 'ASM P2EDIT\r' \
  --wait-for 'B>ASM P2EDIT' \
  --wait-for 'END OF ASSEMBLY' \
  --run 20000000 \
  --send '\r' \
  --run 5000000 \
  --send 'LOAD P2EDIT\r' \
  --wait-for 'B>LOAD P2EDIT' \
  --wait-for 'LAST  ADDRESS' \
  --run 5000000 \
  --send 'DIR\r' \
  --wait-for 'B>DIR' \
  --wait-for 'P2FILE   COM' \
  --wait-for 'P2EDIT   COM' \
  --run 5000000 \
  --send 'P2FILE\r' \
  --wait-for 'P2FILE: READING DRIVE A:' \
  --wait-for 'P2FILE: READING DRIVE B:' \
  --run 20000000 \
  --wait-for 'Q QUIT' \
  --send 'Q' \
  --wait-for 'P2FILE finished.' \
  --wait-for 'B>' \
  --send 'P2EDIT\r' \
  --wait-for 'New buffer' \
  --run 5000000 \
  --send '\x1b' \
  --wait-for 'P2EDIT finished.' \
  --wait-for 'B>' \
  --output text >"${build_log}" 2>&1; then
  printf 'P2UTIL build or smoke test failed. Emulator output follows:\n' >&2
  sed -n '1,260p' "${build_log}" >&2
  exit 1
fi

mkdir -p -- "$(dirname -- "${output}")"
cp -- "${work_disk}" "${output}"
printf 'Created %s\n' "${output}"
printf '%s\n' \
  'Files: ASM.COM LOAD.COM P2FILE.ASM P2FILE.PRN P2FILE.HEX P2FILE.COM' \
  '       P2EDIT.ASM P2EDIT.PRN P2EDIT.HEX P2EDIT.COM'
