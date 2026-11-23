# Diagnose a crash

A panic writes a core dump to its own flash partition. It survives the reboot that follows
& it survives an update, so the dump you find may describe a build that is no longer
running. This page turns one into a line of source.

**Before you start**

- The ELF file of the build that crashed. Not the one running now — see step 2
- `esp-coredump`, which ships with ESP-IDF (`esp-coredump -h`)
- Either the device's web interface, or a serial cable

## 1. Notice there is one

The boot banner reports a stored dump without being asked:

```
I (568) app_diag:  boot    #56, reset reason: power-on
W (1458) app_diag:  CRASH   23776-byte dump stored: task "main" at pc 0x4037d395
W (1468) app_diag:          abort() was called at PC 0x4201235d on core 0
```

The same facts are on the status page & at `GET /api/coredump`:

```bash
curl http://esp-ota-84a084.local/api/coredump | python3 -m json.tool
```

```json
{
  "present": true,
  "size": 23776,
  "task": "main",
  "pc": "0x4037d395",
  "reason": "abort() was called at PC 0x4201235d on core 0",
  "elf_sha256": "98e05c7ee",
  "downloadable": true
}
```

## 2. Match it to a build

`elf_sha256` identifies the build that crashed, which is the whole reason it is recorded.
Compare it against the first bytes of the application descriptor in a candidate image:

```bash
python3 -c "print(open('build/esp-ota.bin','rb').read(0x90)[0x70:0x90].hex()[:9])"
```

Decoding against the wrong ELF produces a backtrace that is confidently wrong rather than
obviously wrong, which is worse than none.

## 3. Get the dump off the device

`downloadable` reports whether this build serves it over HTTP.

> **NOTE:** a core dump is a copy of task stacks, so a crash during Wi-Fi work can contain
> the network passphrase, & the device's web interface has no authentication. Enable the
> download only on a network you control, & erase the dump once you have it.

```bash
curl -o coredump.bin http://esp-ota-84a084.local/api/coredump.bin
```

That endpoint exists only when `CONFIG_APP_HTTPD_COREDUMP_DOWNLOAD=y`, which is off by
default. With it off, read the partition over serial instead:

```bash
esptool --port /dev/ttyUSB0 read-flash 0x7f0000 0x10000 coredump.bin
```

Where:

- `0x7f0000` & `0x10000` are the offset & size of the 'coredump' partition. Those values
  are for the 8 MB table; the 4 MB table places it at `0x3f0000`. Both are listed in
  'partitions_8mb.csv' & 'partitions_4mb.csv', & the bootloader prints them at every boot

## 4. Decode it

```bash
esp-coredump info_corefile --core-format raw --core coredump.bin build/esp-ota.elf
```

Where:

- `--core-format raw` says the file is the partition contents rather than a base64 capture
- the final argument is the ELF from step 2

```
Crashed task handle: 0x3fca5e2c, name: 'main'
Panic reason: abort() was called at PC 0x4201235d on core 0

==================== CURRENT THREAD STACK =====================
#0  panic_abort (details=0x3fca5bf0 "abort() was called at PC 0x4201235d on core 0")
#2  0x4037cc06 in abort ()
#3  0x42012360 in app_main () at /home/repo/esp-ota/main/main.c:22
```

Every task's frame is captured, not only the one that faulted. A deadlock or a stack
overflow is usually explained by what the other tasks were doing.

## 5. Erase it, which is also how you arm the next one

The partition holds exactly one dump, & it is the **first** crash after an erase rather
than the most recent. That is the right way round — the first failure usually explains the
ones that follow — but it means a stored dump prevents a later one being recorded.

```bash
curl -X POST http://esp-ota-84a084.local/api/coredump/erase
# → {"ok":true}
```

## If something goes wrong

**`"present": false` after a crash you watched happen** — either a dump was already stored
(see step 5), or the panic was too early for the core-dump component to run. A brownout
during the write also leaves nothing; the checksum is verified at boot & a dump that fails
it is reported as absent rather than offered as evidence.

**The backtrace is nonsense, or every frame is `??`** — wrong ELF. Repeat step 2. This is
the failure the `elf_sha256` field exists to prevent.

**`esp-coredump: No such command 'info-corefile'`** — the subcommand uses an underscore:
`info_corefile`.

## Why it is local

ESP Insights would upload crash data to a cloud service, which undoes the premise of the
whole design — see [PLAN §5](../PLAN.md). Nothing here leaves the device unless somebody
fetches it.

## See also

[Publish an update](publish-an-update.md) · [Design & test record](../PLAN.md)
