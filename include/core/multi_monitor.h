#ifndef MULTI_MONITOR_H
#define MULTI_MONITOR_H

#include <stddef.h>





#define MULTI_MONITOR_MAX_OUTPUTS 16


int multi_monitor_launch(int argc, char *argv[], const char *config_path,
                         int watch_config, char **output_names,
                         size_t output_count);

#endif  
