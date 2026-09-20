#!/usr/bin/env bash
#
# Runs everything that has to be green before a branch is merged:
#
#     tools/run-checks.sh            # unit tests + every environment
#     tools/run-checks.sh --tests    # unit tests only, for a quick round
#     tools/run-checks.sh --sim      # additionally start the simulator once
#
# Run it inside the dev shell, or let it put itself there:
#
#     nix develop --command tools/run-checks.sh
#
# Why a script rather than a command line:
#
#   * `pio run ... | grep something` reports the exit code of *grep*, so a
#     failed build looks like a success. That is not a hypothetical - it
#     happened, and a broken build was nearly merged because of it. Hence
#     `set -o pipefail` and an explicit exit code below.
#   * the full output goes to a log file and only the summary to the terminal,
#     so a failure can be looked at afterwards instead of having to be
#     reproduced.
#   * two `pio` runs on the same project at the same time fight over
#     .pio/build and fail in ways that look like real errors. Everything here
#     runs one after the other, on purpose.

set -euo pipefail

cd "$(dirname "$0")/.."

LOG_DIR="${LOG_DIR:-.pio/checks}"
mkdir -p "$LOG_DIR"

ENVIRONMENTS=(
  esp32-Rev1toRev4
  esp32-s3-Rev5andHigher
  # the BLE configuration transport. Here because it is the only check that
  # code gets - NimBLE does not exist in the simulator, so it cannot be run at
  # all without hardware. At least it has to compile.
  esp32-s3-Rev5andHigher-ble
  esp32_testboard-Rev1toRev4
  esp32-s3_testboard-Rev5andHigher
  linux_64bit
  config_export
)

TESTS_ONLY=0
RUN_SIMULATOR=0
for argument in "$@"; do
  case "$argument" in
    --tests) TESTS_ONLY=1 ;;
    --sim) RUN_SIMULATOR=1 ;;
    *) echo "unknown option: $argument" >&2; exit 2 ;;
  esac
done

failures=0

report() {
  # $1 = name, $2 = exit code, $3 = log file, $4 = one line of detail
  if [ "$2" -eq 0 ]; then
    printf 'ok    %-34s %s\n' "$1" "${4:-}"
  else
    printf 'FAIL  %-34s see %s\n' "$1" "$3"
    failures=$((failures + 1))
  fi
}

# --- unit tests --------------------------------------------------------------
echo "== unit tests =="
# Two environments, because the ui renderer needs a real LVGL and everything
# else needs the stub. See platformio.ini.
for test_environment in native_test native_test_ui; do
  log="$LOG_DIR/$test_environment.log"
  set +e
  pio test -e "$test_environment" >"$log" 2>&1
  status=$?
  set -e
  summary=$(grep -oE '[0-9]+ test cases: [^=]*' "$log" | tail -1 || true)
  report "$test_environment" "$status" "$log" "$summary"
done

if [ "$TESTS_ONLY" -eq 1 ]; then
  echo
  [ "$failures" -eq 0 ] && echo "all good" || echo "$failures check(s) failed"
  exit "$failures"
fi

# --- one build per environment, sequentially ---------------------------------
echo
echo "== builds =="
for environment in "${ENVIRONMENTS[@]}"; do
  log="$LOG_DIR/$environment.log"
  set +e
  pio run -e "$environment" >"$log" 2>&1
  status=$?
  set -e
  # the size line only exists for the firmware builds
  size=$(grep -oE 'Flash: .*' "$log" | tail -1 || true)
  report "$environment" "$status" "$log" "$size"
done

# --- the simulator, if asked -------------------------------------------------
if [ "$RUN_SIMULATOR" -eq 1 ]; then
  echo
  echo "== simulator =="
  log="$LOG_DIR/simulator.log"

  # Leftovers from an earlier run, started with a plain `timeout` that sent
  # SIGTERM. The simulator ignores it, timeout then waits forever, and the
  # program spins at 100% CPU until somebody notices. Five of those once made
  # the whole machine look like it was hanging, which is why the run below uses
  # -s KILL and why this sweeps up first.
  # Anchored to the start of the command line on purpose. An unanchored
  # `pkill -f` also matches any *shell* whose command line happens to mention
  # the program - including the one running this script, which then kills
  # itself and silently skips everything below.
  pkill -KILL -f '^\.?/?\.pio/build/linux_64bit/program' 2>/dev/null || true
  # -s KILL because the program ignores SIGTERM and would outlive the timeout,
  # stdbuf because a killed process never flushes a block buffered stdout and
  # the log would come out empty - both learned the hard way.
  # Being killed is the expected end here, but bash announces it with a "Killed"
  # line that has no business in the summary. An inner shell reaps the process
  # and reports it to the log instead of the terminal - and the trailing
  # `exit 0` is what keeps that shell around: bash turns a lone command in
  # `bash -c` into an exec, and the inner shell would then *be* the process
  # that gets killed.
  SDL_VIDEODRIVER=dummy bash -c \
    "timeout -s KILL 8 stdbuf -oL -eL ./.pio/build/linux_64bit/program; exit 0" \
    >"$log" 2>&1 || true

  errors=$(grep -c 'OMOTE E' "$log" || true)
  lines=$(wc -l <"$log")
  if [ "$lines" -lt 3 ]; then
    report "startup" 1 "$log" "only $lines line(s) of output"
  elif [ "$errors" -ne 0 ]; then
    report "startup" 1 "$log" "$errors error line(s)"
  else
    report "startup" 0 "$log" "$lines lines, no errors"
  fi
fi

echo
if [ "$failures" -eq 0 ]; then
  echo "all good"
else
  echo "$failures check(s) failed"
fi
exit "$failures"
