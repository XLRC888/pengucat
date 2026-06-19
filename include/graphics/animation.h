#ifndef ANIMATION_H
#define ANIMATION_H

#include "config/config.h"
#include "core/bongocat.h"
#include "utils/error.h"

#include <pthread.h>
#include <stdint.h>






extern int anim_index;
extern pthread_mutex_t anim_lock;


typedef struct {
  uint8_t *data;  
  int width;
  int height;
} cached_frame_t;

extern cached_frame_t anim_cached_frames[NUM_FRAMES];

void animation_cache_frames(int target_w, int target_h, int mirror_x,
                            int mirror_y, int enable_aa);
void animation_invalidate_cache(void);






BONGOCAT_NODISCARD bongocat_error_t animation_init(config_t *config);


BONGOCAT_NODISCARD bongocat_error_t animation_start(void);


void animation_cleanup(void);


void animation_trigger(void);






void blit_cached_frame(uint8_t *dest, int dest_w, int dest_h,
                       const uint8_t *src, int src_w, int src_h, int offset_x,
                       int offset_y);

#endif  