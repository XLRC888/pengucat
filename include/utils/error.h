#ifndef ERROR_H
#define ERROR_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>






#if __STDC_VERSION__ >= 202311L
#  define BONGOCAT_NODISCARD [[nodiscard]]
#else
#  define BONGOCAT_NODISCARD __attribute__((warn_unused_result))
#endif


#if __STDC_VERSION__ >= 202311L
#  define BONGOCAT_NULLPTR nullptr
#else
#  define BONGOCAT_NULLPTR NULL
#endif


#if __STDC_VERSION__ >= 202311L
#  define BONGOCAT_UNREACHABLE() unreachable()
#else
#  define BONGOCAT_UNREACHABLE() __builtin_unreachable()
#endif





typedef enum {
  BONGOCAT_SUCCESS = 0,
  BONGOCAT_ERROR_MEMORY,
  BONGOCAT_ERROR_FILE_IO,
  BONGOCAT_ERROR_WAYLAND,
  BONGOCAT_ERROR_CONFIG,
  BONGOCAT_ERROR_INPUT,
  BONGOCAT_ERROR_ANIMATION,
  BONGOCAT_ERROR_THREAD,
  BONGOCAT_ERROR_INVALID_PARAM
} bongocat_error_t;






#define BONGOCAT_CHECK_NULL(ptr, error_code)                          \
  do {                                                                \
    if ((ptr) == BONGOCAT_NULLPTR) {                                  \
      bongocat_log_error("NULL pointer: %s at %s:%d", #ptr, __FILE__, \
                         __LINE__);                                   \
      return (error_code);                                            \
    }                                                                 \
  } while (0)


#define BONGOCAT_CHECK_ERROR(condition, error_code, message)          \
  do {                                                                \
    if (condition) {                                                  \
      bongocat_log_error("%s at %s:%d", message, __FILE__, __LINE__); \
      return (error_code);                                            \
    }                                                                 \
  } while (0)


#define BONGOCAT_GUARD(condition, return_value) \
  do {                                          \
    if (condition) {                            \
      return (return_value);                    \
    }                                           \
  } while (0)





void bongocat_log_error(const char *format, ...);
void bongocat_log_warning(const char *format, ...);
void bongocat_log_info(const char *format, ...);
void bongocat_log_debug(const char *format, ...);





void bongocat_error_init(int enable_debug);
BONGOCAT_NODISCARD const char *bongocat_error_string(bongocat_error_t error);

#endif  