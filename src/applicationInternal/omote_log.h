#ifndef __OMOTE_LOG_H__
#define __OMOTE_LOG_H__

// The macros below expand to Serial.printf() and millis(). Both used to have to
// be in scope already at the point of inclusion, which happened to be true for
// every file that included hardwarePresenter.h first. A file that only wants to
// log failed to compile - and only in builds where the log level is high enough
// to expand the macros, so the unit tests (log level NONE) stayed green while
// the firmware build broke. Including the arduino layer here makes this header
// stand on its own.
#include "applicationInternal/hardware/arduinoLayer.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OMOTE_LOG_LEVEL_NONE       (0)
#define OMOTE_LOG_LEVEL_ERROR      (1)
#define OMOTE_LOG_LEVEL_WARN       (2)
#define OMOTE_LOG_LEVEL_INFO       (3)
#define OMOTE_LOG_LEVEL_DEBUG      (4)
#define OMOTE_LOG_LEVEL_VERBOSE    (5)

#ifndef CONFIG_OMOTE_LOG_DEFAULT_LEVEL
#define CONFIG_OMOTE_LOG_DEFAULT_LEVEL OMOTE_LOG_LEVEL_NONE
#endif

#ifndef OMOTE_LOG_LEVEL
#define OMOTE_LOG_LEVEL CONFIG_OMOTE_LOG_DEFAULT_LEVEL
#endif

#define OMOTE_LOG_FORMAT(letter, format) "[OMOTE " #letter "][%8lu]: " format , (unsigned long) millis()

/*
  Every log line goes through here rather than straight to Serial.

  The serial port is shared: the log writes to it, and from step 12 the
  configuration transport talks over it as well. A log line landing in the
  middle of a file transfer would corrupt the file, so a transport session
  silences the log for its duration with omote_log_setMuted().

  The indirection is also where the ring buffer of step 26 will hook in - a
  second sink is then one function rather than forty call sites.
*/
int omote_log_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void omote_log_setMuted(int muted);
int omote_log_isMuted(void);

#if OMOTE_LOG_LEVEL >= OMOTE_LOG_LEVEL_VERBOSE
#define omote_log_v(format, ...) omote_log_printf(OMOTE_LOG_FORMAT(V, format), ##__VA_ARGS__)
#else
#define omote_log_v(format, ...)  do {} while(0)
#endif

#if OMOTE_LOG_LEVEL >= OMOTE_LOG_LEVEL_DEBUG
#define omote_log_d(format, ...) omote_log_printf(OMOTE_LOG_FORMAT(D, format), ##__VA_ARGS__)
#else
#define omote_log_d(format, ...)  do {} while(0)
#endif

#if OMOTE_LOG_LEVEL >= OMOTE_LOG_LEVEL_INFO
#define omote_log_i(format, ...) omote_log_printf(OMOTE_LOG_FORMAT(I, format), ##__VA_ARGS__)
#else
#define omote_log_i(format, ...) do {} while(0)
#endif

#if OMOTE_LOG_LEVEL >= OMOTE_LOG_LEVEL_WARN
#define omote_log_w(format, ...) omote_log_printf(OMOTE_LOG_FORMAT(W, format), ##__VA_ARGS__)
#else
#define omote_log_w(format, ...) do {} while(0)
#endif

#if OMOTE_LOG_LEVEL >= OMOTE_LOG_LEVEL_ERROR
#define omote_log_e(format, ...) omote_log_printf(OMOTE_LOG_FORMAT(E, format), ##__VA_ARGS__)
#else
#define omote_log_e(format, ...) do {} while(0)
#endif

#if OMOTE_LOG_LEVEL >= OMOTE_LOG_LEVEL_NONE
#define omote_log_n(format, ...) omote_log_printf(OMOTE_LOG_FORMAT(E, format), ##__VA_ARGS__)
#else
#define omote_log_n(format, ...) do {} while(0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __OMOTE_LOG_H__ */
