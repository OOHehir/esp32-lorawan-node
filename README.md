# esp32-lorawan-node

ESP-IDF firmware for a LoRaWAN Class C I/O node: contact inputs report to The Things
Network within seconds, and outputs switch on commands from TTN, typically within 2 s.
Wi-Fi is used only for over-the-air updates, so the node works where Wi-Fi is weak.
Built on esp-ota, an ESP-IDF over-the-air update template, and RadioLib.

Reference board: Heltec WiFi LoRa 32 V2 (ESP32, SX1276, 8 MB flash), EU868.

## Install & first run

```bash
source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh"
./tools/gen_signing_key.sh              # signs every OTA image; back up keys/
./tools/gen_certs.sh <host> <ip>          # CA the device trusts for OTA; see nas/README.md
idf.py set-target esp32
idf.py build flash monitor
```

The LoRaWAN identity and the setup-network passphrase salt go in `sdkconfig.local`,
which git ignores:

```
CONFIG_APP_CFG_AP_PASS_SALT="<random string>"
CONFIG_APP_OTA_BASE_URL="https://<your server>:8443/fw"
CONFIG_APP_LORA_DEV_EUI="<16 hex digits>"
CONFIG_APP_LORA_JOIN_EUI="0000000000000000"
CONFIG_APP_LORA_APP_KEY="<32 hex digits>"
```

Register the device in TTN as LoRaWAN 1.0.4, regional parameters RP002-1.0.3, frequency
plan EU868 (SF9 for RX2), **Class C supported**, and paste [`ttn/formatter.js`](ttn/formatter.js)
as both the uplink and downlink formatter. On first boot the node joins, then sends a
confirmed `boot` status:

```
I (1098) app_lora: joined
I (6326) app_lora: uplink sent: port 1, fcnt 0, confirmed
```

Wi-Fi: join the setup network the node raises, or write credentials by cable with
`APP_WIFI_PASS=... ./tools/provision_nvs.sh -p /dev/ttyUSB0 -s <ssid>`.

## Inputs, outputs & payloads

| Signal | Default GPIO | Notes |
|---|---|---|
| PRG button | 0 | active low |
| Input 1, 2 | 32, 33 | contact to GND, internal pull-up |
| Output 1, 2 | 23, 22 | active high; `menuconfig` → Shed I/O to change or invert |

Uplinks (port 1) decode to `button`, `input1`, `input2`, `output1`, `output2` and `reason`
(`heartbeat` every 15 min, `input`, `output`, `boot`). Downlinks (port 2) take
`{"output": 1, "action": "on" | "off" | "toggle" | "pulse", "seconds": 5}`. Byte layout:
[`main/shed_proto.h`](main/shed_proto.h).

With a TTN → Home Assistant MQTT bridge that maps each field to an entity,
an automation switches an output with:

```yaml
action: mqtt.publish
data:
  topic: ttn/<device-id>/down
  payload: '{"decoded_payload": {"output": 1, "action": "pulse", "seconds": 3}}'
```

## What it does not do

- **No more than about 10 commands a day.** TTN's fair-use policy allows about ten
  downlinks per device per day, acknowledgements included; only the boot status is
  confirmed for that reason
- **No back-to-back events.** TTN's 5 s RX1 delay keeps the radio busy about 6 s per
  uplink; quick input changes queue and arrive that far apart
- **No battery operation.** The node is Class C and listens continuously; the V2 board
  also draws milliamps in deep sleep
- **No regions other than EU868** without changing `app_lora`

## Updates & state

OTA works as in esp-ota: tag, build, `NAS_HOST=... ./tools/publish.sh`, see
[Publish an update](docs/publish-an-update.md). A new image stays on probation until it
reaches the update server **and** has a LoRaWAN session, within 30 minutes.

The LoRaWAN session and DevNonce counter live in the `persist` partition, which Wi-Fi
provisioning and factory reset leave alone. Losing the counter makes TTN reject every
join as a replay.

## Tests

```bash
./tools/run_tests.sh      # host tests: C units, formatter contract, partition layout
```

The radio, GPIO and OTA paths need the board; they were exercised by hand on the
reference board, not by these tests.

## Documentation

[Set up a board](docs/setup.md) · [Publish an update](docs/publish-an-update.md) ·
[Diagnose a crash](docs/diagnose-a-crash.md) · [Design & test record of the template](PLAN.md)

## Licence

Apache-2.0, see [LICENSE](LICENSE). RadioLib, fetched at build time, is MIT.
