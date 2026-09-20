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
- Getestet wird jeder Branch **vor** dem Merge nach `main` mit
  `nix develop --command tools/run-checks.sh` (siehe unten).
- Nach dem Merge nach `main` rebasen die offenen Folgebranches auf `main`.
- Zurückrollen: `git checkout feature/<nr-1>-…` bzw. `git revert` des Merge-Commits.

### Prüfen: `tools/run-checks.sh`

```bash
nix develop --command tools/run-checks.sh          # Tests + alle Environments
nix develop --command tools/run-checks.sh --tests  # nur Tests, schnelle Runde
nix develop --command tools/run-checks.sh --sim    # zusätzlich Simulator starten
```

Das Skript existiert, weil drei Fehler beim Prüfen von Hand teuer waren:

1. **`pio run … | grep …` liefert den Exit-Code von `grep`.** Ein fehlgeschlagener
   Build meldet damit Erfolg. Das ist einmal passiert und wäre beinahe gemergt worden.
   Das Skript setzt `set -o pipefail` und gibt einen echten Exit-Code zurück.
2. **Zwei gleichzeitige `pio`-Läufe streiten um `.pio/build`** und scheitern auf eine
   Art, die wie ein echter Fehler aussieht. Im Skript läuft alles nacheinander.
3. **Der Simulator ignoriert SIGTERM und puffert blockweise.** `timeout 6 program >
   log` liefert deshalb eine leere Datei. Es braucht `timeout -s KILL` **und**
   `stdbuf -oL` — beides steht im Skript.

Die vollständige Ausgabe landet unter `.pio/checks/<env>.log`; auf dem Terminal steht
nur die Zusammenfassung. Ein Fehlschlag lässt sich also nachlesen, statt reproduziert
werden zu müssen.

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
| `feature/06` scenes+keys | 1.987.941 (63,2 %) | 101.012 | 1.884.713 (35,9 %) | 63.736 |
| `feature/07` Export | 2.008.613 (63,9 %) | 101.052 | 1.905.317 (36,3 %) | 63.760 |
| `feature/08` JSON-Loader | 2.010.097 (63,9 %) | 101.068 | 1.906.833 (36,4 %) | 63.784 |
| `feature/09` Sequenz-Engine | 2.015.453 (64,1 %) | 101.116 | 1.912.289 (36,5 %) | 63.832 |
| `feature/10` Zugangsdaten | 2.021.457 (64,3 %) | 101.132 | 1.918.453 (36,6 %) | 63.864 |
| `feature/11` Transport | 2.022.057 (64,3 %) | 101.132 | 1.919.057 (36,6 %) | 63.864 |
| `feature/12` USB + omotectl | 2.035.521 (64,7 %) | — | 1.932.741 (36,9 %) | — |
| `feature/13` system.json wirkt | 2.035.841 (64,7 %) | — | 1.933.061 (36,9 %) | — |

⚠️ Schritt 7 kostet **20,6 KB Flash** — der größte Sprung seit Phase 0. Grund ist der
Serial-Dump im Settings-Screen: er zieht `configExport` samt Serialisierung aller vier
Dateitypen in die Firmware. Sobald Schritt 18 die HTTP-API bringt, braucht die Firmware
das ohnehin. Falls Rev1–4 eng wird, ist dieser Button der erste Kandidat für
`#if (ENABLE_JSON_CONFIG == 1)`.

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
| 6 | Schema und Dateiaufteilung | 1 | ✅ getestet (ui.json → Schritt 15) | `feature/05` + `feature/06-scenes-and-keys` |
| 7 | Export des einkompilierten Zustands | 1 | ✅ getestet | `feature/07-config-export` |
| 8 | Geräte und Befehle aus JSON registrieren | 1 | ✅ getestet, im Simulator verifiziert | `feature/08-json-device-loader` |
| 9 | Sequenz-Engine für Szenen | 1 | ✅ getestet | `feature/09-sequence-engine` |
| 10 | Zugangsdaten im NVS | 1 | ✅ getestet | `feature/10-credentials-nvs` |
| 11 | Transport-Abstraktion | 2 | ✅ getestet | `feature/11-transport` |
| 12 | USB-Transport plus Host-Werkzeug | 2 | ✅ gegen Simulator verifiziert | `feature/12-usb-transport` |
| 8b | `system.json` anwenden | 1 | ✅ gegen Simulator verifiziert | `feature/13-apply-system-config` |
| 8c | `scenes.json` und `keys.json` anwenden | 1 | ⬜ offen | |
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

> **Stand der Prüfung:** `pio test -e native_test` (242 Fälle) und `pio run` für
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
> | 8 | Freier Heap mit 10 aus JSON geladenen Geräten |
> | 9 | Display baut sich während einer laufenden Szene weiter auf |
> | 10 | AP `OMOTE-setup` kommt hoch, NVS überlebt einen Neustart |
> | 12 | Kommt `Serial` unter Last mit dem Protokoll mit? |
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

**`feature/06-scenes-and-keys` — erledigt**

- [x] `keyNames` — stabile Namen für die 24 Tasten. Die Tabelle zeigt auf die
      `KEY_*`-**Variablen**, nicht auf Kopien ihrer Werte, damit ein späteres Remapping
      mitgeht statt still zu veralten.
- [x] `/cfg/scenes.json` — Tastenbelegung mit Repeat-Modus, Kurz- und Langbefehl,
      `guiList`, Aktivierungsbefehl, Start-/End-Sequenz
- [x] Die Sequenz ist bereits die `{command, payload, delayAfter}`-Struktur, die die
      Engine aus Schritt 9 ausführt — das Dateiformat muss dafür nicht noch einmal
      geändert werden
- [x] `/cfg/keys.json` — die 5×5-Matrix, **mit Hardware-Revision in der Datei**:
      Rev5 und Rev1–4 haben dieselben Tasten in *umgekehrter Zeilenreihenfolge*
      (`keypad_keys_hal_esp32.cpp`). Die falsche Datei zu importieren würde das
      Tastenfeld spiegeln. Eine Abweichung wird gemeldet, nicht abgelehnt — die
      Web-UI kann dann anbieten, die Zeilen zu drehen.
- [x] Abgelehnt **mit Namen**: unbekannte Taste, unbekannter Repeat-Modus, Langbefehl
      auf einer Taste ohne `SHORTorLONG` (könnte nie auslösen), doppelter Szenenname,
      falsche Matrixgröße, dieselbe Taste auf zwei Positionen
- [x] 25 Tests

**Bewusst nicht in Schritt 6:**

- `/cfg/ui.json` → **Schritt 15.** Der Inhalt ist der Widget-Satz des Renderers;
  ihn jetzt zu erfinden hieße, ihn zweimal zu bauen. Typ und Pfad sind in
  `configFile.h` reserviert.
- Migrationsfunktionen: der Mechanismus steht (`parseAndCheckEnvelope` reicht die
  gefundene Version an den Parser durch, beide Parser haben die Stelle markiert),
  die erste echte Migration kommt mit der ersten Schemaänderung. Kein Vorrat auf
  Verdacht.
- Test „zu große Datei" → sinnvoll erst mit einem Größenlimit aus Schritt 11.

## Schritt 7 — Export des einkompilierten Zustands ✅

**Ziel:** Sofort realistische Testdaten und das Werkzeug, um `devices_pool` zu konvertieren.

- [x] `devicePackFromRegisteredCommands()` — Gerätepaket aus den registrierten Befehlen,
      nach Namenspräfix gefiltert
- [x] `configExport::scenes()` und `::keys()` aus den Live-Registries
- [x] `get_keypadMatrix()` über die Hardware-Fassade — sonst bräuchte der Export eine
      zweite Kopie des Layouts, die von der ersten wegdriftet
- [x] `dumpConfigAsJson()` über Serial, Button im Settings-Screen. BEGIN/END-Marker
      pro Datei, damit ein Host-Skript die Dateien aus einem Serial-Log schneiden kann
- [x] `schema/` mit vier JSON-Schemas
- [x] `tools/validate_config.py` (uv, jsonschema) wählt das Schema am `type`-Feld
- [x] `[env:config_export]` — natives Werkzeug, registriert den kompletten
      `devices_pool` und schreibt `devices_library/*.json`. Liest jede Datei vor dem
      Schreiben wieder ein: eine Datei, die der Exporter selbst nicht parsen kann,
      gibt man niemandem
- [x] **12 Geräte, 235 Befehle** in `devices_library/`, alle gegen das Schema validiert
- [x] CI prüft Schema-Validität **und** dass die Bibliothek aktuell ist
- [x] Round-Trip-Tests: Gerät, Szene und Matrix je Export → JSON → Import

**Was der Export in den bestehenden Quellen gefunden hat:**

1. **`device_denonAvr` registriert nichts.** Alle 41 `register_command()`-Zeilen sind
   auskommentiert. Das Gerät ist im Pool, aber leer — bewusst nicht in der Bibliothek.
2. **Vier Präfixe waren anders als erwartet** (`LGTV_` statt `LG_TV_` usw.). Genau
   deshalb meldet das Werkzeug ein nicht passendes Präfix als Fehler, statt eine leere
   Datei zu schreiben.
3. **Absturz in `setKeysForAllRegisteredGUIsAndScenes()`**: Die Schleife über die Szenen
   rief `this_scene_setKeys()` ohne NULL-Prüfung auf, während die GUI-Schleife direkt
   darunter seit jeher prüft. Eine Szene ohne Tastenbelegung zu registrieren führte zum
   Segfault. Behoben.
4. **Nur die aktiven Befehle sind exportierbar.** Die meisten Gerätequellen tragen
   deutlich mehr Codes, als sie registrieren („every command takes 100 bytes, whether
   used or not") — `lgsoundbar` hat 31 Codes und registriert 2. Ab Schritt 8 entfällt
   dieser Kompromiss: ein Befehl in einer Datei kostet nichts, bis er benutzt wird.

**Nicht enthalten:** Die Start-/End-Sequenzen der Szenen. Sie sind C++-Funktionen mit
`delay()`, keine Daten. Der Export sagt das ausdrücklich, statt eine unvollständige
Szene als vollständig auszugeben. Schritt 9 macht Daten daraus — das Dateiformat hat
den Platz dafür bereits.

## Schritt 8 — Geräte und Befehle aus JSON registrieren ✅

**Ziel:** Ab hier ein IR-Gerät ohne Neukompilieren hinzufügen.

- [x] `ConfigFileSystem::list()` — die Anzahl der Geräte steht nicht vorher fest,
      das Verzeichnis muss gelesen statt geraten werden
- [x] Laden **nach** den C++-Registrierungen beim Start
- [x] Namenskonflikt: JSON gewinnt, Warnung ins Log, alte ID funktioniert weiter
- [x] Defektes JSON: Datei wird übersprungen, alle anderen laden weiter,
      Grund bleibt im `Report` für die spätere UI erhalten
- [x] Reines JSON ohne Envelope lädt auch (von Hand aufgespielte Datei), während
      eine Datei **mit** Envelope und falscher Prüfsumme als „damaged" gemeldet wird
      — die beiden schicken einen an ganz verschiedene Stellen zur Fehlersuche
- [x] `.bak` und `.tmp` werden übersprungen: ein `.bak` zu laden würde die vorige
      Version eines Geräts still neben der aktuellen wiederbeleben
- [x] Hinter `ENABLE_JSON_CONFIG`; im Simulator **auf 1**, weil dort getestet wird
- [x] Safe-Mode überspringt den Pfad vollständig
- [x] Snapshot-Test unverändert
- [x] 15 Tests für den Loader, 3 für die Idempotenz der Registrierung

**Im Simulator verifiziert**, nicht nur kompiliert:

```
configLoader: /cfg/devices/broken.json was skipped: not valid JSON
configLoader: device 'lgTV' with 42 commands
configLoader: device 'shield' with 13 commands
configLoader: 2 device(s), 55 command(s), 1 file(s) skipped
```

**Was dieser Lauf zusätzlich zutage gefördert hat** — ein Leck, das kein Unit-Test
gezeigt hätte: `setKeysForAllRegisteredGUIsAndScenes()` ruft
`register_scene_defaultKeys()` bei **jeder** GUI- und Szenen-Registrierung auf. Vier
Befehle wurden dadurch zehnmal registriert: 38 neue IDs, 38 verwaiste `commandData`
zu je rund 100 Byte, 38 Warnzeilen — bei jedem Boot.
Eine byte-gleiche Registrierung verwendet jetzt die vorhandene ID weiter. Das ist auch
die sicherere Antwort: wer die alte ID noch hält, hat weiterhin einen funktionierenden
Befehl, weil es *dieselbe* ID ist. Eine Registrierung mit **anderen** Daten überschreibt
nach wie vor, mit Warnung. Aus 38 Warnungen beim Boot wurden 0.

**Noch offen:** Speichermessung mit 10 geladenen Geräten. Sinnvoll erst auf Hardware,
weil es um freien Heap geht, nicht um Flash.

## Schritt 9 — Sequenz-Engine für Szenen ✅

**Ziel:** `delay()` raus aus den Szenen — derselbe Kern, den Schritt 24 für Makros erweitert.

- [x] Sequenz ist Datenstruktur: `{command, payload, delayAfter}` — genau das, was
      `scenes.json` seit Schritt 6 speichert
- [x] Engine läuft nicht-blockierend über die Hauptschleife
- [x] Alle fünf Szenen umgestellt, kein `delay()` mehr in `src/scenes/`
- [x] Timing identisch zur `delay()`-Fassung
- [x] Tests mit vom Test gesteuerter Uhr: Ablauf, Reihenfolge, Abbruch, Anhängen,
      Auflösung über Befehlsnamen, unbekannter Befehl, `millis()`-Überlauf

**Eine Entscheidung, die nicht offensichtlich ist:** `enqueue()` **hängt an**, statt zu
ersetzen. Beim Szenenwechsel läuft erst die End-Sequenz der alten, dann die
Start-Sequenz der neuen Szene — mit `delay()` war das garantiert. Hätte ich „neu
ersetzt alt" gebaut, wäre `scene_allOff` mitten im Ausschalten abgeschnitten worden und
die Geräte blieben an. Ein *neuer* Szenenwechsel verwirft dagegen sehr wohl, was noch
aussteht: `sceneHandler` ruft vorher `abort()`.

**Lücke aus Schritt 7 geschlossen:** Die Szenen-Sequenzen sind jetzt exportierbar. Sie
sind weiterhin C++-Funktionen, tun aber nur noch eines — Schritte einreihen. Der Export
lässt sie in eine leere Warteschlange einreihen und nimmt das Ergebnis ab, ohne
`loop()` aufzurufen. Ein Test hält fest, dass beim Exportieren **kein einziger
IR-Befehl** am Fernseher landet.

**Nochmal in dieselbe Falle getappt** wie bei `omote_log.h`: `Step` hatte
Default-Initialisierer und war damit unter dem Standard des Arduino-Cores kein
Aggregat — die geklammerten Sequenzen in den Szenen kompilierten nicht. Die Unit-Tests
bauen mit `gnu++17`, wo es funktioniert. 174 Tests grün, Firmware unkompilierbar.
Ein Konstruktor löst es für beide.

**Noch offen:** Auf echter Hardware sehen, dass die Oberfläche während einer Szene
reagiert. Der Test `test_the_loop_is_never_blocked` zeigt es rechnerisch — dass sich das
Display dabei wirklich weiter aufbaut, sieht nur jemand mit dem Gerät.

## Schritt 10 — Zugangsdaten im NVS ✅

- [x] `src/secrets.h` bleibt als Kompilier-Default und wird nie weggenommen
- [x] Setzen und Löschen von WLAN-/MQTT-Zugangsdaten im NVS
- [x] Reihenfolge: NVS → `secrets.h` → nichts
- [x] „Nichts" schließt die **Platzhalter** ein, mit denen `secrets.h` ausgeliefert
      wird. Sonst verbringt eine frische OMOTE drei Fehlversuche damit, einem Netz
      namens „YourWifiSSID" beizutreten, bevor sie irgendeinen Weg zur Korrektur anbietet
- [x] AP-Modus nach **drei** Fehlversuchen, nicht einem — ein neu startender Router
      darf niemanden in einen Konfigurationsmodus werfen. Neue Zugangsdaten setzen den
      Zähler zurück
- [x] `mqtt_hal_esp32` liest die `secrets.h`-Makros nicht mehr direkt
- [x] 17 Tests

**Passwörter.** Sie *können* gelesen werden — sonst ließe sich nicht verbinden. Aber
die Funktionen heißen `passwordForConnecting()`. Wenn eine davon in Schritt 18 in der
Web-API auftaucht, soll der Name den Prüfer stolpern lassen. Die UI bekommt
`hasWifiPassword()`, mehr braucht sie nicht. Zwei Tests halten fest, dass der
Statustext nie ein Passwort enthält und dass `clearWifi()` nichts liegen lässt.

**Offenes Netz und Broker ohne Login sind gültige Konfigurationen**, nicht „noch nichts
gespeichert". Beide haben einen Test, weil das Verwechseln der naheliegende Fehler ist.

**Der Access Point ist bewusst dumm:** er kommt hoch und sagt es im Log. Schritt 17 gibt
ihm die Display-PIN, Schritt 18 stellt einen Webserver dahinter. Jetzt zählt nur, dass
der Pfad existiert und zum richtigen Zeitpunkt genommen wird.

**Noch offen:** `system.json` wird von niemandem geladen. Das Modell steht seit
Schritt 6, aber `configLoader` liest nur Geräte. Die Datei anzuwenden heißt, Helligkeit,
Sleep-Timeout und MQTT-Broker in die Preferences zu schreiben — ein eigener kleiner
Schritt, der vor Schritt 18 fällig ist.

---

# Phase 2 — Import und Export über USB und Bluetooth (Schritte 11–14)

**Ergebnis:** Import/Export über USB und BLE, Gerätepakete · **2 Wochen**

> Diese Phase lässt sich vorziehen, wenn du früher ohne Weboberfläche konfigurieren willst.

## Schritt 11 — Transport-Abstraktion ✅

**Ziel:** Ein schlankes, zeilenbasiertes Protokoll über einem beliebigen Bytestrom.
Der Transport kennt kein JSON, er überträgt nur Dateien.

- [x] Befehle: `LIST`, `GET`, `PUT`, `DEL`, `APPLY`, `INFO`, `REBOOT`
- [x] Chunking mit Quittungen alle 256 Byte
- [x] CRC32 über die ganze Datei (`configStorage::crc32` wiederverwendet)
- [x] Payload ohne Envelope — ein Test prüft, dass `OMOTECFG` nie über die Leitung geht
- [x] Vollständig nativ testbar: wird mit Bytes gefüttert, gibt Bytes zurück.
      Keine Stream-Abstraktion, kein Timing — 28 Tests in 15 Sekunden
- [x] 28 Tests: abgebrochene Übertragung, falsche CRC, unbekannter Befehl,
      Pfad-Traversal, zu lange Zeile, CRLF, über zwei Reads geteiltes Kommando

**Worauf es ankam:**

- **Innerhalb eines `PUT` ist jedes Byte Nutzlast**, Zeilenumbrüche eingeschlossen. Eine
  JSON-Datei ist voll davon, und eine ihrer Zeilen könnte leicht wie ein Kommando
  aussehen. Ein Test schickt eine Datei mit `REBOOT` in einer eigenen Zeile.
- **Eine falsche CRC schreibt gar nichts.** Eine beschädigt angekommene Datei darf
  keine heile ersetzen.
- **Quittungen alle 256 Byte.** BLE übergibt etwa zwanzig Byte am Stück; ohne Rückmeldung
  weiß der Sender nicht, ob die Gegenseite mitkommt.
- **Pfade müssen mit `/cfg/` beginnen** und dürfen weder `..` noch Backslash enthalten.
- **Eine überlange Zeile wird verworfen**, statt einen Puffer wachsen zu lassen, bis dem
  Gerät der Speicher ausgeht.
- **`REBOOT` antwortet vor dem Neustart** — danach ist niemand mehr da, der antworten könnte.

## Schritt 12 — USB-Transport plus Host-Werkzeug ✅

- [x] Serial auf beiden Revisionen — Rev5 über natives USB, Rev1–4 über den
      Brücken-Chip. Von `transportSession` aus sieht beides gleich aus, die älteren
      Revisionen bekommen das also geschenkt
- [x] `transportSession`: Sitzung per Magic-Zeile, Log stumm für die Dauer, Timeout
- [x] `omote_log` routet über `omote_log_printf()` statt direkt auf `Serial`
- [x] `tools/omotectl.py` (uv, pyserial): `list`, `info`, `pull`, `push`, `rm`
- [x] **TCP-Transport im Simulator** auf `127.0.0.1:8377` — dasselbe Protokoll,
      dasselbe Host-Werkzeug, ohne Hardware
- [x] End-to-End gegen den laufenden Simulator verifiziert (siehe unten)

**Das eigentliche Problem war der geteilte Port.** Es gibt genau einen, das Log
schreibt permanent hinein, und eine Log-Zeile mitten in einer Übertragung zerstört die
Datei. Deshalb ist das Protokoll eine *Sitzung*: außerhalb gehört der Port dem Log,
innerhalb schweigt es. Und eine Sitzung muss **immer** enden — ohne Timeout bliebe das
Log nach einem gezogenen Kabel bis zum nächsten Neustart stumm, und das Gerät wäre aus
einem Grund still, den niemand sehen kann.

**`BYE` wird das Protokoll gefragt, nicht im Bytestrom gesucht.** Eine Konfigurationsdatei
ist Text, und eine ihrer Zeilen kann `BYE` lauten. Wer danach sucht, schließt die
Sitzung mitten im Schreiben dieser Datei. Ein Test schickt genau so eine Datei.

**Der TCP-Transport hat sofort einen echten Fehler gefunden:** `LIST` meldete die
Dateigröße *auf der Platte*, also inklusive Envelope, während `GET` die Payload
liefert. Jede gepushte Datei wäre 38 Byte größer gelistet worden, als der Host sie
geschickt hat. Behoben, mit Test.

```
push devices_library/lgTV.json  ->  5870 Byte
list                            ->  5870 Byte
pull                            ->  byte-identisch zum Original
```

**Verschoben:** WebSerial im Browser → Schritt 19, wo das Frontend entsteht. Das
Protokoll ist dasselbe; es fehlt nur die JavaScript-Seite.

**Noch offen auf Hardware:** ob `Serial.available()`/`read()` unter Last mit dem
Protokoll mithalten. Der Simulator liest von einem Socket, das ist nicht dasselbe.

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
