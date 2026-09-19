#pragma once

#include <string>
#include <vector>

/*
  Stable names for the keys of the keypad.

  Internally a key is a single char: KEY_OK is 'k', KEY_VOLUP is '+'. That is
  compact and works well in the C++ key maps, but it is a poor thing to put in a
  configuration file - '^' and 'v' tell nobody that they mean channel up and
  down, and a char cannot be extended without breaking every stored file.

  So the files use the variable name, exactly like commands do since step 5:

      "KEY_OK": { "repeatMode": "SHORT", "short": "SAMSUNG_SELECT" }

  The table maps to the char *variables*, not to copies of their values. The
  KEY_* variables are not const, and a later step may well remap them; the name
  lookup follows along instead of silently going stale.
*/

namespace keyNames {

struct KeyName {
  std::string name; // "KEY_OK"
  char character;   // 'k'
};

// "" if this char is not one of the named keys. The keypad matrix contains a
// '?' on a position that has no name, so this is a normal case, not an error.
std::string nameFromChar(char character);

bool charFromName(const std::string &name, char &character);

// all named keys, in the order they are declared in sceneRegistry.cpp
std::vector<KeyName> all();

} // namespace keyNames
