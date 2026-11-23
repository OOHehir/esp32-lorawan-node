# Set up a board

Take a bare Espressif development board to one that is on your Wi-Fi, serving its status
page & able to update itself. Roughly fifteen minutes, most of it waiting for a build.

**Before you start**

- ESP-IDF v6.0.2 installed & activated (`idf.py --version`)
- The board on USB, & its serial device readable. On this machine it appears as
  `/dev/ttyUSB0`; a board with native USB may appear as `/dev/ttyACM0`
- A phone or laptop with Wi-Fi, for the setup portal
- The hostname or IP address your firmware server will answer on

## 1. Generate the two secrets

```bash
./tools/gen_signing_key.sh
./tools/gen_certs.sh nas.lan 192.168.1.10
```

Where:

- `nas.lan` & `192.168.1.10` are every name & address a device might dial. Both go into
  the certificate's Subject Alternative Name list. A device rejects any name absent from
  it, & the failure looks exactly like a network fault, so list both the name & the address
  if you are unsure which the firmware will use

```
==> generating app_signing_rsa3072.pem  (secure boot v2, RSA-3072)
==> generating CA (3650d)
    server_certs/ca_cert.pem     -> embedded in firmware (NOT in git -- yours alone)
```

> **NOTE:** the signing key is the fleet. Verification without hardware secure boot takes
> its trusted key from the signature block of the application already running, so a lost
> key means no deployed device can ever be updated over the air again — the only way back
> is a cable, per board. Copy 'keys/' somewhere outside this working tree now.

## 2. Point the firmware at your server

Local settings belong in 'sdkconfig.local' (a gitignored file layered over the tracked
defaults), never in 'sdkconfig.defaults', which is tracked & would carry your values into
every copy of the template:

```ini
CONFIG_APP_OTA_BASE_URL="https://nas.lan:8443/fw"
```

Where:

- the URL must use the same name or address that appears in the certificate from step 1
- the path ends at the directory `publish.sh` writes into; the device appends
  `/<project>/<chip>/manifest.json` itself

## 3. Build & flash

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

Where:

- `set-target` accepts any Espressif target with Wi-Fi. Per-chip settings come from
  'sdkconfig.defaults.<target>'; nothing in the firmware names a chip

The build ends with the three configuration guards & the image size:

```
-- check_partitions: partitions_8mb.csv fits 8MB (8192/8192 KB used, app slot 3840 KB)
-- check_signing_key: signing with keys/app_signing_rsa3072.pem
-- check_server_cert: embedding server_certs/ca_cert.pem
esp-ota.bin binary size 0x101000 bytes. Smallest app partition is 0x3c0000 bytes. 0x2bf000 bytes (73%) free.
```

## 4. Commission it over the setup portal

With no stored credentials the device raises its own access point immediately & prints the
passphrase:

```
W (1234) app_net: provisioning AP up: SSID "esp-ota-84a084"  pass "K7MPQ2XR9WTF"  http://192.168.4.1/
```

The SSID ends in the last three bytes of the station MAC address, so a bench of identical
boards stays distinguishable. The passphrase is derived from that MAC & a build-time salt.

Join that network from a phone. The captive-portal sheet should open on its own; if it does
not, browse to `http://192.168.4.1/`. Choose a network, enter its password & **wait for the
page to report the result** — the setup access point stays up afterwards precisely so it
can tell you, & a mistyped passphrase is otherwise a board that silently never appears.

The access point runs alongside the station interface rather than instead of it, so the
device keeps retrying stored credentials underneath. A router that reboots does not strand
it.

## 5. Confirm it is up

```bash
curl http://esp-ota-84a084.local/api/status | python3 -m json.tool
```

Where:

- the hostname is `<project>-<last three MAC bytes>`, advertised over mDNS. Use the IP
  address the portal reported if mDNS does not resolve on your network

```json
{
  "device": {"name": "esp-ota-84a084", "id": "84A084", "chip": "ESP32-S3 rev0.2"},
  "firmware": {"version": "v0.6.1", "partition": "ota_0", "pending_verify": false},
  "net": {"state": "connected", "ip": "192.168.1.117", "time_synced": true}
}
```

`time_synced` must become `true` before an update can work: certificate validity cannot be
checked on a device that believes it is 1970. SNTP has been observed taking anywhere from
7 s to 90 s after the DHCP lease on the same network.

## Commissioning without a portal

On a production line, walking a captive portal per board is absurd. Write the same NVS keys
directly:

```bash
APP_WIFI_PASS='...' ./tools/provision_nvs.sh -p /dev/ttyUSB0 -s 'MySSID'
```

Where:

- `-p` is the serial device; `-s` is the network name
- `APP_WIFI_PASS` is read from the environment so the passphrase stays out of shell history

The firmware cannot tell the difference, & `POST /api/forget` still returns the board to
the portal.

## If something goes wrong

**The build stops with `The OTA server's CA certificate is missing`** — step 1 was skipped,
or run in a different tree. Run `./tools/gen_certs.sh`. To trust the public root
certificate authorities instead of your own, set `CONFIG_APP_TRUST_CERT_BUNDLE=y`, which
embeds nothing & costs 64 KB of flash.

**The build stops with `Signed images are enabled but the signing key is missing`** — run
`./tools/gen_signing_key.sh`.

**The setup network does not appear** — the board already has credentials. `POST
/api/forget` clears them, or hold the factory-reset path in `app_cfg`. A board that has
associated will not raise the access point.

**The captive-portal sheet never opens** — browse to `http://192.168.4.1/` directly. Probe
detection differs between iOS, Android & Windows, & changes between releases; the direct
address always works.

**`state` stays `connecting` & `retries` climbs** — the passphrase is wrong, or the network
is 5 GHz only. Espressif parts here are 2.4 GHz. The backoff runs from about 1 s to 60 s
with jitter, & the setup access point is raised again after 120 s without an association.

## See also

[Publish an update](publish-an-update.md) · [The server side](../nas/README.md)
