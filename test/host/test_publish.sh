#!/usr/bin/env bash
# tools/publish.sh against a fake build, published to a local directory with no server
# listening. Runs on the host with bash, python3 and curl; no ESP-IDF.
#
# The rsync branch is not covered: it needs an ssh target.

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pass=0 fail=0
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check() {
    local desc="$1"; shift
    if "$@"; then
        pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
    else
        fail=$((fail + 1)); printf '  FAIL  %s\n' "$desc"
    fi
}

# A build directory with just what publish.sh reads. Port 9 (discard) refuses at once.
build="$tmp/build"
mkdir -p "$build/config"
printf 'not really firmware' > "$build/demo.bin"
cat > "$build/project_description.json" << 'JSON'
{"project_name": "demo", "project_version": "v1.2.3", "target": "esp32", "app_bin": "demo.bin"}
JSON
cat > "$build/config/sdkconfig.json" << 'JSON'
{"APP_OTA_BASE_URL": "https://127.0.0.1:9/fw", "SECURE_SIGNED_APPS_NO_SECURE_BOOT": true}
JSON

# The shared firmware root: world-writable and sticky, owned by someone else on a real
# NAS, with an old timestamp. Publishing must not touch either.
dest="$tmp/ota"
mkdir "$dest"
chmod 1777 "$dest"
touch -d '2020-01-01 00:00' "$dest"

out="$(NAS_FIRMWARE_DIR="$dest" "$repo_root/tools/publish.sh" "$build" 2>&1)"
rc=$?

echo "publish.sh"
check "an unreachable server is reported as not verified, not as an error" \
    test "$rc" -eq 0
check "the 'could not reach' message names the server" \
    grep -q "could not reach https://127.0.0.1:9/fw" <<< "$out"
check "the image lands at <project>/<chip>/<version>/<project>.bin" \
    test -f "$dest/demo/esp32/v1.2.3/demo.bin"
check "the manifest URL comes from the build's CONFIG_APP_OTA_BASE_URL" \
    grep -q '"url": "https://127.0.0.1:9/fw/demo/esp32/v1.2.3/demo.bin"' "$dest/demo/esp32/manifest.json"
check "the firmware root keeps its mode" \
    test "$(stat -c %a "$dest")" = 1777

# Creating demo/ above legitimately updated the root's mtime. A second publish adds no
# entry to the root, so its timestamp must now stay put: the old script reset it, which
# on a NAS fails when the root belongs to another user.
touch -d '2020-01-01 00:00' "$dest"
NAS_FIRMWARE_DIR="$dest" "$repo_root/tools/publish.sh" "$build" > /dev/null 2>&1
check "republishing leaves the firmware root's timestamp alone" \
    test "$(date -r "$dest" +%Y)" = 2020

echo
echo "$((pass + fail)) checks, $fail failed"
[[ $fail -eq 0 ]] || { echo "$out" | sed 's/^/    | /'; exit 1; }
