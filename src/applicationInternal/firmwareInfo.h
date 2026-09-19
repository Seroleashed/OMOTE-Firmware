#pragma once

#include <string>

/*
  Which firmware is running.

  Needed as soon as updates are installed over the air: without a version on the
  screen there is no way to tell whether an update actually took effect, and
  after an automatic rollback the device would silently run the old image.

  The version comes from the build:

      -D OMOTE_FIRMWARE_VERSION="\"0.9.0-dev\""

  and is set in platformio.ini. If the flag is missing the firmware still
  compiles and reports "dev". The build date is taken from the compiler, so it
  is always right even if somebody forgets to bump the version.
*/
namespace firmwareInfo {

// e.g. "0.9.0-dev"
std::string version();

// e.g. "2026-09-19", derived from the compiler's __DATE__
std::string buildDate();

// e.g. "0.9.0-dev (2026-09-19)". This is what the settings screen shows.
std::string versionLine();

/*
  Turns the compiler's "Mmm dd yyyy" into "yyyy-mm-dd".
  Exposed so it can be tested without rebuilding at a certain date. Returns the
  input unchanged if it does not have the expected shape.
*/
std::string normalizeCompilerDate(const std::string &compilerDate);

} // namespace firmwareInfo
