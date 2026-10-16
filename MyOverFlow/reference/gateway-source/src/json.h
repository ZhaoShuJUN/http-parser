#ifndef JSON_H
#define JSON_H
#include <stddef.h>
/* Self-contained bounded JSON tokenizer: offsets refer to the input string. */
typedef enum { JT_OBJECT=1, JT_ARRAY, JT_STRING, JT_PRIMITIVE } jtype_t;
typedef struct { int start, end, next; jtype_t type; } jtoken_t;
typedef struct { const char *s; jtoken_t *t; int count, max; } jdoc_t;
int json_parse(jdoc_t *d, const char *s, jtoken_t *t, int max);
int json_member(const jdoc_t *d, int obj, const char *key);
int json_get_str(const jdoc_t *d, int i, char *out, size_t cap);
int json_get_int(const jdoc_t *d, int i, int *out);
int json_copy_raw(const jdoc_t *d, int i, char *out, size_t cap);
#endif
