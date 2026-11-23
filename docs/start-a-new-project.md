# Start a new project from this template

Copy the tree & change three things. There is deliberately no rename script: it would have
to guess which occurrences of the name are identity & which are content, & the answer
differs per project.

**Before you start**

- The template building & flashing for your target — see [Set up a board](setup.md)
- A name for the new project, valid as a DNS label: lowercase letters, digits & hyphens

## 1. Copy the tree

```bash
git archive HEAD | tar -x -C ../doorbell
cd ../doorbell && git init
```

Where:

- `../doorbell` is the new project directory
- `git archive` copies tracked files only, which leaves behind the certificates, keys &
  build output that must not be inherited

> **NOTE:** run `git init` before building anything you intend to install. The version
> comes from `git describe`, & a tree that is not a repository falls back to
> `0.0.0-nogit`. Update policy compares version strings for equality, so two different
> builds both carrying that version compare as identical & every device reports itself up
> to date for ever.

## 2. Change the three names

| File | Setting | Why it matters |
|---|---|---|
| `CMakeLists.txt` | `project(esp-ota)` | Written into the image header. Checked against the manifest & against the incoming image before a byte is committed, so a publish into the wrong directory is refused rather than installed |
| `main/Kconfig.projbuild` | `APP_PROJECT_NAME_SLUG` | The mDNS hostname & the setup access point SSID |
| `components/app_cfg/Kconfig` | `APP_CFG_AP_PASS_SALT` | Left at the default, anyone who knows this template can derive any device's setup passphrase from its MAC address, which the SSID advertises |

```bash
sed -i 's/^project(esp-ota)$/project(doorbell)/' CMakeLists.txt
```

## 3. Generate this project's own secrets

```bash
./tools/gen_signing_key.sh
./tools/gen_certs.sh doorbell.lan 192.168.1.10
```

Neither is inherited, & neither should be: the certificate authority decides what the
device believes, & the signing key decides who may update it.

## 4. Build & confirm the identity changed

```bash
./tools/build_matrix.sh esp32s3
```

```
  ok  doorbell.bin binary size 0x101000 bytes. ... 0x2bf000 bytes (73%) free.
```

```bash
./tools/publish.sh build_esp32s3
```

```
==> doorbell v0.1.0 (esp32s3), 1028 KiB
    manifest: https://doorbell.lan:8443/fw/doorbell/esp32s3/manifest.json
```

The `<project>/<chip>/` path is what keeps a mixed fleet apart, so one server carries any
number of projects from one firmware root with no collision.

## What does not need changing

Around a dozen tracked files still mention the original name after a fork: comments, the
certificate authority subject line, test fixtures & the container stack path. Those are
content rather than identity. The container path is deliberate — one server, many projects.

## If something goes wrong

**`build_matrix.sh` reports `FAILED` for a target that built** — an older copy of the
script grepped for the original project name. Current versions read the image name from
'project_description.json'.

**Every device reports itself up to date & never updates** — the version is `0.0.0-nogit`
on both sides. See the note in step 1.

**A device refuses an update with `wrong_project`** — the manifest names a different project
from the one in the running image. Check `project()` against the directory the image was
published into.

## See also

[Set up a board](setup.md) · [Publish an update](publish-an-update.md)
