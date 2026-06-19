#ifndef CONFIG_H
#define CONFIG_H

#include "core/bongocat.h"
#include "utils/error.h"

#include <stdbool.h>





typedef enum {
  POSITION_TOP = 0,
  POSITION_BOTTOM = 1
} overlay_position_t;

typedef enum {
  LAYER_TOP = 0,
  LAYER_OVERLAY = 1
} layer_type_t;

typedef enum {
  ALIGN_LEFT = -1,
  ALIGN_CENTER = 0,
  ALIGN_RIGHT = 1,
} align_type_t;





typedef struct {
  int hour;
  int min;
} config_time_t;

typedef struct {
  
  int screen_width;
  char *output_name;
  char **output_names;
  int num_output_names;
  int overlay_height;
  int overlay_opacity;
  layer_type_t layer;
  overlay_position_t overlay_position;

  
  const char *asset_paths[NUM_FRAMES];
  int cat_x_offset;
  int cat_y_offset;
  int cat_height;
  int mirror_x;             
  int mirror_y;             
  int enable_antialiasing;  
  align_type_t cat_align;

  
  int idle_frame;
  int keypress_duration;
  int test_animation_duration;
  int test_animation_interval;
  int fps;
  int enable_hand_mapping;  

  
  char **keyboard_devices;
  int num_keyboard_devices;
  int hotplug_scan_interval;

  
  char **keyboard_names;
  int num_names;

  
  int enable_scheduled_sleep;
  config_time_t sleep_begin;
  config_time_t sleep_end;
  int idle_sleep_timeout_sec;

  
  int disable_fullscreen_hide;

  
  int enable_debug;
} config_t;






BONGOCAT_NODISCARD bongocat_error_t load_config(config_t *config,
                                                const char *config_file_path);


BONGOCAT_NODISCARD int get_screen_width(void);




char *config_resolve_path(const char *explicit_path);


void config_cleanup(void);
void config_cleanup_full(config_t *config);

#endif  
