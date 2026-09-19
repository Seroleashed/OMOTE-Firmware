#pragma once

#include <vector>

#include "applicationInternal/storage/configExport.h"

/*
  Which devices this firmware has compiled in, and which command name prefix
  belongs to each of them.

  The prefix is the whole trick behind the export: every command of the Samsung
  TV is called SAMSUNG_something, so the export can tell the devices apart even
  though the registry is one flat list. This file is where that convention is
  written down.

  Keep it in step with the register_device_*() calls in main.cpp. The list is
  also used by tools/exportDeviceLibrary, so the firmware and the converter
  cannot drift apart on the devices they share.
*/
const std::vector<configExport::DeviceSelector> &compiledInDevices();
