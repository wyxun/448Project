#ifndef RF448_TEST_LOG_H
#define RF448_TEST_LOG_H
void test_Log(const char *pchFormat, ...);
#define MLOGF(L, ...) test_Log(__VA_ARGS__)
#endif
