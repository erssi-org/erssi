#ifndef IRSSI_FE_FUZZ_E2E_E2E_FUZZ_H
#define IRSSI_FE_FUZZ_E2E_E2E_FUZZ_H

/* the two entry points of a fuzz target, called by libFuzzer or by
   replay.c */

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#endif
