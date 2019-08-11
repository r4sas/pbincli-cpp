#ifndef MAIN_H__
#define MAIN_H__

#define PROGRAM_NAME "pbincli"
#define PACKAGE "pbincli"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "pbincli"
#define PACKAGE_URL "https://github.com/r4sas/pbincli-c++"
#define PACKAGE_VERSION "0.1"

#if JANSSON_MAJOR_VERSION >= 2
#define JSON_LOADS(str, err_ptr) json_loads((str), 0, (err_ptr))
#define JSON_LOADF(str, err_ptr) json_load_file((str), 0, (err_ptr))
#else
#define JSON_LOADS(str, err_ptr) json_loads((str), (err_ptr))
#define JSON_LOADF(str, err_ptr) json_load_file((str), (err_ptr))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#undef unlikely
#undef likely
#if defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
#define unlikely(expr) (__builtin_expect(!!(expr), 0))
#define likely(expr) (__builtin_expect(!!(expr), 1))
#else
#define unlikely(expr) (expr)
#define likely(expr) (expr)
#endif

#endif