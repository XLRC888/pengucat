#ifndef INPUT_H
#define INPUT_H

#include "core/bongocat.h"
#include "utils/error.h"

#include <stdatomic.h>






extern atomic_int *any_key_pressed;

extern atomic_int *last_key_code;






BONGOCAT_NODISCARD bongocat_error_t
input_start_monitoring(char **device_paths, int num_devices, char **names,
                       int num_names, int scan_interval, int enable_debug);


BONGOCAT_NODISCARD bongocat_error_t
input_restart_monitoring(char **device_paths, int num_devices, char **names,
                         int num_names, int scan_interval, int enable_debug);


void input_cleanup(void);


pid_t input_get_child_pid(void);


int input_get_wake_fd(void);

#endif  