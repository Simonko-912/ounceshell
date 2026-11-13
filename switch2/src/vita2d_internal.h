/*
  VitaShell for Switch 2
  Copyright (C) 2026, VitaShell Switch 2 port contributors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* State shared by the vita2d reimplementation's drawing and font files. */

#ifndef VITASHELL_SWITCH2_VITA2D_INTERNAL_H
#define VITASHELL_SWITCH2_VITA2D_INTERNAL_H

#include <vita2d.h>

#include <SDL.h>

/* The Vita's screen: VitaShell lays everything out in this space. */
#define VITA2D_LOGICAL_WIDTH 960
#define VITA2D_LOGICAL_HEIGHT 544

/* The renderer, or NULL before vita2d_init. Used only on the main thread. */
SDL_Renderer *vita2dRenderer(void);

/* Output pixels per logical pixel (about 1.32 at 1280x720), for rendering
   glyphs at the output resolution. */
float vita2dOutputScale(void);

/* vita2d colour 0xAABBGGRR to SDL's components. */
static inline void vita2dColorComponents(unsigned int color, Uint8 *r, Uint8 *g, Uint8 *b,
                                         Uint8 *a) {
  *r = color & 0xFF;
  *g = (color >> 8) & 0xFF;
  *b = (color >> 16) & 0xFF;
  *a = (color >> 24) & 0xFF;
}

#endif
