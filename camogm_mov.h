#ifndef _CAMOGM_MOV_H
#define _CAMOG_MOV_H

#include "camogm.h"

int camogm_init_mov(void);
int camogm_start_mov(camogm_state *state);
int camogm_frame_mov(camogm_state *state);
int camogm_end_mov(camogm_state *state);
void camogm_free_mov(void);

#endif /* _CAMOGM_MOV_H */
