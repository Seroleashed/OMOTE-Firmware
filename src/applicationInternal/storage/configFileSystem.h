#pragma once

#include <string>

/*
  Thin file system interface.

  The configuration storage must not know whether it writes to LittleFS on the
  ESP32, to a folder on the development machine (simulator) or to an in-memory
  fake in a unit test. Everything it needs are these five operations.

  Implementations:
    hardware/ESP32/configFileSystem_hal_esp32.cpp      LittleFS
    hardware/windows_linux/configFileSystem_hal_pc.cpp local folder
    test/fake_filesystem.*                             in memory, with fault
                                                       injection for power loss
*/
class ConfigFileSystem {
public:
  virtual ~ConfigFileSystem() {}

  virtual bool exists(const std::string &path) = 0;
  // returns false if the file does not exist or cannot be read completely
  virtual bool read(const std::string &path, std::string &content) = 0;
  // creates or truncates the file
  virtual bool write(const std::string &path, const std::string &content) = 0;
  // must overwrite an existing destination; this is the operation the atomic
  // save relies on
  virtual bool rename(const std::string &from, const std::string &to) = 0;
  virtual bool remove(const std::string &path) = 0;
};

// provided by the active hardware layer
ConfigFileSystem *get_configFileSystem();
