#include "applicationInternal/omote_log.h"

#include <stdarg.h>
#include <stdio.h>

/*
  The one place every log line passes through. See omote_log.h for why.

  The line is formatted into a fixed buffer rather than handed to Serial piece
  by piece: this runs from the middle of scene handling and from interrupt-ish
  callbacks, and a partially written line interleaved with another one is worse
  than a truncated one.
*/

static int logMuted = 0;

void omote_log_setMuted(int muted) { logMuted = muted; }
int omote_log_isMuted(void) { return logMuted; }

int omote_log_printf(const char *format, ...) {
  if (logMuted) return 0;

  // 256 is enough for every line the firmware writes; the longest are file
  // paths with a message around them. A longer one is cut off rather than
  // dropped, because half a message still says where to look.
  char buffer[256];
  va_list arguments;
  va_start(arguments, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);

  if (length <= 0) return length;
  Serial.printf("%s", buffer);
  return length;
}
