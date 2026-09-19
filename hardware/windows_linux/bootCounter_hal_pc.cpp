#if defined(WIN32) || defined(__linux__) || defined(__APPLE__)

#include <filesystem>
#include <fstream>

#include "applicationInternal/bootGuard.h"

/*
  Boot counter for the simulator.

  Stored in a file next to the simulated configuration, so safe mode can
  actually be tried out on the development machine: kill the simulator three
  times during startup and the next start comes up in safe mode, exactly like
  the device would.
*/

static const char *const SIM_ROOT = "omote_data";
static const char *const COUNTER_FILE = "omote_data/bootguard.txt";

class FileBootCounterStorage : public BootCounterStorage {
public:
  uint8_t readFailedBoots() override { return (uint8_t)read().failedBoots; }

  void writeFailedBoots(uint8_t count) override {
    State state = read();
    state.failedBoots = count;
    write(state);
  }

  bool readSafeModeRequested() override { return read().safeModeRequested; }

  void writeSafeModeRequested(bool requested) override {
    State state = read();
    state.safeModeRequested = requested;
    write(state);
  }

private:
  struct State {
    int failedBoots = 0;
    bool safeModeRequested = false;
  };

  State read() {
    State state;
    std::ifstream file(COUNTER_FILE);
    if (!file) return state;
    int requested = 0;
    file >> state.failedBoots >> requested;
    if (!file) return State(); // unreadable: start over instead of guessing
    state.safeModeRequested = (requested != 0);
    return state;
  }

  void write(const State &state) {
    std::error_code error;
    std::filesystem::create_directories(SIM_ROOT, error);
    std::ofstream file(COUNTER_FILE, std::ios::trunc);
    if (!file) return;
    file << state.failedBoots << " " << (state.safeModeRequested ? 1 : 0) << "\n";
  }
};

BootCounterStorage *get_bootCounterStorage() {
  static FileBootCounterStorage storage;
  return &storage;
}

#endif
