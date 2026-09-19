#include "applicationInternal/bootGuard.h"

#include "applicationInternal/omote_log.h"

namespace bootGuard {

static BootCounterStorage *counterStorage = NULL;
static bool safeMode = false;
static SafeModeReason reason = SafeModeReason::None;

void begin(BootCounterStorage *storage) {
  counterStorage = storage;
  safeMode = false;
  reason = SafeModeReason::None;
  if (counterStorage == NULL) return;

  if (counterStorage->readSafeModeRequested()) {
    // clear it right away: a request is for exactly one boot
    counterStorage->writeSafeModeRequested(false);
    counterStorage->writeFailedBoots(0);
    safeMode = true;
    reason = SafeModeReason::Requested;
    omote_log_w("bootGuard: safe mode was requested, the stored configuration is skipped\r\n");
    return;
  }

  uint8_t failedBoots = counterStorage->readFailedBoots();
  if (failedBoots >= FAILED_BOOTS_UNTIL_SAFE_MODE) {
    // reset the counter: the boot after this one tries the stored
    // configuration again. Safe mode is a rescue, not a one-way street.
    counterStorage->writeFailedBoots(0);
    safeMode = true;
    reason = SafeModeReason::RepeatedCrash;
    omote_log_e("bootGuard: %u boots in a row did not finish, starting without the stored configuration\r\n",
                (unsigned)failedBoots);
    return;
  }

  // count this attempt. Cleared again by markBootSuccessful().
  counterStorage->writeFailedBoots((uint8_t)(failedBoots + 1));
  if (failedBoots > 0) {
    omote_log_w("bootGuard: %u boot(s) before this one did not finish\r\n", (unsigned)failedBoots);
  }
}

bool isSafeMode() { return safeMode; }

SafeModeReason safeModeReason() { return reason; }

std::string statusText() {
  switch (reason) {
    case SafeModeReason::Requested: return "safe mode (requested)";
    case SafeModeReason::RepeatedCrash: return "safe mode (repeated crash)";
    case SafeModeReason::None: return "normal";
  }
  return "normal";
}

void markBootSuccessful() {
  if (counterStorage == NULL) return;
  counterStorage->writeFailedBoots(0);
}

void requestSafeModeOnNextBoot() {
  if (counterStorage == NULL) return;
  counterStorage->writeSafeModeRequested(true);
  omote_log_i("bootGuard: safe mode armed, it takes effect after a restart\r\n");
}

void cancelSafeModeOnNextBoot() {
  if (counterStorage == NULL) return;
  counterStorage->writeSafeModeRequested(false);
  omote_log_i("bootGuard: safe mode disarmed\r\n");
}

bool isSafeModeRequestedForNextBoot() {
  if (counterStorage == NULL) return false;
  return counterStorage->readSafeModeRequested();
}

} // namespace bootGuard
