#if (ENABLE_BLE_CONFIG == 1)

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "applicationInternal/omote_log.h"
#include "applicationInternal/transport/transportSession.h"

/*
  The configuration transport over BLE.

  A second service next to the HID keyboard, on the same NimBLE server. Shaped
  like the Nordic UART everyone's BLE terminal already speaks: one
  characteristic the host writes into, one it subscribes to.

  ## Why this is behind its own flag

  ENABLE_BLE_CONFIG defaults to 0, and the plan warned why: a second service
  costs both flash and the scarcer resource, connection memory, next to a HID
  profile that has to keep working. Measured on Rev5:

      without   1.933.601 bytes   36.9%
      with      see docs/PLAN.md

  It is also the one piece of this rework that cannot be run without hardware -
  NimBLE does not build in the simulator. Everything above the byte stream is
  covered by the 42 transport tests and shared with USB; what is unverified is
  this file. Defaulting to off means a firmware that has never been near a
  radio cannot be destabilised by it.

  ## Threading

  onWrite() runs on the BLE stack's task, loop() on the main one. The buffer
  between them is guarded by a spinlock rather than a mutex: the critical
  sections are a memcpy of at most one MTU, and blocking the BLE task on a
  mutex held by a busy main loop is how connections get dropped.
*/

namespace {

// Nordic UART, so an off-the-shelf BLE terminal can talk to the remote without
// anybody writing an app first.
const char *const SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char *const RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // host writes
const char *const TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // host subscribes

// A host that writes faster than the main loop reads must not grow this
// without bound. Two kilobytes is eight MTUs of headroom; beyond that the link
// is faster than the device and the sender has to wait for its ACK anyway.
const size_t MAX_BUFFERED = 2048;

portMUX_TYPE bufferLock = portMUX_INITIALIZER_UNLOCKED;
std::string incoming;
bool overflowed = false;

NimBLECharacteristic *txCharacteristic = NULL;
bool connected = false;

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic) override {
    std::string value = characteristic->getValue();
    if (value.empty()) return;

    portENTER_CRITICAL(&bufferLock);
    if (incoming.size() + value.size() > MAX_BUFFERED) {
      overflowed = true;
    } else {
      incoming += value;
    }
    portEXIT_CRITICAL(&bufferLock);
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server) override {
    (void)server;
    connected = true;
  }
  void onDisconnect(NimBLEServer *server) override {
    (void)server;
    connected = false;
    // A link that went away mid-transfer must not leave the session open: the
    // log would stay muted and a half written file would wait forever.
    transportSession::close();
  }
};

RxCallbacks rxCallbacks;
ServerCallbacks serverCallbacks;

class BleByteStream : public transportSession::ByteStream {
public:
  size_t read(std::string &into) override {
    std::string taken;

    portENTER_CRITICAL(&bufferLock);
    taken.swap(incoming);
    bool didOverflow = overflowed;
    overflowed = false;
    portEXIT_CRITICAL(&bufferLock);

    if (didOverflow) {
      // Say so rather than carrying on with a hole in the middle of a file.
      omote_log_e("bleTransport: input overflowed, the transfer is broken\r\n");
      transportSession::close();
      return 0;
    }

    if (taken.empty()) return 0;
    into += taken;
    return taken.size();
  }

  void write(const std::string &bytes) override {
    if (txCharacteristic == NULL || !connected || bytes.empty()) return;

    // One notification per MTU. Handing NimBLE more than it can carry silently
    // truncates, which on a file transfer means a wrong crc and no clue why.
    size_t mtu = NimBLEDevice::getMTU();
    size_t chunk = mtu > 3 ? mtu - 3 : 20; // 3 bytes of ATT header
    size_t sent = 0;
    while (sent < bytes.size()) {
      size_t take = bytes.size() - sent;
      if (take > chunk) take = chunk;
      txCharacteristic->setValue((const uint8_t *)bytes.data() + sent, take);
      txCharacteristic->notify();
      sent += take;
    }
  }
};

BleByteStream bleByteStream;

} // namespace

void init_bleTransport_HAL() {
  /*
    createServer() hands back the one that already exists, so this sits next to
    the HID keyboard rather than replacing it. If the keyboard is switched off,
    init() has not run yet and we do it here.
  */
  if (!NimBLEDevice::getInitialized()) {
    NimBLEDevice::init("OMOTE");
  }

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);

  NimBLEService *service = server->createService(SERVICE_UUID);

  NimBLECharacteristic *rx = service->createCharacteristic(
      RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&rxCallbacks);

  txCharacteristic = service->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);

  service->start();
  server->getAdvertising()->addServiceUUID(SERVICE_UUID);

  omote_log_i("bleTransport: service up, advertising %s\r\n", SERVICE_UUID);
}

transportSession::ByteStream *get_bleTransportByteStream() { return &bleByteStream; }

#endif // ENABLE_BLE_CONFIG
