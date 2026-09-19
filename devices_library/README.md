# Device library

One JSON file per device, each of them a complete device pack: metadata plus
every command with its handler and payloads. This is the format that gets
imported, exported and shared.

These files are **generated**, not written by hand:

```bash
pio run -e config_export
.pio/build/config_export/program devices_library
uv run tools/validate_config.py devices_library/*.json
```

The exporter registers the same devices the firmware registers and writes out
what landed in the command registry. The payloads therefore come from the
registry rather than from somebody reading the C++ and retyping 300 IR codes.
A change to a device shows up here as a readable diff.

## What is in here

| File | Commands | Source |
|---|---|---|
| `samsungTV.json` | 30 | `src/devices/TV/device_samsungTV/` |
| `yamahaAmp.json` | 8 | `src/devices/AVreceiver/device_yamahaAmp/` |
| `appleTV.json` | 15 | `src/devices/mediaPlayer/device_appleTV/` |
| `smarthome.json` | 4 | `src/devices/misc/device_smarthome/` |
| `lgTV.json` | 42 | `src/devices_pool/TV/device_lgTV/` |
| `sonyTV.json` | 45 | `src/devices_pool/TV/device_sonyTV/` |
| `boseAmp.json` | 27 | `src/devices_pool/AVreceiver/device_boseAmp/` |
| `lgsoundbar.json` | 2 | `src/devices_pool/AVreceiver/device_lgsoundbar/` |
| `lgbluray.json` | 22 | `src/devices_pool/mediaPlayer/device_lgbluray/` |
| `samsungbluray.json` | 26 | `src/devices_pool/mediaPlayer/device_samsungbluray/` |
| `shield.json` | 13 | `src/devices_pool/mediaPlayer/device_shield/` |
| `airconditioner.json` | 1 | `src/devices_pool/misc/device_airconditioner/` |

235 commands in total.

**Only the active commands are in here.** Most device sources carry far more
codes than they register - they are commented out with the note "every command
takes 100 bytes, whether used or not". `lgsoundbar` for instance has 31 codes in
the source and registers 2 of them. Uncomment what you need and run the export
again. Once the JSON configuration is loaded at runtime (step 8) that trade-off
disappears: a command in a file costs nothing until it is used.

**`denonAvr` is missing on purpose.** Every single `register_command()` in
`src/devices_pool/AVreceiver/device_denonAvr/device_denonAvr.cpp` is commented
out, so the device registers nothing at all. There is nothing to export until
somebody uncomments the codes they need.

## Notes on the format

* Commands are referenced by **name**, never by the numeric id. Ids are handed
  out in registration order and shift as soon as a device is added.
* `handler` is the enum name, not a number, so the file survives a reordering of
  the enum. A firmware built without MQTT rejects an MQTT device pack by name
  rather than misreading it.
* Payloads are strings, including the IR protocol number. An IR code is not a
  number you want a JSON parser to round.

The schema lives in [`schema/devicePack.schema.json`](../schema/devicePack.schema.json).
