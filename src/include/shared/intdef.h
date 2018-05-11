#ifndef TINY_OS_LIB_INT_DEF_H
#define TINY_OS_LIB_INT_DEF_H

#ifndef TINY_OS_NO_INTDEF

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef uint32_t size_t;

#endif /* no TINY_OS_NO_INTDEF */

#ifndef NULL
    #define NULL ((void*)0)
#endif

#define ARRAY_SIZE(A) (sizeof(A)/sizeof(A[0]))

#endif /* TINY_OS_LIB_INT_DEF_H */
