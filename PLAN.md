# esp-ota — plan for a reusable ESP-IDF project template with OTA

Status: phases 0–7 done and verified on hardware, bar the optional BLE transport. The
whole chain runs: tag, build, sign, publish to the NAS, verify the publish, and a device
fetches it over TLS, checks four independent things about it, boots into it on probation,
and **puts the old one back if the new one cannot prove itself** — proven by publishing
builds that were broken on purpose, and by forking the template into a second project.
See §8.
Goal: a repo that can be forked/copied as the starting point for any future Espressif
project, with a working, safe, no-cloud OTA path served from a Synology DS225+.

---

## 1. Decisions to lock before writing code

| Decision | Recommendation | Why |
|---|---|---|
| ESP-IDF version | Pin **v6.0.x** (latest stable; v6.0.2 as of Jul 2026) | v5.5 went to maintenance Jul 2026. v6.0 has breaking changes vs 5.x, so pin now rather than migrate later. Record the exact tag in `.idf-version` + CI. Installed with **EIM** (Espressif's IDF Installation Manager), which keeps versions side by side — so `.idf-version` must be kept honest against whatever EIM actually activated. |
| Target chip | **Chip-agnostic.** No reference chip; target is chosen at build time via `idf.py set-target` | Decided. See §1a — this is a hard constraint on the whole template, not a default. |
| Transport | `esp_https_ota` (esp-http-client + mbedTLS) over TLS to a static file server | This *is* the native Espressif method. The server side is nothing special — the IDF examples just run `openssl s_server -WWW` as a dev toy. |
| Server | **nginx in Container Manager on the DS225+** | See §2. |
| Server trust | **Own CA, embedded in firmware** for now; Kconfig switch for the IDF cert bundle later | See §3. |
| Cloud | **None initially.** Re-evaluate RainMaker Neo when a project actually needs a phone app | See §5. |
| Device UI | **A small `esp_http_server` status page on the device**, plain HTTP, LAN-only | Answers "what is this board running?" from a phone, and gives OTA a manual trigger with no serial cable attached. See §4a. |
| Provisioning | **SoftAP + browser captive portal**, entered when unprovisioned or after 120 s without association | Any phone can commission a board with no app installed, and the same path recovers a board that has lost its network. BLE optional behind Kconfig. See §4b. |

### 1a. Chip-agnosticism as a constraint

The template must build for any Espressif target with the chip named only in build
configuration. That is easy to state and easy to violate, so the rules are explicit:

- **No `CONFIG_IDF_TARGET` checks in application code.** Gate on capability, not on
  chip: `#if SOC_BLE_SUPPORTED` from `soc_caps.h`, never `#ifdef CONFIG_IDF_TARGET_ESP32S3`.
- **Per-target configuration lives in `sdkconfig.defaults.<target>`**, which ESP-IDF
  loads automatically on top of `sdkconfig.defaults`. Chip-specific settings go there
  and nowhere else. `sdkconfig` itself stays out of git.
- **Provisioning is SoftAP-first, because every Wi-Fi part can raise an AP.** The fallback
  AP in §4b is universal and unconditional. BLE provisioning is an optional *additional*
  transport where `SOC_BLE_SUPPORTED`, never the only way onto a network — a capability-
  chosen transport would leave ESP32-S2 boards with a different recovery story from their
  S3 siblings, which is exactly the per-chip divergence this section exists to prevent.
  Same API to the application either way.
- **Partition tables are selected by flash size, not by chip.** Ship `partitions_4mb.csv`
  and `partitions_8mb.csv`; a 4 MB build gets ~1.9 MB per app slot, which is workable
  but tight once TLS and provisioning are in. Fail the build loudly if the selected
  table does not fit the configured flash size rather than at first OTA.
- **The publish tooling reads the target from the build output** rather than being told
  — `tools/publish.sh` already does this, and the server stores images per
  `<project>/<chip>`, so one NAS feeds a mixed fleet.
- **CI builds a matrix of targets** (at minimum one Xtensa and one RISC-V, e.g. esp32s3
  and esp32c6) so a chip-specific assumption breaks the build the day it lands, not
  months later.
- **The status LED is a per-target detail, not application code.** Devkits disagree: a
  plain GPIO on some boards, an addressable WS2812 on many ESP32-S3 and C6 kits, and the
  pin number moves between boards carrying the same chip. Keep the pin and the LED type in
  `sdkconfig.defaults.<target>` behind one `app_led` API, gate the addressable path on
  `SOC_RMT_SUPPORTED`, and let the template build cleanly with no LED configured at all.

The cost is that some conveniences cannot be assumed: no USB-Serial-JTAG on classic
ESP32, no BLE on ESP32-S2, differing RAM budgets for TLS. Where a capability is genuinely
required, the template should refuse to build with a clear message, not fail at runtime.

---

## 2. HTTPS server on the DS225+

The DS225+ is a Celeron J4125 (x86-64, 4 cores) with 2 GB RAM, so it runs Container
Manager comfortably; nginx:alpine is ~10 MB resident.

### Recommended: Container Manager (Docker) + nginx

```
/volume1/docker/esp-ota/
├── docker-compose.yml
├── nginx.conf
├── certs/            # server.crt, server.key  (0400, owned by the container user)
└── firmware/
    ├── manifest.json
    └── myproject/1.2.3/myproject.bin
```

Why this over Web Station:
- Config is a file in this git repo, not clicks in DSM that a DSM upgrade can undo.
- Own port (e.g. 8443) — no fighting DSM's 80/443/5000/5001.
- Full control of TLS version/ciphers, which matters because mbedTLS on the device is
  fussier than a browser.
- Trivial to add a second container later (Mosquitto — see §7).

Checklist:
- Package Center → **Container Manager** → Install. The **Project** wizard then rejects
  this compose file as invalid and the file is not the reason — bring the stack up with
  `sudo docker-compose up -d` over ssh instead. `nas/README.md` has the evidence.
- Bind port 8443, mount `firmware/` read-only, `restart: unless-stopped`.
- The DSM firewall is off on this NAS; the router is what keeps 8443 off the internet.
- DHCP reservation for the NAS + a stable hostname; the cert SAN must match exactly
  what the device dials (put **both** the IP and the DNS name in the SAN list).

### Alternative: Web Station (no Docker)

Perfectly workable if you'd rather stay in DSM: install **Web Station**, create a Web
Service pointing at a shared folder, create a Web Portal on a chosen port with HTTPS,
then assign a certificate in Control Panel → Security → Certificate. Two caveats: the
`http` group needs read permission on the folder, and the config lives in DSM rather
than in version control.

### Not recommended
- Serving straight out of File Station / DSM's own 5001 port — mixes firmware hosting
  with your NAS admin surface.
- `openssl s_server -WWW` for anything but a five-minute smoke test: single-connection,
  no range requests, and known to corrupt binaries on some platforms.

---

## 3. Certificates — the actual design fork

The device must validate the NAS. Two viable strategies:

**(a) Private CA, pinned in firmware** — *recommended to start.*
- `tools/gen_certs.sh` makes a 10-year CA, then issues a server cert (with IP + DNS SANs)
  signed by it.
- Embed **the CA cert**, not the leaf, via `EMBED_TXTFILES` — then you can re-issue the
  server cert without reflashing.
- Import the server cert+key into the nginx container (and optionally into DSM's
  certificate store if you also use Web Station).
- Works fully offline. Downside: CA expiry eventually requires an OTA to rotate.

**(b) Let's Encrypt via DSM + `esp_crt_bundle_attach()`**
- DSM can issue and auto-renew Let's Encrypt certs if you use a Synology DDNS name
  (`something.synology.me`) or DNS-01.
- No pinning, rotation is automatic, but you need a real resolvable name (split-horizon
  DNS if you want it to stay LAN-only), and the bundle costs ~64 KB of flash.

Either way: **SNTP must sync before the first TLS handshake**, or cert validity checks
fail on a device that boots at 1970. Never ship `ESP_TLS_INSECURE`.

Both are implemented, as `CONFIG_APP_TRUST_PRIVATE_CA` (default) and
`CONFIG_APP_TRUST_CERT_BUNDLE`. One helper in `app_ota` fills in whichever anchor is
selected, so there is a single answer to "what does this device trust?" and no second call
site can quietly differ. The bundle costs **exactly 64 KB** — measured, not estimated —
and in that mode nothing embeds `ca_cert.pem` at all, so a tree without one still builds.

Verified on hardware: a bundle build pointed at a public host completed the handshake and
came back with an HTTP status (`manifest HTTP 404`), which a failed handshake would not
have done. Trusting a private CA and trusting the public roots are now both real options,
not one option and a paragraph.

---

## 4. Firmware architecture

```
esp-ota/
├── CMakeLists.txt              # PROJECT_VER from `git describe --tags --dirty`
├── sdkconfig.defaults[.esp32s3]
├── partitions.csv              # nvs, otadata, phy, factory/ota_0, ota_1, storage
├── main/                       # project-specific code ONLY
│   ├── main.c
│   └── Kconfig.projbuild
├── components/                 # the reusable part — this is the actual deliverable
│   ├── app_net/                # provisioning, connect w/ backoff, SNTP, mDNS
│   ├── app_ota/                # manifest fetch, version compare, esp_https_ota, rollback
│   ├── app_cfg/                # NVS-backed config + factory reset
│   ├── app_diag/               # log routing, core dump, boot-reason counters
│   ├── app_led/                # one LED owner: status patterns + manual override
│   └── app_httpd/              # device status page + control endpoints (§4a)
├── server_certs/               # CA cert (public); keys gitignored
├── nas/                        # docker-compose.yml, nginx.conf, DS225+ setup notes
├── tools/                      # gen_certs.sh, publish.sh, bump_version.sh
└── docs/
```

Key firmware decisions:

- **Manifest-driven updates.** `manifest.json` on the NAS:
  ```json
  { "project": "myproject", "chip": "esp32s3", "version": "1.2.3",
    "url": "https://nas.lan:8443/fw/myproject/1.2.3/myproject.bin",
    "sha256": "...", "min_version": "1.0.0", "mandatory": false }
  ```
  Device compares against `esp_app_get_description()->version` (populated from
  `PROJECT_VER`, i.e. git tags) before downloading a single byte of image.

- **Advanced OTA API**, not the one-shot call: `esp_https_ota_begin()` →
  read image header → sanity-check project name / chip / version →
  `esp_https_ota_perform()` loop with progress callback → abort cleanly on mismatch.

- **Rollback is the whole point.** `BOOTLOADER_APP_ROLLBACK_ENABLE` is on; the new image
  boots in `PENDING_VERIFY` and only calls `esp_ota_mark_app_valid_cancel_rollback()`
  after a self-test passes. Otherwise the bootloader reverts. Without this, one bad build
  bricks a remote device.

  The self-test is three checks, and only one of them is really the point:
  **minimum uptime** (120 s, so a crash during start-up is not confirmed), **network
  up**, and **the update server reachable over TLS**. That last one is the criterion that
  matters — confirming an image is a promise that a fix can still reach the device, and
  fetching the manifest is the only check that tests the promise directly. The fourth
  criterion originally listed here, *no panic*, needs no code: an image that panics or
  trips a watchdog while in `PENDING_VERIFY` is reverted by the bootloader on the way
  back up. That is what the state is for.

  Note what the server probe implies, because it is a deliberate trade: an image that
  boots perfectly but has broken Wi-Fi, the clock, or the embedded CA is rolled back —
  correctly, since a device that cannot be updated needs a cable. The cost is that a
  *good* image also rolls back on a device whose network is genuinely gone. It will be
  offered again once the network is back, so the failure mode is a delay rather than a
  loss. `CONFIG_APP_OTA_SELFTEST_REQUIRE_SERVER=n` trades that away.

- **Signed images without full secure boot**
  (`SECURE_SIGNED_APPS_NO_SECURE_BOOT` + `SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`): every
  OTA image has its signature verified before it is accepted. Cheap, reversible, catches
  a compromised file server. **ON in the template by default.**

  Two consequences that are not obvious and are not optional:

  - Without hardware secure boot, the trusted key is read from the signature block of the
    **app already running**. So the first signed image has to arrive over serial — a
    device running an unsigned image refuses every signed update with *"No signatures
    were found for the running app"* — and **losing the signing key means no deployed
    device can ever be updated again**. Key rotation needs a bridge build carrying both
    signatures. `keys/README.md` says all of this where somebody will read it.
  - The signing *scheme* follows the chip, so the key does too: RSA-3072 (secure boot v2)
    everywhere except a classic ESP32 below revision v3.0, which has only the v1 ECDSA
    scheme. That is the one place a chip difference reaches something a human has to
    manage by hand. It is handled the §1a way — `CONFIG_SECURE_BOOT_SIGNING_KEY` is a
    per-target setting in `sdkconfig.defaults.<target>`, and `tools/gen_signing_key.sh`
    makes both keys — but it is worth knowing it exists.

- **The manifest sha256 is checked against the slot, not the stream.** After the last byte
  is written and *before* `esp_https_ota_finish()` marks the slot bootable, the passive
  partition is read back and hashed. A mismatch is then just an abort; checked after
  `finish()` it would need undoing. Reading flash rather than hashing the download also
  tests the bytes that will actually be executed — and `esp_https_ota` offers no hook to
  see the stream anyway (`ESP_HTTPS_OTA_WRITE_FLASH` carries a length, not the data).

  This is integrity, not authenticity: the hash and the image come from the same server.
  It catches a truncated upload, not a hostile one. Signing is the independent check.

- **Secure boot v2 / flash encryption / anti-rollback**: documented, Kconfig-gated,
  **default OFF**. These burn eFuses irreversibly — the template should make it a
  deliberate, well-documented production step, never something you enable by accident.

- **Triggers**: jittered periodic poll (default 6 h) + manual trigger (button / console
  command / later MQTT), so you're not waiting on a timer during development.

- **Resilience**: exponential backoff, resume-on-reboot, don't retry a version that has
  already failed twice (store the failed version in NVS).

  Implemented as one NVS record — the version, and how many times it has failed — plus
  `ota_try`, the version written to the passive slot, stored *before* the restart. That
  ordering is the whole trick: an image that panics during its first boot, or a power cut
  midway through it, still leaves something the next boot can attribute the failure to.
  Two attempts and the periodic poll stops offering that version; **pressing "Check for
  updates" retries it anyway**, because a version broken by a bad *upload* rather than a
  bad build must not be a dead end reachable only by factory reset. Transport failures
  deliberately do not count — flaky Wi-Fi must not quarantine a good version.

### 4a. On-device web UI

Every board serves a small status-and-control page on port 80 from
`components/app_httpd/`, built on `esp_http_server`. It exists so that *what is this board
running, and is there an update?* is answerable from a phone on the same Wi-Fi, with no
serial cable and no laptop toolchain — which is the situation you are actually in once a
device is mounted somewhere awkward.

One page, four things:

| Element | Shows / does |
|---|---|
| Firmware panel | `PROJECT_VER`, chip, running partition (`ota_0`/`ota_1`), build time, uptime, IP |
| **Check for updates** | Runs the same manifest fetch and version compare as the periodic poll, then reports: already current, a version available, or download progress |
| **Restart** | `esp_restart()`, behind a confirm step |
| **Toggle LED** | Flips the status LED and reports the state it landed in |

Design notes:

- **The page is one embedded file.** A self-contained `index.html` via `EMBED_FILES` — no
  CDN, no framework, no build step. These devices frequently sit on a network with no
  route to the internet, so anything fetched from outside renders as a broken page.
- **HTML is static; state arrives as JSON.** `GET /api/status` returns the firmware panel
  and any in-flight OTA state; buttons `POST` to `/api/ota/check`, `/api/restart` and
  `/api/led`. That keeps the embedded asset small and lets `curl` drive exactly the same
  surface as the browser, which matters on the bench and in CI.
- **Buttons return immediately.** *Check for updates* queues work on the OTA task and
  returns; the page polls `/api/status` for progress. Never run the download inside the
  HTTP handler — it blocks the server for minutes and times out the browser.
- **Refuse a restart mid-update.** `/api/restart` returns 409 while an image is being
  written, so a stray click cannot abandon a half-written slot.
- **Surface the rollback state.** When the running image is in `PENDING_VERIFY`, say so on
  the page. This is the one place a human can watch the self-test decide, and it turns the
  most confusing failure mode in §4 into something visible.

Deliberately out of scope for v1: **no authentication and no TLS on the device.** It is a
plain HTTP control surface, and anyone who can reach the board can reboot it. That is a
reasonable trade on a lab network and a poor one anywhere else, so the whole component sits
behind a Kconfig option that can be compiled out, with room for a shared-secret header
later. Serving TLS *from* the device would also mean a second certificate to rotate and
RAM that overlaps badly with the OTA download — see §7.

### 4b. Commissioning and the SoftAP fallback

A board that cannot reach Wi-Fi is useless and, worse, silent about it. Getting onto a
network is a first-class state machine in `app_net`, not something bolted on at the end:

| State | Trigger | Behaviour |
|---|---|---|
| Unprovisioned | No credentials in NVS | Raise the AP immediately — there is nothing to wait for |
| Connecting | Credentials present | Normal STA connect with backoff |
| Fallback | No association **120 s** after boot (Kconfig, default tunable) | Raise the AP *without giving up on STA* |
| Connected | Association + DHCP lease | Tear the AP down, carry on |

**The AP runs in `WIFI_MODE_APSTA` and STA retries continue underneath it.** This is the
detail that matters most. A device that flips to AP mode and stops trying gets stranded by
a router reboot, a power cut that restores the NAS before the access point, or a brief ISP
blip — and stranded means somebody walks to it. Keep retrying the stored credentials on a
backoff; the moment STA associates, drop the AP.

**The portal is the §4a page in a different mode**, not a second web stack — same
component, same embedded-asset approach, one extra view:

- SSID `<project>-<last 3 MAC bytes>` (e.g. `esp-ota-A1B2C3`), so a bench full of boards
  stays distinguishable.
- **WPA2, not open.** An open AP lets anyone in range hand the device credentials or point
  it at their own update server. Derive the passphrase per device, and put it in the boot
  log and on the label.
- The page **lists scanned SSIDs** rather than asking the user to type one, with a manual
  box for hidden networks.
- On submit: store to NVS, attempt the connect, and **report the result on the page before
  tearing the AP down.** Dropping the AP the instant credentials are saved is the classic
  bug — a mistyped passphrase becomes a board that silently never appears, with no way for
  the user to tell whether it worked.
- Captive-portal detection needs a DNS responder answering every query with the AP address,
  plus the OS probe URLs (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`) returning a
  redirect. Right, and the login sheet pops by itself; wrong, and the user has to somehow
  know to browse to 192.168.4.1.
- A distinct `app_led` pattern for fallback mode, so "it never joined" is readable across a
  room without a serial cable.

Factory reset (`app_cfg`) clears credentials and returns the device to Unprovisioned, which
makes this the recovery path for a mis-provisioned board as well as the first-run flow.

**Why not IDF's `wifi_provisioning` manager with `scheme_softap`:** it is protobuf-over-HTTP
aimed at the ESP SoftAP Prov phone app, and the requirement here is that any phone with a
browser can commission a board with nothing installed. The cost of going our own way is
that we do not inherit its SRP6a proof-of-possession handshake — which is precisely why the
AP is WPA2-protected rather than open. BLE provisioning stays available behind Kconfig for
projects that do want the app flow.

---

## 5. ESP RainMaker Neo — assessment

Worth knowing what it actually is: Espressif open-sourced the *entire* RainMaker stack
under Apache 2.0 in 2026 — cloud backend (AWS CDK, serverless), firmware SDK
(`espressif/esp-rainmaker-neo-firmware`), iOS/Android apps, admin dashboard, Alexa and
Google Home integrations, Matter support. You deploy it into **your own AWS account**;
there's also a public hosted deployment for evaluation, and a build-from-source path.

**But: Neo does not run on your Synology.** "Self-hosted" here means your AWS account,
not your NAS — it's built on AWS IoT Core (Device Shadow, Thing Groups, AWS IoT OTA),
Lambda, DynamoDB, Cognito. So it doesn't complement the NAS plan, it replaces it.

Trade-offs:

| | NAS OTA template | RainMaker Neo |
|---|---|---|
| Gets you | Firmware updates, LAN-only, zero recurring cost | Phone app, user accounts, telemetry, schedules, automations, voice assistants, fleet OTA |
| Cost | £0 | AWS pay-as-you-go — small for a handful of devices but nonzero and ongoing |
| Coupling | Your own thin components | Firmware structured around the rmaker device/param model, claiming, and provisioning flow |
| Offline / lab use | Works with the internet down | Needs the cloud |
| Effort to first blink | Hours | Days, plus AWS account hygiene |

Also note: Classic and Neo are **not** migration-compatible; Neo is aimed at new
development.

**Recommendation:** build the lean template first — it's the smaller, more general base,
and you need a local HTTPS OTA path anyway for bench work and production line flashing
even if you later adopt a cloud. Keep the door open by hiding provisioning, connectivity
and OTA behind narrow internal interfaces in `components/`, so a Neo-backed
implementation can be swapped in per-project without touching application code.
Revisit Neo the first time a project genuinely needs an app + user accounts.

---

## 6. Phases

0. **Decide** ✅ — chips, flash size, IDF tag pinned, toolchain installed via EIM, repo
   initialised as git. Flash size answered: `esptool flash-id` reports the bench board is
   an **ESP32-S3-WROOM-1-N8R8** — 8 MB flash, 8 MB embedded PSRAM, QFN56 rev v0.2, MAC
   `8c:bf:ea:84:a0:84`. So `sdkconfig.defaults.esp32s3` selects `partitions_8mb.csv`
   (~3.75 MB per app slot). PSRAM is left disabled: nothing needs it, and enabling it
   would make the template's memory behaviour depend on which module you happened to buy.
1. **Skeleton** ✅ — project builds for esp32s3/esp32c6/esp32; partition tables selected by
   flash size with a configure-time fit check; version from `git describe`.
2. **Connectivity + device UI** ✅ (verified on hardware) — Wi-Fi connect with
   retry/backoff, SNTP, mDNS. `app_led` with per-target pin and type, and the `app_httpd`
   page serving the firmware panel, **Restart** and **Toggle LED**. Commissioning ships
   with it — the §4b fallback AP and portal — because without it a board that cannot
   associate has no way back. The **Check for updates** button is present and returns 501
   until phase 4 registers the OTA hooks.
3. **NAS host** ✅ *(verified as a real origin)* — `gen_certs.sh`, nginx compose stack on
   the DS225+, and a device that pulled a full image from it over TLS with the bench rig
   stopped. The first ever run of `publish.sh`'s rsync leg broke the server outright; see
   §8, because the fix and the reason nothing had caught it are both worth knowing.
4. **OTA v1** ✅ — manual trigger, embedded CA, `esp_https_ota`, device pulls and reboots
   into a new build. **Check for updates** is wired to the same path. *This was the
   milestone that proves the whole idea, and it does.*
5. **Make it safe** ✅ *(verified on hardware)* — rollback with a self-test, image
   signing, the manifest sha256 check against the written slot, and a failed-version
   memory that stops the poll re-installing a broken build. Proven by publishing images
   broken on purpose and watching the board put itself back. The header pre-check landed
   in phase 4.
6. **Automate** ✅ — `publish.sh` does build → sign → sha256 →
   manifest → copy and has now been run dozens of times against `test/rig/`; manifest
   polling is jittered and on by default; progress reporting is the status page and
   `/api/status`. `tools/bump_version.sh` tags a release and refuses on a dirty tree,
   because an image built from uncommitted changes is versioned `<tag>-dirty` and devices
   would never then recognise the real tag as an update. The rsync leg has now run, and
   `publish.sh` verifies its own publish rather than assuming a successful copy is a
   successful release.
7. **Make it a template** — *reuse proved; optional BLE transport still open.* A second
   project ("doorbell") was copied out of this tree, given the three edits the README
   names, its own CA and signing keys, and built: distinct `project_name` in the image
   header, distinct `<project>/<chip>/` path on the server, own trust anchor. NVS config,
   factory reset and the README shipped earlier. What the exercise found is in §8.
   **No `new_project.sh`**: decided against on 2026-08-26. A rename script has to
   guess which occurrences of the project name are identity and which are content, and
   the answer differs per project; copying the tree and editing is a two-minute job that
   is always right. The names worth finding are in `CMakeLists.txt` (`project()`),
   `main/Kconfig.projbuild` (`APP_PROJECT_NAME_SLUG`) and `app_cfg`'s salt.
8. **Optional extras** ✅ *(verified on hardware)* — crash dumps and the MQTT broker link.
   A panic writes a core dump to its own flash partition, survives the reboot **and an
   update**, is reported on the boot banner and the status page, and can be pulled off the
   device and decoded to a source line. Local, never uploaded: ESP Insights is a cloud
   service and would undo §5's premise. And a Mosquitto container in the NAS stack with
   `app_mqtt` against it: retained status, a last will, and the fleet-wide "check for an
   update now" that was the whole reason for wanting a broker.

---

## 7. Risks & known gotchas

- **Clock skew breaks TLS.** SNTP before first HTTPS call, and handle the no-DNS case.
  **SNTP timing varies far more than is comfortable**: 7 s after the DHCP lease on one
  boot and about 90 s on the next, on the same network, depending on whether the
  DHCP-supplied server answers before the configured fallback. Anything gated on a valid
  clock must therefore *wait* for it rather than test once and fail — `app_ota` waits up
  to `CONFIG_APP_OTA_TIME_SYNC_WAIT_S` (45 s). Failing immediately is accurate and
  useless: it fixes itself a minute later, and in the meantime it teaches people the
  button does not work.
- **Cert CN/SAN mismatch** if the device dials by IP but the cert only has a DNS name.
  Put both in the SAN.
- **DSM port collisions** — 80/443/5000/5001 are DSM's. Use a dedicated high port.
- **DSM updates** restart containers and can reset Web Station config; `restart:
  unless-stopped` and keep config in git.
- **4 MB flash** is tight for TLS + provisioning + two app slots. Not fatal, but check the
  partition table fits at configure time, on every target.
- **eFuse operations are permanent.** Secure boot / flash encryption / anti-rollback stay
  default-off with loud documentation.
- **mbedTLS buffer sizes** — the default RX buffer may need raising for larger chains;
  symptom is a handshake failure that looks like a network problem.
- **Don't pin the leaf certificate** — pin your CA, or you'll need an OTA to rotate a cert.
- **A device stranded in AP mode.** The failure is not raising the AP, it is never leaving
  it: if STA retries stop once the portal is up, a router that rebooted turns every board
  into a site visit. `APSTA` plus continued backoff retries, tested by power-cycling the
  access point rather than the device.
- **Captive-portal detection is OS-specific and fragile.** iOS, Android and Windows each
  probe differently and change it between releases. Test on all three, and make sure
  browsing to the AP address directly always works as the fallback-to-the-fallback.
- **An open provisioning AP is a device takeover.** Anyone in range can supply credentials
  and a server address. WPA2 with a per-device passphrase, or the recovery path becomes the
  attack path.
- **The device's web UI is an unauthenticated control surface.** Anyone on the LAN can
  reboot a board or make it check for an update. Acceptable on a lab network, not on a
  shared one — Kconfig-gate the component and disable it, or add a shared secret, before
  anything ships.
- **`esp_http_server` and a TLS download compete for RAM.** Per-connection server buffers
  sit alongside mbedTLS's during an update, and on a 4 MB / low-RAM part that is precisely
  when both are largest. Keep `max_open_sockets` low and test the update path *with the
  page open in a browser* — that is the case that actually bites.
- **Two owners for one LED.** Connectivity status wants to blink it; the toggle button
  wants to hold it. Give `app_led` a single owner and an explicit manual-override state, or
  the button will look broken while the status pattern keeps overwriting it.
- **The signing key is the fleet.** Without hardware secure boot the trusted key comes
  from the signature block of the app *already running*, so losing the key means no
  deployed device can ever be updated again — the only way back is a cable, per device.
  Back it up off the working tree. Rotating it needs a bridge image carrying both
  signatures, shipped before the new key is used alone.
- **A device running unsigned firmware cannot OTA its way into signing.** It has no
  trusted key, so it refuses every signed image with *"No signatures were found for the
  running app"*. The first signed image goes over serial. This applies to the bench board
  right now, which is running a phase-4 build.
- **A self-test that requires the server will roll back a good image on a broken
  network.** That is the intended trade — a device that cannot be updated needs a cable,
  so reverting to a known-updatable image is right — but it means a fleet whose access
  point is down long enough will revert. The image is offered again once the network is
  back, so it costs a delay rather than a version.
- **Confirming an image too eagerly throws the safety net away.** Anything that calls
  `esp_ota_mark_app_valid_cancel_rollback()` early — a library, a well-meaning `app_main`
  — cancels the rollback before the self-test has an opinion. `app_ota` is the only caller
  in this template, and it should stay that way.
- **A quarantine keyed on a version string assumes versions do not get reused.** They come
  from git tags here, so they do not. Republish a *fixed* image under a version that has
  already failed twice and the poll will still skip it — press "Check for updates" once,
  which deliberately overrides the quarantine, or factory-reset the device.

### 7a. Toolchain and ESP-IDF v6 traps that actually bit

Found while building phases 1–2, all of them silent-until-confusing:

- **`REQUIRES` cannot depend on `CONFIG_*`.** ESP-IDF evaluates every component
  `CMakeLists.txt` **once for the dependency graph before Kconfig has been read**. A
  CONFIG-guarded `PRIV_REQUIRES` resolves to nothing there, the include paths vanish, and
  the error you get is a missing header — not a missing dependency. `SRCS` may be
  CONFIG-conditional; requirements may not.
- **mbedTLS 4 in v6.0.** `mbedtls/sha256.h` and the whole `mbedtls_sha256_*` API are gone.
  Hashing goes through PSA: `psa/crypto.h`, `psa_crypto_init()`, `psa_hash_compute()`.
- **cJSON left IDF core.** The bundled `json` component no longer exists; depend on
  `espressif/cjson` from the registry.
- **EIM defines `idf.py` as a shell function**, not an executable on `PATH`. Any script
  that calls `idf.py` gets "command not found" inside a fully activated environment.
  Invoke `python "$IDF_PATH/tools/idf.py"` instead — this will matter for CI and for
  `publish.sh`.
- **`CONFIG_LWIP_SNTP_MAX_SERVERS` defaults to 1.** The DHCP-plus-fallback SNTP pattern
  sets `index_of_first_server = 1`, which writes one past the end of a one-element array.
  Raise it to 2 whenever `server_from_dhcp` is used.
- **GCC 15 rejects `char x[N] = "N-char-string"`.** `-Werror=unterminated-string-initialization`
  is on in IDF's default flags.
- **Signed images are not chip-agnostic.** `SECURE_SIGNED_APPS_NO_SECURE_BOOT` picks the
  signing scheme from the chip: RSA-3072 (secure boot v2) wherever `SOC_SECURE_BOOT_V2_RSA`
  holds, but a classic ESP32 defaults to `ESP32_REV_MIN_0`, which has no v2 support, so the
  choice silently falls through to v1 ECDSA — a different *kind* of key file at a different
  path. Nothing warns about this; the build just wants a key it cannot find. It is a
  per-target `sdkconfig.defaults.<target>` setting like any other, but it is the only one
  that produces a second artefact a human has to keep safe.
- **Secure boot v2 pads the image to the next 64 KiB boundary** before appending its 4 KiB
  signature sector, so the cost of turning signing on is lumpy rather than linear: here it
  took a 984.7 KiB image to 1028 KiB, of which ~39 KiB is padding. A build that sits just
  over a boundary pays for the whole next block.
- **An obsolete `CONFIG_` line is a NOTE, not an error.** `ESP_COREDUMP_DATA_FORMAT_ELF`
  and `ESP_COREDUMP_CHECKSUM_SHA256` were set in `sdkconfig.defaults` for a whole phase
  and do not exist in v6.0 -- ELF and SHA-256 are simply what espcoredump does now. The
  build said so every time, in a line that scrolls past above the part anyone reads:

      NOTE: unknown kconfig symbol 'ESP_COREDUMP_DATA_FORMAT_ELF' assigned to 'y'

  Harmless here, since the behaviour was the default anyway. The problem is that inert
  configuration reads as a decision, and the next person to touch it has no way to tell.
  `build_matrix.sh` now prints any such symbols per target, which is the cheapest place to
  notice them. Same family as the other v5→v6 moves: things do not error, they quietly
  stop existing.
- **`ESP_ERROR_CHECK` in start-up is a boot loop, and a boot loop is a brick.** Pointing
  the template at an existing plaintext broker made `esp_mqtt_client_init()` return NULL
  -- esp-mqtt refuses a config carrying TLS verification settings for an `mqtt://` scheme,
  which `app_mqtt` was setting unconditionally. `app_core` had that init wrapped in
  `ESP_ERROR_CHECK`, so the device aborted, rebooted, aborted, roughly every four seconds:
  unreachable over the network and therefore unfixable over the air, because of an
  **optional** component and a **configuration** mistake.

  Two fixes, and the second is the general one. `app_mqtt` now applies the trust anchor
  only for a TLS scheme. And `app_core` no longer `ESP_ERROR_CHECK`s an optional
  component -- it logs and carries on, because a wrong broker URI should cost you MQTT,
  not the device. Anything that runs before the network is up deserves that scrutiny:
  the blast radius of an abort there is the whole device.

  The crash-dump work from part one proved itself incidentally here -- the boot loop
  wrote a dump on the way past, unprompted.
- **`idf.py flash` builds before it flashes.** Obvious in hindsight, and it wasted three
  attempts at capturing a crash dump: the method was *edit `main.c` to crash, build, flash,
  restore `main.c`* — but restoring before flashing meant the flash step quietly
  recompiled the restored source and installed a perfectly healthy image. The device kept
  not crashing, and the build log said nothing, because the build genuinely succeeded.

  What broke the loop was disassembling the artefact rather than re-reading the source:
  `app_main` in the flashed ELF was seven instructions with no delay and no `abort`. When
  a bench test does not do what the code says it should, check what is actually on the
  chip before checking anything else.
- **A store to address 0 does not fault on ESP32-S3.** The first attempt at a deliberate
  crash used a null dereference. GCC deleted it as undefined behaviour; routing the
  address through a `volatile` global stopped that, and then the store simply went nowhere
  without raising an exception. `abort()` is the reliable way to panic on purpose, and it
  is closer to how a real failure arrives anyway — an assertion or a failed
  `ESP_ERROR_CHECK`.
- **Serial-flashing an older build while an OTA is on probation looks exactly like a
  rollback**, and is counted as one. The download-cycle test above flashed `v0.6.0-nas`
  over a `v0.6.1-nas` that was still in `PENDING_VERIFY`; the next boot found the version
  it had installed was not the version running, which is the definition of a revert, and
  charged a failure to `v0.6.1-nas`. Three cycles took a perfectly good image to failure
  #3 and quarantined it.

  Not a defect — the device cannot tell a developer with a USB cable from a bootloader
  that gave up, and the evidence really is identical. But on a bench it means your own
  reflashing can quarantine a version you are trying to test, and the only clue is the
  status page suddenly refusing an automatic update. Worth knowing before it wastes an
  afternoon. (The cycles kept working anyway because `/api/ota/check` defaults to forced,
  which overrides the quarantine — the override demonstrating itself by accident.)
- **A fresh clone could not build, and CI had never noticed.** `server_certs/ca_cert.pem`
  is gitignored — correctly, since the device trusts whatever CA is embedded and shipping
  one developer's would have every fork pinning it — but `EMBED_TXTFILES` named it
  unconditionally. So every CI build since phase 3 would have died on CMake's own
  `Cannot find source file: .../server_certs/ca_cert.pem`, at *generate* time, naming a
  certificate as a source file. It went unseen because the repo has no remote yet, so the
  workflow has never actually run. Two fixes: `cmake/check_server_cert.cmake` says what to
  do, and every CI job now generates a throwaway CA and signing key of its own — which is
  also why CI artifacts are deliberately not installable on a real device.

  The general lesson is duller than the bug: **anything gitignored is something CI does
  not have**, and a workflow that has never run is not evidence of anything.
- **The passive slot is erased only as far as it is written**, so a shorter image leaves
  the tail of the previous one behind. This is what made the first signature test pass
  when it should have failed: `esp-ota-unsigned.bin` is the signed image minus its
  trailing 4 KiB signature sector, and serving it into a slot that already held the *same*
  build left that build's still-valid signature sector exactly where the verifier looks.
  Not a vulnerability — a stale block only verifies if the content is identical — but it
  makes the obvious way to test the unsigned path silently useless. Test with a build that
  is in neither slot.
- **`esp_https_ota` gives no view of the downloaded bytes.** `ESP_HTTPS_OTA_WRITE_FLASH`
  carries a length, not the data, and `esp_https_ota_perform()` reads the socket directly
  rather than through the http_client event handler — so `HTTP_EVENT_ON_DATA` never fires
  either. Hashing the image means reading the passive partition back after the transfer.
  `esp_https_ota_abort()` is valid in the post-`perform` success state, which is what makes
  "verify, then finish or abort" possible at all.
- **A *new* Kconfig symbol needs an explicit `idf.py reconfigure`.** Adding one to a
  component's `Kconfig` and running `idf.py build` compiles against the old
  `sdkconfig.h`, so the symbol is simply undeclared and the error points at your C file
  rather than at the stale config. Editing an existing symbol's value is picked up
  normally, which makes this easy to misread as a typo in the code.

---

## 8. Status

Decided: Docker/nginx on the NAS; chip-agnostic firmware with the target set at build time.

### Done

**Server (phase 3).**
- `tools/gen_certs.sh` — private CA + NAS server certificate with DNS/IP SANs.
- `nas/` — nginx stack, read-only, TLS on 8443, per-chip manifests.
- `tools/publish.sh` — build → sha256 → manifest → rsync to the NAS; target auto-detected.
  Written but **never run against a real build** — still unproven.
- `nas/README.md` — DS225+ setup, verification, and the DSM-specific traps.
- **The server is up and verified end to end.** `/healthz` → `ok`, `/fw/` lists, `POST` →
  405, unknown path → 404; TLSv1.2 with `ECDHE-RSA-AES128-GCM-SHA256`, verify code 0; SANs
  cover both `nas.lan` and `192.168.1.10`; the embedded CA is byte-identical to the one that
  signed the server cert. An OTA failure from here is a firmware-side problem.

**Firmware (phases 1–2).** Builds clean on ESP-IDF v6.0.2 for esp32s3, esp32c6 and esp32
(both instruction sets, both LED backends), plus a fourth build with the web UI compiled
out. 862 KB image; 78% of the 3.75 MB slot free on the S3, 54% of the 1.9 MB slot free on
a 4 MB ESP32 — room for TLS and OTA in phase 4.

- `components/app_core` — bring-up order and the glue between components. `main.c` is one
  line, so a new project never has to reorder the startup sequence to add its own code.
- `components/app_cfg` — device identity (name, id, derived SoftAP passphrase) and
  NVS-backed config with factory reset.
- `components/app_diag` — version, slot, boot count, reset reason, heap, and the
  `PENDING_VERIFY` rollback state.
- `components/app_led` — one LED owner, per-target backend chosen at build time
  (none / GPIO / addressable-over-RMT), status patterns plus manual override.
- `components/app_net` — the §4b state machine: APSTA fallback AP with station retries
  continuing underneath it, exponential backoff with jitter, SNTP (DHCP + fallback), mDNS.
- `components/app_httpd` — the §4a status page and JSON control surface, the provisioning
  portal, and the captive-portal DNS responder. OTA controls call through a hook table so
  phase 4 is purely additive.
- `partitions_{4mb,8mb}.csv` + `cmake/check_partitions.cmake` — the table is selected by
  flash size and a mismatch **fails the build**, on every target, at configure time.
- `test/host/` + `tools/run_tests.sh` — host-only tests for the two units whose bugs are
  silent on a device (JSON escaping, DNS reply packing) and for the partition guard itself.
- `tools/build_matrix.sh` and `.github/workflows/build.yml` — the §1a CI matrix.

Two real bugs were caught by writing those tests rather than by reading the code: the
partition guard silently skipped rows with an implicit offset (CMake's `CMP0007` drops
empty list elements), and the SNTP config indexed one past the end of a one-element
server array. See §7a.

### Verified on hardware

Run on the bench board (ESP32-S3-WROOM-1-N8R8, `8c:bf:ea:84:a0:84`), driven entirely over
`curl` from a wired host on the same LAN:

| Behaviour | Result |
|---|---|
| Unprovisioned first boot | Raises the AP immediately, captive DNS on 192.168.4.1:53, httpd up at 760 ms |
| Provisioned boot | Associates in 2.5 s, DHCP lease, `state -> connected` |
| SNTP | Synced — the clock is set before anything needs TLS |
| mDNS | `esp-ota-84a084.local` resolves to the right address |
| `GET /` | 11 132 bytes, byte-exact with the source file — no truncation |
| `GET /api/status` | Full document: device, firmware, runtime, net, led, ota |
| `GET /api/scan` | 5 networks, duplicates collapsed, sorted strongest-first |
| `POST /api/led` | Toggle on → off → on, reported state matches read-back |
| `POST /api/restart` | Response arrives first, `boot_count` increments, reason `software`, back in ~4 s |
| `POST /api/forget` | Drops off the LAN, reboots unprovisioned, AP back up |
| `POST /api/ota/check` | 501 with a clear message, as designed until phase 4 |
| Captive probes on the real LAN | 404, **not** a redirect — the device does not pretend to be a portal on your network |
| **§4b fallback** | Backoff 0.9 s → 60 s with jitter; AP raised at 120.7 s; **station retry attempt 8 logged at 153 s** — retries genuinely continue underneath the AP |
| Status LED | Blue while connecting → green heartbeat when connected → white under manual hold → green again on release. Colour order (GRB) correct |

The LED needed the pin corrected first: **the bench board is a DevKitC-1 v1.1, which
wires the addressable LED to GPIO38, not the GPIO48 of v1.0.** On GPIO48 nothing moved at
all. Worth stating plainly because it is the §1a argument in miniature — the pin moved
between two revisions of the same board carrying the same chip, so it belongs in
`sdkconfig.defaults.<target>` and nowhere near application code.

Four more bugs surfaced only by running it, all invisible to a clean build (see the commit
log): the driver silently adopting Wi-Fi credentials left in NVS by previous firmware;
SNTP refusing to start without `CONFIG_LWIP_DHCP_GET_NTP_SRV`; a station config applied
before the Wi-Fi mode was set, which wedged the device in "connecting" forever with the
failing return value discarded; and a LED toggle that sampled the pin mid-blink and read
its own result back before the pattern task had applied it.

`tools/provision_nvs.sh` was written to do this testing and is worth keeping: it writes
the same NVS keys the portal writes, which is the production-line flashing path §5 assumes
exists.

### Phase 4 verified on hardware

The device pulled a new image over TLS and booted into it. Concretely: running
`v0.2.0-2-gb1063be` from `ota_0`, it fetched the manifest, compared versions, downloaded
999 808 bytes in about 10 s (~800 kbit/s over Wi-Fi), validated the image, wrote it to
`ota_1` and restarted into **v0.3.0**. The slot really did change.

`tools/publish.sh` was exercised for the first time and worked unmodified — build →
sha256 → manifest → copy, with the target read out of the build.

Every refusal path was then made to fire, on hardware:

| Manifest says | Device does |
|---|---|
| the version it is already running | `up_to_date: already running v0.3.0` |
| `project: some-other-project` | refused, `wrong_project` — this one would be a brick |
| `url: http://...` | refused, `insecure_url` |
| `chip: esp32c6` | refused, `wrong_chip` |
| `v9.9.9`, but the image header says `v0.3.0` | refused **after** the download began — the header check |
| truncated JSON | `manifest is not valid JSON` |
| nothing at all (404) | `manifest unavailable: ESP_ERR_NOT_FOUND` |

The version-mismatch case is the one worth noting: the transfer had already started, the
image header was read, the two sources disagreed, and it aborted before committing
anything to the passive slot. That is the entire reason for using the advanced
`esp_https_ota` API rather than the one-shot call.

The device stayed on the network and healthy through all seven refusals.

One further defect came from watching rather than testing: a check triggered in the first
minute after boot failed on the clock guard, because SNTP had not finished. That is now a
bounded wait — observed holding 12 s and then proceeding. See §7.

**Caveat on where it was served from.** The NAS is up and its certificate verifies, but
this workstation has no key on it, so nothing could be published there. The test instead
used `test/rig/`, which serves the same `/fw/<project>/<chip>/` layout over TLS with a
certificate minted from the *same* private CA — which the device accepts precisely because
§3 chose to embed the CA rather than the leaf. What remains unproven about the NAS is the
NAS: nginx's behaviour under a long download, and `publish.sh`'s rsync-over-ssh leg.

Image cost of OTA, measured: about 110 KB. Slot headroom is now 75% (esp32s3, 8 MB),
71% (esp32c6, 8 MB) and **48% (esp32, 4 MB)** — so §7's warning that 4 MB is tight is
real but not yet binding.

### Phase 5 verified on hardware

Rollback is the one feature whose whole value lies in what happens when things go wrong,
so it was proven the only way that means anything: by publishing images that were broken
on purpose and watching the board put itself back. All of it ran on the bench board
(ESP32-S3-WROOM-1-N8R8, `8c:bf:ea:84:a0:84`), served from `test/rig/` over TLS, driven
by `curl` from a wired host.

**Four independent things now have to agree** before a new image runs for good, and they
fail at four different moments — which is the point, because each is cheap at the moment
it happens and expensive later:

| Check | When | What it catches |
|---|---|---|
| Manifest policy (`update_decide`) | before a byte is downloaded | wrong project, wrong chip, non-https URL, a version already known to fail here |
| Image header (`esp_https_ota_get_img_desc`) | after the transfer starts, before anything is committed | a manifest that disagrees with the image it points at |
| sha256 of the written slot | after the last byte, before the slot is bootable | a truncated or corrupted upload |
| Signature (`SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`) | in `esp_https_ota_finish()` | an image that did not come from the holder of the signing key |

And then it still has to survive its self-test.

**What each boot now means.** `rollback_on_boot()` reduces it to three facts — the version
we recorded as installed, the version actually running, and whether the bootloader left it
in `PENDING_VERIFY`:

| | Meaning | What app_ota does |
|---|---|---|
| pending | on probation | run the self-test; confirm it or roll back |
| installed ≠ running | the bootloader already put the old image back | count a failure against the version that was installed |
| installed = running, not pending | an earlier boot's self-test passed | clear the record, lift any quarantine it earned |
| nothing installed, not pending | an ordinary boot | nothing |

The middle row is the one that covers both ways an image can die: the self-test rejected
it, or it panicked before a self-test could run and the bootloader reverted on its own.
The evidence is identical, so the code does not need to distinguish them.

**New in `components/app_ota/`**, following the existing split between logic that can be
tested on a host and code that cannot:

- `rollback_policy.c` — the boot accounting above, and the failure tally. Pure C.
- `update_policy.c` — restructured around `update_offer_t`/`update_device_t` and given the
  quarantine verdict. Pure C.
- `sha_hex.c` — formatting and comparing a digest as text. Pure C, and tested precisely
  because a comparison that returns true for everything would turn the manifest's sha256
  field into decoration with nothing about the device looking different.
- `image_check.c` — reads the passive slot back and hashes it through PSA (mbedTLS 4, per
  §7a).
- `ota_record.c` — the three NVS keys, in `app_cfg`'s namespace so factory reset still has
  one definition.
- `selftest.c` — uptime, network, clock, then the server probe on a backoff until the
  deadline.

**Evidence, such as it is:**

- **141 host checks pass**, up from 82. The new ones are `rollback_on_boot` (including the
  two cases that would silently break the tally: a pending image with no record at all,
  and a tally that starts at 0 and therefore never reaches a threshold of 2), the
  quarantine paths through `update_decide`, and `sha_hex` against the real SHA-256 of the
  empty input. A second build-time guard, `check_signing_key`, gets the same six-case
  shell test the partition guard has — both fire only on a misconfigured tree, which is
  exactly when nobody is watching to notice they stopped working.
- **The matrix builds**: esp32s3, esp32c6 and esp32 all build *and sign*, on both
  instruction sets and both signing schemes.
- **The configurations that turn phase 5 off build too** — unsigned, rollback disabled,
  self-test server probe disabled, sha256 not required, OTA compiled out, web UI compiled
  out. These are the `#if` paths that rot quietly otherwise, so CI now builds an
  unsigned/no-rollback variant on every push.
- **Both build-time guards fire and report**: `check_partitions` and the new
  `check_signing_key`, which turns a missing key from an esptool error about a path into
  the one command that fixes it.

**Image cost, measured on esp32s3.** Phase 4's image was 999 808 bytes. The same build
with phase 5's code and signing turned *off* is 1 008 352 — so the rollback, self-test,
hashing and record-keeping cost **about 8 KB**. Signing takes it to 1 052 672, **about
43 KB more**, of which only 4 KB is the signature sector: the rest is padding to the next
64 KiB boundary that secure boot v2 requires (§7a), so that figure is lumpy and a build
sitting just over a boundary pays for the whole next block.

Slot headroom is now **73%** (esp32s3, 8 MB), **70%** (esp32c6, 8 MB) and **45%**
(esp32, 4 MB). The 4 MB part has gone from 48% to 45% free — §7's warning is still not
binding, but it is now the number to watch.

#### What actually happened on the board

Every refusal and every recovery was made to fire. The device was serial-flashed with a
signed v0.5.0 first, because §7 says it has to be:

| What was published | What the device did |
|---|---|
| a good v0.5.1 | installed it, booted `ota_1` in `PENDING_VERIFY`, **passed its self-test at 120 s uptime and confirmed it** — with the shipped defaults, not shortened ones |
| a good image with one byte flipped, manifest hash untouched | downloaded all 1 052 672 bytes, hashed the slot, refused: `sha256 mismatch: 0e9adf2ff7a1… not 7e790bef5c21…`. Never rebooted |
| the same image re-signed with a **different** RSA key | hash matched — as it would, the attacker publishes both — then `image valid, signature bad`, refused |
| an unsigned image | refused identically |
| a version already refused twice, on an automatic poll | `failed to run here 2 times -- not retrying automatically`. **Zero bytes downloaded** |
| the same version, from the button | downloaded it anyway and refused it again on its merits — the override works |
| **an image that boots but cannot reach its update server** | probed three times over 60 s, gave up at the deadline, **rolled back to v0.5.0** and reported `v0.5.4-noserver was rolled back (failure #1)` |
| **an image that `abort()`s three seconds in** | the **bootloader** reverted it with no help from `app_ota` at all; the device was back on the old image **four seconds** after the panic, and the failure was still counted |
| that panicking image a second time | count reached 2, and the next poll refused to install it. A build that bricks itself on boot now stops being offered after two tries |

Three more went on after the first pass, once the accounting fix below was in:

| Test | Result |
|---|---|
| **an OTA with the status page open**, polling `/api/status` every 700 ms — §7's "case that actually bites" | 33 consecutive polls through the download, **zero failures**, page served whole (15 708 bytes). Free heap bottomed out at 208 KB, ~42 KB below idle. On an 8 MB S3 that is a wide margin; a 4 MB part is a different question and still open |
| `POST /api/restart` while an image is being written | **409**, `an update is being written -- restart refused`. `POST /api/led` in the same window still returned 200, so it is the specific refusal §4a promised and not a wedged server |
| `POST /api/ota/check` while the running image is on probation | **409**, with the reason spelled out. ESP-IDF would refuse to move the boot partition anyway; this refuses before fetching a megabyte to find out |
| **§3's cert-bundle option**, pointed at a public host with no private CA embedded at all | `manifest HTTP 404` — a *status code*, which means the handshake against the public roots succeeded. Then the self-test failed on that 404 and **the board rolled itself back**, which is as good a demonstration of phase 5 as any: a build pointed at the wrong server recovered with no cable |

One thing worth noting that was never a planned test: v0.5.0 had a sha256 failure recorded
against it early on, was later installed successfully, and **its quarantine record was
cleared when it passed its self-test**. That is `ota_record_clear_failure()` doing exactly
what it should, observed by accident.

Measured, on Wi-Fi at about −51 dBm: a 1 052 672-byte image takes ~14 s to download
(~600 kbit/s); hashing the whole slot back out of flash takes **160 ms**; RSA-3072
signature verification takes **90 ms**. The cost of checking is nothing next to the cost
of fetching.

#### Two things the bench changed

**The signature test lied the first time, and the reason is a trap.** Serving the
*unsigned* twin of a build already in the slot verified successfully. `esp-ota-unsigned.bin`
is byte-for-byte the signed image minus its trailing 4 KiB signature sector, and
`esp_ota_write` erases **only as far as it writes** — so the signature sector left behind
by the earlier serial flash of that same build was still there, still valid for that exact
content, and still found. It is not a hole (a stale block only verifies if the content is
identical, which is what a signature means) but it makes the obvious way to test the
unsigned path silently useless. The fix is to test with a build that is in neither slot.

**Failure accounting moved to one place.** The first rollback worked and the device came
back — and said nothing about it. `run_probation()` had counted its own failure and
cleared the attempt record before rebooting, so the image that came back saw no
outstanding attempt and reported an ordinary boot. The record now survives the rollback and
the *reverted* boot accounts for it, which means both ways an image can die — self-test
rejected it, or it panicked before one could run — go through the same branch, and the
status page of the device you can actually reach says `v0.5.4-noserver was rolled back
(failure #1)`. Re-tested before and after.

That also explains why `ROLLBACK_BOOT_CONFIRMED` is rarer than the table above suggests:
on a normal confirmation the record is cleared immediately, so it only fires if power is
lost in the window between `esp_ota_mark_app_valid_cancel_rollback()` and that clear. It
is a crash-window tidy-up, not a routine path.

### Phase 7: what forking the template actually found

A second project was copied out of the tree and built, which is the only way to test the
claim the whole repo exists to make. It worked — `doorbell.bin`, `project_name: doorbell`
in the image header, `doorbell/esp32s3/` on the server, its own CA and signing keys — and
it found two things that no amount of building `esp-ota` ever would:

- **`build_matrix.sh` hardcoded the project name.** It grepped for
  `^esp-ota.bin binary size`, which matches nothing in a fork, so a build that had in fact
  succeeded printed `ok` for the target, `FAILED` in the summary, and `all targets built`
  at the end — three different answers to the same question. It now reads `app_bin` out of
  `project_description.json`, the way `publish.sh` already did.
- **A fork that is not yet a git repository is versioned `0.0.0-nogit`**, from the
  fallback in the top-level `CMakeLists.txt`. Update policy is string equality, so two
  *different* builds both carrying that version compare as identical and every device
  reports itself up to date forever. Harmless the moment you `git init` and tag, invisible
  until then, and precisely the failure this template is supposed to prevent. Now called
  out in the README's forking section.

The other dozen files that still say "esp-ota" after a fork are content, not identity:
comments, the CA subject line, test fixtures, the NAS stack path. The last of those is
deliberate — one nginx serves every project from one firmware root, because the layout is
`<project>/<chip>/`.

### The NAS, at last — and what publishing to it broke

`publish.sh`'s rsync-over-ssh leg had never run since phase 3, and running it **took the
server down**. Not the transfer: the transfer was fine. `/fw/` returned 200 before the
publish and **403 all the way down afterwards**.

`publish.sh` stages into `mktemp -d`, which is **0700**, and `rsync -a "$staging/"
host:"$dest/"` applies the *source directory's* mode to the destination directory. So
`/volume1/docker/esp-ota/firmware` became 0700 owned by the publishing user, and nginx —
which runs as a different user inside its container — could no longer read the directory
it serves. The publish reported success. A device polling six hours later would have said
`manifest unavailable: ESP_ERR_NOT_FOUND`, and nothing would have pointed back at the
publish that caused it.

**The bench rig could not have caught this, ever.** It serves as the same user that
published, so 0700 is perfectly readable. Every earlier test looked fine right up until a
real server with a second user was involved — which is exactly what §8 kept meaning by
"what remains unproven about the NAS is the NAS".

Two fixes, both in `publish.sh`:

- `chmod -R a+rX,go-w "$staging"` before the transfer. Capital `X` sets the search bit on
  directories only, so the firmware stays 0644 rather than becoming executable for no
  reason; `go-w` stops a permissive umask publishing a group-writable web root. It fixes
  the local-copy path, which had the same flaw.
- **The script now verifies its own publish**: it fetches the manifest back over HTTPS and
  checks that the version it just wrote is the one the server returns — not merely that
  something answered. A 403, or a `OTA_BASE_URL` pointing at a different server than the
  one written to, is now a non-zero exit with the reason. Copying a file somewhere is not
  publishing it, and this run is the proof.

Then the device did the whole thing against nginx, with the bench rig **stopped** so
nothing else could possibly have served it:

| | |
|---|---|
| Hop onto a NAS-pointing build | took `v0.6.0-nas` from the rig, and its self-test probed the *NAS* — passed at 120 s, confirming that nginx serves a real device over TLS against the embedded CA |
| Full update from nginx | `v0.6.0-nas -> v0.6.1-nas`, 1 052 672 bytes, sha256 matched, installed, `ota_0 -> ota_1`, self-test passed, confirmed |
| With the status page open throughout | 38 polls at 700 ms, **zero failures**, 208 KB free at the trough |

Then the download was repeated five more times — serial-flash the older build, let it update,
repeat — because one success is an anecdote:

| | download | throughput |
|---|---|---|
| 1 | 14.49 s | 581 kbit/s |
| 2 | 14.51 s | 580 kbit/s |
| 3 | 14.63 s | 576 kbit/s |
| 4 | 14.51 s | 580 kbit/s |
| 5 | 14.50 s | 581 kbit/s |

**140 ms of spread across five megabyte transfers**, every hash matching. nginx is not the
variable here; the Wi-Fi link is, and it is steady. Six full downloads from the NAS in
total, no failures.

**And a soak, which found the gap in the above.** Left polling the NAS every 60 s
(the Kconfig minimum, jittered): **30 manifest fetches over 30 minutes, zero failures**,
and the automatic poll timer observed firing on hardware for the first time — intervals of
58, 54, 56, 62, 60, 58, 64, 58, 60 s around the configured 60. Every check today before
that had been triggered by hand; `{"forced":false}` exercises the same *decision* but never
`esp_timer` → `poll_fired` → reschedule.

Heap across those 30 TLS handshakes: `heap_free` settled at 249 612 after the first
handshake and read 249 420 at the end, **−192 bytes over half an hour**; the low-water mark
drifted 212 692 → 212 372, **−320 bytes across 30 handshakes**, roughly 10 bytes each. Too
small to distinguish from variation in peak allocation at this length, and nowhere near a
leak that would matter — but it is the number to watch if the template ever runs for weeks.

The soak is also worth reading as a caution about test design: it polls the NAS constantly
and **never downloads anything**, because it was built with the same version string the NAS
serves so every poll resolves to `up_to_date`. It exercises the handshake and the small GET
thoroughly and the transfer path not at all. That is why the five download cycles above
exist separately.

### Phase 8: crash dumps, proven by crashing

`app_diag` was given "core dump" as a responsibility back in §4 and never got it. It has
it now, and it was tested the way everything else here has been — by breaking something on
purpose.

A deliberate `abort()` fifteen seconds into start-up, on the bench board:

| | |
|---|---|
| The panic | 23 736 bytes written to the `coredump` partition, checksum verified on the next boot |
| Survives a reflash | the device was then flashed with a **different build**, and the dump was still there — it records the crashed build's ELF id precisely because it can outlive the firmware that produced it |
| Reported without asking | `CRASH   23776-byte dump stored: task "main" at pc 0x4037d395` on the boot banner, and on the status page |
| `GET /api/coredump` | task, PC, `abort() was called at PC 0x4201235d on core 0`, size, crashed-build id |
| `GET /api/coredump.bin` | 23 776 bytes, matching the size the device reported |
| Decoded off-device | `#3  0x42012360 in app_main () at /home/repo/esp-ota/main/main.c:22` — the exact line of the deliberate abort, with every task's frame |
| `POST /api/coredump/erase` | dump gone, raw download then 404s |

**The raw download is Kconfig-gated and off by default**, which is the one judgement call
here worth stating. A core dump is a copy of task stacks; a crash during Wi-Fi work can
have the passphrase in it, and this server has no authentication (§4a). The *summary* is
always available because it gives nothing away, and with the download compiled out the
dump is still recoverable over a serial cable. Turning it on is a decision about whose LAN
this is.

The partition is 64 KB, carved off the tail of `storage` so **every offset above it is
unchanged** and a device already in the field keeps booting the slot it is running. It
still cannot reach that device: OTA cannot rewrite a partition table, so this arrives by
cable or not at all.

One design note that only becomes obvious in use: the partition holds exactly one dump,
and it is the **first** crash after an erase, not the most recent. That is the right way
round — the first failure usually explains the ones that follow — but it means erasing is
how you arm it, not merely how you tidy up, and the API and the page both say so.

### Phase 8, part two: the broker

The reason to want one is narrow and worth stating plainly. An update reaches devices on
their own jittered six-hour poll, which is correct and unhurried and **no help at all**
when you have just published a fix and want to watch it land. That is what this buys, and
almost the only thing:

```
mosquitto_pub -t esp-ota/all/cmd -m check
```

Everything else follows from being there anyway — retained status, and a last will so a
device that loses power stops claiming to be fine.

Proven on the bench against `test/rig/broker.py`, a minimal MQTT 3.1.1 server written for
the purpose, using a certificate minted from the project CA exactly as `test/rig/`'s HTTPS
stand-in does:

| | |
|---|---|
| Connect | `CONNECT MQTTv4 id='esp-ota-84a084' keepalive=60s user='bench-user' will='esp-ota/84A084/online'` over TLS, validated against the embedded CA |
| Subscribe | its own `cmd` topic and the fleet-wide one |
| Publish | retained `online=1`, then retained status every 20 s — version, slot, uptime, heap, RSSI, OTA state, whether a crash dump is stored |
| Commands | `led-on`, `led-off` addressed to the device; `led-auto` over **`esp-ota/all/cmd`**, all reflected in `/api/status` |
| Garbage | `restartnow` and `{"cmd":"restart"}` did nothing at all — no action, and no acknowledgement either |
| Last will | device rebooted without a clean disconnect; the broker declared it gone after 1.5× keepalive and published retained `online=0` |
| Reconnect | unprompted, on the next boot |
| **`check` to the fleet** | `app_mqtt: command: check` → `app_ota: checking` → manifest fetched **from the NAS** → `v0.9.0-mqtt -> v0.6.1-nas` downloaded, installed, restarted |

Three decisions in there are worth more than the mechanics:

**A pushed `check` does not override the quarantine.** It calls `app_ota_check_now(false)`,
the same as the automatic poll. A fleet-wide push is automation, not a human looking at
one device — the override exists for someone who has decided a version deserves another
go, and one broken release plus one broadcast should not re-download it onto every device
that already rejected it twice.

**Unrecognised payloads are ignored silently, with no reply.** A broker carries other
people's traffic. A device that answers everything it does not understand fills a log, and
a parser with a default case eventually reboots something nobody asked it to.

**Payloads are bare words.** `mosquitto_pub -m check` is something you can type from
memory at a terminal; a JSON envelope on a command topic is complexity that has to earn
its place, and here it does not. The parser is pure C and host-tested against unterminated
buffers, because MQTT payloads are counted rather than NUL-terminated and treating one as
a C string is how you read past the end of it.

The broker is **off by default** and nothing in the OTA path depends on it: a broker that
goes down means the fleet stops reporting, not that a device is stuck. Credentials are a
username and password shared across the fleet, which is a real limit and is documented as
one — what the broker controls is "check now", and the update itself is signed and
verified independently.

#### The refactor phase 8 forced

`app_mqtt` needs the same CA `app_ota` does, and ESP-IDF refused to build:

```
ninja: error: build.ninja:26473: multiple rules generate ca_cert.pem.S
```

`EMBED_TXTFILES` names its generated stub after the file's *basename*, so two components
embedding one certificate collide. The fix and the correct design turned out to be the
same thing: the trust anchor is a project-level decision — §3 says exactly that — not
`app_ota`'s private business. It now lives in `app_trust`, a component with one job, and
`CONFIG_APP_OTA_TRUST_*` became `CONFIG_APP_TRUST_*`.

`test/host/test_server_cert.sh` caught the rename immediately, which is the entire
argument for having written it.

### Still unverified

- **Rollback on a 4 MB part.** Everything above was proven on the 8 MB ESP32-S3. A 4 MB
  board has two ~1.9 MB slots against a ~1 MB signed image, so the mechanism is the same
  and the margin is not.
- **Key rotation.** The bridge build carrying two signature blocks is documented in
  `keys/README.md` and has never been produced, let alone installed.
- **A self-test that fails because the *network* is gone** rather than because the image
  is broken — §4's deliberate trade. The failing image was given an unresolvable server
  address, which exercises the same code but not the same situation.
- **Captive-portal detection on real phones.** Cannot be tested from a wired host — the
  DNS responder and the probe handlers are unit-tested and confirmed running, but whether
  the login sheet actually pops on iOS, Android and Windows needs a phone each.
- **The portal's own join flow** (scan list → passphrase → result reported before the AP
  drops). The `/api/provision` endpoint is tested; the browser path through it is not.
- **Power-cycling the access point**, which is the failure §7 actually cares about. The
  fallback was proven with a nonexistent SSID instead.
- **An OTA with the status page open, on a 4 MB part.** Proven on the 8 MB S3 — 33 polls
  at 700 ms through a download with no failures and 208 KB still free — but §7 names this
  as the case that bites precisely because `esp_http_server`'s buffers and mbedTLS's are
  both largest at once, and it is the low-RAM part where that matters.

### Next

Phases 0–7 are done bar one optional piece, and the chain is unbroken end to end: tag,
build, sign, publish, verify, fetch, check four ways, boot on probation, prove, keep — or
put the old one back. What is left is genuinely optional or genuinely needs hardware.

1. **The BLE provisioning transport** (§1a) — the last unbuilt thing in phase 7, and
   optional by design: SoftAP is the universal path and BLE is an *additional* transport
   where `SOC_BLE_SUPPORTED`, never the only way onto a network.
2. **Phase 8**, all of it optional: Mosquitto on the NAS for command/telemetry and a
   "check now" push, ESP Insights or local crash-dump collection.
3. **The verification debt below**, which needs hardware that is not on this bench: a
   phone each for captive-portal detection, a 4 MB board, an access point you can
   power-cycle.

### Open

- The Container Manager **Project** wizard rejects the compose file for reasons that are
  not the file. Cosmetic — the stack runs over ssh — but unexplained.
