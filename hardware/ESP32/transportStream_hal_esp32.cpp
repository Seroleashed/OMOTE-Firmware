#include <Arduino.h>

#include "applicationInternal/transport/transportSession.h"

/*
  The configuration transport over the serial port.

  On Rev5 that is native USB, on Rev1-4 the USB-to-UART bridge - from here both
  look like Serial, which is why the older revisions get this for free.

  Reads are deliberately capped: whatever has arrived is taken in one go, but
  never so much that a single loop iteration turns into a long one. The whole
  point of the session is that the remote stays responsive while a file is
  being written to it.
*/

namespace {

class SerialByteStream : public transportSession::ByteStream {
public:
  size_t read(std::string &into) override {
    size_t count = 0;
    while (Serial.available() > 0 && count < MAX_PER_LOOP) {
      int value = Serial.read();
      if (value < 0) break;
      into += (char)value;
      count++;
    }
    return count;
  }

  void write(const std::string &bytes) override {
    if (bytes.empty()) return;
    Serial.write((const uint8_t *)bytes.data(), bytes.size());
  }

private:
  // one MTU-ish worth per iteration, so the gui keeps redrawing during a
  // transfer rather than stuttering
  static const size_t MAX_PER_LOOP = 512;
};

SerialByteStream serialByteStream;

} // namespace

transportSession::ByteStream *get_transportByteStream() { return &serialByteStream; }
