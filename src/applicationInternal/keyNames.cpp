#include "applicationInternal/keyNames.h"

#include "applicationInternal/scenes/sceneRegistry.h"

namespace keyNames {

namespace {

struct Entry {
  const char *name;
  char *character; // points at the KEY_* variable, so a remap is picked up
};

// Same order as the declarations in sceneRegistry.cpp, which is also the order
// the web UI will list them in.
const Entry ENTRIES[] = {
    {"KEY_OFF", &KEY_OFF},     {"KEY_STOP", &KEY_STOP},   {"KEY_REWI", &KEY_REWI},
    {"KEY_PLAY", &KEY_PLAY},   {"KEY_FORW", &KEY_FORW},   {"KEY_CONF", &KEY_CONF},
    {"KEY_INFO", &KEY_INFO},   {"KEY_UP", &KEY_UP},       {"KEY_DOWN", &KEY_DOWN},
    {"KEY_LEFT", &KEY_LEFT},   {"KEY_RIGHT", &KEY_RIGHT}, {"KEY_OK", &KEY_OK},
    {"KEY_BACK", &KEY_BACK},   {"KEY_SRC", &KEY_SRC},     {"KEY_VOLUP", &KEY_VOLUP},
    {"KEY_VOLDO", &KEY_VOLDO}, {"KEY_MUTE", &KEY_MUTE},   {"KEY_REC", &KEY_REC},
    {"KEY_CHUP", &KEY_CHUP},   {"KEY_CHDOW", &KEY_CHDOW}, {"KEY_RED", &KEY_RED},
    {"KEY_GREEN", &KEY_GREEN}, {"KEY_YELLO", &KEY_YELLO}, {"KEY_BLUE", &KEY_BLUE},
};

const size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

} // namespace

std::string nameFromChar(char character) {
  for (size_t i = 0; i < ENTRY_COUNT; i++) {
    if (*ENTRIES[i].character == character) return ENTRIES[i].name;
  }
  return "";
}

bool charFromName(const std::string &name, char &character) {
  for (size_t i = 0; i < ENTRY_COUNT; i++) {
    if (name == ENTRIES[i].name) {
      character = *ENTRIES[i].character;
      return true;
    }
  }
  return false;
}

std::vector<KeyName> all() {
  std::vector<KeyName> result;
  result.reserve(ENTRY_COUNT);
  for (size_t i = 0; i < ENTRY_COUNT; i++) {
    KeyName entry;
    entry.name = ENTRIES[i].name;
    entry.character = *ENTRIES[i].character;
    result.push_back(entry);
  }
  return result;
}

} // namespace keyNames
