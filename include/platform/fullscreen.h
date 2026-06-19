#ifndef FULLSCREEN_H
#define FULLSCREEN_H

#include <stdbool.h>
#include <wayland-client.h>



struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_manager_v1_listener;







void fullscreen_init(struct zwlr_foreign_toplevel_manager_v1 *manager);



void fullscreen_cleanup(void);



bool fullscreen_is_detected(void);



bool fs_detector_available(void);



const struct zwlr_foreign_toplevel_manager_v1_listener *
fullscreen_get_manager_listener(void);

#endif  
