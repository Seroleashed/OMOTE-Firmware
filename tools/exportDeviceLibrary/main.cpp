/*
  Converts the devices compiled into the firmware into a library of JSON device
  packs, one file per device.

      pio run -e config_export
      .pio/build/config_export/program devices_library

  Runs on the development machine, no hardware involved: it registers the same
  devices the firmware would, then writes out what landed in the command
  registry. That is the point - the payload formats come from the registry
  rather than from somebody reading the C++ and retyping it.

  The result is checked in under devices_library/, so a user can import a
  device without owning the C++ sources, and so a change to a device shows up
  as a readable diff in that folder.

  Scenes and the keypad matrix are not exported here: scenes need the gui
  symbols and the matrix needs the hardware layer, neither of which this small
  native program links. Both come out of the firmware's own dumpConfigAsJson()
  over serial, or out of the simulator.
*/

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/storage/configExport.h"
#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configModel.h"

// the devices that main.cpp registers
#include "devices/AVreceiver/device_yamahaAmp/device_yamahaAmp.h"
#include "devices/TV/device_samsungTV/device_samsungTV.h"
#include "devices/mediaPlayer/device_appleTV/device_appleTV.h"
#include "devices/misc/device_smarthome/device_smarthome.h"
// and the whole pool of devices that ship with the firmware but are commented out
#include "devices_pool/AVreceiver/device_boseAmp/device_boseAmp.h"
#include "devices_pool/AVreceiver/device_denonAvr/device_denonAvr.h"
#include "devices_pool/AVreceiver/device_lgsoundbar/device_lgsoundbar.h"
#include "devices_pool/TV/device_lgTV/device_lgTV.h"
#include "devices_pool/TV/device_sonyTV/device_sonyTV.h"
#include "devices_pool/mediaPlayer/device_lgbluray/device_lgbluray.h"
#include "devices_pool/mediaPlayer/device_samsungbluray/device_samsungbluray.h"
#include "devices_pool/mediaPlayer/device_shield/device_shield.h"
#include "devices_pool/misc/device_airconditioner/device_airconditioner.h"

namespace {

struct Device {
  configExport::DeviceSelector selector;
  void (*registerDevice)();
  // A device whose register_command() calls are all commented out in the
  // source. It registers nothing, so there is nothing to export - that is a
  // property of the source file, not a failure of this program.
  bool allCommandsCommentedOut = false;
};

/*
  The prefix is what ties a device to its commands: every command of the Samsung
  TV is called SAMSUNG_something. That convention is what makes the export
  possible at all, so a device whose prefix matches nothing is reported as an
  error rather than written as an empty file.
*/
std::vector<Device> deviceList() {
  std::vector<Device> devices;
  devices.push_back({{"samsungTV", "Samsung TV", "SAMSUNG_", "Samsung", "UE32EH5300"},
                     &register_device_samsungTV});
  devices.push_back({{"yamahaAmp", "Yamaha amplifier", "YAMAHA_", "Yamaha", ""},
                     &register_device_yamahaAmp});
  devices.push_back({{"appleTV", "Apple TV", "APPLETV_", "Apple", ""}, &register_device_appleTV});
  devices.push_back({{"smarthome", "Smart home", "SMARTHOME_", "", ""}, &register_device_smarthome});

  devices.push_back({{"lgTV", "LG TV", "LGTV_", "LG", ""}, &register_device_lgTV});
  devices.push_back({{"sonyTV", "Sony TV", "SONY_", "Sony", ""}, &register_device_sonyTV});
  devices.push_back({{"boseAmp", "Bose amplifier", "BOSE_", "Bose", ""}, &register_device_boseAmp});
  // every single register_command() in device_denonAvr.cpp is commented out
  devices.push_back({{"denonAvr", "Denon AV receiver", "DENON_", "Denon", ""}, &register_device_denonAvr,
                     true});
  devices.push_back({{"lgsoundbar", "LG soundbar", "LGSOUNDBAR_", "LG", ""}, &register_device_lgsoundbar});
  devices.push_back({{"lgbluray", "LG Blu-ray player", "LGBLURAY_", "LG", ""}, &register_device_lgbluray});
  devices.push_back({{"samsungbluray", "Samsung Blu-ray player", "SAMSUNGBLURAY_", "Samsung", ""},
                     &register_device_samsungbluray});
  devices.push_back({{"shield", "NVIDIA Shield", "SHIELD_", "NVIDIA", ""}, &register_device_shield});
  devices.push_back({{"airconditioner", "Air conditioner", "AIRCONDITIONER_", "De'Longhi", "PAC N81"},
                     &register_device_airconditioner});
  return devices;
}

bool writeFile(const std::string &path, const std::string &content) {
  std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file << content;
  return file.good();
}

} // namespace

int main(int argc, char **argv) {
  std::string outputDirectory = argc > 1 ? argv[1] : "devices_library";

  std::vector<Device> devices = deviceList();
  for (size_t i = 0; i < devices.size(); i++) {
    devices[i].registerDevice();
  }

  int written = 0;
  int failed = 0;
  int skipped = 0;
  size_t totalCommands = 0;

  for (size_t i = 0; i < devices.size(); i++) {
    configModel::DevicePack pack = configExport::devicePack(devices[i].selector);

    if (pack.commands.empty()) {
      if (devices[i].allCommandsCommentedOut) {
        std::printf("skip  %-16s registers nothing, all its commands are commented out\n",
                    pack.id.c_str());
        skipped++;
      } else {
        std::printf("ERROR %-16s prefix '%s' matched no command\n", pack.id.c_str(),
                    devices[i].selector.namePrefix.c_str());
        failed++;
      }
      continue;
    }

    std::string json = configModel::serializeDevicePack(pack);

    // Read it back straight away. A file this program cannot parse itself is
    // not one to hand out to anybody else.
    configModel::DevicePack reparsed;
    std::string error;
    if (!configModel::parseDevicePack(json, reparsed, error)) {
      std::printf("ERROR %-16s exported but does not parse: %s\n", pack.id.c_str(), error.c_str());
      failed++;
      continue;
    }
    if (reparsed.commands.size() != pack.commands.size()) {
      std::printf("ERROR %-16s round trip lost commands: %u written, %u read back\n", pack.id.c_str(),
                  (unsigned)pack.commands.size(), (unsigned)reparsed.commands.size());
      failed++;
      continue;
    }

    std::string path = outputDirectory + "/" + pack.id + ".json";
    if (!writeFile(path, json)) {
      std::printf("ERROR %-16s could not write %s\n", pack.id.c_str(), path.c_str());
      failed++;
      continue;
    }

    std::printf("ok    %-16s %3u commands -> %s\n", pack.id.c_str(), (unsigned)pack.commands.size(),
                path.c_str());
    totalCommands += pack.commands.size();
    written++;
  }

  std::printf("\n%d devices, %u commands written to %s/\n", written, (unsigned)totalCommands,
              outputDirectory.c_str());
  if (skipped > 0) std::printf("%d device(s) skipped, see above\n", skipped);
  if (failed > 0) std::printf("%d devices FAILED\n", failed);
  return failed == 0 ? 0 : 1;
}
