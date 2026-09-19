#include "devices/deviceSelectors.h"

const std::vector<configExport::DeviceSelector> &compiledInDevices() {
  static std::vector<configExport::DeviceSelector> devices;
  if (!devices.empty()) return devices;

  // the same devices main.cpp registers, in the same order
  devices.push_back({"samsungTV", "Samsung TV", "SAMSUNG_", "Samsung", "UE32EH5300"});
  devices.push_back({"yamahaAmp", "Yamaha amplifier", "YAMAHA_", "Yamaha", ""});
  devices.push_back({"appleTV", "Apple TV", "APPLETV_", "Apple", ""});
  devices.push_back({"smarthome", "Smart home", "SMARTHOME_", "", ""});

  return devices;
}
