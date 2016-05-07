#ifndef _CAMOGM_JPEG_H
#define _CAMOGM_JPEG_H

#include "camogm.h"

int camogm_init_jpeg(void);
int camogm_start_jpeg(camogm_state *state);
int camogm_frame_jpeg(camogm_state *state);
int camogm_end_jpeg(void);
void camogm_free_jpeg(void);

#endif /* _CAMOGM_JPEG_H */
