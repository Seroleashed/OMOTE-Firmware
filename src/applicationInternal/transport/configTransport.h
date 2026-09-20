#pragma once

#include <stdint.h>

#include <string>
#include <vector>

/*
  A small line based protocol for moving configuration files on and off the
  device, over any byte stream at all.

  The transport knows nothing about JSON. It moves files. That is deliberate:
  USB (step 12) and BLE (step 13) then share every line of this, and the
  protocol does not have to change when a new kind of configuration file turns
  up.

  ## The conversation

      > LIST
      < OK 2
      < /cfg/devices/lgTV.json 4821
      < /cfg/system.json 312
      < END

      > GET /cfg/system.json
      < OK 312 1c291ca3
      < {"schemaVersion":1, ... }
      < END

      > PUT /cfg/devices/tv.json 512 deadbeef
      < READY
      > ...512 bytes...
      < ACK 256                     (one per chunk, so a slow link can keep up)
      < OK

      > DEL /cfg/devices/tv.json
      < OK

      > INFO
      < OK
      < version 0.9.0-dev
      < ...
      < END

      > APPLY      -> the firmware re-reads what was just written
      > REBOOT     -> restart

  Anything that goes wrong answers with one line: `ERR <what happened>`, in
  words meant for whoever is at the other end.

  ## Why acknowledgements

  BLE hands over something like twenty bytes at a time. Without a word back
  from the device, the sender has no idea whether the far end is keeping up or
  whether the file is landing in a buffer that overflowed two chunks ago. So
  the device says how much it has, and the sender waits for it.

  ## Payload, not envelope

  What crosses the wire is the file as an editor would show it - plain JSON.
  The storage envelope (magic, length, crc32) is added when the device writes
  the file and stripped when it reads one, and never leaves the device. See
  configFile.h for why.

  ## Everything is testable without a stream

  The protocol is fed bytes and hands back bytes. No serial port, no BLE stack,
  no timing. feed() what the far end said, read what to send with takeOutput().
*/

class ConfigFileSystem;

namespace configTransport {

// How much the device takes before it says ACK. Small enough that a BLE link
// gets an answer while the sender still has buffer left.
const size_t CHUNK_SIZE = 256;

// A file larger than this is refused before a single byte is stored. The
// biggest thing we ship is a 45 command device pack at about 6 kB.
const size_t MAX_FILE_SIZE = 64 * 1024;

// A line longer than this cannot be a command of ours and is more likely a
// stream that has lost sync, so it is dropped rather than buffered forever.
const size_t MAX_LINE_LENGTH = 512;

/*
  What the firmware has to provide. Kept as callbacks rather than direct calls
  so the protocol stays free of everything it would otherwise drag in - and so
  a test can watch what was asked of it.
*/
struct Callbacks {
  // re-read the configuration that was just written. May be NULL.
  void (*apply)() = NULL;
  // restart the device. May be NULL.
  void (*reboot)() = NULL;
  // lines for INFO: "version 0.9.0-dev". May be NULL.
  std::vector<std::string> (*info)() = NULL;
};

void begin(ConfigFileSystem *fileSystem, const Callbacks &callbacks);

// Bytes that arrived from the far end.
void feed(const std::string &bytes);

// Bytes to send back. Empties the buffer.
std::string takeOutput();

// Drops a half finished transfer and any buffered input. For a link that was
// disconnected in the middle of a PUT.
void reset();

// true while a PUT is in progress
bool isReceiving();

} // namespace configTransport
