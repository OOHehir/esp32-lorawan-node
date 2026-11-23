#!/usr/bin/env bash
# Create the Mosquitto password file the broker in nas/docker-compose.yml mounts.
#
#   ./tools/gen_mqtt_user.sh                 # user "esp-ota", generated password
#   ./tools/gen_mqtt_user.sh alice           # a named user, generated password
#   MQTT_PASS='...' ./tools/gen_mqtt_user.sh # a password you chose
#
# Writes nas/mosquitto/passwd, which is gitignored. Mosquitto stores a salted hash, so
# the file is not the secret -- but the password it prints is, and it goes into firmware
# via CONFIG_APP_MQTT_PASSWORD, so treat it as build-time configuration rather than a
# per-device secret. Every device on this broker shares it.
#
# That is a deliberate limit of this design, not an oversight: what the broker controls is
# "check for an update now", and the update itself is signed and verified independently
# (PLAN §5). If MQTT ever carries something that matters more, move to per-device client
# certificates -- mosquitto.conf has a note on where.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dir="$repo_root/nas/mosquitto"
user="${1:-esp-ota}"

mkdir -p "$dir/data"

pass="${MQTT_PASS:-}"
if [[ -z "$pass" ]]; then
    # 24 chars from a URL-safe alphabet; long enough that the shared-secret caveat above
    # is the weak point rather than the password.
    pass="$(LC_ALL=C tr -dc 'A-Za-z0-9' < /dev/urandom | head -c 24)"
fi

if command -v mosquitto_passwd > /dev/null; then
    rm -f "$dir/passwd"
    mosquitto_passwd -b -c "$dir/passwd" "$user" "$pass"
elif command -v docker > /dev/null; then
    # No local mosquitto tooling: borrow the image the stack already uses.
    docker run --rm -i eclipse-mosquitto:2 \
        sh -c "mosquitto_passwd -b -c /tmp/p '$user' '$pass' && cat /tmp/p" > "$dir/passwd"
else
    echo "error: need either mosquitto_passwd or docker to hash the password" >&2
    echo "       (apt install mosquitto-clients)" >&2
    exit 1
fi
chmod 600 "$dir/passwd"

echo "==> wrote nas/mosquitto/passwd for user '$user'"
echo
echo "    username: $user"
echo "    password: $pass"
echo
echo "Put these in the firmware config:"
echo "    CONFIG_APP_MQTT_USERNAME=\"$user\""
echo "    CONFIG_APP_MQTT_PASSWORD=\"$pass\""
echo
echo "and ship the file to the NAS with the rest of the stack:"
echo "    rsync -a nas/mosquitto.conf nas/mosquitto/passwd admin@nas.lan:/volume1/docker/esp-ota/"
echo
echo "The password is not recoverable from passwd -- it is hashed. Save it now."
