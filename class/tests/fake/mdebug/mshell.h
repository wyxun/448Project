#ifndef RF448_TEST_SHELL_H
#define RF448_TEST_SHELL_H
#include "mshell_parse.h"
typedef void (*mshell_cmd_handler_t)(const char *);
#define MODUS_SHELL_CMD(N, H, S)                                                \
    mshell_cmd_handler_t test_##N##_handler = (H)
void mshell_Write(const char *pchText);
#endif
