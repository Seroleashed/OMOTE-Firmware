#pragma once

#include <stdint.h>

#include <string>

/*
  Puts the configuration transport onto a byte stream that is also used for
  something else - which, on this device, the serial port always is.

  ## The problem this solves

  There is one serial port. The log writes to it continuously, and it is the
  only way a host can talk to the remote over USB. A log line landing in the
  middle of a file transfer corrupts the file; a host reading the log cannot
  tell a protocol answer from a status message.

  So the protocol is a *session*. Outside one, the port is the log, exactly as
  before. A host opens a session by sending a line nothing else would send:

      > OMOTE-CONFIG-V1
      < OMOTE-CONFIG-V1 READY
      ... the protocol of configTransport.h ...
      > BYE
      < OMOTE-CONFIG-V1 CLOSED

  While the session is open the log is silent. That is the whole trick, and it
  is why a session has to end reliably - see below.

  ## Why a timeout

  A cable pulled out mid-session would otherwise leave the log muted until the
  next restart, and the device silent for no visible reason. Anything the user
  cannot explain, they cannot work around. So a session with nothing arriving
  for a while closes itself and the log comes back.
*/

namespace transportSession {

// what a host sends to get the protocol's attention
extern const char *const MAGIC;

// A session with nothing arriving for this long closes itself.
const unsigned long IDLE_TIMEOUT_MS = 60000;

/*
  The byte stream underneath. Serial on the device, a TCP socket in the
  simulator, a recording fake in the tests - the session logic does not care,
  which is why all of it can be tested without any of them.
*/
class ByteStream {
public:
  virtual ~ByteStream() {}
  // appends whatever has arrived, returns how much. Never blocks.
  virtual size_t read(std::string &into) = 0;
  virtual void write(const std::string &bytes) = 0;
};

// How many links can be waiting for a host at once. Two today: the serial port
// and BLE.
const size_t MAX_STREAMS = 3;

void begin(ByteStream *stream);
// A second way in - BLE next to the cable. Both are watched for the magic line.
bool addStream(ByteStream *stream);

void loop(unsigned long currentMillis);

bool isActive();
/*
  Which link the running session belongs to, or NULL.

  A session belongs to the stream that opened it, and the others are left alone
  until it closes. Two hosts talking into the same protocol state would
  interleave a LIST into the middle of somebody else's file transfer, and
  neither would get an error - the file would just be wrong.
*/
ByteStream *activeStream();

// ends a running session and unmutes the log. For a link that went away.
void close();

} // namespace transportSession

// provided by the active hardware layer: Serial on the device, a TCP socket on
// localhost in the simulator
transportSession::ByteStream *get_transportByteStream();

#if (ENABLE_BLE_CONFIG == 1)
// The BLE service next to the HID keyboard. Only on the ESP32 - NimBLE does
// not exist in the simulator.
void init_bleTransport_HAL();
transportSession::ByteStream *get_bleTransportByteStream();
#endif
