#include "applicationInternal/storage/configStorage.h"

#include <stdio.h>
#include <stdlib.h>

#include "applicationInternal/omote_log.h"

namespace configStorage {

static ConfigFileSystem *fs = nullptr;

static const char *const HEADER_MAGIC = "OMOTECFG1";

void setFileSystem(ConfigFileSystem *fileSystem) { fs = fileSystem; }
ConfigFileSystem *fileSystem() { return fs; }

bool looksLikeEnvelope(const std::string &content) { return content.rfind(HEADER_MAGIC, 0) == 0; }

// --- crc32 (IEEE 802.3, same polynomial as zlib) -----------------------------
uint32_t crc32(const std::string &data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1) - 1)));
    }
  }
  return ~crc;
}

// --- envelope ----------------------------------------------------------------
std::string buildEnvelope(const std::string &payload, uint16_t schemaVersion) {
  char header[96];
  snprintf(header, sizeof(header), "%s v=%u len=%u crc=0x%08x\n", HEADER_MAGIC,
           (unsigned)schemaVersion, (unsigned)payload.size(), (unsigned)crc32(payload));
  return std::string(header) + payload;
}

static bool parseEnvelope(const std::string &raw, uint16_t &schemaVersion, std::string &payload) {
  size_t newline = raw.find('\n');
  if (newline == std::string::npos) return false;

  unsigned version = 0, length = 0, crc = 0;
  char magic[16] = {0};
  // %15s stops at the first space, the format is fixed, so sscanf is enough
  int parsed = sscanf(raw.substr(0, newline).c_str(), "%15s v=%u len=%u crc=0x%x", magic, &version,
                      &length, &crc);
  if (parsed != 4) return false;
  if (std::string(magic) != HEADER_MAGIC) return false;

  std::string body = raw.substr(newline + 1);
  if (body.size() != length) {
    omote_log_w("configStorage: length mismatch, expected %u got %u\r\n", length,
                (unsigned)body.size());
    return false;
  }
  if (crc32(body) != crc) {
    omote_log_w("configStorage: crc mismatch\r\n");
    return false;
  }

  schemaVersion = (uint16_t)version;
  payload = body;
  return true;
}

// --- helpers -----------------------------------------------------------------
static std::string tmpName(const std::string &name) { return name + ".tmp"; }
static std::string bakName(const std::string &name) { return name + ".bak"; }

static bool readAndVerify(const std::string &path, uint16_t &schemaVersion, std::string &payload) {
  if (fs == nullptr || !fs->exists(path)) return false;
  std::string raw;
  if (!fs->read(path, raw)) return false;
  return parseEnvelope(raw, schemaVersion, payload);
}

// --- api ---------------------------------------------------------------------
bool save(const std::string &name, const std::string &payload, uint16_t schemaVersion) {
  if (fs == nullptr) {
    omote_log_e("configStorage: no file system set\r\n");
    return false;
  }

  const std::string tmp = tmpName(name);
  const std::string bak = bakName(name);

  // 1. write to the temporary file
  if (!fs->write(tmp, buildEnvelope(payload, schemaVersion))) {
    omote_log_e("configStorage: could not write %s\r\n", tmp.c_str());
    fs->remove(tmp);
    return false;
  }

  // 2. read it back - this catches a full flash, a truncated write and bad hardware
  uint16_t verifyVersion = 0;
  std::string verifyPayload;
  if (!readAndVerify(tmp, verifyVersion, verifyPayload) || verifyPayload != payload) {
    omote_log_e("configStorage: verification of %s failed\r\n", tmp.c_str());
    fs->remove(tmp);
    return false;
  }

  // 3. keep the previous version as backup (only if it is intact)
  if (fs->exists(name)) {
    uint16_t previousVersion = 0;
    std::string previousPayload;
    if (readAndVerify(name, previousVersion, previousPayload)) {
      fs->remove(bak);
      fs->rename(name, bak);
    } else {
      // the current file is broken anyway, an older backup is worth more
      fs->remove(name);
    }
  }

  // 4. the atomic step
  if (!fs->rename(tmp, name)) {
    omote_log_e("configStorage: could not activate %s\r\n", name.c_str());
    // the backup is still there, so nothing is lost
    return false;
  }

  omote_log_i("configStorage: saved %s (%u bytes, schema %u)\r\n", name.c_str(),
              (unsigned)payload.size(), (unsigned)schemaVersion);
  return true;
}

LoadedConfig load(const std::string &name) {
  LoadedConfig loaded;
  if (fs == nullptr) {
    omote_log_e("configStorage: no file system set\r\n");
    return loaded;
  }

  if (readAndVerify(name, loaded.schemaVersion, loaded.payload)) {
    loaded.result = LoadResult::Ok;
    return loaded;
  }

  const bool mainExists = fs->exists(name);

  if (readAndVerify(bakName(name), loaded.schemaVersion, loaded.payload)) {
    omote_log_w("configStorage: %s unusable, using backup\r\n", name.c_str());
    loaded.result = LoadResult::OkFromBackup;
    return loaded;
  }

  loaded.payload.clear();
  loaded.schemaVersion = 0;
  loaded.result = (mainExists || fs->exists(bakName(name))) ? LoadResult::Corrupt : LoadResult::NotFound;
  if (loaded.result == LoadResult::Corrupt) {
    omote_log_e("configStorage: %s and its backup are unusable\r\n", name.c_str());
  }
  return loaded;
}

bool remove(const std::string &name) {
  if (fs == nullptr) return false;
  bool ok = true;
  if (fs->exists(name)) ok = fs->remove(name) && ok;
  if (fs->exists(bakName(name))) ok = fs->remove(bakName(name)) && ok;
  if (fs->exists(tmpName(name))) ok = fs->remove(tmpName(name)) && ok;
  return ok;
}

bool hasStoredConfig(const std::string &name) {
  if (fs == nullptr) return false;
  return fs->exists(name) || fs->exists(bakName(name));
}

} // namespace configStorage
