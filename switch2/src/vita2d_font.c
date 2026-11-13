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
 * vita2d's PGF text interface over SDL_ttf.
 *
 * The Vita's PGF system font does not exist on the Switch. A "PGF" here is
 * an ordered list of TrueType/OpenType faces; each character is drawn with
 * the first face that has it:
 *  - the system shared fonts from pl:u (libounce-nx's plGetSharedFont),
 *    which include the Nintendo extension font with the controller button
 *    symbols, then
 *  - Noto Sans (SIL OFL 1.1), linked into the program, so that text still
 *    appears if the shared fonts are unavailable.
 * A theme may add its own font.ttf/font.otf in front.
 *
 * Glyphs are rasterized at the output resolution (VitaShell's 20 px em
 * times the logical-to-output scale) into atlas textures and drawn at the
 * logical size, so text stays sharp after SDL scales the 960x544 frame.
 */

#include "vita2d_internal.h"

#include <switch2_platform.h>

#include <switch.h>

#include <SDL_ttf.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The em size, in logical pixels, that matches VitaShell's PGF layout:
   23 px lines with the baseline 20 px below the line's top. */
#define FONT_EM_PIXELS 20.0f

/* Drawn text starts this far right of its position: VitaShell clips file
   names one pixel right of where it draws them, which the PGF glyphs'
   left bearing allowed for. Measured widths do not include it. */
#define TEXT_LEFT_INSET 1.0f

#define MAX_FONT_FACES 10

#define ATLAS_SIZE 1024
#define MAX_ATLAS_PAGES 12
#define GLYPH_PADDING 1

#define GLYPH_TABLE_INITIAL_SIZE 1024

extern unsigned char _binary_switch2_resources_NotoSans_Regular_ttf_start[];
extern unsigned char _binary_switch2_resources_NotoSans_Regular_ttf_end[];

typedef struct {
  uint32_t codepoint; // 0 marks an empty table slot
  int rasterized;     // its pixels are in the atlas (or it has none)
  int page;           // -1 for a glyph without pixels (a space)
  SDL_Rect source;    // in the atlas page, output pixels
  float advance;      // output pixels
  int ascent;         // of the glyph's face: its cell's top is this far above the baseline
} Glyph;

struct vita2d_pgf {
  SDL_mutex *lock;
  int face_count;
  TTF_Font *faces[MAX_FONT_FACES];
  void *face_data[MAX_FONT_FACES]; // file contents a face reads from, or NULL
  int uses_shared_fonts;

  float raster_scale; // output pixels per logical pixel when rasterized
  int ascent;         // output pixels
  int line_skip;      // output pixels

  Glyph *glyphs;
  size_t glyph_table_size;
  size_t glyph_count;

  SDL_Texture *pages[MAX_ATLAS_PAGES];
  int page_count;
  int cursor_x;
  int cursor_y;
  int row_height;
};

/* Symbols and the characters drawn instead when no face provides them:
   the controller buttons of the Nintendo extension font, and arrows. */
typedef struct {
  uint32_t codepoint;
  uint32_t replacement;
} SymbolFallback;

static const SymbolFallback symbol_fallbacks[] = {
  { 0xE0A0, 'A' }, { 0xE0A1, 'B' }, { 0xE0A2, 'X' }, { 0xE0A3, 'Y' },
  { 0xE0E0, 'A' }, { 0xE0E1, 'B' }, { 0xE0E2, 'X' }, { 0xE0E3, 'Y' },
  { 0xE0E4, 'L' }, { 0xE0E5, 'R' }, { 0xE0E6, 'L' }, { 0xE0E7, 'R' },
  { 0xE0EF, '+' }, { 0xE0F0, '-' },
  // Arrows VitaShell draws in context menus and symlink names.
  { 0x25B6, '>' }, { 0x25C0, '<' }, { 0x2192, '>' },
};

#define N_SYMBOL_FALLBACKS (sizeof(symbol_fallbacks) / sizeof(SymbolFallback))

static int shared_fonts_initialized = 0;

/* UTF-8 */

/* Decodes one character; invalid bytes are taken as Latin-1. */
static const char *decodeUtf8(const char *text, uint32_t *codepoint) {
  const unsigned char *bytes = (const unsigned char *)text;
  uint32_t value = bytes[0];
  int length = 1;

  if (value >= 0xF0 && value < 0xF8)
    length = 4;
  else if (value >= 0xE0)
    length = value < 0xF0 ? 3 : 1;
  else if (value >= 0xC0)
    length = 2;

  if (length > 1) {
    uint32_t decoded = value & (0x7F >> length);
    int i;
    for (i = 1; i < length; i++) {
      if ((bytes[i] & 0xC0) != 0x80) {
        length = 1;
        break;
      }
      decoded = (decoded << 6) | (bytes[i] & 0x3F);
    }
    if (length > 1)
      value = decoded;
  }

  *codepoint = value;
  return text + length;
}

/* Faces */

static int pixelSizeFor(float raster_scale) {
  int size = (int)lroundf(FONT_EM_PIXELS * raster_scale);
  return size > 4 ? size : 4;
}

static vita2d_pgf *createFont(void) {
  vita2d_pgf *font = calloc(1, sizeof(vita2d_pgf));
  if (!font)
    return NULL;

  font->lock = SDL_CreateMutex();
  if (!font->lock) {
    free(font);
    return NULL;
  }

  font->raster_scale = vita2dOutputScale();
  return font;
}

static int addFace(vita2d_pgf *font, const void *data, size_t size, void *owned_data) {
  if (font->face_count >= MAX_FONT_FACES)
    return -1;

  SDL_RWops *stream = SDL_RWFromConstMem(data, (int)size);
  if (!stream)
    return -1;

  TTF_Font *face = TTF_OpenFontRW(stream, 1, pixelSizeFor(font->raster_scale));
  if (!face) {
    fprintf(stderr, "font: face of %zu bytes: %s\n", size, TTF_GetError());
    return -1;
  }

  TTF_SetFontHinting(face, TTF_HINTING_LIGHT);

  font->faces[font->face_count] = face;
  font->face_data[font->face_count] = owned_data;
  font->face_count++;

  // The first face sets the line metrics.
  if (font->face_count == 1) {
    font->ascent = TTF_FontAscent(face);
    font->line_skip = TTF_FontLineSkip(face);
  }

  return 0;
}

static void addBundledFace(vita2d_pgf *font) {
  size_t size = (size_t)(_binary_switch2_resources_NotoSans_Regular_ttf_end -
                         _binary_switch2_resources_NotoSans_Regular_ttf_start);
  addFace(font, _binary_switch2_resources_NotoSans_Regular_ttf_start, size, NULL);
}

/* "en-US" as a Switch LanguageCode: the ASCII bytes in a u64. */
static u64 englishLanguageCode(void) {
  u64 code = 0;
  memcpy(&code, "en-US", 5);
  return code;
}

static void addSharedFaces(vita2d_pgf *font) {
  Result rc = plInitialize(PlServiceType_User);
  if (R_FAILED(rc)) {
    fprintf(stderr, "font: plInitialize: 0x%x\n", rc);
    return;
  }

  PlFontData fonts[PlSharedFontType_Total];
  s32 total = 0;
  rc = plGetSharedFont(englishLanguageCode(), fonts, PlSharedFontType_Total, &total);
  if (R_FAILED(rc)) {
    fprintf(stderr, "font: plGetSharedFont: 0x%x\n", rc);
    plExit();
    return;
  }

  s32 i;
  for (i = 0; i < total; i++) {
    if (fonts[i].address && fonts[i].size)
      addFace(font, fonts[i].address, fonts[i].size, NULL);
  }

  shared_fonts_initialized = 1;
  font->uses_shared_fonts = 1;
}

vita2d_pgf *switch2LoadSystemFonts(void) {
  vita2d_pgf *font = createFont();
  if (!font)
    return NULL;

  addSharedFaces(font);
  addBundledFace(font);

  if (font->face_count == 0) {
    vita2d_free_pgf(font);
    return NULL;
  }

  return font;
}

static void *readWholeFile(const char *path, size_t *size) {
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd < 0)
    return NULL;

  void *data = NULL;
  SceOff length = sceIoLseek(fd, 0, SCE_SEEK_END);
  if (length > 0 && sceIoLseek(fd, 0, SCE_SEEK_SET) == 0) {
    data = malloc((size_t)length);
    if (data && sceIoRead(fd, data, (SceSize)length) != (int)length) {
      free(data);
      data = NULL;
    }
    *size = (size_t)length;
  }

  sceIoClose(fd);
  return data;
}

/* A theme's font: "x/font.pgf" becomes x/font.ttf or x/font.otf, followed
   by the system fonts for characters it lacks. */
vita2d_pgf *vita2d_load_custom_pgf(const char *path) {
  static const char *extensions[] = { ".ttf", ".otf" };

  char base[1024];
  snprintf(base, sizeof(base), "%s", path);
  char *extension = strrchr(base, '.');
  if (extension && strchr(extension, '/') == NULL)
    *extension = '\0';

  size_t i;
  for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
    char font_path[1100];
    snprintf(font_path, sizeof(font_path), "%s%s", base, extensions[i]);

    size_t size = 0;
    void *data = readWholeFile(font_path, &size);
    if (!data)
      continue;

    vita2d_pgf *font = createFont();
    if (!font) {
      free(data);
      return NULL;
    }

    if (addFace(font, data, size, data) != 0) {
      free(data);
      vita2d_free_pgf(font);
      continue;
    }

    addSharedFaces(font);
    addBundledFace(font);
    return font;
  }

  return NULL;
}

/* Glyph cache
 *
 * A glyph's metrics are measured when it is first used. Its pixels are
 * rendered into an atlas page only when it is drawn, which happens on the
 * main thread (the renderer's). VitaShell's worker threads measure text
 * (for example when they open a message dialog), so every operation on a
 * font holds its lock, and measuring never touches the renderer. */

static void releaseAtlas(vita2d_pgf *font) {
  int i;
  for (i = 0; i < font->page_count; i++)
    SDL_DestroyTexture(font->pages[i]);

  font->page_count = 0;
  font->cursor_x = 0;
  font->cursor_y = 0;
  font->row_height = 0;

  size_t slot;
  for (slot = 0; slot < font->glyph_table_size; slot++) {
    font->glyphs[slot].rasterized = 0;
    font->glyphs[slot].page = -1;
  }
}

void vita2d_free_pgf(vita2d_pgf *font) {
  if (!font)
    return;

  releaseAtlas(font);
  free(font->glyphs);

  int i;
  for (i = 0; i < font->face_count; i++) {
    TTF_CloseFont(font->faces[i]);
    free(font->face_data[i]);
  }

  // The shared font memory stays mapped until every face reading it is closed.
  if (font->uses_shared_fonts && shared_fonts_initialized) {
    plExit();
    shared_fonts_initialized = 0;
  }

  if (font->lock)
    SDL_DestroyMutex(font->lock);

  free(font);
}

static size_t glyphSlot(const vita2d_pgf *font, uint32_t codepoint) {
  size_t mask = font->glyph_table_size - 1;
  size_t slot = (codepoint * 2654435761u) & mask;
  while (font->glyphs[slot].codepoint != 0 && font->glyphs[slot].codepoint != codepoint)
    slot = (slot + 1) & mask;
  return slot;
}

static int growGlyphTable(vita2d_pgf *font) {
  size_t old_size = font->glyph_table_size;
  Glyph *old_glyphs = font->glyphs;

  size_t new_size = old_size ? old_size * 2 : GLYPH_TABLE_INITIAL_SIZE;
  Glyph *new_glyphs = calloc(new_size, sizeof(Glyph));
  if (!new_glyphs)
    return -1;

  font->glyphs = new_glyphs;
  font->glyph_table_size = new_size;

  size_t i;
  for (i = 0; i < old_size; i++) {
    if (old_glyphs[i].codepoint != 0)
      font->glyphs[glyphSlot(font, old_glyphs[i].codepoint)] = old_glyphs[i];
  }

  free(old_glyphs);
  return 0;
}

static TTF_Font *faceFor(vita2d_pgf *font, uint32_t codepoint) {
  int i;
  for (i = 0; i < font->face_count; i++) {
    if (TTF_GlyphIsProvided32(font->faces[i], codepoint))
      return font->faces[i];
  }
  return NULL;
}

static uint32_t symbolReplacement(uint32_t codepoint) {
  size_t i;
  for (i = 0; i < N_SYMBOL_FALLBACKS; i++) {
    if (symbol_fallbacks[i].codepoint == codepoint)
      return symbol_fallbacks[i].replacement;
  }
  return 0;
}

/* The face and character that draw a code point: the first face that has
   it, else a replacement character, else the first face's missing-glyph box. */
static TTF_Font *resolveCharacter(vita2d_pgf *font, uint32_t codepoint, uint32_t *drawn) {
  *drawn = codepoint;

  TTF_Font *face = faceFor(font, codepoint);
  if (face)
    return face;

  uint32_t replacement = symbolReplacement(codepoint);
  if (replacement) {
    face = faceFor(font, replacement);
    if (face) {
      *drawn = replacement;
      return face;
    }
  }

  return font->faces[0];
}

static void measureGlyph(vita2d_pgf *font, uint32_t codepoint, Glyph *glyph) {
  uint32_t drawn;
  TTF_Font *face = resolveCharacter(font, codepoint, &drawn);

  memset(glyph, 0, sizeof(Glyph));
  glyph->codepoint = codepoint;
  glyph->page = -1;
  glyph->ascent = TTF_FontAscent(face);

  int advance = 0;
  if (TTF_GlyphMetrics32(face, drawn, NULL, NULL, NULL, NULL, &advance) == 0)
    glyph->advance = (float)advance;
}

static int addAtlasPage(vita2d_pgf *font) {
  if (font->page_count == MAX_ATLAS_PAGES)
    return -1;

  SDL_Texture *page = SDL_CreateTexture(vita2dRenderer(), SDL_PIXELFORMAT_ABGR8888,
                                        SDL_TEXTUREACCESS_STATIC, ATLAS_SIZE, ATLAS_SIZE);
  if (!page)
    return -1;

  SDL_SetTextureBlendMode(page, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(page, SDL_ScaleModeNearest);

  font->pages[font->page_count++] = page;
  font->cursor_x = 0;
  font->cursor_y = 0;
  font->row_height = 0;
  return 0;
}

static int fitsOnCurrentPage(const vita2d_pgf *font, int width, int height) {
  if (font->page_count == 0)
    return 0;

  int x = font->cursor_x;
  int y = font->cursor_y;
  if (x + width + GLYPH_PADDING > ATLAS_SIZE) {
    x = 0;
    y += font->row_height + GLYPH_PADDING;
  }

  return y + height + GLYPH_PADDING <= ATLAS_SIZE;
}

/* Reserves a rectangle in the atlas (shelf packing). Returns the page, or
   -1 when every page is full. */
static int allocateAtlasSpace(vita2d_pgf *font, int width, int height, SDL_Rect *rectangle) {
  if (width + GLYPH_PADDING > ATLAS_SIZE || height + GLYPH_PADDING > ATLAS_SIZE)
    return -1;

  if (!fitsOnCurrentPage(font, width, height) && addAtlasPage(font) != 0)
    return -1;

  if (font->cursor_x + width + GLYPH_PADDING > ATLAS_SIZE) {
    font->cursor_x = 0;
    font->cursor_y += font->row_height + GLYPH_PADDING;
    font->row_height = 0;
  }

  rectangle->x = font->cursor_x;
  rectangle->y = font->cursor_y;
  rectangle->w = width;
  rectangle->h = height;

  font->cursor_x += width + GLYPH_PADDING;
  if (height > font->row_height)
    font->row_height = height;

  return font->page_count - 1;
}

/* Renders a measured glyph into the atlas (main thread only). Returns -1
   if every atlas page is full. A glyph without pixels (a space) gets none. */
static int rasterizeGlyph(vita2d_pgf *font, Glyph *glyph) {
  uint32_t drawn;
  TTF_Font *face = resolveCharacter(font, glyph->codepoint, &drawn);

  glyph->rasterized = 1;
  glyph->page = -1;

  SDL_Color white = { 0xFF, 0xFF, 0xFF, 0xFF };
  SDL_Surface *rendered = TTF_RenderGlyph32_Blended(face, drawn, white);
  if (!rendered)
    return 0;

  SDL_Surface *converted = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(rendered);
  if (!converted)
    return 0;

  SDL_Rect rectangle;
  int page = allocateAtlasSpace(font, converted->w, converted->h, &rectangle);
  if (page >= 0) {
    SDL_UpdateTexture(font->pages[page], &rectangle, converted->pixels, converted->pitch);
    glyph->page = page;
    glyph->source = rectangle;
  }

  int atlas_full = page < 0 && font->page_count == MAX_ATLAS_PAGES;
  SDL_FreeSurface(converted);

  if (atlas_full) {
    glyph->rasterized = 0;
    return -1;
  }
  return 0;
}

/* The cached glyph of a code point, measured on first use; with
   need_pixels, also rendered. The pointer is valid until the next call. */
static const Glyph *findGlyph(vita2d_pgf *font, uint32_t codepoint, int need_pixels) {
  if (codepoint == 0)
    return NULL;

  if (font->glyph_count * 2 >= font->glyph_table_size) {
    if (growGlyphTable(font) != 0)
      return NULL;
  }

  size_t slot = glyphSlot(font, codepoint);
  Glyph *glyph = &font->glyphs[slot];
  if (glyph->codepoint != codepoint) {
    measureGlyph(font, codepoint, glyph);
    font->glyph_count++;
  }

  if (need_pixels && !glyph->rasterized) {
    if (rasterizeGlyph(font, glyph) != 0) {
      // Every page is full: start the atlas over. Text drawn earlier this
      // frame was submitted before its pages are destroyed.
      releaseAtlas(font);
      rasterizeGlyph(font, glyph);
    }
  }

  return glyph;
}

/* Rasterizes the font again at a new output scale, after the window was
   resized (vita2d_sdl.c). The faces take the new pixel size, and the
   glyph metrics, which are in output pixels, are measured again. Runs on
   the main thread, as it destroys the atlas textures, with the lock held. */
static void rescaleFont(vita2d_pgf *font, float raster_scale) {
  releaseAtlas(font);
  if (font->glyphs)
    memset(font->glyphs, 0, font->glyph_table_size * sizeof(Glyph));
  font->glyph_count = 0;

  font->raster_scale = raster_scale;
  int i;
  for (i = 0; i < font->face_count; i++)
    TTF_SetFontSize(font->faces[i], pixelSizeFor(raster_scale));

  if (font->face_count > 0) {
    font->ascent = TTF_FontAscent(font->faces[0]);
    font->line_skip = TTF_FontLineSkip(font->faces[0]);
  }
}

/* Text */

/* Draws (if draw is set) and measures text whose first baseline is at
   (x, y); returns the width of the widest line in logical pixels. */
static int layoutText(vita2d_pgf *font, int x, int y, unsigned int color, float scale,
                      const char *text, int draw) {
  if (!font || !text || font->face_count == 0)
    return 0;

  SDL_LockMutex(font->lock);

  // Drawing is on the main thread; measuring can be on a worker thread, and
  // keeps the old scale until the next draw. Its logical widths differ
  // from the new scale's only by hinting.
  if (draw && font->raster_scale != vita2dOutputScale())
    rescaleFont(font, vita2dOutputScale());

  float raster_scale = font->raster_scale;
  float size_scale = scale / raster_scale; // output pixels to logical pixels

  float start_x = (float)x * raster_scale;
  float pen_x = start_x;
  float max_x = start_x;
  float baseline = (float)y * raster_scale;

  Uint8 r = 0, g = 0, b = 0, a = 0;
  int colored_pages = 0;
  if (draw)
    vita2dColorComponents(color, &r, &g, &b, &a);

  while (*text) {
    uint32_t codepoint;
    text = decodeUtf8(text, &codepoint);

    if (codepoint == '\n') {
      if (pen_x > max_x)
        max_x = pen_x;
      pen_x = start_x;
      baseline += (float)font->line_skip * scale;
      continue;
    }

    const Glyph *glyph = findGlyph(font, codepoint, draw);
    if (!glyph)
      continue;

    if (draw && glyph->page >= 0) {
      // Pages created while laying out this text need its colour too.
      while (colored_pages < font->page_count) {
        SDL_SetTextureColorMod(font->pages[colored_pages], r, g, b);
        SDL_SetTextureAlphaMod(font->pages[colored_pages], a);
        colored_pages++;
      }

      // Whole output pixels keep the nearest-filtered glyphs sharp.
      float top = baseline - (float)glyph->ascent * scale;

      SDL_FRect destination;
      destination.x = floorf(pen_x + TEXT_LEFT_INSET * raster_scale) / raster_scale;
      destination.y = floorf(top) / raster_scale;
      destination.w = (float)glyph->source.w * size_scale;
      destination.h = (float)glyph->source.h * size_scale;
      SDL_RenderCopyF(vita2dRenderer(), font->pages[glyph->page], &glyph->source, &destination);
    }

    pen_x += glyph->advance * scale;
  }

  if (pen_x > max_x)
    max_x = pen_x;

  SDL_UnlockMutex(font->lock);

  return (int)lroundf((max_x - start_x) / raster_scale);
}

int vita2d_pgf_draw_text(vita2d_pgf *font, int x, int y, unsigned int color, float scale,
                         const char *text) {
  return layoutText(font, x, y, color, scale, text, 1);
}

int vita2d_pgf_draw_textf(vita2d_pgf *font, int x, int y, unsigned int color, float scale,
                          const char *text, ...) {
  char buffer[1024];
  va_list arguments;
  va_start(arguments, text);
  vsnprintf(buffer, sizeof(buffer), text, arguments);
  va_end(arguments);
  return vita2d_pgf_draw_text(font, x, y, color, scale, buffer);
}

int vita2d_pgf_text_width(vita2d_pgf *font, float scale, const char *text) {
  return layoutText(font, 0, 0, 0, scale, text, 0);
}

int vita2d_pgf_text_height(vita2d_pgf *font, float scale, const char *text) {
  if (!font || font->face_count == 0)
    return 0;

  int lines = 1;
  const char *p;
  for (p = text; p && *p; p++) {
    if (*p == '\n')
      lines++;
  }

  return (int)lroundf((float)(font->line_skip * lines) * scale / font->raster_scale);
}
