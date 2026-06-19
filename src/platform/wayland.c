#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "platform/wayland.h"

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "../protocols/fractional-scale-v1-client-protocol.h"
#include "../protocols/viewporter-client-protocol.h"
#include "../protocols/wlr-foreign-toplevel-management-v1-client-protocol.h"
#include "../protocols/xdg-output-unstable-v1-client-protocol.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
#include "graphics/animation.h"
#include "platform/fullscreen.h"
#include "platform/hyprland.h"

#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/time.h>






atomic_bool configured = false;
atomic_bool fullscreen_detected = false;
struct wl_display *display;
struct wl_compositor *compositor;
struct wl_shm *shm;
struct zwlr_layer_shell_v1 *layer_shell;
struct xdg_wm_base *xdg_wm_base;
struct wl_output *output;
struct wl_surface *surface;
struct wl_buffer *buffer;
struct zwlr_layer_surface_v1 *layer_surface;
uint8_t *pixels;
static size_t pixel_buffer_size = 0;


static struct wp_viewporter *viewporter = NULL;
static struct wp_fractional_scale_manager_v1 *fractional_scale_mgr = NULL;
static struct wp_viewport *viewport = NULL;
static struct wp_fractional_scale_v1 *fractional_scale_obj = NULL;



static uint32_t current_scale_120 = 120;

static int physical_buffer_w = 0;
static int physical_buffer_h = 0;


static inline int phys_dim(int logical) {
  if (logical <= 0)
    return 0;
  return (int)(((int64_t)logical * (int64_t)current_scale_120 + 119) / 120);
}

int wayland_phys_dim(int logical) {
  return phys_dim(logical);
}

static config_t *current_config;
static void (*tick_callback_fn)(void) = NULL;
static int applied_width = 0;
static int applied_height = 0;
static layer_type_t applied_layer = LAYER_TOP;
static overlay_position_t applied_position = POSITION_BOTTOM;
static char *applied_output_name = NULL;





output_ref_t outputs[MAX_OUTPUTS];
size_t output_count = 0;
static struct zxdg_output_manager_v1 *xdg_output_manager = NULL;
static output_ref_t *current_output_info = NULL;


static struct wl_registry *global_registry = NULL;
static uint32_t bound_output_name = 0;   
static atomic_bool output_lost = false;  
static bool using_named_output =
    false;  
static char *bound_screen_name = NULL;

BONGOCAT_NODISCARD static struct wl_output *wayland_find_new_output(void) {
  if (current_config->output_name) {
    for (size_t i = 0; i < output_count; ++i) {
      if (outputs[i].name_received &&
          strcmp(outputs[i].name_str, current_config->output_name) == 0) {
        return outputs[i].wl_output;
      }
    }
  }
  return NULL;
}

BONGOCAT_NODISCARD static int wayland_get_new_screen_width(void) {
  struct wl_output *matching_wl_output = wayland_find_new_output();
  for (size_t i = 0; i < output_count; ++i) {
    if (outputs[i].wl_output == matching_wl_output) {
      return outputs[i].screen_width;
    }
  }

  return 0;
}

static void wayland_update_current_output_info(void) {
  bool output_found = false;
  if (output) {
    wl_display_roundtrip(display);

    for (size_t i = 0; i < MAX_OUTPUTS; i++) {
      if (outputs[i].wl_output == output) {
        bongocat_log_info("Detected screen name: %s", outputs[i].name_str);
        bound_screen_name = outputs[i].name_str;
      }
      if (outputs[i].wl_output == output) {
        if (outputs[i].screen_width > 0 && outputs[i].screen_width <= 32768) {
          bongocat_log_info("Detected screen width: %d",
                            outputs[i].screen_width);
          current_output_info = &outputs[i];
          current_config->screen_width = outputs[i].screen_width;
          output_found = true;
        }
      }
    }
  }

  if (!output_found) {
    bongocat_log_warning("No output found, using default screen width: %d",
                         DEFAULT_SCREEN_WIDTH);
    current_config->screen_width = DEFAULT_SCREEN_WIDTH;
    current_output_info = NULL;
  }
}






static bongocat_error_t wayland_setup_surface(void);

static void
handle_xdg_output_name(void *data,
                       [[maybe_unused]] struct zxdg_output_v1 *xdg_output,
                       const char *name) {
  
  if (!data || !name) {
    return;
  }

  output_ref_t *oref = data;
  snprintf(oref->name_str, sizeof(oref->name_str), "%s", name);
  oref->name_received = true;
  bongocat_log_debug("xdg-output name received: %s", name);

  
  if (!atomic_load(&output_lost) || !current_config) {
    return;
  }

  bool should_reconnect = false;

  
  if (using_named_output && current_config->output_name) {
    should_reconnect = (strcmp(name, current_config->output_name) == 0);
  }
  
  else if (!using_named_output) {
    should_reconnect = true;
    bongocat_log_debug("Using fallback output, accepting '%s'", name);
  }

  if (should_reconnect) {
    bongocat_log_info("Target output '%s' reconnected!", name);

    
    if (layer_surface) {
      zwlr_layer_surface_v1_destroy(layer_surface);
      layer_surface = NULL;
    }
    if (surface) {
      wl_surface_destroy(surface);
      surface = NULL;
    }

    
    output = oref->wl_output;
    bound_output_name = oref->name;
    atomic_store(&output_lost, false);
    bound_screen_name = oref->name_str;

    
    
    
    
    if (wayland_setup_surface() == BONGOCAT_SUCCESS) {
      
      wl_display_roundtrip(display);
      wayland_update_current_output_info();
      bongocat_log_info("Surface recreated, configure event processed");
    } else {
      bongocat_log_error("Failed to recreate surface on reconnected output");
    }
  }
}

static void handle_xdg_output_logical_position(
    void *data, [[maybe_unused]] struct zxdg_output_v1 *xdg_output, int32_t x,
    int32_t y) {
  
  if (!data) {
    return;
  }

  output_ref_t *oref = data;

  oref->x = x;
  oref->y = y;

  bongocat_log_debug("xdg-output logical position received: %d,%d", x, y);
}
static void handle_xdg_output_logical_size(
    void *data, [[maybe_unused]] struct zxdg_output_v1 *xdg_output,
    int32_t width, int32_t height) {
  
  if (!data) {
    return;
  }

  output_ref_t *oref = data;

  oref->width = width;
  oref->height = height;

  bongocat_log_debug("xdg-output logical size received: %dx%d", width, height);
}
static void
handle_xdg_output_done([[maybe_unused]] void *data,
                       [[maybe_unused]] struct zxdg_output_v1 *xdg_output) {}

static void handle_xdg_output_description(
    [[maybe_unused]] void *data,
    [[maybe_unused]] struct zxdg_output_v1 *xdg_output,
    [[maybe_unused]] const char *description) {}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = handle_xdg_output_logical_position,
    .logical_size = handle_xdg_output_logical_size,
    .done = handle_xdg_output_done,
    .name = handle_xdg_output_name,
    .description = handle_xdg_output_description};





static void screen_calculate_dimensions(output_ref_t *screen_info) {
  if (!screen_info) {
    return;
  }

  if (!screen_info->mode_received || !screen_info->geometry_received ||
      screen_info->screen_width > 0) {
    return;
  }

  bool is_rotated = (screen_info->transform == WL_OUTPUT_TRANSFORM_90 ||
                     screen_info->transform == WL_OUTPUT_TRANSFORM_270 ||
                     screen_info->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
                     screen_info->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270);

  if (is_rotated) {
    screen_info->screen_width = screen_info->raw_height;
    screen_info->screen_height = screen_info->raw_width;
    bongocat_log_info("Detected rotated screen: %dx%d (transform: %d)",
                      screen_info->raw_height, screen_info->raw_width,
                      screen_info->transform);
  } else {
    screen_info->screen_width = screen_info->raw_width;
    screen_info->screen_height = screen_info->raw_height;
    bongocat_log_info("Detected screen: %dx%d (transform: %d)",
                      screen_info->raw_width, screen_info->raw_height,
                      screen_info->transform);
  }
}





int create_shm(int size) {
  int fd = memfd_create("bongocat-shm", MFD_CLOEXEC);
  if (fd < 0) {
    bongocat_log_error("memfd_create failed: %s", strerror(errno));
    return -1;
  }

  if (ftruncate(fd, size) < 0) {
    bongocat_log_error("ftruncate failed: %s", strerror(errno));
    close(fd);
    return -1;
  }

  return fd;
}

void draw_bar(void) {
  if (!atomic_load(&configured)) {
    bongocat_log_debug("Surface not configured yet, skipping draw");
    return;
  }

  pthread_mutex_lock(&anim_lock);

  
  if (!current_config || !pixels || !surface || !buffer) {
    bongocat_log_debug("Config or pixels not ready, skipping draw");
    pthread_mutex_unlock(&anim_lock);
    return;
  }

  
  bool is_overlay_layer = current_config->layer == LAYER_OVERLAY;
  bool is_fullscreen = !is_overlay_layer &&
                       !current_config->disable_fullscreen_hide &&
                       atomic_load(&fullscreen_detected);
  int effective_opacity = is_fullscreen ? 0 : current_config->overlay_opacity;

  
  
  
  
  int phys_w = physical_buffer_w;
  int phys_h = physical_buffer_h;
  size_t buffer_size = (size_t)phys_w * (size_t)phys_h * 4U;
  memset(pixels, 0, buffer_size);

  if (effective_opacity > 0) {
    uint32_t fill = (uint32_t)effective_opacity << 24;
    uint32_t *px = (uint32_t *)pixels;
    size_t pixel_count = buffer_size / 4;
    for (size_t i = 0; i < pixel_count; i++) {
      px[i] = fill;
    }
  }

  
  if (!is_fullscreen) {
    
    
    int cat_height_phys = phys_dim(current_config->cat_height);
    int cat_width_phys = (cat_height_phys * CAT_IMAGE_WIDTH) / CAT_IMAGE_HEIGHT;
    int cat_y_phys =
        (phys_h - cat_height_phys) / 2 + phys_dim(current_config->cat_y_offset);

    int cat_x_phys = 0;
    switch (current_config->cat_align) {
    case ALIGN_CENTER:
      cat_x_phys = (phys_w - cat_width_phys) / 2 +
                   phys_dim(current_config->cat_x_offset);
      break;
    case ALIGN_LEFT:
      cat_x_phys = phys_dim(current_config->cat_x_offset);
      break;
    case ALIGN_RIGHT:
      cat_x_phys =
          phys_w - cat_width_phys - phys_dim(current_config->cat_x_offset);
      break;
    }

    cached_frame_t *frame = &anim_cached_frames[anim_index];
    if (frame->data && frame->width > 0 && frame->height > 0) {
      
      blit_cached_frame(pixels, phys_w, phys_h, frame->data, frame->width,
                        frame->height, cat_x_phys, cat_y_phys);
    } else {
      bongocat_log_debug("Frame %d cache not ready, skipping draw", anim_index);
    }
  } else {
    bongocat_log_debug("Cat hidden due to fullscreen detection");
  }

  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, phys_w, phys_h);
  wl_surface_commit(surface);
  pthread_mutex_unlock(&anim_lock);

  
  wl_display_flush(display);
}





static void layer_surface_configure([[maybe_unused]] void *data,
                                    struct zwlr_layer_surface_v1 *ls,
                                    uint32_t serial, uint32_t w, uint32_t h) {
  bongocat_log_debug("Layer surface configured: %dx%d", w, h);
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  atomic_store(&configured, true);
  draw_bar();
}


static void
layer_surface_closed([[maybe_unused]] void *data,
                     [[maybe_unused]] struct zwlr_layer_surface_v1 *ls) {
  bongocat_log_info("Layer surface closed by compositor");
  atomic_store(&configured, false);
}

static struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void xdg_wm_base_ping([[maybe_unused]] void *data,
                             struct xdg_wm_base *wm_base, uint32_t serial) {
  xdg_wm_base_pong(wm_base, serial);
}

static struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void
output_geometry([[maybe_unused]] void *data, struct wl_output *wl_output,
                [[maybe_unused]] int32_t x, [[maybe_unused]] int32_t y,
                [[maybe_unused]] int32_t physical_width,
                [[maybe_unused]] int32_t physical_height,
                [[maybe_unused]] int32_t subpixel,
                [[maybe_unused]] const char *make,
                [[maybe_unused]] const char *model, int32_t transform) {
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    if (outputs[i].wl_output == wl_output) {
      outputs[i].transform = transform;
      outputs[i].geometry_received = true;
      bongocat_log_debug("Output transform: %d", transform);
      screen_calculate_dimensions(&outputs[i]);
      break;
    }
  }
}

static void output_mode([[maybe_unused]] void *data,
                        struct wl_output *wl_output, uint32_t flags,
                        int32_t width, int32_t height,
                        [[maybe_unused]] int32_t refresh) {
  if (flags & WL_OUTPUT_MODE_CURRENT) {
    for (size_t i = 0; i < MAX_OUTPUTS; i++) {
      if (outputs[i].wl_output == wl_output) {
        outputs[i].raw_width = width;
        outputs[i].raw_height = height;
        outputs[i].mode_received = true;
        bongocat_log_debug("Received raw screen mode: %dx%d", width, height);
        screen_calculate_dimensions(&outputs[i]);
        break;
      }
    }
  }
}

static void output_done([[maybe_unused]] void *data,
                        struct wl_output *wl_output) {
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    if (outputs[i].wl_output == wl_output) {
      screen_calculate_dimensions(&outputs[i]);
      bongocat_log_debug("Output configuration complete");
      break;
    }
  }
}

static void output_scale([[maybe_unused]] void *data,
                         struct wl_output *wl_output, int32_t factor) {
  if (factor < 1)
    factor = 1;
  for (size_t i = 0; i < output_count; ++i) {
    if (outputs[i].wl_output == wl_output) {
      outputs[i].wl_scale = factor;
      break;
    }
  }
}

static struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};






static bongocat_error_t wayland_recreate_buffer_for_scale(void);
static void wayland_recache_frames_for_scale(void);

static void fractional_scale_preferred_scale(
    [[maybe_unused]] void *data,
    [[maybe_unused]] struct wp_fractional_scale_v1 *fs, uint32_t scale) {
  if (scale == 0 || scale == current_scale_120) {
    return;
  }
  bongocat_log_info("Compositor requested fractional scale %u/120 (%.3f)",
                    scale, (double)scale / 120.0);
  current_scale_120 = scale;
  if (surface) {
    if (wayland_recreate_buffer_for_scale() == BONGOCAT_SUCCESS) {
      wayland_recache_frames_for_scale();
      if (atomic_load(&configured)) {
        draw_bar();
      }
    }
  }
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener =
    {
        .preferred_scale = fractional_scale_preferred_scale,
};



static uint32_t scale_120_from_output(void) {
  if (current_output_info && current_output_info->wl_scale > 0) {
    return (uint32_t)current_output_info->wl_scale * 120u;
  }
  return 120;
}





static void registry_global([[maybe_unused]] void *data,
                            struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t ver) {
#define BIND_MIN_VER(v, desired) ((v) < (desired) ? (v) : (desired))

  if (strcmp(iface, wl_compositor_interface.name) == 0) {
    compositor = (struct wl_compositor *)wl_registry_bind(
        reg, name, &wl_compositor_interface, BIND_MIN_VER(ver, 4));
  } else if (strcmp(iface, wl_shm_interface.name) == 0) {
    shm = (struct wl_shm *)wl_registry_bind(reg, name, &wl_shm_interface,
                                            BIND_MIN_VER(ver, 1));
  } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
    layer_shell = (struct zwlr_layer_shell_v1 *)wl_registry_bind(
        reg, name, &zwlr_layer_shell_v1_interface, BIND_MIN_VER(ver, 1));
  } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
    xdg_wm_base = (struct xdg_wm_base *)wl_registry_bind(
        reg, name, &xdg_wm_base_interface, BIND_MIN_VER(ver, 1));
    if (xdg_wm_base) {
      xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
    }
  } else if (strcmp(iface, zxdg_output_manager_v1_interface.name) == 0) {
    xdg_output_manager = wl_registry_bind(
        reg, name, &zxdg_output_manager_v1_interface, BIND_MIN_VER(ver, 3));
  } else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
    viewporter = (struct wp_viewporter *)wl_registry_bind(
        reg, name, &wp_viewporter_interface, BIND_MIN_VER(ver, 1));
  } else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) ==
             0) {
    fractional_scale_mgr =
        (struct wp_fractional_scale_manager_v1 *)wl_registry_bind(
            reg, name, &wp_fractional_scale_manager_v1_interface,
            BIND_MIN_VER(ver, 1));
  } else if (strcmp(iface, wl_output_interface.name) == 0) {
    if (output_count < MAX_OUTPUTS) {
      outputs[output_count].name = name;
      outputs[output_count].wl_output = wl_registry_bind(
          reg, name, &wl_output_interface, BIND_MIN_VER(ver, 2));
      wl_output_add_listener(outputs[output_count].wl_output, &output_listener,
                             NULL);

      
      
      if (atomic_load(&output_lost) && xdg_output_manager) {
        outputs[output_count].xdg_output =
            zxdg_output_manager_v1_get_xdg_output(
                xdg_output_manager, outputs[output_count].wl_output);
        outputs[output_count].name_received = false;
        zxdg_output_v1_add_listener(outputs[output_count].xdg_output,
                                    &xdg_output_listener,
                                    &outputs[output_count]);
        bongocat_log_debug(
            "New output appeared while output_lost, checking name...");
      }

      output_count++;
    }
  } else if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) ==
             0) {
    struct zwlr_foreign_toplevel_manager_v1 *fs_manager =
        (struct zwlr_foreign_toplevel_manager_v1 *)wl_registry_bind(
            reg, name, &zwlr_foreign_toplevel_manager_v1_interface,
            BIND_MIN_VER(ver, 3));
    fullscreen_init(fs_manager);
  }

#undef BIND_MIN_VER
}

static void registry_remove([[maybe_unused]] void *data,
                            [[maybe_unused]] struct wl_registry *registry,
                            uint32_t name) {
  size_t removed_index = output_count;
  for (size_t i = 0; i < output_count; ++i) {
    if (outputs[i].name == name) {
      removed_index = i;
      break;
    }
  }

  if (removed_index == output_count) {
    return;
  }

  bool removed_bound = (name == bound_output_name && bound_output_name != 0);
  if (removed_bound) {
    bongocat_log_warning("Bound output disconnected (registry name %u)", name);
    atomic_store(&output_lost, true);
    atomic_store(&configured, false);
    output = NULL;
    bound_output_name = 0;
    bound_screen_name = NULL;
    current_output_info = NULL;
  }

  if (outputs[removed_index].xdg_output) {
    zxdg_output_v1_destroy(outputs[removed_index].xdg_output);
    outputs[removed_index].xdg_output = NULL;
  }
  if (outputs[removed_index].wl_output) {
    wl_output_destroy(outputs[removed_index].wl_output);
    outputs[removed_index].wl_output = NULL;
  }

  for (size_t j = removed_index; j + 1 < output_count; ++j) {
    outputs[j] = outputs[j + 1];
  }

  if (output_count > 0) {
    memset(&outputs[output_count - 1], 0, sizeof(output_ref_t));
    output_count--;
  }

  if (!removed_bound && output != NULL) {
    current_output_info = NULL;
    bound_screen_name = NULL;

    for (size_t i = 0; i < output_count; ++i) {
      if (outputs[i].wl_output == output) {
        bound_screen_name = outputs[i].name_str;
      }
      if (outputs[i].wl_output == output) {
        current_output_info = &outputs[i];
      }
    }
  }
}

static struct wl_registry_listener reg_listener = {
    .global = registry_global, .global_remove = registry_remove};





static void wayland_update_output(void) {
  output = NULL;
  bound_output_name = 0;
  using_named_output = false;
  current_output_info = NULL;
  bound_screen_name = NULL;

  if (current_config->output_name) {
    for (size_t i = 0; i < output_count; ++i) {
      if (outputs[i].name_received &&
          strcmp(outputs[i].name_str, current_config->output_name) == 0) {
        output = outputs[i].wl_output;
        bound_output_name =
            outputs[i].name;  
        bound_screen_name = outputs[i].name_str;
        using_named_output = true;  
        current_output_info = &outputs[i];
        bongocat_log_info("Matched output: %s (registry name %u, %s)",
                          outputs[i].name_str, bound_output_name,
                          bound_screen_name);
        break;
      }
    }

    if (!output) {
      bongocat_log_error(
          "Could not find output named '%s', defaulting to first output",
          current_config->output_name);
    }
  }

  
  if (!output && output_count > 0) {
    output = outputs[0].wl_output;
    bound_output_name = outputs[0].name;
    bound_screen_name = outputs[0].name_str;
    for (size_t i = 0; i < MAX_OUTPUTS; i++) {
      if (outputs[i].wl_output == output) {
        current_output_info = &outputs[i];
      }
    }
    using_named_output = false;  
    bongocat_log_warning("Falling back to first output (registry name %u, %s)",
                         bound_output_name, bound_screen_name);
  }
}

static bongocat_error_t wayland_setup_protocols(void) {
  global_registry = wl_display_get_registry(display);
  if (!global_registry) {
    bongocat_log_error("Failed to get Wayland registry");
    return BONGOCAT_ERROR_WAYLAND;
  }

  wl_registry_add_listener(global_registry, &reg_listener, NULL);
  wl_display_roundtrip(display);

  if (xdg_output_manager) {
    for (size_t i = 0; i < output_count; ++i) {
      outputs[i].xdg_output = zxdg_output_manager_v1_get_xdg_output(
          xdg_output_manager, outputs[i].wl_output);
      outputs[i].x = 0;
      outputs[i].y = 0;
      outputs[i].width = 0;
      outputs[i].height = 0;
      outputs[i].hypr_id = -1;
      zxdg_output_v1_add_listener(outputs[i].xdg_output, &xdg_output_listener,
                                  &outputs[i]);
    }

    
    wl_display_roundtrip(display);

    hypr_update_outputs_with_monitor_ids();
  }

  wayland_update_output();

  if (!compositor || !shm || !layer_shell) {
    if (!compositor)
      bongocat_log_error("Missing protocol: wl_compositor");
    if (!shm)
      bongocat_log_error("Missing protocol: wl_shm");
    if (!layer_shell)
      bongocat_log_error("Missing protocol: wlr-layer-shell (required for "
                         "overlay rendering). Your compositor may not support "
                         "this protocol.");
    bongocat_log_error(
        "Cannot start: required Wayland protocols not available");
    wl_registry_destroy(global_registry);
    global_registry = NULL;
    return BONGOCAT_ERROR_WAYLAND;
  }

  
  if (!fs_detector_available()) {
    bongocat_log_warning("Foreign toplevel protocol not available — fullscreen "
                         "detection disabled. Overlay will not auto-hide when "
                         "apps go fullscreen.");
  }

  
  wayland_update_current_output_info();

  
  return BONGOCAT_SUCCESS;
}

static bongocat_error_t wayland_setup_surface(void) {
  if (!current_config) {
    bongocat_log_error("Cannot setup surface: config is NULL");
    return BONGOCAT_ERROR_INVALID_PARAM;
  }

  uint32_t wl_layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  if (current_config && current_config->layer == LAYER_OVERLAY) {
    wl_layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  }

  surface = wl_compositor_create_surface(compositor);
  if (!surface) {
    bongocat_log_error("Failed to create surface");
    return BONGOCAT_ERROR_WAYLAND;
  }

  
  
  
  if (viewporter && !viewport) {
    viewport = wp_viewporter_get_viewport(viewporter, surface);
  }
  if (fractional_scale_mgr && !fractional_scale_obj) {
    fractional_scale_obj = wp_fractional_scale_manager_v1_get_fractional_scale(
        fractional_scale_mgr, surface);
    if (fractional_scale_obj) {
      wp_fractional_scale_v1_add_listener(fractional_scale_obj,
                                          &fractional_scale_listener, NULL);
    }
  }

  
  
  if (!fractional_scale_obj) {
    current_scale_120 = scale_120_from_output();
  }

  layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, surface, output, wl_layer, "bongocat-overlay");

  if (!layer_surface) {
    bongocat_log_error("Failed to create layer surface");
    return BONGOCAT_ERROR_WAYLAND;
  }

  
  uint32_t anchor =
      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  if (current_config->overlay_position == POSITION_TOP) {
    anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
  } else {
    anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  }

  zwlr_layer_surface_v1_set_anchor(layer_surface, anchor);
  zwlr_layer_surface_v1_set_size(layer_surface, 0,
                                 current_config->overlay_height);
  zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  zwlr_layer_surface_v1_add_listener(layer_surface, &layer_listener, NULL);

  
  struct wl_region *input_region = wl_compositor_create_region(compositor);
  if (input_region) {
    wl_surface_set_input_region(surface, input_region);
    wl_region_destroy(input_region);
  }

  wl_surface_commit(surface);
  return BONGOCAT_SUCCESS;
}

static bongocat_error_t wayland_setup_buffer(void) {
  int logical_w = current_config->screen_width;
  int logical_h = current_config->overlay_height;
  int phys_w = phys_dim(logical_w);
  int phys_h = phys_dim(logical_h);

  size_t size = (size_t)phys_w * (size_t)phys_h * 4U;
  if (size == 0 || size > (size_t)INT32_MAX) {
    bongocat_log_error("Invalid buffer size: %zu", size);
    return BONGOCAT_ERROR_WAYLAND;
  }

  int fd = create_shm((int)size);
  if (fd < 0) {
    return BONGOCAT_ERROR_WAYLAND;
  }

  pixels =
      (uint8_t *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pixels == MAP_FAILED) {
    bongocat_log_error("Failed to map shared memory: %s", strerror(errno));
    close(fd);
    return BONGOCAT_ERROR_MEMORY;
  }
  pixel_buffer_size = size;

  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)size);
  if (!pool) {
    bongocat_log_error("Failed to create shared memory pool");
    munmap(pixels, size);
    pixels = NULL;
    close(fd);
    return BONGOCAT_ERROR_WAYLAND;
  }

  buffer = wl_shm_pool_create_buffer(pool, 0, phys_w, phys_h, phys_w * 4,
                                     WL_SHM_FORMAT_ARGB8888);
  if (!buffer) {
    bongocat_log_error("Failed to create buffer");
    wl_shm_pool_destroy(pool);
    munmap(pixels, size);
    pixels = NULL;
    close(fd);
    return BONGOCAT_ERROR_WAYLAND;
  }

  wl_shm_pool_destroy(pool);
  close(fd);

  physical_buffer_w = phys_w;
  physical_buffer_h = phys_h;

  
  
  
  
  if (viewport) {
    wp_viewport_set_destination(viewport, logical_w, logical_h);
    wl_surface_set_buffer_scale(surface, 1);
  } else {
    int integer_scale = (int)(current_scale_120 / 120u);
    if (integer_scale < 1)
      integer_scale = 1;
    wl_surface_set_buffer_scale(surface, integer_scale);
  }

  bongocat_log_debug(
      "Buffer allocated: logical %dx%d, physical %dx%d, scale %u/120",
      logical_w, logical_h, phys_w, phys_h, current_scale_120);
  return BONGOCAT_SUCCESS;
}



static bongocat_error_t wayland_recreate_buffer_for_scale(void) {
  if (!current_config || !surface) {
    return BONGOCAT_ERROR_INVALID_PARAM;
  }

  pthread_mutex_lock(&anim_lock);
  atomic_store(&configured, false);
  if (buffer) {
    wl_buffer_destroy(buffer);
    buffer = NULL;
  }
  if (pixels && pixel_buffer_size > 0) {
    munmap(pixels, pixel_buffer_size);
    pixels = NULL;
    pixel_buffer_size = 0;
  }
  bongocat_error_t err = wayland_setup_buffer();
  pthread_mutex_unlock(&anim_lock);

  if (err != BONGOCAT_SUCCESS) {
    return err;
  }

  
  
  wl_surface_commit(surface);
  wl_display_roundtrip(display);
  return BONGOCAT_SUCCESS;
}


static void wayland_recache_frames_for_scale(void) {
  if (!current_config)
    return;
  pthread_mutex_lock(&anim_lock);
  animation_invalidate_cache();
  int cat_h_phys = phys_dim(current_config->cat_height);
  int cat_w_phys = (cat_h_phys * CAT_IMAGE_WIDTH) / CAT_IMAGE_HEIGHT;
  animation_cache_frames(cat_w_phys, cat_h_phys, current_config->mirror_x,
                         current_config->mirror_y,
                         current_config->enable_antialiasing);
  pthread_mutex_unlock(&anim_lock);
}

bongocat_error_t wayland_init(config_t *config) {
  BONGOCAT_CHECK_NULL(config, BONGOCAT_ERROR_INVALID_PARAM);

  current_config = config;
  bongocat_log_info("Initializing Wayland connection");

  display = wl_display_connect(NULL);
  if (!display) {
    bongocat_log_error("Failed to connect to Wayland display");
    return BONGOCAT_ERROR_WAYLAND;
  }

  bongocat_error_t result;
  if ((result = wayland_setup_protocols()) != BONGOCAT_SUCCESS ||
      (result = wayland_setup_surface()) != BONGOCAT_SUCCESS ||
      (result = wayland_setup_buffer()) != BONGOCAT_SUCCESS) {
    wayland_cleanup();
    return result;
  }

  
  
  
  wl_display_roundtrip(display);

  applied_width = current_config->screen_width;
  applied_height = current_config->overlay_height;
  applied_layer = current_config->layer;
  applied_position = current_config->overlay_position;
  if (applied_output_name) {
    free(applied_output_name);
    applied_output_name = NULL;
  }
  if (current_config->output_name) {
    applied_output_name = strdup(current_config->output_name);
  }

  bongocat_log_info("Wayland initialization complete (%dx%d buffer)",
                    current_config->screen_width,
                    current_config->overlay_height);
  return BONGOCAT_SUCCESS;
}

bongocat_error_t wayland_run(volatile sig_atomic_t *running) {
  BONGOCAT_CHECK_NULL(running, BONGOCAT_ERROR_INVALID_PARAM);

  bongocat_log_info("Starting Wayland event loop");

  while (*running && display) {
    if (tick_callback_fn) {
      tick_callback_fn();
    }

    
    struct pollfd pfd = {
        .fd = wl_display_get_fd(display),
        .events = POLLIN,
    };

    while (wl_display_prepare_read(display) != 0) {
      if (wl_display_dispatch_pending(display) == -1) {
        bongocat_log_error("Failed to dispatch pending events");
        return BONGOCAT_ERROR_WAYLAND;
      }
    }

    int poll_result = poll(&pfd, 1, 100);

    if (poll_result > 0) {
      if (wl_display_read_events(display) == -1 ||
          wl_display_dispatch_pending(display) == -1) {
        bongocat_log_error("Failed to handle Wayland events");
        return BONGOCAT_ERROR_WAYLAND;
      }
    } else if (poll_result == 0) {
      wl_display_cancel_read(display);
    } else {
      wl_display_cancel_read(display);
      if (errno != EINTR) {
        bongocat_log_error("Poll error: %s", strerror(errno));
        return BONGOCAT_ERROR_WAYLAND;
      }
    }

    wl_display_flush(display);
  }

  bongocat_log_info("Wayland event loop exited");
  return BONGOCAT_SUCCESS;
}





int wayland_get_screen_width(void) {
  return current_output_info ? current_output_info->screen_width : 0;
}

const char *wayland_get_output_name(void) {
  return bound_screen_name;
}

struct wl_output *wayland_get_current_screen_output(void) {
  return current_output_info ? current_output_info->wl_output : NULL;
}

void wayland_set_tick_callback(void (*callback)(void)) {
  tick_callback_fn = callback;
}


static void apply_layer_properties(const config_t *config, bool do_position,
                                   bool do_layer) {
  if (do_position) {
    uint32_t anchor =
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    if (config->overlay_position == POSITION_TOP) {
      anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    } else {
      anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    }
    zwlr_layer_surface_v1_set_anchor(layer_surface, anchor);
    bongocat_log_info("Overlay position changed to %s",
                      config->overlay_position == POSITION_TOP ? "top"
                                                               : "bottom");
  }
  if (do_layer) {
    uint32_t wl_layer = (config->layer == LAYER_OVERLAY)
                            ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
                            : ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    zwlr_layer_surface_v1_set_layer(layer_surface, wl_layer);
    bongocat_log_info("Layer changed to %s",
                      config->layer == LAYER_OVERLAY ? "overlay" : "top");
  }
}

void wayland_update_config(config_t *config) {
  if (!config) {
    bongocat_log_error("Cannot update wayland config: config is NULL");
    return;
  }

  current_config = config;

  int old_height = applied_height;
  int old_width = applied_width;
  layer_type_t old_layer = applied_layer;
  char *old_output_name =
      applied_output_name ? strdup(applied_output_name) : NULL;
  int new_width = wayland_get_new_screen_width();

  bool dimensions_changed = (old_height != config->overlay_height) ||
                            (old_width != config->screen_width);
  if (new_width > 0 && new_width != config->screen_width) {
    dimensions_changed = true;
  }

  bool layer_changed = (old_layer != config->layer);
  bool position_changed = (applied_position != config->overlay_position);
  bool output_name_changed =
      ((old_output_name == NULL) != (config->output_name == NULL)) ||
      (old_output_name && config->output_name &&
       strcmp(old_output_name, config->output_name) != 0);
  bool bound_output_changed =
      (bound_screen_name && config->output_name &&
       strcmp(bound_screen_name, config->output_name) != 0);
  bool screen_changed = output_name_changed || bound_output_changed;

  
  
  
  
  
  
  bool needs_full_recreate = screen_changed;
  bool needs_buffer_recreate =
      dimensions_changed && old_height > 0 && old_width > 0;
  bool needs_property_update = layer_changed || position_changed;

  if (needs_full_recreate) {
    
    bongocat_log_info("Output changed, recreating surface");

    pthread_mutex_lock(&anim_lock);
    atomic_store(&configured, false);

    if (buffer) {
      wl_buffer_destroy(buffer);
      buffer = NULL;
    }
    if (pixels && pixel_buffer_size > 0) {
      munmap(pixels, pixel_buffer_size);
      pixels = NULL;
      pixel_buffer_size = 0;
    }
    if (layer_surface) {
      zwlr_layer_surface_v1_destroy(layer_surface);
      layer_surface = NULL;
    }
    
    
    if (fractional_scale_obj) {
      wp_fractional_scale_v1_destroy(fractional_scale_obj);
      fractional_scale_obj = NULL;
    }
    if (viewport) {
      wp_viewport_destroy(viewport);
      viewport = NULL;
    }
    if (surface) {
      wl_surface_destroy(surface);
      surface = NULL;
    }

    wayland_update_output();
    wayland_update_current_output_info();

    if (wayland_setup_surface() != BONGOCAT_SUCCESS) {
      bongocat_log_error("Failed to recreate surface after output change");
      pthread_mutex_unlock(&anim_lock);
      free(old_output_name);
      return;
    }

    wayland_update_output();
    wayland_update_current_output_info();

    if (wayland_setup_buffer() != BONGOCAT_SUCCESS) {
      bongocat_log_error("Failed to recreate buffer after output change");
      pthread_mutex_unlock(&anim_lock);
      free(old_output_name);
      return;
    }

    animation_invalidate_cache();
    int cat_h = phys_dim(config->cat_height);
    int cat_w = (cat_h * CAT_IMAGE_WIDTH) / CAT_IMAGE_HEIGHT;
    animation_cache_frames(cat_w, cat_h, config->mirror_x, config->mirror_y,
                           config->enable_antialiasing);

    pthread_mutex_unlock(&anim_lock);
    wl_display_roundtrip(display);
    wayland_update_current_output_info();

    bongocat_log_info("Surface recreated successfully (%dx%d)",
                      config->screen_width, config->overlay_height);

  } else if (needs_buffer_recreate) {
    
    bongocat_log_info("Overlay dimensions changed (%dx%d -> %dx%d)", old_width,
                      old_height, config->screen_width, config->overlay_height);

    
    zwlr_layer_surface_v1_set_size(layer_surface, 0, config->overlay_height);
    apply_layer_properties(config, position_changed, layer_changed);
    wl_surface_commit(surface);

    
    pthread_mutex_lock(&anim_lock);
    atomic_store(&configured, false);

    if (buffer) {
      wl_buffer_destroy(buffer);
      buffer = NULL;
    }
    if (pixels && pixel_buffer_size > 0) {
      munmap(pixels, pixel_buffer_size);
      pixels = NULL;
      pixel_buffer_size = 0;
    }

    if (wayland_setup_buffer() != BONGOCAT_SUCCESS) {
      bongocat_log_error("Failed to recreate buffer after resize");
      pthread_mutex_unlock(&anim_lock);
      free(old_output_name);
      return;
    }

    animation_invalidate_cache();
    int cat_h = phys_dim(config->cat_height);
    int cat_w = (cat_h * CAT_IMAGE_WIDTH) / CAT_IMAGE_HEIGHT;
    animation_cache_frames(cat_w, cat_h, config->mirror_x, config->mirror_y,
                           config->enable_antialiasing);

    pthread_mutex_unlock(&anim_lock);

    
    wl_display_roundtrip(display);

    bongocat_log_info("Buffer resized successfully (%dx%d)",
                      config->screen_width, config->overlay_height);

  } else if (needs_property_update) {
    
    apply_layer_properties(config, position_changed, layer_changed);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
  }

  
  
  if (!needs_full_recreate && !needs_buffer_recreate) {
    pthread_mutex_lock(&anim_lock);
    animation_invalidate_cache();
    int cat_h = phys_dim(config->cat_height);
    int cat_w = (cat_h * CAT_IMAGE_WIDTH) / CAT_IMAGE_HEIGHT;
    animation_cache_frames(cat_w, cat_h, config->mirror_x, config->mirror_y,
                           config->enable_antialiasing);
    pthread_mutex_unlock(&anim_lock);
  }

  free(old_output_name);
  old_output_name = NULL;

  applied_width = config->screen_width;
  applied_height = config->overlay_height;
  applied_layer = config->layer;
  applied_position = config->overlay_position;
  free(applied_output_name);
  applied_output_name =
      config->output_name ? strdup(config->output_name) : NULL;

  if (atomic_load(&configured)) {
    draw_bar();
  }
}

void wayland_cleanup(void) {
  bongocat_log_info("Cleaning up Wayland resources");

  
  for (size_t i = 0; i < output_count; ++i) {
    if (outputs[i].xdg_output) {
      bongocat_log_debug("Destroying xdg_output %zu", i);
      zxdg_output_v1_destroy(outputs[i].xdg_output);
      outputs[i].xdg_output = NULL;
    }
  }

  
  if (xdg_output_manager) {
    bongocat_log_debug("Destroying xdg_output_manager");
    zxdg_output_manager_v1_destroy(xdg_output_manager);
    xdg_output_manager = NULL;
  }

  
  for (size_t i = 0; i < output_count; ++i) {
    if (outputs[i].wl_output) {
      bongocat_log_debug("Destroying wl_output %zu", i);
      wl_output_destroy(outputs[i].wl_output);
      outputs[i].wl_output = NULL;
    }
  }

  output_count = 0;

  if (buffer) {
    wl_buffer_destroy(buffer);
    buffer = NULL;
  }

  if (pixels && pixel_buffer_size > 0) {
    munmap(pixels, pixel_buffer_size);
    pixels = NULL;
    pixel_buffer_size = 0;
  }

  if (layer_surface) {
    zwlr_layer_surface_v1_destroy(layer_surface);
    layer_surface = NULL;
  }

  
  
  if (fractional_scale_obj) {
    wp_fractional_scale_v1_destroy(fractional_scale_obj);
    fractional_scale_obj = NULL;
  }
  if (viewport) {
    wp_viewport_destroy(viewport);
    viewport = NULL;
  }

  if (surface) {
    wl_surface_destroy(surface);
    surface = NULL;
  }

  if (fractional_scale_mgr) {
    wp_fractional_scale_manager_v1_destroy(fractional_scale_mgr);
    fractional_scale_mgr = NULL;
  }
  if (viewporter) {
    wp_viewporter_destroy(viewporter);
    viewporter = NULL;
  }

  
  
  output = NULL;

  if (layer_shell) {
    zwlr_layer_shell_v1_destroy(layer_shell);
    layer_shell = NULL;
  }

  if (xdg_wm_base) {
    xdg_wm_base_destroy(xdg_wm_base);
    xdg_wm_base = NULL;
  }

  fullscreen_cleanup();

  if (shm) {
    wl_shm_destroy(shm);
    shm = NULL;
  }

  if (compositor) {
    wl_compositor_destroy(compositor);
    compositor = NULL;
  }

  if (display) {
    wl_display_disconnect(display);
    display = NULL;
  }

  
  atomic_store(&configured, false);
  atomic_store(&fullscreen_detected, false);
  atomic_store(&output_lost, false);
  bound_output_name = 0;
  using_named_output = false;
  global_registry = NULL;  
  bound_screen_name = NULL;
  current_output_info = NULL;
  free(applied_output_name);
  applied_output_name = NULL;
  applied_width = 0;
  applied_height = 0;
  applied_layer = LAYER_TOP;
  applied_position = POSITION_BOTTOM;
  tick_callback_fn = NULL;
  memset(&outputs, 0, sizeof(output_ref_t) * MAX_OUTPUTS);

  bongocat_log_debug("Wayland cleanup complete");
}

const char *wayland_get_current_layer_name(void) {
  if (!current_config) {
    return "TOP";
  }
  return current_config->layer == LAYER_OVERLAY ? "OVERLAY" : "TOP";
}
