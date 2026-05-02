#include "message_log.h"
#include <psp2/kernel/processmgr.h>

VitaChiakiMessageLog *message_log_create() {
  VitaChiakiMessageLog *ml = (VitaChiakiMessageLog *)malloc(sizeof(VitaChiakiMessageLog));
  ml->start_offset = 0;  // offset of first line
  ml->lines = 0;         // total lines
  ml->last_update = 0;   // update time

  // create an extra blank line at the end
  ml->log[MLOG_LINES][0] = 0;
  return ml;
}

void write_message_log(VitaChiakiMessageLog *ml, const char *text) {
  size_t text_len = strlen(text);
  if (text_len == 0)
    return;

  int text_lines = 1 + (text_len - 1) / MLOG_LINE_LEN;
  int start_line = text_lines > MLOG_LINES ? (text_lines - MLOG_LINES) : 0;
  int offset = 0;
  for (int j = start_line; j < text_lines; j++) {
    int n = MLOG_LINE_LEN;
    if (j == text_lines - 1) {  // on the last line
      n = ((text_len - 1) % MLOG_LINE_LEN) + 1;
    }

    int line_offset = ml->lines;  // if not yet wrapped
    if (ml->lines >= MLOG_LINES) {
      line_offset = ml->start_offset;
      ml->start_offset = (ml->start_offset + 1) % MLOG_LINES;
    }

    memcpy(ml->log[line_offset], text + offset, n);
    ml->log[line_offset][n + 1] = 0;  // add null char
    ml->lines++;
    if (ml->lines > MLOG_LINES)
      ml->lines = MLOG_LINES;
    offset += MLOG_LINE_LEN;
  }
  ml->last_update = sceKernelGetProcessTimeWide();
}

char *get_message_log_line(VitaChiakiMessageLog *ml, size_t line) {
  if (ml->lines == 0)
    return ml->log[MLOG_LINES];

  int line_offset = (ml->start_offset + line) % MLOG_LINES;

  if (line_offset < 0)
    line_offset = MLOG_LINES;
  if (line_offset >= ml->lines)
    line_offset = MLOG_LINES;
  if (line_offset >= MLOG_LINES)
    line_offset = MLOG_LINES;

  return ml->log[line_offset];
}
