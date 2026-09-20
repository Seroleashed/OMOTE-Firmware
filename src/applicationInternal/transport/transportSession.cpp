#include "applicationInternal/transport/transportSession.h"

#include "applicationInternal/omote_log.h"
#include "applicationInternal/transport/configTransport.h"

namespace transportSession {

const char *const MAGIC = "OMOTE-CONFIG-V1";

namespace {

ByteStream *stream = NULL;
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
}

} // namespace

void begin(ByteStream *aStream) {
  stream = aStream;
  active = false;
  pendingLine.clear();
  lastActivityMs = 0;
}

bool isActive() { return active; }

void close() { finish("closed by the firmware"); }

void loop(unsigned long currentMillis) {
  if (stream == NULL) return;

  std::string incoming;
  size_t received = stream->read(incoming);

  if (received > 0) lastActivityMs = currentMillis;

  if (!active) {
    // Outside a session the port belongs to the log, so only one thing is
    // looked for: the magic line. Everything else is somebody with a terminal
    // open, and they get to type whatever they like.
    for (size_t i = 0; i < incoming.size(); i++) {
      char character = incoming[i];
      if (character == '\n') {
        if (!pendingLine.empty() && pendingLine[pendingLine.size() - 1] == '\r') {
          pendingLine.erase(pendingLine.size() - 1);
        }
        if (pendingLine == MAGIC) {
          pendingLine.clear();
          open();
          // whatever else arrived in the same read is already protocol
          if (i + 1 < incoming.size()) {
            std::string rest = incoming.substr(i + 1);
            configTransport::feed(rest);
            stream->write(configTransport::takeOutput());
          }
          return;
        }
        pendingLine.clear();
      } else {
        pendingLine += character;
        // a terminal left open collects keystrokes forever otherwise
        if (pendingLine.size() > 64) pendingLine.clear();
      }
    }
    return;
  }

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
