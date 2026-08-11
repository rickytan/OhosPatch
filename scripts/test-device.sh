#!/usr/bin/env bash
set -euo pipefail

deveco_home="${DEVECO_STUDIO_HOME:-/Applications/DevEco-Studio.app/Contents}"
sdk_home="${OHOS_BASE_SDK_HOME:-$HOME/Library/OpenHarmony/Sdk}"
default_hdc="$sdk_home/20/toolchains/hdc"
if [[ ! -x "$default_hdc" ]]; then
  default_hdc="$deveco_home/sdk/default/openharmony/toolchains/hdc"
fi
hdc="${HDC:-$default_hdc}"
hvigorw="${HVIGORW:-$deveco_home/tools/hvigor/bin/hvigorw}"

test -x "$hdc"
test -x "$hvigorw"

hdc_retry() {
  local attempt
  for attempt in 1 2 3; do
    if "$hdc" "$@"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

hdc_retry list targets

"$hvigorw" --mode module -p module=entry clean assembleHap --no-daemon
"$hvigorw" --mode module -p module=entry@ohosTest assembleHap --no-daemon

hdc_retry install -r entry/build/default/outputs/default/entry-default-unsigned.hap
hdc_retry install -r entry/build/default/outputs/ohosTest/entry-ohosTest-unsigned.hap

output_file="$(mktemp "${TMPDIR:-/tmp}/ohospatch-ohostest.XXXXXX")"
trap 'rm -f "$output_file"' EXIT

hdc_retry shell aa test -b com.rickytan.ohospatch -m entry_test -s unittest OpenHarmonyTestRunner -s timeout 60000 > "$output_file"
cat "$output_file"

if ! grep -q 'OHOS_REPORT_CODE: 0' "$output_file"; then
  exit 1
fi
