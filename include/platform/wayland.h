#ifndef WAYLAND_H
#define WAYLAND_H

#include "../protocols/xdg-shell-client-protocol.h"
#include "../protocols/zwlr-layer-shell-v1-client-protocol.h"
#include "config/config.h"
#include "core/bongocat.h"
#include "utils/error.h"

#include <signal.h>
#include <stdatomic.h>






extern struct wl_display *display;
extern struct wl_compositor *compositor;
extern struct wl_shm *shm;
extern struct wl_output *output;


extern struct zwlr_layer_shell_v1 *layer_shell;
extern struct zwlr_layer_surface_v1 *layer_surface;
extern struct xdg_wm_base *xdg_wm_base;


extern struct wl_surface *surface;
extern struct wl_buffer *buffer;
extern uint8_t *pixels;


extern atomic_bool configured;
extern atomic_bool fullscreen_detected;






BONGOCAT_NODISCARD bongocat_error_t wayland_init(config_t *config);


BONGOCAT_NODISCARD bongocat_error_t wayland_run(volatile sig_atomic_t *running);


void wayland_cleanup(void);






extern output_ref_t outputs[];
extern size_t output_count;






void wayland_update_config(config_t *config);


void draw_bar(void);


BONGOCAT_NODISCARD int create_shm(int size);


BONGOCAT_NODISCARD int wayland_get_screen_width(void);


BONGOCAT_NODISCARD const char *wayland_get_output_name(void);


BONGOCAT_NODISCARD struct wl_output *wayland_get_current_screen_output(void);


void wayland_set_tick_callback(void (*callback)(void));


BONGOCAT_NODISCARD const char *wayland_get_current_layer_name(void);




BONGOCAT_NODISCARD int wayland_phys_dim(int logical);

#endif  
