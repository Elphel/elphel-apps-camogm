/** @file camogm_mov.h
 * @brief Provides writing to file compatible with Apple Quicktime(R) for @e camogm
 * @copyright Copyright (C) 2016 Elphel, Inc.
 *
 * @par <b>License</b>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CAMOGM_MOV_H
#define _CAMOG_MOV_H

#include "camogm.h"

int camogm_init_mov(void);
int camogm_start_mov(camogm_state *state);
int camogm_frame_mov(camogm_state *state);
int camogm_end_mov(camogm_state *state);
void camogm_free_mov(void);

#endif /* _CAMOGM_MOV_H */
