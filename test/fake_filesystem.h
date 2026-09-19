#pragma once
/*
  In-memory file system for the storage tests, with fault injection.

  A power loss during a flash write does not produce a nice error - it produces
  a file that is only partly written. failWriteAfterBytes() reproduces exactly
  that: the file ends up truncated and the write reports failure, just like a
  reset in the middle of it.
*/

#include <map>
#include <string>

#include "applicationInternal/storage/configFileSystem.h"

class FakeFileSystem : public ConfigFileSystem {
public:
  std::map<std::string, std::string> files;

  // fault injection
  size_t writeFailsAfterBytes = 0; // 0 = off; writes only the first n bytes and fails
  // a rename to exactly this destination fails once (empty = off). Lets a test
  // hit the final "activate" rename instead of the backup rename.
  std::string failRenameTo;
  int writeCount = 0;
  int renameCount = 0;

  bool exists(const std::string &path) override { return files.count(path) > 0; }

  bool read(const std::string &path, std::string &content) override {
    if (!exists(path)) return false;
    content = files[path];
    return true;
  }

  bool write(const std::string &path, const std::string &content) override {
    writeCount++;
    if (writeFailsAfterBytes > 0) {
      files[path] = content.substr(0, writeFailsAfterBytes);
      writeFailsAfterBytes = 0;
      return false;
    }
    files[path] = content;
    return true;
  }

  bool rename(const std::string &from, const std::string &to) override {
    renameCount++;
    if (!failRenameTo.empty() && to == failRenameTo) {
      failRenameTo.clear();
      return false;
    }
    if (!exists(from)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }

  bool remove(const std::string &path) override {
    if (!exists(path)) return false;
    files.erase(path);
    return true;
  }

  std::vector<std::string> list(const std::string &directory) override {
    std::vector<std::string> paths;
    std::string prefix = directory;
    if (prefix.empty() || prefix[prefix.size() - 1] != '/') prefix += "/";

    for (std::map<std::string, std::string>::const_iterator it = files.begin(); it != files.end(); ++it) {
      if (it->first.rfind(prefix, 0) != 0) continue;
      // direct children only, no recursion - same as the real implementations
      if (it->first.find('/', prefix.size()) != std::string::npos) continue;
      paths.push_back(it->first);
    }
    return paths; // std::map already keeps them sorted
  }

  // --- test helpers ---------------------------------------------------------
  void reset() {
    files.clear();
    writeFailsAfterBytes = 0;
    failRenameTo.clear();
    writeCount = 0;
    renameCount = 0;
  }

  // flips one bit in the payload of a stored file, like flash wear would
  void corrupt(const std::string &path) {
    if (!exists(path)) return;
    std::string &data = files[path];
    if (data.empty()) return;
    data[data.size() - 1] = (char)(data[data.size() - 1] ^ 0x20);
  }
};
