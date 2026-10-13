#ifndef TEST_MSHELL_H
#define TEST_MSHELL_H
#define MSHELL_ENABLE 1
#include "mdebug/mshell_parse.h"
#define MODUS_SHELL_CMD(N, H, D) \
    static void (*const test_##N)(const char *) __attribute__((unused)) = (H)
#endif
