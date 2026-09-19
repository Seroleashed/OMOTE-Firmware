#include "applicationInternal/firmwareInfo.h"

#ifndef OMOTE_FIRMWARE_VERSION
  #define OMOTE_FIRMWARE_VERSION "dev"
#endif

namespace firmwareInfo {

std::string version() {
  return std::string(OMOTE_FIRMWARE_VERSION);
}

std::string normalizeCompilerDate(const std::string &compilerDate) {
  // __DATE__ is "Mmm dd yyyy", with the day space padded ("Sep  1 2026")
  if (compilerDate.size() != 11) return compilerDate;

  static const char *const months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::string month = compilerDate.substr(0, 3);
  int monthNumber = 0;
  for (int i = 0; i < 12; i++) {
    if (month == months[i]) {
      monthNumber = i + 1;
      break;
    }
  }
  if (monthNumber == 0) return compilerDate;

  std::string day = compilerDate.substr(4, 2);
  if (day[0] == ' ') day[0] = '0';
  if (day[0] < '0' || day[0] > '9' || day[1] < '0' || day[1] > '9') return compilerDate;

  std::string year = compilerDate.substr(7, 4);
  for (size_t i = 0; i < year.size(); i++) {
    if (year[i] < '0' || year[i] > '9') return compilerDate;
  }

  std::string monthText = (monthNumber < 10 ? "0" : "") + std::to_string(monthNumber);
  return year + "-" + monthText + "-" + day;
}

std::string buildDate() {
  return normalizeCompilerDate(__DATE__);
}

std::string versionLine() {
  return version() + " (" + buildDate() + ")";
}

} // namespace firmwareInfo
