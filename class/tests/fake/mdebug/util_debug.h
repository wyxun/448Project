#ifndef TEST_UTIL_DEBUG_H
#define TEST_UTIL_DEBUG_H
void test_log(const char *format, ...);
#define MLOGF(LEVEL, ...) test_log(__VA_ARGS__)
#endif
