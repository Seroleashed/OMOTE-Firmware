# OMOTE — Umbauplan „Konfigurierbar ohne Neukompilieren"

Arbeitsdokument. 28 Schritte in 7 Phasen. Jeder Schritt ist ein PR.
Abgehakt wird erst, wenn der PR auf `main` ist und alle Environments bauen.

Stand: 2026-09-19 · Basis-Commit `f19ad63`

---

## Querschnittsregeln (gelten für jeden Schritt)

- [ ] Jeder PR baut **alle** Environments (`esp32-Rev1toRev4`, `esp32-s3-Rev5andHigher`,
      `esp32-s3_testboard-Rev5andHigher`, `windows_linux`, `native_test`).
- [ ] Das **Standardverhalten bleibt unverändert**. Neue Funktionen liegen hinter
      `-D ENABLE_WEB_CONFIG`, `-D ENABLE_JSON_CONFIG`, `-D ENABLE_OTA`, die erst am
      Meilensteinende auf `1` gesetzt werden.
- [ ] Die **einkompilierte C++-Konfiguration bleibt dauerhaft Fallback**. JSON wird
      *zusätzlich* geladen, nie ersetzend. Das rettet bei jedem Parsefehler.
- [ ] Nach jedem Schritt `pio run --target size` gegen das Speicherbudget
      (freier Heap nach Boot, größter zusammenhängender Block) — Zahl in den PR schreiben.
- [ ] `pio test -e native_test` ist grün.
- [ ] Rev1–4 wird bis Schritt 26 über Flags mitgebaut, aber **nicht garantiert**.
      Wo es ohne Zusatzaufwand geht, bleibt es drin.

### Branch-Workflow

Jede in sich abgeschlossene Implementierung bekommt einen eigenen Feature-Branch, damit
jederzeit der Zustand vor dem jeweiligen Paket wiederherstellbar ist.

```
main
 └── feature/01-phase0-foundation        ← Patchserie aus new_files/
      └── feature/02-command-snapshot    ← baut darauf auf
           └── feature/03-…
```

Regeln:

- Namensschema `feature/<nr>-<kurzname>`, fortlaufend nummeriert.
- Ein Branch = ein in sich abgeschlossenes Paket = ein Eintrag in der Statusübersicht.
- Solange ein Paket noch nicht getestet und gemergt ist, zweigt das nächste vom
  **Kopf des Vorgängerbranches** ab (die Pakete bauen fachlich aufeinander auf).
- Getestet wird jeder Branch **vor** dem Merge nach `main`:
  `pio test -e native_test` plus `pio run` für alle Environments.
- Nach dem Merge nach `main` rebasen die offenen Folgebranches auf `main`.
- Zurückrollen: `git checkout feature/<nr-1>-…` bzw. `git revert` des Merge-Commits.

Aktuelle Branches siehe Spalte „Branch" in der Statusübersicht.

### Speicherbudget

Gemessen mit `pio run`, Angaben in Bytes. Rev5 wechselt mit Schritt 3 von
`noota_16MB_custom.csv` (12,5 MB App-Slot) auf `ota_16MB_custom.csv` (5 MB), deshalb
springt die Prozentzahl dort, obwohl die Firmware kaum wächst.

| Stand | Rev1–4 Flash | Rev1–4 RAM | Rev5 Flash | Rev5 RAM |
|---|---|---|---|---|
| `main` f19ad63 (Basis) | 1.934.785 (61,5 %) | 100.900 | 1.833.417 (14,0 %) | 63.608 |
| `feature/01` Phase 0 | 1.979.317 (62,9 %) | 100.988 | 1.875.953 (35,8 %) | 63.712 |
| `feature/02` Snapshot | unverändert (nur Tests) | | | |
| `feature/03` OTA/Version | 1.983.381 (63,0 %) | 100.988 | 1.880.233 (35,9 %) | 63.712 |
| `feature/04` Safe-Mode | 1.985.593 (63,1 %) | 101.012 | 1.882.361 (35,9 %) | 63.736 |
| `feature/05` system.json | 1.986.305 (63,1 %) | 101.012 | 1.883.073 (35,9 %) | 63.736 |

**Phase 0 kostet insgesamt rund 49 KB Flash und 130 Byte statisches RAM** — im
Wesentlichen ArduinoJson, `configStorage` und LittleFS. Der 5-MB-App-Slot ist zu
35,9 % gefüllt, es bleiben 3,3 MB für Web-UI, JSON-Renderer und WASM-Vorschau.

Rev1–4 liegt bei 63,1 % von `huge_app.csv` (3 MB) — noch entspannt, aber das ist die
Zahl, die ab Phase 4 zuerst anschlägt. Siehe Querschnittsregel zu Rev1–4.

Noch offen: freier Heap nach Boot und größter zusammenhängender Block. Beides ist nur
auf echter Hardware messbar (`get_heapUsage()` ist da, der Settings-Screen zeigt es mit
eingeschaltetem „Show mem usage").

---

## Statusübersicht

| # | Schritt | Phase | Status | Branch |
|---|---|---|---|---|
| 1 | Test-Environment und CI | 0 | ✅ getestet | `feature/01-phase0-foundation` |
| 2 | Erste Tests gegen bestehende Logik | 0 | ✅ getestet | `feature/01` + `feature/02-command-snapshot` |
| 3 | Neue Partitionstabelle | 0 | ✅ getestet | `feature/01-phase0-foundation` |
| 3b | Rollback-Absicherung, Versionsanzeige | 0 | ✅ getestet | `feature/03-ota-rollback-and-version` |
| 4 | Storage-Layer | 0 | ✅ getestet | `feature/01-phase0-foundation` |
| 4b | Safe-Mode | 0 | ✅ getestet | `feature/04-safe-mode` |
| 5 | Referenzen über stabile Namen | 1 | ✅ getestet | `feature/01-phase0-foundation` |
| 6 | Schema und Dateiaufteilung | 1 | 🟡 system+devices fertig, scenes+keys offen | `feature/05-config-files-and-flags` |
| 7 | Export des einkompilierten Zustands | 1 | 🟡 getestet, nur C++-Seite | `feature/01-phase0-foundation` |
| 8 | Geräte und Befehle aus JSON registrieren | 1 | ⬜ offen | |
| 9 | Sequenz-Engine für Szenen | 1 | ⬜ offen | |
| 10 | Zugangsdaten im NVS | 1 | ⬜ offen | |
| 11 | Transport-Abstraktion | 2 | ⬜ offen | |
| 12 | USB-Transport plus Host-Werkzeug | 2 | ⬜ offen | |
| 13 | BLE-Transport | 2 | ⬜ offen | |
| 14 | Gerätepakete | 2 | ⬜ offen | |
| 15 | JSON→LVGL-Renderer | 3 | ⬜ offen | |
| 16 | Mitgelieferte Screens auf JSON umstellen | 3 | ⬜ offen | |
| 17 | Konfigurationsmodus | 4 | ⬜ offen | |
| 18 | HTTP-Server und API | 4 | ⬜ offen | |
| 19 | Frontend-Grundgerüst | 4 | ⬜ offen | |
| 20 | Geräte-Editor mit IR-Anlernen | 4 | ⬜ offen | |
| 21 | Screen-Editor mit Vorschau | 4 | ⬜ offen | |
| 22 | Visueller Tasten-Mapper | 4 | ⬜ offen | |
| 23 | Zustands-Tracking | 5 | ⬜ offen | |
| 24 | Makros | 5 | ⬜ offen | |
| 25 | Home Assistant | 5 | ⬜ offen | |
| 26 | Log- und Diagnoseansicht | 5 | ⬜ offen | |
| 27 | OTA | 5 | ⬜ offen | |
| 28 | Härtung und Doku | 6 | ⬜ offen | |

Legende: ⬜ offen · 🟡 teilweise · ✅ Tests und alle Builds grün

> **Stand der Prüfung:** `pio test -e native_test` (102 Fälle) und `pio run` für
> `esp32-Rev1toRev4`, `esp32-s3-Rev5andHigher`, beide Testboard-Environments und
> `linux_64bit` laufen auf jedem Branch durch.
>
> **Was das nicht abdeckt:** alles, was echte Hardware braucht. Es liegt noch keine
> Fernbedienung vor, die Hardware-Prüfungen sammeln sich also an — das ist eine
> bewusste Entscheidung, aber die Liste wächst mit jedem Schritt und sollte nicht
> stillschweigend länger werden:
>
> | Schritt | offen auf Hardware |
> |---|---|
> | 3 | Bootet die neue Partitionstabelle? Einmalig `-t erase` nötig |
> | 4 | LittleFS mounten und beim ersten Start formatieren |
> | 3b | `Image: app0 (valid)` im Settings-Screen, `mark_app_valid` |
> | 4b | Safe-Mode nach drei abgewürgten Starts |
> | — | freier Heap nach Boot, größter Block (Budget-Tabelle) |
>
> **Ersatz, solange keine Hardware da ist:** `linux_64bit` ist der einzige Build, der
> das Zusammenspiel wirklich ausführt — `configFileSystem_hal_pc` schreibt nach
> `./omote_data/`, `bootCounter_hal_pc` legt dort seinen Zähler ab. Ab Schritt 8
> sollte jeder Branch zusätzlich im Simulator gestartet werden, nicht nur kompiliert.

---

## Reihenfolgenlogik

Harte Abhängigkeiten:

```
3 (Partitionen)  → muss früh, weil einmaliges Flash-Löschen
4 → 6 → 8        → Storage vor Schema vor Laden
5 → 6,7,8        → stabile Namen vor allem JSON-Bezogenen
9 → 24           → Sequenz-Engine vor Makros
15 → 16, 21      → Renderer vor JSON-Screens und WASM-Vorschau
23 → 24          → Zustände vor Bedingungen in Makros
3 → 27           → Partitionen lange stabil vor erstem OTA
17 → 18 → 19 → 20,21,22
11 → 12, 13      → Transportprotokoll vor den beiden Transporten
```

Alles andere ist verschiebbar. **Phase 2 lässt sich vorziehen**, wenn du früher ohne
Weboberfläche konfigurieren willst — dann ist nach Phase 0–2 bereits der größte
Alltagsgewinn erreicht (Fernbedienung ohne Neukompilieren konfigurierbar).

---

## Prüfergebnis der Dateien in `new_files/`

`new_files/files.zip` enthält fünf `git format-patch`-Dateien, die die Schritte 1–7
teilweise abdecken. **Geprüft:** Die Serie lässt sich in numerischer Reihenfolge
sauber auf `f19ad63` anwenden:

```bash
unzip new_files/files.zip -d /tmp/omote-patches
git checkout -b feat/config-phase0
git am /tmp/omote-patches/0001*.patch /tmp/omote-patches/0002*.patch \
       /tmp/omote-patches/0003*.patch /tmp/omote-patches/0004*.patch \
       /tmp/omote-patches/0005*.patch
```

Einzeln angewendet kollidieren 0002/0005 in der `platformio.ini` und 0004/0005
referenzieren Testdateien aus 0001 — die Serie muss also komplett und in Reihenfolge
laufen. Getestet wurde bisher nur die Anwendbarkeit; `pio test` und `pio run` sind
in dieser Umgebung nicht verfügbar und müssen von dir einmal lokal laufen.

### Was die Patches liefern

| Patch | Inhalt | deckt Schritt |
|---|---|---|
| 0001 | `[env:native_test]`, `test/omote_fakes.*`, `test/lvgl.h`, 26 Tests (command registry, scene registry, keys), Workflow `unit-tests.yml`, `flake.nix` + `.envrc`, Include-Guard in `IRremoteProtocols.h` | 1, 2 |
| 0002 | `ota_16MB_custom.csv` (app0/app1 je 5 MB, littlefs 5,875 MB, coredump), Rev5-Env auf neue Tabelle + `filesystem = littlefs` | 3 |
| 0003 | `ConfigFileSystem`-Interface + HALs für ESP32/PC, `configStorage` mit Envelope (Magic, Version, Länge, CRC32), `.tmp`→verify→`.bak`→rename, 14 Tests inkl. simuliertem Stromausfall, Verdrahtung in `main.cpp` | 4 |
| 0004 | `register_command()` als stringifizierendes Makro (alle 486 Call-Sites unverändert), `register_command_withName()`, Lookup in beide Richtungen, Iteration, 6 Tests | 5 |
| 0005 | `configModel` mit Device-Pack-Schema v1, strikte Validierung mit Nutzermeldungen, `devicePackFromRegisteredCommands()`, ArduinoJson 7 in allen Envs, 15 Tests | 6 (teilw.), 7 (teilw.) |

### Offene Punkte aus der Prüfung

Diese sind unten in die jeweiligen Schritte als Checklistenpunkte eingearbeitet:

1. **Feature-Flags fehlen komplett.** `ENABLE_JSON_CONFIG` / `ENABLE_WEB_CONFIG` /
   `ENABLE_OTA` kommen in keinem Patch vor. Bis Schritt 7 unkritisch (nichts ändert
   Laufzeitverhalten), ab Schritt 8 Pflicht. → Schritt 6.
2. **Snapshot-Test aus Schritt 2 fehlt.** Genau das Sicherheitsnetz für die gesamte
   Umbauphase. Sinnvollerweise jetzt auf Basis von `devicePackFromRegisteredCommands()`
   nachziehen. → Schritt 2/7.
3. **Aus Schritt 3 fehlen `esp_ota_mark_app_valid_cancel_rollback()` und die
   Versionsanzeige.** → eigener kleiner Schritt 3b, bevor Phase 1 startet.
4. **Safe-Mode beim Booten mit gedrückter Taste (Schritt 4) fehlt.** → Schritt 4b,
   spätestens zusammen mit Schritt 8 nötig, da erst dann JSON gelesen wird.
5. **Envelope-Format vs. Austauschformat.** `configStorage` schreibt eine Kopfzeile
   vor das JSON. Dateien auf LittleFS sind damit **kein gültiges JSON**. Folge für
   Schritt 11/12 (Transport) und 18/19 (Webserver): es muss festgelegt werden, dass
   Transport und API immer die *Payload* übertragen und die Hülle beim Schreiben neu
   gebildet wird. → Entscheidung in Schritt 6 festhalten.
6. **Handler-Namen sind buildabhängig.** `handlerToString`/`handlerFromString` sind
   per `#if` an `ENABLE_WIFI_AND_MQTT` / `ENABLE_KEYBOARD_BLE` gebunden. Ein
   MQTT-Gerätepaket auf einem Build ohne MQTT liefert korrekt „unknown handler",
   aber das muss in der UI als verständliche Meldung ankommen. → Schritt 14.
7. **CI:** Patch 0001 legt einen eigenen Workflow an, statt die drei bestehenden
   Build-Workflows zu erweitern. Das ist besser als geplant (schnelles Feedback ohne
   Toolchain-Download) — Plan hier bewusst angepasst.
8. **`noota_16MB_custom.csv`** bleibt als auskommentierte Referenz liegen.
   Aufräumen in Schritt 28.

### Beim ersten Compilerlauf gefunden (behoben)

9. **`omote_log.h` stand nicht für sich allein.** Die Makros expandieren zu
   `Serial.printf()` und `millis()`, ohne die dafür nötigen Deklarationen selbst
   einzubinden — das ging bisher nur gut, weil jeder Nutzer vorher
   `hardwarePresenter.h` inkludiert hatte. `configStorage.cpp` und `configModel.cpp`
   tun das nicht, **alle drei Firmware-Builds brachen ab**.
   Bemerkenswert ist, *warum* es durchrutschte: bei `OMOTE_LOG_LEVEL_NONE` expandieren
   die Makros zu `do {} while(0)` und referenzieren gar nichts. `env:native_test`
   benutzt genau dieses Level — 61 Tests grün, Firmware unkompilierbar.
   **Lehre für die weiteren Schritte: `pio test` allein beweist nichts.** Jeder Branch
   braucht zusätzlich `pio run` über alle Environments.
   → behoben in `feature/01`, `omote_log.h` inkludiert jetzt `arduinoLayer.h`.
10. **`linux_64bit` baut auf NixOS nicht** — auch auf unverändertem `main` nicht, also
    kein Problem dieses Umbaus. `SDL_image.h` macht intern ein schlichtes
    `#include "SDL.h"`, der Nix-Compiler-Wrapper legt aber nur `…/include` auf den
    Pfad, nicht `…/include/SDL2`. → behoben im `shellHook` des flakes über
    `pkg-config`; `platformio.ini` bleibt unangetastet und damit portabel.

---

# Phase 0 — Fundament (Schritte 1–4)

**Ergebnis:** Tests laufen, OTA-fähige Partitionen, sicherer Speicher · **2–3 Wochen**

## Schritt 1 — Test-Environment und CI 🟡

**Ziel:** Hardwareunabhängige Module nativ testbar machen.

- [ ] `[env:native_test]` in `platformio.ini` (platform `native`, Unity, ohne SDL)
- [ ] `test/omote_fakes.{h,cpp}` — aufzeichnende Fakes für die `hardwarePresenter.h`-Fassade,
      den Scene-Handler und die GUI-Schicht; steuerbare Uhr; gepollte Keypad-Attrappe
- [ ] `test/lvgl.h` — minimaler Stub, damit GUI-Header nativ kompilieren
- [ ] `test/README.md` — Konventionen (Uhr gehört dem Test, Keypad wird gepollt,
      `fakes::reset()` in `setUp()`, Assertions auf aufgezeichnete Calls statt Logausgabe)
- [ ] CI-Job `pio test -e native_test` (eigener Workflow `unit-tests.yml`)
- [ ] `pio test -e native_test` lokal grün

**Anmerkung:** Der Patch bringt zusätzlich `flake.nix` + `.envrc`. Die NixOS-Umgebung
verwaltest du selbst — prüfe, ob du die Dateien im Repo haben willst, oder ob sie
in `.gitignore` gehören.

## Schritt 2 — Erste Tests gegen bestehende Logik 🟡

**Ziel:** Das heutige Verhalten festnageln, bevor irgendetwas umgebaut wird.

- [ ] `test/test_command_registry/` — Registrierung, `executeCommand`-Dispatch pro
      Handler-Typ (IR, MQTT, Scene, GUI, Special)
- [ ] `test/test_scene_registry/` — Lookup, Fallbacks, `get_gui_list_withFallback`,
      Kette GUI → Scene → Default
- [ ] `test/test_keys/` — `SHORT`, `SHORT_REPEATED`, `SHORTorLONG` mit simulierten
      Zeitstempeln, Haltezeit, Wiederholrate
- [ ] **Offen: Snapshot-Test** — gibt alle registrierten Befehle samt Payloads als
      Textdatei aus, Diff gegen eingecheckte Referenz. Das Sicherheitsnetz für die
      gesamte Umbauphase. Baut praktischerweise auf Schritt 5 auf → kann als Teil von
      Schritt 5 oder 7 nachgezogen werden, muss aber **vor** Schritt 8 existieren.
- [ ] Referenz-Snapshot eingecheckt, CI schlägt bei Abweichung fehl

## Schritt 3 — Neue Partitionstabelle 🟡

**Ziel:** Das einmalige vollständige Löschen per USB jetzt erledigen, nicht später.

- [ ] `ota_16MB_custom.csv`: `nvs`, `otadata`, `app0` 5 MB, `app1` 5 MB,
      `littlefs` ≈5,9 MB, `coredump`
- [ ] Rev5-Env nutzt die neue Tabelle und `board_build.filesystem = littlefs`
- [ ] Rev1–4 bleibt bei `huge_app.csv` (kein OTA dort)
- [ ] Migrationshinweis im CSV-Kopf und im README: `pio run -t erase` + `upload`
- [ ] Verifiziert: Firmware passt mit Reserve in 5 MB (aktuell ≈2 MB)

### Schritt 3b — Rollback-Absicherung und Versionsanzeige ⬜ *(neu, aus Schritt 3 herausgelöst)*

- [ ] `esp_ota_mark_app_valid_cancel_rollback()` nach erfolgreichem Boot aufrufen
- [ ] Firmware-Version (Git-Beschreibung, Build-Datum) einkompilieren und im
      Settings-Screen anzeigen
- [ ] `esp_ota_get_state_partition()` im Log ausgeben, damit ein Rollback sichtbar wird

## Schritt 4 — Storage-Layer 🟡

**Ziel:** Schreiben, das einen Stromausfall überlebt.

- [ ] LittleFS mounten, Formatierung beim ersten Start
- [ ] Atomares Schreiben: `.tmp` schreiben, zurücklesen und verifizieren, `fsync`,
      alte Datei nach `.bak` rotieren, dann umbenennen
- [ ] Envelope mit Magic, Schema-Version, Länge, CRC32
- [ ] `load()` liefert `Ok` / `OkFromBackup` / `NotFound` / `Corrupt`
- [ ] `ConfigFileSystem`-Interface mit HALs für ESP32 (LittleFS) und PC (lokaler Ordner),
      damit die Logik nativ testbar bleibt
- [ ] Tests: simulierter Stromausfall mitten im Schreiben, Stromausfall zwischen Backup
      und Aktivierung, Bitflips, abgeschnittene Dateien, CRC32-Vektor
- [ ] **Offen: Safe-Mode** beim Booten mit gedrückter Taste — überspringt das Laden der
      JSON-Konfiguration und meldet das auf dem Display. → Schritt 4b

### Schritt 4b — Safe-Mode ⬜ *(neu, aus Schritt 4 herausgelöst)*

- [ ] Taste beim Boot abfragen (vor dem ersten JSON-Zugriff)
- [ ] Safe-Mode: nur einkompilierte Konfiguration, deutlicher Hinweis auf dem Display
- [ ] Im Safe-Mode: „Konfiguration zurücksetzen" erreichbar
- [ ] Muss vor Schritt 8 stehen (erst dann wird JSON überhaupt gelesen)

---

# Phase 1 — Konfigurationsmodell (Schritte 5–10)

**Ergebnis:** Konfiguration vollständig als JSON abbildbar · **3–4 Wochen**

## Schritt 5 — Referenzen über stabile Namen 🟡

**Ziel:** Befehls-IDs (`uint16_t`, in Registrierungsreihenfolge vergeben) sind als
Referenz in Dateien unbrauchbar — sie verschieben sich, sobald ein Gerät dazukommt.

- [ ] `register_command()` wird ein Makro, das den Variablennamen stringifiziert →
      alle bestehenden Call-Sites bleiben unverändert und bekommen automatisch einen
      Namen wie `SAMSUNG_POWER`
- [ ] `register_command_withName()` für zur Laufzeit aus JSON erzeugte Befehle
- [ ] Lookup Name→ID und ID→Name, Iteration über alle Befehle
- [ ] Doppelte Registrierung eines Namens verschiebt ihn auf den neuen Befehl; die alte
      ID funktioniert weiter, verliert aber ihren Namen (der JSON-überschreibt-Fall)
- [ ] **Der Snapshot-Test aus Schritt 2 läuft unverändert durch** — keine Verhaltensänderung

## Schritt 6 — Schema und Dateiaufteilung 🟡

**Ziel:** Aufteilung statt einer Monsterdatei, weil das Teil-Import und -Export erst ermöglicht.

Aufgeteilt auf zwei Branches, weil vier Dateiformate in einem PR niemand mehr
sinnvoll durchsieht:

**`feature/05-config-files-and-flags` — erledigt**

- [x] Gemeinsamer Rahmen `configFile`: Pfade, Typ-Strings, `schemaVersion` + `type`
      für alle Dateien, mit Fehlermeldungen, die beide Typen benennen
- [x] `/cfg/devices/<id>.json` — ein Gerät mit allen Befehlen; zugleich das Austauschformat
- [x] `/cfg/system.json` — Gerätename, Sleep, Helligkeit, MQTT-Broker
- [x] WLAN- und MQTT-Zugangsdaten **nicht** in der Datei (Test beweist es) → NVS in Schritt 10
- [x] Fehlendes Feld behält den übergebenen Wert; Feld mit falschem Typ wird ignoriert
      statt genullt (sonst schwarzes Display nach einem Tippfehler)
- [x] **Entscheidung festgehalten** in `configFile.h`: der `configStorage`-Envelope
      (Magic, Länge, CRC) ist reines Speicherformat. Transport (Schritt 11) und
      HTTP-API (Schritt 18) übertragen immer die nackte Payload; die Hülle wird beim
      Schreiben auf dem Gerät neu gebildet. Eine vom Gerät gezogene Datei ist damit
      gültiges JSON, das jeder Editor öffnet.
- [x] **Feature-Flags:** `ENABLE_JSON_CONFIG`, `ENABLE_WEB_CONFIG`, `ENABLE_OTA`,
      alle auf `0`
- [x] 17 Tests (Round-Trip, Teildatei, falscher Typ, fremder Dateityp, neuere Version,
      keine Zugangsdaten im Export)

**`feature/06-scenes-and-keys` — offen**

- [ ] `/cfg/scenes.json` — Name, `key_repeatModes`, `key_commands_short`,
      `key_commands_long`, Start-/End-Sequenz, `gui_list`, Aktivierungsbefehl
- [ ] `/cfg/keys.json` — die 5×5-Matrix; heute sind die 24 `KEY_*` einzelne `char`
      (`KEY_OK = 'k'`), die Datei braucht stabile Namen statt Zeichen
- [ ] `/cfg/ui.json` — **bewusst auf Schritt 15 verschoben.** Der Inhalt ist der
      Widget-Satz des Renderers; ihn jetzt zu erfinden hieße, ihn zweimal zu bauen.
      Typ und Pfad sind in `configFile.h` schon reserviert.
- [ ] Migrationsfunktion je Datei: der Mechanismus steht (`parseAndCheckEnvelope`
      reicht die gefundene Version an den Parser durch), die erste echte Migration
      kommt mit der ersten Schemaänderung. Kein Vorrat auf Verdacht.
- [ ] Test „zu große Datei" — sinnvoll erst mit einem Größenlimit, das aus dem
      Transport (Schritt 11) kommt

## Schritt 7 — Export des einkompilierten Zustands 🟡

**Ziel:** Sofort realistische Testdaten und das Werkzeug, um `devices_pool` zu konvertieren.

- [ ] `devicePackFromRegisteredCommands()` — Gerätepaket aus den registrierten Befehlen,
      optional nach Namenspräfix gefiltert 🟡 *(vorhanden)*
- [ ] **Offen:** `dumpConfigAsJson()` über Serial aufrufbar (Geräte, Szenen, Tastenbelegungen)
- [ ] **Offen:** Python-Skript (mit `uv`) validiert die Ausgabe gegen ein JSON-Schema
- [ ] **Offen:** `schema/devicePack.schema.json` und die weiteren Schemadateien einchecken
- [ ] **Offen:** Skript wandelt den kompletten `src/devices_pool/` (aktuell 9 Geräte:
      boseAmp, denonAvr, lgsoundbar, lgbluray, samsungbluray, shield, airconditioner,
      lgTV, sonyTV) in eine mitgelieferte Gerätebibliothek unter `devices_library/` um
- [ ] Round-Trip-Test: Export → JSON → Import ergibt identische Befehlsliste

## Schritt 8 — Geräte und Befehle aus JSON registrieren ⬜

**Ziel:** Ab hier ein IR-Gerät ohne Neukompilieren hinzufügen.

- [ ] Laden **nach** den C++-Registrierungen beim Start
- [ ] Namenskonflikt: JSON gewinnt, Warnung ins Log
- [ ] Defektes oder fehlendes JSON: einkompilierte Konfiguration bleibt aktiv,
      Fehlermeldung wird für die spätere UI aufgehoben
- [ ] Hinter `ENABLE_JSON_CONFIG`
- [ ] Safe-Mode (4b) überspringt diesen Pfad
- [ ] Snapshot-Test: ohne JSON-Dateien identisch zum Referenz-Snapshot
- [ ] Test: JSON-Gerät überschreibt ein einkompiliertes Gerät korrekt
- [ ] Speichermessung mit 10 geladenen Geräten

## Schritt 9 — Sequenz-Engine für Szenen ⬜

**Ziel:** `delay()` raus aus den Szenen — derselbe Kern, den Schritt 24 für Makros erweitert.

- [ ] `scene_start_sequence_*` wird zur Datenstruktur: Liste aus
      `{command, payload, delayAfter}`
- [ ] Engine läuft nicht-blockierend über die Hauptschleife
- [ ] Bestehende Szenen (`scene_TV`, `scene_appleTV`, `scene_chromecast`, `scene_fireTV`,
      `scene_allOff`) auf die Engine umgestellt — Timing bleibt identisch
- [ ] Tests mit simulierter Uhr: Ablauf, Abbruch, Verschachtelung
- [ ] Manuell verifiziert: UI friert während einer Szene nicht mehr ein

## Schritt 10 — Zugangsdaten im NVS ⬜

- [ ] `src/secrets.h` bleibt als Kompilier-Default
- [ ] Setzen und Löschen von WLAN-/MQTT-Zugangsdaten im NVS (API kommt in Schritt 18)
- [ ] Reihenfolge: NVS gewinnt über `secrets.h`
- [ ] AP-Modus-Fallback, wenn keine WLAN-Daten vorhanden sind **oder** die Verbindung
      dreimal scheitert
- [ ] Passwörter werden nie zurückgelesen, nur gesetzt oder gelöscht
- [ ] Test: Fallback-Kette NVS → `secrets.h` → AP-Modus

---

# Phase 2 — Import und Export über USB und Bluetooth (Schritte 11–14)

**Ergebnis:** Import/Export über USB und BLE, Gerätepakete · **2 Wochen**

> Diese Phase lässt sich vorziehen, wenn du früher ohne Weboberfläche konfigurieren willst.

## Schritt 11 — Transport-Abstraktion ⬜

**Ziel:** Ein schlankes, zeilenbasiertes Protokoll über einem beliebigen Bytestrom.
Der Transport kennt kein JSON, er überträgt nur Dateien.

- [ ] Befehle: `LIST`, `GET <pfad>`, `PUT <pfad> <länge> <crc>`, `DEL`, `APPLY`, `INFO`, `REBOOT`
- [ ] Chunking mit Quittungen (BLE hat nur kleine MTUs)
- [ ] CRC32 pro Chunk (`configStorage::crc32` wiederverwenden)
- [ ] Payload ohne Envelope (Entscheidung aus Schritt 6)
- [ ] Vollständig nativ testbar: Protokollparser gegen Bytestrom-Attrappe
- [ ] Tests: abgebrochene Übertragung, falsche CRC, unbekannter Befehl, Pfad-Traversal

## Schritt 12 — USB-Transport plus Host-Werkzeug ⬜

- [ ] S3: natives USB-CDC
- [ ] Rev1–4: derselbe Code über den UART-Brücken-Chip (leichter Bonus für ältere Revisionen)
- [ ] CLI in Python mit `uv`: `omotectl pull`, `omotectl push`,
      `omotectl device export samsung-tv`
- [ ] WebSerial-Anbindung im Browser — damit ist die Web-UI später auch ohne WLAN nutzbar
- [ ] End-to-End manuell: Gerät exportieren, Datei ändern, zurückspielen, Neustart

## Schritt 13 — BLE-Transport ⬜

- [ ] Zusätzlicher NimBLE-Service **parallel** zum bestehenden HID-Profil
- [ ] Nur aktiv im Konfigurationsmodus (Schritt 17)
- [ ] Pairing-Bestätigung über eine auf dem Display angezeigte PIN
- [ ] ⚠️ Speicher wird hier eng: `pio run --target size` **vorher und nachher** vergleichen
      und beide Zahlen in den PR schreiben
- [ ] Verifiziert: HID-Tastatur funktioniert weiterhin parallel

## Schritt 14 — Gerätepakete ⬜

- [ ] Metadaten: Hersteller, Modell, Protokoll, Autor, Schema-Version
- [ ] Ein Gerät als eigenständige Datei exportieren und importieren
- [ ] Kollisionsbehandlung bei Namen (überschreiben / umbenennen / abbrechen)
- [ ] Vorschau vor dem Übernehmen
- [ ] Verständliche Meldung, wenn ein Paket einen Handler nutzt, den dieser Build nicht
      kennt (z. B. MQTT-Gerät auf Build ohne `ENABLE_WIFI_AND_MQTT`)
- [ ] Die konvertierte `devices_library/` aus Schritt 7 lässt sich importieren

---

# Phase 3 — UI-Engine (Schritte 15–16)

**Ergebnis:** Screens aus JSON · **2–3 Wochen** · ⚠️ größtes Risiko, großzügig planen

## Schritt 15 — JSON→LVGL-Renderer ⬜

- [ ] Bewusst kleiner Widget-Satz: Grid-Container, Button mit Label oder Icon, Label,
      Slider, Arc, Liste, Statuszeile
- [ ] Jedes Widget bindet an einen **Befehlsnamen** (Schritt 5)
- [ ] `guiMemoryOptimizer` mit seinen drei Tabs bleibt unangetastet; dynamische Screens
      fügen sich dort ein
- [ ] Unbekanntes Widget / fehlender Befehl: Platzhalter statt Absturz
- [ ] Tests: Rendern im Simulator plus Screenshot-Vergleich in der CI
- [ ] Speichermessung mit dem größten realistischen Screen

## Schritt 16 — Mitgelieferte Screens auf JSON umstellen ⬜

- [ ] `gui_numpad` als Default-JSON
- [ ] `gui_sceneSelection` als Default-JSON
- [ ] Handgeschriebene Varianten bleiben als Referenz im Repo
- [ ] Screenshot-Test beweist Gleichheit (pixelgleich)

---

# Phase 4 — Weboberfläche (Schritte 17–22)

**Ergebnis:** Weboberfläche mit Editor, Vorschau, Tasten-Mapper · **5–7 Wochen**

## Schritt 17 — Konfigurationsmodus ⬜

- [ ] Explizit startbar über Menü **oder** Tastenkombination
- [ ] Unterdrückt Deep Sleep
- [ ] Zeigt IP, Hostname und PIN auf dem Display
- [ ] Eigenes Timeout
- [ ] Warnung bei niedrigem Akku
- [ ] mDNS `omote.local` (Basis für Schritt 25)

## Schritt 18 — HTTP-Server und API ⬜

- [ ] Gepflegter AsyncWebServer-Fork (Auswahl im PR begründen)
- [ ] REST für die Konfiguration, WebSocket für Livedaten
- [ ] Authentifizierung über ein Token, das aus der Display-PIN abgeleitet wird
- [ ] Passwörter werden nie zurückgegeben
- [ ] CORS erlaubt, damit ein extern gehostetes Frontend gegen das Gerät arbeiten kann
- [ ] API liefert Payload ohne Envelope (Entscheidung aus Schritt 6)
- [ ] Hinter `ENABLE_WEB_CONFIG`, nur im Konfigurationsmodus aktiv
- [ ] Speichermessung mit aktivem Server

## Schritt 19 — Frontend-Grundgerüst ⬜

- [ ] Vite mit Svelte oder Preact über `pnpm`
- [ ] Gzipptes Build wird per PlatformIO-`extra_script` ins LittleFS-Image gepackt
- [ ] Erst nur Anzeigen und Bearbeiten von `system.json` und den Geräten
- [ ] CI-Job baut das Frontend
- [ ] Größe des Bundles gegen die littlefs-Partition prüfen

## Schritt 20 — Geräte-Editor mit IR-Anlernen ⬜

- [ ] IR-Empfänger liefert statt eines lesbaren Strings eine **strukturierte** Ausgabe:
      Protokoll, Wert, Bits, Raw-Timings bei unbekannten Codes
- [ ] Ausgabe per WebSocket in den Browser
- [ ] Im Browser: Lernen, Testsenden, Duplikaterkennung, direkte Zuordnung zu einem
      Funktionsnamen
- [ ] `gui_irReceiver` auf dem Gerät weiterhin funktionsfähig

## Schritt 21 — Screen-Editor mit Vorschau ⬜

- [ ] Renderer aus Schritt 15 zusätzlich per Emscripten nach WebAssembly bauen
- [ ] Eigener CI-Job für den WASM-Build
- [ ] Vorschau im Browser ist pixelgleich mit dem Gerät
- [ ] Fallback: fällt das WASM-Bundle zu groß aus, aus dem extern gehosteten Frontend
      ausliefern statt aus dem Flash

## Schritt 22 — Visueller Tasten-Mapper ⬜

- [ ] SVG-Overlay-Karte über einem Render der Rev5-Tastatur
- [ ] 5×5-Matrix mit ihren `char`-Kennungen wird zu `keys.json`
- [ ] Taste am Gerät drücken markiert sie im Browser (über WebSocket)
- [ ] Kurz- und Langdruck samt Repeat-Modus pro Taste getrennt belegbar
- [ ] Die Repeat-Modi aus Schritt 2 (`SHORT`, `SHORT_REPEATED`, `SHORTorLONG`) sind
      vollständig abgebildet

---

# Phase 5 — Laufzeitfunktionen (Schritte 23–27)

**Ergebnis:** Makros, Zustände, HA, Diagnose, OTA · **3–4 Wochen**

## Schritt 23 — Zustands-Tracking ⬜

- [ ] State-Engine mit Variablen pro Gerät, z. B. `tv.power` ∈ {`on`, `off`, `unknown`}
- [ ] Quellen: gesendete Befehle (optimistisch), MQTT-Rückmeldungen (verlässlich),
      optional empfangene IR-Signale der Originalfernbedienung
- [ ] Zustand überlebt Deep Sleep im RTC-Speicher
- [ ] Beim Einschlafen in den NVS schreiben
- [ ] **Ehrliches `unknown` nach Stromverlust** — sonst frustriert das Feature mehr als
      es hilft
- [ ] Manuelle Korrektur in der UI

## Schritt 24 — Makros ⬜

- [ ] Sequenz-Engine aus Schritt 9 erweitern um: Bedingungen (`if tv.power != on`),
      Wartezeiten, Wiederholungen, Setzen von Zuständen
- [ ] Szenen werden zum Spezialfall von Makros
- [ ] Engine bleibt nicht-blockierend
- [ ] Vollständig nativ testbar (hier besonders wertvoll)
- [ ] Endlosschleifen-Schutz

## Schritt 25 — Home Assistant ⬜

- [ ] MQTT-Discovery mit `device_trigger` für Tastendrücke
- [ ] Sensoren für Akku, Heap und RSSI
- [ ] Buttons für Befehle und Szenen
- [ ] Verfügbarkeitsmeldung per Last Will
- [ ] Verifiziert: Fernbedienung taucht automatisch in HA auf und kann Automationen auslösen

## Schritt 26 — Log- und Diagnoseansicht ⬜

- [ ] `omote_log_*`-Makros bekommen eine zusätzliche Senke: Ringpuffer im RAM
- [ ] `/api/diag`: Heap (`get_heapUsage` existiert bereits), Akkuspannung und -stand,
      RSSI, Uptime, Wakeup-Grund, Coredump-Status, freier Flash
- [ ] Frontend: Livediagramm
- [ ] Gerät: schlichter Log-Screen
- [ ] Ab hier ist Rev1–4 nicht mehr garantiert (siehe Querschnittsregeln)

## Schritt 27 — OTA ⬜

- [ ] Upload über die Web-UI
- [ ] Prüfsumme vor dem Schreiben
- [ ] Schreiben nach `app1`, Neustart
- [ ] `mark_app_valid` erst nach erfolgreichem Boot **plus** bestandenem Selbsttest,
      sonst automatischer Rollback (Basis in 3b gelegt)
- [ ] Verifiziert: Konfiguration bleibt bei Updates erhalten (eigene Partition)
- [ ] Optional: Signaturprüfung, falls du Geräte an Dritte gibst
- [ ] Hinter `ENABLE_OTA`

---

# Phase 6 — Abschluss (Schritt 28)

**Ergebnis:** Härtung und Doku · **1–2 Wochen**

## Schritt 28 — Härtung und Doku ⬜

- [ ] Fuzzing des JSON-Parsers mit zufälligen und abgeschnittenen Eingaben
- [ ] Stromausfall-Tests über den **gesamten** Konfigurationspfad
- [ ] Messung des Speicherbudgets in allen Modi (normal, Konfigurationsmodus, OTA)
- [ ] Dokumentation im Wiki-Stil
- [ ] Klarer Migrationspfad für Bestandsnutzer inkl. des einmaligen Löschvorgangs aus Schritt 3
- [ ] Aufräumen: `noota_16MB_custom.csv` entfernen, tote Referenzen, auskommentierte Blöcke
- [ ] Feature-Flags auf `1` setzen, wo sie es dauerhaft bleiben sollen

---

## Aufwandsübersicht

| Phase | Ergebnis | Aufwand |
|---|---|---|
| 0 | Tests laufen, OTA-fähige Partitionen, sicherer Speicher | 2–3 Wochen |
| 1 | Konfiguration vollständig als JSON abbildbar | 3–4 Wochen |
| 2 | Import/Export über USB und BLE, Gerätepakete | 2 Wochen |
| 3 | Screens aus JSON | 2–3 Wochen |
| 4 | Weboberfläche mit Editor, Vorschau, Tasten-Mapper | 5–7 Wochen |
| 5 | Makros, Zustände, HA, Diagnose, OTA | 3–4 Wochen |
| 6 | Härtung und Doku | 1–2 Wochen |
| **Summe** | | **18–25 Personenwochen** |

Die Phasen 0 bis 2 lohnen sich auch dann, wenn du irgendwann abbrichst: Danach ist die
Fernbedienung ohne Neukompilieren konfigurierbar, und das ist bereits der größte
Alltagsgewinn.

---

## Nächster konkreter Schritt

Phase 0 ist implementiert, Tests und Builds sind auf allen vier Branches grün.
Was jetzt noch fehlt, geht nur mit der Fernbedienung in der Hand:

1. **Einmalig löschen und flashen.** Branch 01 wechselt die Partitionstabelle:
   ```bash
   pio run -e esp32-s3-Rev5andHigher -t erase
   pio run -e esp32-s3-Rev5andHigher -t upload
   ```
   Danach bleibt das Layout stabil, OTA und Konfiguration überleben jedes Update.
2. **Schritt 4 auf dem Gerät:** Bootet es sauber? Im Log muss LittleFS beim ersten
   Start formatiert und gemountet werden (`configFileSystem: littlefs mounted`).
3. **Schritt 3b:** Serial-Log zeigt `OMOTE 0.9.0-dev (…), running from 'app0'
   (valid), OTA capable`; im Settings-Screen steht die Firmware-Box.
4. **Schritt 4b:** Schalter „Safe mode next start" umlegen, neu starten →
   `Config: safe mode (requested)`. Gegenprobe: das Gerät dreimal während des Starts
   vom Strom nehmen, der vierte Start muss `safe mode (repeated crash)` melden.
5. **Budget-Tabelle vervollständigen:** freier Heap nach Boot und größter Block,
   abzulesen über „Show mem usage" im Settings-Screen.
6. Erst danach Phase 1 fortsetzen: Schritt 6 (restliche Schemadateien +
   Feature-Flags), dann Schritt 7 (Serial-Dump + Python-Werkzeug), dann Schritt 8
   (JSON-Registrierung, erster Konsument von `bootGuard::isSafeMode()`).
