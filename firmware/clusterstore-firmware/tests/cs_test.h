#ifndef CS_TEST_H
#define CS_TEST_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define CS_TEST_ASSERT(CONDITION)                                                \
    do {                                                                         \
        if (!(CONDITION)) {                                                      \
            fprintf(stderr,                                                      \
                    "Assertion failed at %s:%d: %s\n",                           \
                    __FILE__,                                                    \
                    __LINE__,                                                    \
                    #CONDITION);                                                 \
            return EXIT_FAILURE;                                                 \
        }                                                                        \
    } while (0)

#define CS_TEST_ASSERT_EQ_U32(ACTUAL, EXPECTED)                                  \
    do {                                                                         \
        unsigned long _actual = (unsigned long)(ACTUAL);                         \
        unsigned long _expected = (unsigned long)(EXPECTED);                     \
        if (_actual != _expected) {                                              \
            fprintf(stderr,                                                      \
                    "Assertion failed at %s:%d: %s (%lu) != %s (%lu)\n",         \
                    __FILE__,                                                    \
                    __LINE__,                                                    \
                    #ACTUAL,                                                     \
                    _actual,                                                     \
                    #EXPECTED,                                                   \
                    _expected);                                                  \
            return EXIT_FAILURE;                                                 \
        }                                                                        \
    } while (0)

#define CS_TEST_ASSERT_TRUE(VALUE) CS_TEST_ASSERT((VALUE))
#define CS_TEST_ASSERT_FALSE(VALUE) CS_TEST_ASSERT(!(VALUE))

#endif
