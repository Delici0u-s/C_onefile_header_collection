#ifndef NAME_HEADER
#define NAME_HEADER
#ifdef __cplusplus
extern "C" {
#endif

/*
to get function definitions #define NAME_IMPLEMENTATION before the #include
*/

/* possible options to set before 'first' include */
#ifndef NAME_OPTIONS
#ifdef NAME_OPTIONS
/* no options */
#endif
#endif

#ifdef NAME_IMPLEMENTATION
#define NAME_API
#else
#define NAME_API extern
#endif

#define NAME_INTERNAL

/* clang-format off */
/* declarations */

/* clang-format on */
#ifdef __cplusplus
} // extern "C"
#endif

#endif

// #define NAME_IMPLEMENTATION_DEVELOPMENT
#if defined(NAME_IMPLEMENTATION) || defined(NAME_IMPLEMENTATION_DEVELOPMENT)
#ifndef NAME_IMPLEMENTATION_INTERNAL
#define NAME_IMPLEMENTATION_INTERNAL
#ifdef __cplusplus
extern "C" {
#endif

/* implementations */

#ifdef __cplusplus
} // extern "C"
#endif
#endif
#endif