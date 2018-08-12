#define _GNU_SOURCE
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#include "quake_common.h"
#include "patches.h"
#include "common.h"

signal_handler_ptr signal_handler;

Cmd_CallVote_f_ptr Cmd_CallVote_f;

int patch_by_mask(pint offset, char* pattern, char* mask) {
  int res, page_size;

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size == -1) return errno;
  res = mprotect((void*)(offset & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
  if (res) return errno;

  for (int i=0; mask[i]; i++) {
    if (mask[i] != 'X')
      continue;

    *(int8_t*)(offset+i) = pattern[i];
  }
  return 0;
}

void vote_clientkick_fix(void) {
  Cmd_CallVote_f = (Cmd_CallVote_f_ptr)PatternSearch((void*)((pint)qagame + 0xB000),
    0xB0000, PTRN_CMD_CALLVOTE_F, MASK_CMD_CALLVOTE_F);
  if (Cmd_CallVote_f == NULL) {
    DebugPrint("WARNING: Unable to find Cmd_CallVote_f. Skipping callvote-clientkick patch...\n");
    return;
  }

  patch_by_mask( ADDR_VOTE_CLIENTKICK_FIX, PTRN_VOTE_CLIENTKICK_FIX, MASK_VOTE_CLIENTKICK_FIX );
}

void __cdecl My_signal_handler(int sig) {
  void* callstack[128];
  int frames = backtrace(callstack, 128);
  char** strs = backtrace_symbols(callstack, frames);
  for (int i = 0; i < frames; ++i) {
    printf("%s\n", strs[i]);
  }
  free(strs);
  signal_handler(sig);
}

void patch_backtrace(void) {
  int res;

  signal_handler = (signal_handler_ptr)PatternSearchModule(&qzeroded_module, PTRN_SIGNAL_HANDLER, MASK_SIGNAL_HANDLER);
  if (signal_handler == NULL) {
    DebugPrint("WARNING: Unable to find signal_handler. Skipping backtrace patch...\n");
    return;
  }

  res = Hook((void*)signal_handler, My_signal_handler, (void*)&signal_handler);
  if (res) {
    DebugPrint("WARNING: Failed to hook signal_handler: %d. Skipping backtrace patch...\n", res);
    return;
  }
}

void patch_static(void) {
  patch_backtrace();
}

void patch_vm(void) {
  vote_clientkick_fix();
}
