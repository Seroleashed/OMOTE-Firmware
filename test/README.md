# Unit tests

These tests run on the development machine. No OMOTE hardware, no ESP32
toolchain and no SDL window are needed.

```bash
pio test -e native_test            # all tests
pio test -e native_test -f test_keys   # a single test folder
pio test -e native_test -v         # with output of every assertion
```

## Layout

| Path | Purpose |
|---|---|
| `test/omote_fakes.h/.cpp` | Test doubles for the whole hardware facade (`hardwarePresenter.h`), the scene handler and the gui layer. Records what the firmware *tried* to do. |
| `test/lvgl.h` | Minimal LVGL stub. Several OMOTE headers include `<lvgl.h>` although the modules under test never call LVGL. |
| `test/test_command_registry/` | Command registration and dispatch (IR, MQTT, scene, gui, special). |
| `test/test_scene_registry/` | Scene registration and the key lookup chain gui → scene → default. |
| `test/test_keys/` | Key handling: `SHORT`, `SHORT_REPEATED`, `SHORTorLONG`, hold time, repeat rate. |
| `test/fake_filesystem.h` | In-memory file system with fault injection (truncated write, failing rename, bit flip). |
| `test/test_config_storage/` | Crash safe storage: atomic save, backup rotation, crc, power loss. |
| `test/test_config_model/` | JSON schema v1: round trip, validation of foreign files, export of the compiled-in configuration. |
| `test/test_command_snapshot/` | Snapshot of every registered command. The safety net for the whole rework. |
| `test/test_firmware_info/` | Firmware version and the conversion of the compiler's build date. |
| `test/test_boot_guard/` | Safe mode: boot counting, rescue boot, requested safe mode. |

Files in the root of `test/` are shared by every test folder, which is why the
fakes live there.

## The snapshot test

`test_command_snapshot` registers the same devices that `main.cpp` registers and
writes name, handler and payloads of every command to a text file, sorted by
name. `commands.snapshot.txt` is the committed reference.

A step that must not change behaviour (stable names, the sequence engine, the
JSON loader with no JSON files present) leaves that file untouched. A step that
changes it does so visibly, in a reviewable diff.

Command ids are deliberately **not** part of the snapshot: they are handed out in
registration order and are allowed to move. The name is the stable reference.

The first run has no reference yet: the test writes `commands.snapshot.txt` and
fails with a note. Review the file and commit it - from then on the test guards
it. To update it after an intended change:

```bash
pio test -e native_test -f test_command_snapshot     # fails, writes .actual
mv test/test_command_snapshot/commands.actual.txt \
   test/test_command_snapshot/commands.snapshot.txt
```

The snapshot reflects the flags of `env:native_test`, not those of the firmware
build. With `ENABLE_KEYBOARD_BLE=0` and `ENABLE_KEYBOARD_MQTT=0` the keyboard
commands are the dummies, which only get an id and are therefore not in the
table.

## Conventions

* **The clock belongs to the test.** `millis()` is faked. `fakes::reset()`
  advances the clock by 60 s instead of resetting it, because `keys.cpp` keeps
  static timestamps and a clock jumping backwards would silently rate-limit the
  first key press of the next test.
* **The keypad is polled, not written.** `fakes::pressKey()` marks a key as
  held; the faked `getKeys()` re-reports it on every `keypad_loop()`, exactly
  like the TCA8418 driver does. Writing `rawKeys` directly would not survive
  `keypad_resetReleasedKeys()`.
* **Call `fakes::reset()` in `setUp()`.** It clears all recordings, settles the
  keypad and makes sure `COMMAND_UNKNOWN` has an id of its own.
* **Assert on recorded calls**, e.g. `fakes::irSends`, `fakes::mqttPublishes`,
  `fakes::sceneCalls`. Never on log output.

## What these tests are for

They pin down the behaviour that the JSON based configuration has to reproduce
exactly. Before a module is changed to be configurable at runtime, its current
behaviour should be covered here - the tests then show whether the refactoring
changed anything a user would notice.
