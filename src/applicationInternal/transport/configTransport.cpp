#include "applicationInternal/transport/configTransport.h"

#include <stdio.h>
#include <stdlib.h>

#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configFileSystem.h"
#include "applicationInternal/storage/configStorage.h"

namespace configTransport {

namespace {

ConfigFileSystem *fs = NULL;
Callbacks callbacks;

std::string inputBuffer;
std::string outputBuffer;

// --- state of a running PUT --------------------------------------------------
bool receiving = false;
bool goodbye = false;
std::string receivePath;
size_t receiveExpected = 0;
uint32_t receiveCrc = 0;
std::string receiveBuffer;
size_t acknowledgedUpTo = 0;

void send(const std::string &line) {
  outputBuffer += line;
  outputBuffer += "\n";
}

void sendError(const std::string &what) {
  send("ERR " + what);
  omote_log_w("transport: %s\r\n", what.c_str());
}

std::string toHex(uint32_t value) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%08x", (unsigned int)value);
  return buffer;
}

bool parseHex(const std::string &text, uint32_t &value) {
  if (text.empty() || text.size() > 8) return false;
  char *end = NULL;
  unsigned long parsed = strtoul(text.c_str(), &end, 16);
  if (end == NULL || *end != '\0') return false;
  value = (uint32_t)parsed;
  return true;
}

bool parseSize(const std::string &text, size_t &value) {
  if (text.empty()) return false;
  char *end = NULL;
  unsigned long parsed = strtoul(text.c_str(), &end, 10);
  if (end == NULL || *end != '\0') return false;
  value = (size_t)parsed;
  return true;
}

/*
  Only below /cfg, and no way out of it.

  The far end is not necessarily friendly, and even a friendly one makes typos.
  "/cfg/../../etc/passwd" has no business reaching the file system, and neither
  has a path that happens to name the firmware partition.
*/
bool isAllowedPath(const std::string &path, std::string &reason) {
  const std::string root = std::string(configFile::DIRECTORY) + "/";

  if (path.rfind(root, 0) != 0) {
    reason = "path must start with " + root;
    return false;
  }
  if (path.find("..") != std::string::npos) {
    reason = "path must not contain '..'";
    return false;
  }
  if (path.find('\\') != std::string::npos) {
    reason = "path must not contain a backslash";
    return false;
  }
  // a trailing slash is a directory, and we move files
  if (path[path.size() - 1] == '/') {
    reason = "path must name a file";
    return false;
  }
  return true;
}

std::vector<std::string> splitWords(const std::string &line) {
  std::vector<std::string> words;
  size_t start = 0;
  while (start < line.size()) {
    size_t space = line.find(' ', start);
    if (space == std::string::npos) {
      words.push_back(line.substr(start));
      break;
    }
    if (space > start) words.push_back(line.substr(start, space - start));
    start = space + 1;
  }
  return words;
}

// --- the commands ------------------------------------------------------------

void handleList() {
  if (fs == NULL) {
    sendError("no file system");
    return;
  }

  std::vector<std::string> paths = fs->list(configFile::DIRECTORY);
  std::vector<std::string> devicePaths = fs->list(configFile::DEVICES_DIRECTORY);
  paths.insert(paths.end(), devicePaths.begin(), devicePaths.end());

  std::vector<std::string> lines;
  for (size_t i = 0; i < paths.size(); i++) {
    // .bak and .tmp belong to the storage layer. Handing them out would invite
    // somebody to pull one and wonder why it is not the file they saved.
    if (paths[i].size() < 5 || paths[i].compare(paths[i].size() - 5, 5, ".json") != 0) continue;

    /*
      The size has to be the size of what GET would hand over, which is the
      payload. Reporting the size of the file on disk instead would count the
      storage envelope as well, and a host comparing what it sent with what the
      device lists would find a mismatch for every file it ever wrote.
    */
    configStorage::LoadedConfig stored = configStorage::load(paths[i]);
    size_t size = 0;
    if (stored.usable()) {
      size = stored.payload.size();
    } else {
      std::string content;
      if (!fs->read(paths[i], content)) continue;
      // a file without an envelope is its own payload
      if (configStorage::looksLikeEnvelope(content)) continue; // damaged, GET would refuse it too
      size = content.size();
    }
    lines.push_back(paths[i] + " " + std::to_string(size));
  }

  send("OK " + std::to_string(lines.size()));
  for (size_t i = 0; i < lines.size(); i++) send(lines[i]);
  send("END");
}

void handleGet(const std::vector<std::string> &words) {
  if (words.size() < 2) {
    sendError("GET needs a path");
    return;
  }
  std::string reason;
  if (!isAllowedPath(words[1], reason)) {
    sendError(reason);
    return;
  }
  if (fs == NULL) {
    sendError("no file system");
    return;
  }

  // Hand over the payload, never the envelope - see the header. A file that
  // has one gets it stripped, a file that never had one is passed as it is.
  configStorage::LoadedConfig stored = configStorage::load(words[1]);
  std::string payload;
  if (stored.usable()) {
    payload = stored.payload;
  } else {
    if (!fs->read(words[1], payload)) {
      sendError("no such file: " + words[1]);
      return;
    }
    if (configStorage::looksLikeEnvelope(payload)) {
      sendError("file is damaged: " + words[1]);
      return;
    }
  }

  send("OK " + std::to_string(payload.size()) + " " + toHex(configStorage::crc32(payload)));
  outputBuffer += payload;
  if (!payload.empty() && payload[payload.size() - 1] != '\n') outputBuffer += "\n";
  send("END");
}

void handlePut(const std::vector<std::string> &words) {
  if (words.size() < 4) {
    sendError("PUT needs a path, a length and a crc");
    return;
  }
  std::string reason;
  if (!isAllowedPath(words[1], reason)) {
    sendError(reason);
    return;
  }

  size_t length = 0;
  if (!parseSize(words[2], length)) {
    sendError("length is not a number: " + words[2]);
    return;
  }
  if (length > MAX_FILE_SIZE) {
    sendError("file too large: " + std::to_string(length) + " bytes, the limit is " +
              std::to_string(MAX_FILE_SIZE));
    return;
  }

  uint32_t crc = 0;
  if (!parseHex(words[3], crc)) {
    sendError("crc is not a hex number: " + words[3]);
    return;
  }
  if (fs == NULL) {
    sendError("no file system");
    return;
  }

  receiving = true;
  receivePath = words[1];
  receiveExpected = length;
  receiveCrc = crc;
  receiveBuffer.clear();
  acknowledgedUpTo = 0;
  send("READY");

  // a zero byte file is legitimate and has nothing to wait for
  if (length == 0) {
    receiving = false;
    if (configStorage::save(receivePath, "", configStorage::SCHEMA_VERSION_IN_PAYLOAD)) {
      send("OK");
    } else {
      sendError("could not write " + receivePath);
    }
  }
}

void finishPut() {
  receiving = false;

  uint32_t actual = configStorage::crc32(receiveBuffer);
  if (actual != receiveCrc) {
    // Nothing is written. A file that arrived damaged must not replace one that
    // is fine.
    sendError("crc mismatch: expected " + toHex(receiveCrc) + ", got " + toHex(actual));
    receiveBuffer.clear();
    return;
  }

  if (!configStorage::save(receivePath, receiveBuffer, configStorage::SCHEMA_VERSION_IN_PAYLOAD)) {
    sendError("could not write " + receivePath);
    receiveBuffer.clear();
    return;
  }

  omote_log_i("transport: wrote %u bytes to %s\r\n", (unsigned)receiveBuffer.size(),
              receivePath.c_str());
  receiveBuffer.clear();
  send("OK");
}

void handleDelete(const std::vector<std::string> &words) {
  if (words.size() < 2) {
    sendError("DEL needs a path");
    return;
  }
  std::string reason;
  if (!isAllowedPath(words[1], reason)) {
    sendError(reason);
    return;
  }
  if (fs == NULL) {
    sendError("no file system");
    return;
  }
  if (!configStorage::hasStoredConfig(words[1]) && !fs->exists(words[1])) {
    sendError("no such file: " + words[1]);
    return;
  }

  // removes the backup and the temporary file too, so "delete" really deletes
  configStorage::remove(words[1]);
  fs->remove(words[1]);
  send("OK");
}

void handleInfo() {
  send("OK");
  if (callbacks.info != NULL) {
    std::vector<std::string> lines = callbacks.info();
    for (size_t i = 0; i < lines.size(); i++) send(lines[i]);
  }
  send("END");
}

void handleLine(const std::string &line) {
  if (line.empty()) return;

  std::vector<std::string> words = splitWords(line);
  if (words.empty()) return;
  const std::string &command = words[0];

  if (command == "LIST") {
    handleList();
  } else if (command == "GET") {
    handleGet(words);
  } else if (command == "PUT") {
    handlePut(words);
  } else if (command == "DEL") {
    handleDelete(words);
  } else if (command == "INFO") {
    handleInfo();
  } else if (command == "APPLY") {
    send("OK");
    if (callbacks.apply != NULL) callbacks.apply();
  } else if (command == "REBOOT") {
    // answer first: after the restart there is nobody left to answer with
    send("OK");
    if (callbacks.reboot != NULL) callbacks.reboot();
  } else if (command == "BYE") {
    // whoever owns the stream decides what to do with this; here it is just
    // recorded, because this is the layer that knows where a line ends
    goodbye = true;
    send("OK");
  } else {
    sendError("unknown command: " + command);
  }
}

} // namespace

void begin(ConfigFileSystem *fileSystem, const Callbacks &aCallbacks) {
  fs = fileSystem;
  callbacks = aCallbacks;
  reset();
}

void reset() {
  inputBuffer.clear();
  outputBuffer.clear();
  receiving = false;
  receivePath.clear();
  receiveExpected = 0;
  receiveCrc = 0;
  receiveBuffer.clear();
  acknowledgedUpTo = 0;
  goodbye = false;
}

bool isReceiving() { return receiving; }

bool takeGoodbye() {
  bool seen = goodbye;
  goodbye = false;
  return seen;
}

void feed(const std::string &bytes) {
  size_t position = 0;

  while (position < bytes.size()) {
    if (receiving) {
      // In the middle of a PUT every byte is payload, newlines included.
      size_t missing = receiveExpected - receiveBuffer.size();
      size_t take = bytes.size() - position;
      if (take > missing) take = missing;

      receiveBuffer.append(bytes, position, take);
      position += take;

      // Tell the sender how far we got, so a link with a small buffer knows
      // when to send more.
      while (receiveBuffer.size() - acknowledgedUpTo >= CHUNK_SIZE) {
        acknowledgedUpTo += CHUNK_SIZE;
        send("ACK " + std::to_string(acknowledgedUpTo));
      }

      if (receiveBuffer.size() == receiveExpected) finishPut();
      continue;
    }

    // Outside a PUT the stream is lines.
    size_t newline = bytes.find('\n', position);
    if (newline == std::string::npos) {
      inputBuffer.append(bytes, position, bytes.size() - position);
      position = bytes.size();
    } else {
      inputBuffer.append(bytes, position, newline - position);
      position = newline + 1;

      // tolerate CRLF, because half the serial terminals in the world send it
      if (!inputBuffer.empty() && inputBuffer[inputBuffer.size() - 1] == '\r') {
        inputBuffer.erase(inputBuffer.size() - 1);
      }
      std::string line = inputBuffer;
      inputBuffer.clear();
      handleLine(line);
      continue;
    }

    if (inputBuffer.size() > MAX_LINE_LENGTH) {
      // No command of ours is this long, so the stream has lost sync. Dropping
      // it beats growing a buffer until the device runs out of memory.
      sendError("line too long, input dropped");
      inputBuffer.clear();
    }
  }
}

std::string takeOutput() {
  std::string output = outputBuffer;
  outputBuffer.clear();
  return output;
}

} // namespace configTransport
