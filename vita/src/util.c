#include <stdio.h>
#include <chiaki/base64.h>
#include <psp2/message_dialog.h>
#include "util.h"
#include <string.h>

void parse_b64(const char* val, uint8_t* dest, size_t len) {
  chiaki_base64_decode(val, get_base64_size(len), dest, &len);
}

void parse_mac(const char* mac_str, uint8_t* mac_dest) {
  // Given a string of length 12, e.g. DEADBEEF0000, convert to 6 ints
  for (int j = 0; j < 6; j++) {
    char digit[3];
    digit[0] = mac_str[2*j];
    digit[1] = mac_str[2*j+1];
    digit[2] = 0;

    mac_dest[j] = strtol(digit, NULL, 16);
  }
}

// void parse_b64(const char* val, uint8_t* dest, size_t len) {
//   for (size_t i=0; i < len; i++) {
//     sscanf(val+(i*2), "%02hhx", &(dest[i]));
//   }
// }

/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW
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
void utf16_to_utf8(const uint16_t *src, uint8_t *dst) {
  int i;
  for (i = 0; src[i]; i++) {
    if ((src[i] & 0xFF80) == 0) {
      *(dst++) = src[i] & 0xFF;
    } else if((src[i] & 0xF800) == 0) {
      *(dst++) = ((src[i] >> 6) & 0xFF) | 0xC0;
      *(dst++) = (src[i] & 0x3F) | 0x80;
    } else if((src[i] & 0xFC00) == 0xD800 && (src[i + 1] & 0xFC00) == 0xDC00) {
      *(dst++) = (((src[i] + 64) >> 8) & 0x3) | 0xF0;
      *(dst++) = (((src[i] >> 2) + 16) & 0x3F) | 0x80;
      *(dst++) = ((src[i] >> 4) & 0x30) | 0x80 | ((src[i + 1] << 2) & 0xF);
      *(dst++) = (src[i + 1] & 0x3F) | 0x80;
      i += 1;
    } else {
      *(dst++) = ((src[i] >> 12) & 0xF) | 0xE0;
      *(dst++) = ((src[i] >> 6) & 0x3F) | 0x80;
      *(dst++) = (src[i] & 0x3F) | 0x80;
    }
  }

  *dst = '\0';
}

void utf8_to_utf16(const uint8_t *src, uint16_t *dst) {
  int i;
  for (i = 0; src[i];) {
    if ((src[i] & 0xE0) == 0xE0) {
      *(dst++) = ((src[i] & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F);
      i += 3;
    } else if ((src[i] & 0xC0) == 0xC0) {
      *(dst++) = ((src[i] & 0x1F) << 6) | (src[i + 1] & 0x3F);
      i += 2;
    } else {
      *(dst++) = src[i];
      i += 1;
    }
  }

  *dst = '\0';
}

size_t get_base64_size(size_t in)
{
	// calculate base64 buffer size after encode
	return ((4 * in / 3) + 3) & ~3;
}

// Message dialog
/*
  VitaGL
  Copyright (C) 2020-2024, Rinnegatamante
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

// Initialize sceMsgDialog widget with a given message text
int init_msg_dialog(const char *msg) {
	SceMsgDialogUserMessageParam msg_param;
	memset(&msg_param, 0, sizeof(msg_param));
	msg_param.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
	msg_param.msg = (SceChar8 *)msg;

	SceMsgDialogParam param;
	sceMsgDialogParamInit(&param);
	_sceCommonDialogSetMagicNumber(&param.commonParam);
	param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
	param.userMsgParam = &msg_param;

	return sceMsgDialogInit(&param);
}

// Gets current state for sceMsgDialog running widget
int get_msg_dialog_result(void) {
	if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
		return 0;
	sceMsgDialogTerm();
	return 1;
}
