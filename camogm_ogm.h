#ifndef _CAMOGM_OGM_H
#define _CAMOGM_OGM_H

#include "camogm.h"

int camogm_init_ogm(void);
int camogm_start_ogm(camogm_state *state);
int camogm_frame_ogm(camogm_state *state);
int camogm_end_ogm(camogm_state *state);
void camogm_free_ogm(void);

#endif /* _CAMOGM_OGM_H */
