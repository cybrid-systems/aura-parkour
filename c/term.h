#ifndef PARKOUR_TERM_H
#define PARKOUR_TERM_H
/* Thin terminal viewport. M0 smoke blits one frame to stdout.
   Raw mode is optional and skipped when stdout is not a TTY. */
void parkour_term_blit(const char *frame);
int parkour_term_raw(int on); /* 0 ok, -1 skipped or failed */
#endif
