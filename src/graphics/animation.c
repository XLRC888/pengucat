#define _POSIX_C_SOURCE 199309L
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "graphics/animation.h"

#include "graphics/embedded_assets.h"
#include "platform/input.h"
#include "platform/wayland.h"
#include "utils/memory.h"

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#  pragma GCC diagnostic ignored "-Wmissing-prototypes"
#  pragma GCC diagnostic ignored "-Wstrict-prototypes"
#  pragma GCC diagnostic ignored "-Wold-style-definition"
#endif
#include <nanosvg.h>
#include <nanosvgrast.h>
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
#include <poll.h>
#include <time.h>
#include <unistd.h>





int anim_index = 0;
pthread_mutex_t anim_lock = PTHREAD_MUTEX_INITIALIZER;
cached_frame_t anim_cached_frames[NUM_FRAMES] = {0};


static NSVGimage *anim_svgs[NUM_FRAMES];
static NSVGrasterizer *anim_rasterizer;


static config_t *current_config;
static pthread_t anim_thread;
static atomic_bool animation_running = false;
static bool animation_thread_started = false;
static bool animation_initialized = false;





typedef struct {
  long hold_until;
  int test_counter;
  int test_interval_frames;
  long frame_time_ns;
  long last_key_pressed_timestamp;
  int last_held_keycode;
} animation_state_t;

static long anim_get_current_time_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static bool anim_is_sleep_time(const config_t *config) {
  time_t raw_time;
  struct tm time_info;
  time(&raw_time);
  localtime_r(&raw_time, &time_info);

  const int now_minutes = time_info.tm_hour * 60 + time_info.tm_min;
  const int begin = config->sleep_begin.hour * 60 + config->sleep_begin.min;
  const int end = config->sleep_end.hour * 60 + config->sleep_end.min;

  
  
  
  return (begin == end) ||
         (begin < end ? (now_minutes >= begin && now_minutes < end)
                      : (now_minutes >= begin || now_minutes < end));
}



static int get_frame_for_keycode(int keycode) {
  
  
  static const int left_keys[] = {
    
    2, 3, 4, 5, 6, 7,           
    
    16, 17, 18, 19, 20,         
    
    30, 31, 32, 33, 34,         
    
    44, 45, 46, 47, 48,         
    
    1,                          
    15,                         
    58,                         
    42,                         
    29,                         
    56,                         
    41,                         
    125,                        
      59, 60, 61, 62, 63, 64,   
  };


  for (size_t i = 0; i < sizeof(left_keys) / sizeof(left_keys[0]); i++) {
    if (keycode == left_keys[i]) {
      return 1;  
    }
  }
  return 2;  
}

static int anim_get_active_frame(void) {
  if (current_config && current_config->enable_hand_mapping) {
    int keycode = atomic_load(last_key_code);
    int frame = get_frame_for_keycode(keycode);
    
    if (current_config->mirror_x) {
      frame = (frame == 1) ? 2 : 1;
    }
    return frame;
  }
  return (rand() % 2) + 1;  
}

static void anim_trigger_frame_change(int new_frame, long duration_us,
                                      long current_time_us,
                                      animation_state_t *state) {
  if (current_config->enable_debug) {
    bongocat_log_debug("Animation frame change: %d (duration: %ld us)",
                       new_frame, duration_us);
  }

  anim_index = new_frame;
  state->hold_until = current_time_us + duration_us;
}

static void anim_handle_test_animation(animation_state_t *state,
                                       long current_time_us) {
  if (current_config->test_animation_interval <= 0) {
    return;
  }

  state->test_counter++;
  if (state->test_counter > state->test_interval_frames) {
    int new_frame = anim_get_active_frame();
    long duration_us = current_config->test_animation_duration * 1000;

    bongocat_log_debug("Test animation trigger");
    anim_trigger_frame_change(new_frame, duration_us, current_time_us, state);
    state->test_counter = 0;
  }
}

static void anim_handle_key_press(animation_state_t *state,
                                   long current_time_us) {
  if (!atomic_load(any_key_pressed)) {
    return;
  }

  atomic_store(any_key_pressed, 0);

  int left = atomic_load(left_paw_active);
  int right = atomic_load(right_paw_active);
  int prev_left = atomic_load(prev_left_paw_active);
  int prev_right = atomic_load(prev_right_paw_active);
  int new_frame;

  if (prev_left && !prev_right) {
    new_frame = BONGOCAT_FRAME_BOTH_DOWN;
  } else if (prev_right && !prev_left) {
    new_frame = BONGOCAT_FRAME_BOTH_DOWN;
  } else if (left && right) {
    new_frame = BONGOCAT_FRAME_BOTH_DOWN;
  } else if (left) {
    new_frame = BONGOCAT_FRAME_LEFT_DOWN;
  } else if (right) {
    new_frame = BONGOCAT_FRAME_RIGHT_DOWN;
  } else {
    return;
  }

  if (current_config->mirror_x) {
    if (new_frame == BONGOCAT_FRAME_LEFT_DOWN)
      new_frame = BONGOCAT_FRAME_RIGHT_DOWN;
    else if (new_frame == BONGOCAT_FRAME_RIGHT_DOWN)
      new_frame = BONGOCAT_FRAME_LEFT_DOWN;
  }

  if (new_frame != anim_index) {
    long duration_us = current_config->keypress_duration * 1000;
    anim_trigger_frame_change(new_frame, duration_us, current_time_us, state);
  }

  state->test_counter = 0;
  state->last_key_pressed_timestamp = current_time_us;
}

static void anim_handle_idle_return(animation_state_t *state,
                                    long current_time_us) {
  int show_sleep_frame = 0;
  
  if (current_config->enable_scheduled_sleep) {
    if (anim_is_sleep_time(current_config)) {
      show_sleep_frame = 1;
    }
  }
  
  if (current_config->idle_sleep_timeout_sec > 0 &&
      state->last_key_pressed_timestamp > 0) {
    if (anim_get_current_time_us() - state->last_key_pressed_timestamp >=
        current_config->idle_sleep_timeout_sec * 1000000L) {
      show_sleep_frame = 1;
    }
  }

  if (show_sleep_frame) {
    if (anim_index != BONGOCAT_FRAME_SLEEPING) {
      bongocat_log_debug("Returning to sleep frame");
      anim_index = BONGOCAT_FRAME_SLEEPING;
    }
    return;
  }

  if (current_time_us <= state->hold_until) {
    return;
  }

  int l = atomic_load(left_paw_active);
  int r = atomic_load(right_paw_active);

  if (l && !r) {
    if (anim_index != BONGOCAT_FRAME_LEFT_DOWN) {
      anim_trigger_frame_change(BONGOCAT_FRAME_LEFT_DOWN, 0, current_time_us, state);
    }
    return;
  }
  if (r && !l) {
    if (anim_index != BONGOCAT_FRAME_RIGHT_DOWN) {
      anim_trigger_frame_change(BONGOCAT_FRAME_RIGHT_DOWN, 0, current_time_us, state);
    }
    return;
  }

  if (l || r) {
    return;
  }

  if (anim_index != current_config->idle_frame) {
    bongocat_log_debug("Returning to idle frame %d",
                       current_config->idle_frame);
    anim_index = current_config->idle_frame;
  }
}

static void anim_update_state(animation_state_t *state) {
  long current_time_us = anim_get_current_time_us();

  pthread_mutex_lock(&anim_lock);

  anim_handle_test_animation(state, current_time_us);
  anim_handle_key_press(state, current_time_us);
  anim_handle_idle_return(state, current_time_us);

  pthread_mutex_unlock(&anim_lock);
}





static void anim_init_state(animation_state_t *state) {
  state->hold_until = 0;
  state->test_counter = 0;
  state->test_interval_frames =
      current_config->test_animation_interval * current_config->fps;
  state->frame_time_ns = 1000000000L / current_config->fps;
  state->last_key_pressed_timestamp = anim_get_current_time_us();
  state->last_held_keycode = 0;
}

static void *anim_thread_main([[maybe_unused]] void *arg) {
  animation_state_t state;
  anim_init_state(&state);

  struct timespec frame_delay = {0, state.frame_time_ns};

  bongocat_log_debug("Animation thread main loop started");

  
  int last_drawn_frame = -1;
  bool force_redraw = true;  

  while (animation_running) {
    int prev_frame = anim_index;
    anim_update_state(&state);

    
    bool frame_changed = (anim_index != last_drawn_frame);
    bool state_changed = (anim_index != prev_frame);

    
    if (frame_changed || force_redraw) {
      draw_bar();
      last_drawn_frame = anim_index;
      force_redraw = false;
    }

    
    
    bool animating = state_changed || anim_index != current_config->idle_frame;
    if (animating) {
      nanosleep(&frame_delay, NULL);
    } else {
      int wfd = input_get_wake_fd();
      if (wfd >= 0) {
        struct pollfd pfd = {.fd = wfd, .events = POLLIN};
        poll(&pfd, 1, 1000);
        if (pfd.revents & POLLIN) {
          uint64_t val;
          if (read(wfd, &val, sizeof(val)) < 0) {
            
          }
        }
      } else {
        long idle_ns = state.frame_time_ns * 2;
        if (idle_ns > 999999999L)
          idle_ns = 999999999L;
        struct timespec idle_delay = {0, idle_ns};
        nanosleep(&idle_delay, NULL);
      }
    }
  }

  bongocat_log_debug("Animation thread main loop exited");
  return NULL;
}





typedef struct {
  const unsigned char *data;
  size_t size;
  const char *name;
} embedded_svg_t;

static embedded_svg_t embedded_svgs[NUM_FRAMES];

static void init_embedded_svgs(void) {
  embedded_svgs[BONGOCAT_FRAME_BOTH_UP] = (embedded_svg_t){
      bongo_both_up_svg, bongo_both_up_svg_size, "bongo-both-up.svg"};
  embedded_svgs[BONGOCAT_FRAME_LEFT_DOWN] = (embedded_svg_t){
      bongo_left_down_svg, bongo_left_down_svg_size, "bongo-left-down.svg"};
  embedded_svgs[BONGOCAT_FRAME_RIGHT_DOWN] = (embedded_svg_t){
      bongo_right_down_svg, bongo_right_down_svg_size, "bongo-right-down.svg"};
  embedded_svgs[BONGOCAT_FRAME_BOTH_DOWN] = (embedded_svg_t){
      bongo_both_down_svg, bongo_both_down_svg_size, "bongo-both-down.svg"};
  embedded_svgs[BONGOCAT_FRAME_SLEEPING] = (embedded_svg_t){
      bongo_sleeping_svg, bongo_sleeping_svg_size, "bongo-sleeping.svg"};
}

static void anim_cleanup_svgs(void) {
  for (int i = 0; i < NUM_FRAMES; i++) {
    if (anim_svgs[i]) {
      nsvgDelete(anim_svgs[i]);
      anim_svgs[i] = NULL;
    }
  }
  if (anim_rasterizer) {
    nsvgDeleteRasterizer(anim_rasterizer);
    anim_rasterizer = NULL;
  }
}

static bongocat_error_t anim_parse_embedded_svgs(void) {
  for (int i = 0; i < NUM_FRAMES; i++) {
    const embedded_svg_t *svg = &embedded_svgs[i];

    bongocat_log_debug("Parsing embedded SVG: %s", svg->name);

    
    char *svg_copy = malloc(svg->size + 1);
    if (!svg_copy) {
      bongocat_log_error("Failed to allocate SVG copy for: %s", svg->name);
      anim_cleanup_svgs();
      return BONGOCAT_ERROR_MEMORY;
    }
    memcpy(svg_copy, svg->data, svg->size);
    svg_copy[svg->size] = '\0';

    anim_svgs[i] = nsvgParse(svg_copy, "px", 96.0f);
    free(svg_copy);

    if (!anim_svgs[i]) {
      bongocat_log_error("Failed to parse embedded SVG: %s", svg->name);
      anim_cleanup_svgs();
      return BONGOCAT_ERROR_FILE_IO;
    }

    bongocat_log_debug("Parsed SVG %s: %.0fx%.0f", svg->name,
                       (double)anim_svgs[i]->width,
                       (double)anim_svgs[i]->height);
  }

  anim_rasterizer = nsvgCreateRasterizer();
  if (!anim_rasterizer) {
    bongocat_log_error("Failed to create SVG rasterizer");
    anim_cleanup_svgs();
    return BONGOCAT_ERROR_MEMORY;
  }

  return BONGOCAT_SUCCESS;
}





void animation_invalidate_cache(void) {
  for (int i = 0; i < NUM_FRAMES; i++) {
    free(anim_cached_frames[i].data);
    anim_cached_frames[i].data = NULL;
    anim_cached_frames[i].width = 0;
    anim_cached_frames[i].height = 0;
  }
}

void animation_cache_frames(int target_w, int target_h, int mirror_x,
                            int mirror_y, [[maybe_unused]] int enable_aa) {
  animation_invalidate_cache();

  if (!anim_rasterizer || target_w <= 0 || target_h <= 0) {
    return;
  }

  for (int i = 0; i < NUM_FRAMES; i++) {
    if (!anim_svgs[i]) {
      continue;
    }

    float svg_w = anim_svgs[i]->width;
    float svg_h = anim_svgs[i]->height;
    if (svg_w <= 0 || svg_h <= 0) {
      continue;
    }

    
    float scale = (float)target_w / svg_w;
    size_t buf_size = (size_t)target_w * (size_t)target_h * 4U;
    uint8_t *rgba_buf = calloc(1, buf_size);
    if (!rgba_buf) {
      bongocat_log_error("Failed to allocate raster buffer for frame %d", i);
      continue;
    }

    nsvgRasterize(anim_rasterizer, anim_svgs[i], 0, 0, scale, rgba_buf,
                  target_w, target_h, target_w * 4);

    
    if (mirror_x) {
      for (int y = 0; y < target_h; y++) {
        for (int left = 0, right = target_w - 1; left < right;
             left++, right--) {
          int li = (y * target_w + left) * 4;
          int ri = (y * target_w + right) * 4;
          uint8_t tmp[4];
          memcpy(tmp, &rgba_buf[li], 4);
          memcpy(&rgba_buf[li], &rgba_buf[ri], 4);
          memcpy(&rgba_buf[ri], tmp, 4);
        }
      }
    }

    
    if (mirror_y) {
      size_t row_bytes = (size_t)target_w * 4U;
      uint8_t *tmp_row = malloc(row_bytes);
      if (tmp_row) {
        for (int top = 0, bot = target_h - 1; top < bot; top++, bot--) {
          uint8_t *t = &rgba_buf[(size_t)top * row_bytes];
          uint8_t *b = &rgba_buf[(size_t)bot * row_bytes];
          memcpy(tmp_row, t, row_bytes);
          memcpy(t, b, row_bytes);
          memcpy(b, tmp_row, row_bytes);
        }
        free(tmp_row);
      }
    }

    
    
    for (size_t px = 0; px < buf_size; px += 4) {
      uint8_t r = rgba_buf[px + 0];
      uint8_t g = rgba_buf[px + 1];
      uint8_t b = rgba_buf[px + 2];
      uint8_t a = rgba_buf[px + 3];
      rgba_buf[px + 0] = (uint8_t)((b * a) / 255);
      rgba_buf[px + 1] = (uint8_t)((g * a) / 255);
      rgba_buf[px + 2] = (uint8_t)((r * a) / 255);
      rgba_buf[px + 3] = a;
    }

    anim_cached_frames[i].data = rgba_buf;
    anim_cached_frames[i].width = target_w;
    anim_cached_frames[i].height = target_h;
  }

  bongocat_log_debug("Cached %d animation frames at %dx%d", NUM_FRAMES,
                     target_w, target_h);
}

void blit_cached_frame(uint8_t *dest, int dest_w, int dest_h,
                       const uint8_t *src, int src_w, int src_h, int offset_x,
                       int offset_y) {
  for (int y = 0; y < src_h; y++) {
    int dy = y + offset_y;
    if (dy < 0 || dy >= dest_h)
      continue;
    for (int x = 0; x < src_w; x++) {
      int dx = x + offset_x;
      if (dx < 0 || dx >= dest_w)
        continue;
      int si = (y * src_w + x) * 4;
      int di = (dy * dest_w + dx) * 4;
      uint8_t sa = src[si + 3];
      if (sa == 0)
        continue;
      if (sa == 255) {
        memcpy(&dest[di], &src[si], 4);
      } else {
        
        uint8_t inv_a = 255 - sa;
        dest[di + 0] = src[si + 0] + (uint8_t)((dest[di + 0] * inv_a) / 255);
        dest[di + 1] = src[si + 1] + (uint8_t)((dest[di + 1] * inv_a) / 255);
        dest[di + 2] = src[si + 2] + (uint8_t)((dest[di + 2] * inv_a) / 255);
        dest[di + 3] = sa + (uint8_t)((dest[di + 3] * inv_a) / 255);
      }
    }
  }
}





bongocat_error_t animation_init(config_t *config) {
  BONGOCAT_CHECK_NULL(config, BONGOCAT_ERROR_INVALID_PARAM);

  current_config = config;
  bongocat_log_info("Initializing animation system");

  
  init_embedded_svgs();

  bongocat_error_t result = anim_parse_embedded_svgs();
  if (result != BONGOCAT_SUCCESS) {
    return result;
  }

  animation_initialized = true;

  
  srand((unsigned)time(NULL));

  bongocat_log_info(
      "Animation system initialized successfully with embedded SVG assets");
  return BONGOCAT_SUCCESS;
}

bongocat_error_t animation_start(void) {
  if (animation_thread_started) {
    bongocat_log_warning("Animation thread already running");
    return BONGOCAT_SUCCESS;
  }

  bongocat_log_info("Starting animation thread");

  animation_running = true;
  int result = pthread_create(&anim_thread, NULL, anim_thread_main, NULL);
  if (result != 0) {
    bongocat_log_error("Failed to create animation thread: %s",
                       strerror(result));
    animation_running = false;
    return BONGOCAT_ERROR_THREAD;
  }

  animation_thread_started = true;
  bongocat_log_debug("Animation thread started successfully");
  return BONGOCAT_SUCCESS;
}

void animation_cleanup(void) {
  if (animation_thread_started) {
    bongocat_log_debug("Stopping animation thread");
    animation_running = false;

    
    pthread_join(anim_thread, NULL);
    animation_thread_started = false;
    bongocat_log_debug("Animation thread stopped");
  }

  
  animation_invalidate_cache();

  
  if (animation_initialized) {
    anim_cleanup_svgs();
    animation_initialized = false;
  }

  bongocat_log_debug("Animation cleanup complete");
}

void animation_trigger(void) {
  if (any_key_pressed) {
    atomic_store(any_key_pressed, 1);
  }
}
