#if defined(WIN32) || defined(__linux__) || defined(__APPLE__)

#include <cerrno>
#include <cstring>
#include <string>

#if defined(WIN32)
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include "applicationInternal/omote_log.h"
#include "applicationInternal/transport/transportSession.h"

/*
  The configuration transport over a TCP socket, for the simulator.

  On the device this rides on the serial port. The simulator has no serial port
  worth the name - but it does have a loopback interface, and that turns out to
  be worth far more than a faithful imitation would be:

      pio run -e linux_64bit && ./.pio/build/linux_64bit/program &
      uv run tools/omotectl.py --tcp 127.0.0.1:8377 list

  The whole path - transport, storage, loader - can then be exercised on a
  development machine, with the same host tool that talks to real hardware.
  With no OMOTE in existence yet, this is the only way any of step 12 gets
  tried out at all.

  Localhost only, on purpose. This is a debugging aid, not a service: it has no
  authentication and hands out every configuration file on request.
*/

namespace {

const uint16_t PORT = 8377;

class TcpByteStream : public transportSession::ByteStream {
public:
  size_t read(std::string &into) override {
    if (!ensureListening()) return 0;
    acceptIfWaiting();
    if (client < 0) return 0;

    char buffer[512];
#if defined(WIN32)
    int received = recv(client, buffer, sizeof(buffer), 0);
#else
    ssize_t received = recv(client, buffer, sizeof(buffer), 0);
#endif
    if (received > 0) {
      into.append(buffer, (size_t)received);
      return (size_t)received;
    }
    if (received == 0) {
      // the far end hung up
      dropClient("client disconnected");
      return 0;
    }
    // nothing to read right now is the normal case for a non-blocking socket
    return 0;
  }

  void write(const std::string &bytes) override {
    if (client < 0 || bytes.empty()) return;
    size_t sent = 0;
    while (sent < bytes.size()) {
#if defined(WIN32)
      int result = send(client, bytes.data() + sent, (int)(bytes.size() - sent), 0);
#else
      ssize_t result = send(client, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
#endif
      if (result <= 0) {
        dropClient("write failed");
        return;
      }
      sent += (size_t)result;
    }
  }

private:
  int listener = -1;
  int client = -1;
  bool listenFailed = false;

  bool ensureListening() {
    if (listener >= 0) return true;
    // one complaint is enough; trying again every loop would fill the log
    if (listenFailed) return false;

    listener = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
      listenFailed = true;
      return false;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only

    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 1) < 0) {
      omote_log_w("transport: no socket on port %u (%s), the tcp transport is off\r\n",
                  (unsigned)PORT, std::strerror(errno));
      closeSocket(listener);
      listener = -1;
      listenFailed = true;
      return false;
    }

    setNonBlocking(listener);
    omote_log_i("transport: listening on 127.0.0.1:%u\r\n", (unsigned)PORT);
    return true;
  }

  void acceptIfWaiting() {
    if (client >= 0) return;
    int accepted = (int)accept(listener, NULL, NULL);
    if (accepted < 0) return;
    setNonBlocking(accepted);
    client = accepted;
    omote_log_i("transport: client connected\r\n");
  }

  void dropClient(const char *reason) {
    if (client < 0) return;
    closeSocket(client);
    client = -1;
    // the session belongs to the connection: a client that disappears must not
    // leave the log muted
    transportSession::close();
    omote_log_i("transport: %s\r\n", reason);
  }

  static void setNonBlocking(int socketHandle) {
#if defined(WIN32)
    u_long mode = 1;
    ioctlsocket(socketHandle, FIONBIO, &mode);
#else
    int flags = fcntl(socketHandle, F_GETFL, 0);
    fcntl(socketHandle, F_SETFL, flags | O_NONBLOCK);
#endif
  }

  static void closeSocket(int socketHandle) {
#if defined(WIN32)
    closesocket(socketHandle);
#else
    ::close(socketHandle);
#endif
  }
};

TcpByteStream tcpByteStream;

} // namespace

transportSession::ByteStream *get_transportByteStream() { return &tcpByteStream; }

#endif
