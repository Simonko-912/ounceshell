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

/*
 * The part of the vita2d interface (https://github.com/xerpi/libvita2d)
 * that VitaShell uses, reimplemented over SDL 2's renderer
 * (switch2/src/vita2d_sdl.c) and SDL_ttf (switch2/src/vita2d_font.c).
 * Function names and argument meanings follow vita2d so that VitaShell's
 * drawing code is unchanged. Colours are vita2d's 0xAABBGGRR.
 *
 * Everything is drawn in the Vita's 960x544 coordinate space; SDL scales
 * it to the Switch's output. The "PGF" font type holds TrueType/OpenType
 * faces: the Switch shared system fonts or a theme's font.ttf/font.otf.
 */

#ifndef VITASHELL_SWITCH2_VITA2D_H
#define VITASHELL_SWITCH2_VITA2D_H

#include <vitasdk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vita2d_texture vita2d_texture;
typedef struct vita2d_pgf vita2d_pgf;

int vita2d_init(void);
int vita2d_fini(void);

void vita2d_start_drawing(void);
void vita2d_end_drawing(void);
void vita2d_swap_buffers(void);
int vita2d_common_dialog_update(void);
void vita2d_wait_rendering_done(void);

void vita2d_set_clear_color(unsigned int color);
void vita2d_clear_screen(void);

void vita2d_enable_clipping(void);
void vita2d_disable_clipping(void);
void vita2d_set_clip_rectangle(int x_min, int y_min, int x_max, int y_max);

void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int color);

vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h);
void vita2d_free_texture(vita2d_texture *texture);
unsigned int vita2d_texture_get_width(const vita2d_texture *texture);
unsigned int vita2d_texture_get_height(const vita2d_texture *texture);
unsigned int vita2d_texture_get_stride(const vita2d_texture *texture);
/* RGBA8 pixels (0xAABBGGRR words). Writes reach the GPU texture at its next draw. */
void *vita2d_texture_get_datap(const vita2d_texture *texture);
void vita2d_texture_set_filters(vita2d_texture *texture, SceGxmTextureFilter min_filter,
                                SceGxmTextureFilter mag_filter);

void vita2d_draw_texture(const vita2d_texture *texture, float x, float y);
void vita2d_draw_texture_part(const vita2d_texture *texture, float x, float y,
                              float tex_x, float tex_y, float tex_w, float tex_h);
void vita2d_draw_texture_scale(const vita2d_texture *texture, float x, float y,
                               float x_scale, float y_scale);
void vita2d_draw_texture_scale_rotate_hotspot(const vita2d_texture *texture, float x, float y,
                                              float x_scale, float y_scale, float rad,
                                              float center_x, float center_y);

vita2d_texture *vita2d_load_PNG_file(const char *filename);
vita2d_texture *vita2d_load_PNG_buffer(const void *buffer);
vita2d_texture *vita2d_load_JPEG_file(const char *filename);
vita2d_texture *vita2d_load_JPEG_buffer(const void *buffer, unsigned long buffer_size);
vita2d_texture *vita2d_load_BMP_file(const char *filename);
vita2d_texture *vita2d_load_BMP_buffer(const void *buffer);

/* Loads a theme font. For "x/font.pgf" this tries x/font.ttf and x/font.otf. */
vita2d_pgf *vita2d_load_custom_pgf(const char *path);
void vita2d_free_pgf(vita2d_pgf *font);
int vita2d_pgf_draw_text(vita2d_pgf *font, int x, int y, unsigned int color, float scale,
                         const char *text);
int vita2d_pgf_draw_textf(vita2d_pgf *font, int x, int y, unsigned int color, float scale,
                          const char *text, ...) __attribute__((format(printf, 6, 7)));
int vita2d_pgf_text_width(vita2d_pgf *font, float scale, const char *text);
int vita2d_pgf_text_height(vita2d_pgf *font, float scale, const char *text);

#ifdef __cplusplus
}
#endif

#endif
