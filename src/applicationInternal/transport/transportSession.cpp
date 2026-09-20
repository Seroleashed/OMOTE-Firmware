#include "applicationInternal/transport/transportSession.h"

#include "applicationInternal/omote_log.h"
#include "applicationInternal/transport/configTransport.h"

namespace transportSession {

const char *const MAGIC = "OMOTE-CONFIG-V1";

namespace {

ByteStream *streams[MAX_STREAMS] = {NULL, NULL, NULL};
size_t streamCount = 0;

// the link a running session belongs to, NULL when there is none
ByteStream *stream = NULL;
// one partial line per link: two hosts may each be halfway through typing
std::string pendingLines[MAX_STREAMS];

bool active = false;
std::string pendingLine;
unsigned long lastActivityMs = 0;

void sendLine(const std::string &line) {
  if (stream != NULL) stream->write(line + "\n");
}

void open() {
  active = true;
  pendingLine.clear();
  configTransport::reset();
  // From here the port belongs to the protocol. A log line in the middle of a
  // transfer would corrupt the file being moved.
  omote_log_setMuted(1);
  sendLine(std::string(MAGIC) + " READY");
}

void finish(const char *reason) {
  if (!active) return;
  active = false;
  configTransport::reset();
  sendLine(std::string(MAGIC) + " CLOSED");
  omote_log_setMuted(0);
  // the first line after the log comes back says why it went away
  omote_log_i("transport: session closed (%s)\r\n", reason);
  // the link is free again, and so are the others
  stream = NULL;
}

} // namespace

void begin(ByteStream *aStream) {
  streams[0] = aStream;
  streamCount = aStream != NULL ? 1 : 0;
  for (size_t i = 1; i < MAX_STREAMS; i++) streams[i] = NULL;
  for (size_t i = 0; i < MAX_STREAMS; i++) pendingLines[i].clear();

  stream = NULL;
  active = false;
  pendingLine.clear();
  lastActivityMs = 0;
}

bool addStream(ByteStream *aStream) {
  if (aStream == NULL || streamCount >= MAX_STREAMS) return false;
  streams[streamCount] = aStream;
  streamCount++;
  return true;
}

bool isActive() { return active; }

ByteStream *activeStream() { return active ? stream : NULL; }

void close() { finish("closed by the firmware"); }

// Watches one link for the magic line. Returns true if it opened a session.
static bool watchForMagic(size_t index, unsigned long currentMillis) {
  ByteStream *candidate = streams[index];
  std::string incoming;
  if (candidate->read(incoming) == 0) return false;

  lastActivityMs = currentMillis;
  std::string &partial = pendingLines[index];

  // Outside a session a link belongs to whatever it was before - the log, on
  // the serial port. So only one thing is looked for here: the magic line.
  // Everything else is somebody with a terminal open, typing whatever.
  for (size_t i = 0; i < incoming.size(); i++) {
    char character = incoming[i];
    if (character == '\n') {
      if (!partial.empty() && partial[partial.size() - 1] == '\r') {
        partial.erase(partial.size() - 1);
      }
      if (partial == MAGIC) {
        partial.clear();
        stream = candidate;
        open();
        // whatever else arrived in the same read is already protocol
        if (i + 1 < incoming.size()) {
          configTransport::feed(incoming.substr(i + 1));
          stream->write(configTransport::takeOutput());
        }
        return true;
      }
      partial.clear();
    } else {
      partial += character;
      // a terminal left open collects keystrokes forever otherwise
      if (partial.size() > 64) partial.clear();
    }
  }
  return false;
}

void loop(unsigned long currentMillis) {
  if (streamCount == 0) return;

  if (!active) {
    for (size_t i = 0; i < streamCount; i++) {
      if (watchForMagic(i, currentMillis)) return;
    }
    return;
  }

  /*
    A session belongs to the link that opened it. The others are not read at
    all until it closes - two hosts talking into the same protocol state would
    interleave a LIST into the middle of somebody else's file transfer, and
    neither would get an error. The file would just be wrong.
  */
  std::string incoming;
  size_t received = stream->read(incoming);
  if (received > 0) lastActivityMs = currentMillis;

  // --- inside a session ------------------------------------------------------
  if (!incoming.empty()) {
    configTransport::feed(incoming);
    std::string answer = configTransport::takeOutput();
    if (!answer.empty()) stream->write(answer);

    /*
      Whether a BYE arrived is asked of the protocol rather than looked for in
      the bytes here: that layer is the one that knows where a line ends, and
      it knows not to read anything as a command while a PUT is running. A JSON
      file with BYE on a line of its own is perfectly ordinary and must not
      close the session.
    */
    if (configTransport::takeGoodbye()) {
      finish("BYE");
      return;
    }
  }

  // A cable pulled out mid-session would otherwise leave the log muted until
  // the next restart, with the device silent for no reason the user can see.
  if (currentMillis - lastActivityMs > IDLE_TIMEOUT_MS) {
    finish("idle timeout");
  }
}

} // namespace transportSession
