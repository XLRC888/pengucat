#ifndef HYPRLAND_H
#define HYPRLAND_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>





typedef struct {
  int monitor_id;  
  int x, y;
  int width, height;
  bool fullscreen;
} window_info_t;







ssize_t safe_exec_read(const char *const argv[], char *buf, size_t buf_size);


void hypr_update_outputs_with_monitor_ids(void);


bool hypr_get_active_window(window_info_t *win);

#endif  
