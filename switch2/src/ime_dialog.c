/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW
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
 * Switch 2 replacement for VitaShell's ime_dialog.c, keeping its interface
 * (ime_dialog.h). The Vita's system IME dialog becomes the Switch software
 * keyboard library applet (swkbd). The applet runs while initImeDialog
 * waits; the next updateImeDialog reports its result. If the applet cannot
 * start, the dialog is canceled.
 */

#include "main.h"
#include "init.h"
#include "ime_dialog.h"

#include <switch2_platform.h>

#include <switch.h>

#define MAX_TITLE_BYTES 256

enum KeyboardResult {
  KEYBOARD_FINISHED,
  KEYBOARD_CANCELED,
};

static int ime_dialog_running = 0;
static int keyboard_result = KEYBOARD_CANCELED;

static const char *ime_initial_text = NULL;

static uint16_t ime_input_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint8_t ime_input_text_utf8[4 * SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

/* Decodes UTF-8 into code points; invalid bytes are taken as Latin-1.
   Returns the number of code points. */
static int decodeUtf8(const char *string, uint32_t *out, int max_count) {
  const unsigned char *bytes = (const unsigned char *)string;
  int count = 0;

  while (*bytes && count < max_count) {
    uint32_t value = *bytes;
    int length = 1;

    if (value >= 0xF0 && value < 0xF8)
      length = 4;
    else if (value >= 0xE0 && value < 0xF0)
      length = 3;
    else if (value >= 0xC0 && value < 0xE0)
      length = 2;

    if (length > 1) {
      uint32_t decoded = value & (0x7F >> length);
      int i;
      for (i = 1; i < length; i++) {
        if ((bytes[i] & 0xC0) != 0x80)
          break;
        decoded = (decoded << 6) | (bytes[i] & 0x3F);
      }

      if (i == length)
        value = decoded;
      else
        length = 1;
    }

    out[count++] = value;
    bytes += length;
  }

  return count;
}

/* Stores the keyboard's text as VitaShell reads it: UTF-8 and UTF-16. */
static void storeResult(const char *result) {
  snprintf((char *)ime_input_text_utf8, sizeof(ime_input_text_utf8), "%s", result);

  static uint32_t codepoints[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
  int count = decodeUtf8(result, codepoints, SCE_IME_DIALOG_MAX_TEXT_LENGTH);

  int i;
  int out = 0;
  for (i = 0; i < count && out < SCE_IME_DIALOG_MAX_TEXT_LENGTH; i++) {
    uint32_t codepoint = codepoints[i];
    if (codepoint >= 0x10000) {
      if (out + 2 > SCE_IME_DIALOG_MAX_TEXT_LENGTH)
        break;
      codepoint -= 0x10000;
      ime_input_text_utf16[out++] = (uint16_t)(0xD800 | (codepoint >> 10));
      ime_input_text_utf16[out++] = (uint16_t)(0xDC00 | (codepoint & 0x3FF));
    } else {
      ime_input_text_utf16[out++] = (uint16_t)codepoint;
    }
  }
  ime_input_text_utf16[out] = 0;
}

/* Shows the software keyboard and records its result. */
static void showKeyboard(const char *title, const char *initial_text, int max_length,
                         int type, int password) {
  keyboard_result = KEYBOARD_CANCELED;

  SwkbdConfig config;
  Result rc = swkbdCreate(&config, 0);
  if (R_FAILED(rc)) {
    fprintf(stderr, "ime: swkbdCreate: 0x%x\n", rc);
    return;
  }

  swkbdConfigMakePresetDefault(&config);
  swkbdConfigSetHeaderText(&config, title);
  swkbdConfigSetInitialText(&config, initial_text);
  swkbdConfigSetStringLenMax(&config, (u32)max_length);
  if (type == SCE_IME_TYPE_NUMBER || type == SCE_IME_TYPE_EXTENDED_NUMBER)
    swkbdConfigSetType(&config, SwkbdType_NumPad);
  if (password)
    swkbdConfigSetPasswordFlag(&config, 1);

  static char result[4 * SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
  memset(result, 0, sizeof(result));
  rc = swkbdShow(&config, result, sizeof(result));
  swkbdClose(&config);

  if (R_SUCCEEDED(rc)) {
    storeResult(result);
    keyboard_result = KEYBOARD_FINISHED;
    return;
  }

  // libnx reports a user cancel as a bad library applet exit.
  if (rc != MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit))
    fprintf(stderr, "ime: swkbdShow: 0x%x\n", rc);
}

/* Interface (ime_dialog.h) */

int initImeDialog(const char *title, const char *initial_text, int max_text_length, int type,
                  int option, int password) {
  (void)option;

  if (ime_dialog_running)
    return VITASHELL_ERROR_ALREADY_RUNNING;

  ime_initial_text = initial_text;
  ime_input_text_utf8[0] = '\0';
  ime_input_text_utf16[0] = 0;

  char header[MAX_TITLE_BYTES];
  snprintf(header, sizeof(header), "%s", title ? title : "");

  int max_length = max_text_length;
  if (max_length <= 0 || max_length > SCE_IME_DIALOG_MAX_TEXT_LENGTH)
    max_length = SCE_IME_DIALOG_MAX_TEXT_LENGTH;

  ime_dialog_running = 1;
  showKeyboard(header, initial_text ? initial_text : "", max_length, type, password);
  return 0;
}

int isImeDialogRunning() {
  return ime_dialog_running;
}

uint16_t *getImeDialogInputTextUTF16() {
  return ime_input_text_utf16;
}

uint8_t *getImeDialogInputTextUTF8() {
  return ime_input_text_utf8;
}

const char *getImeDialogInitialText() {
  return ime_initial_text;
}

int updateImeDialog() {
  if (!ime_dialog_running)
    return IME_DIALOG_RESULT_NONE;

  ime_dialog_running = 0;
  return keyboard_result == KEYBOARD_FINISHED ? IME_DIALOG_RESULT_FINISHED
                                              : IME_DIALOG_RESULT_CANCELED;
}
