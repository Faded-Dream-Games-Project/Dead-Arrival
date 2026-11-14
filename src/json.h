// ============================================================================
//  DEAD ARRIVAL - json.h
//  Minimal read-only JSON tree for data files. See json.c.
// ============================================================================
#ifndef JSON_H
#define JSON_H

#include "raylib.h"      // Color
#include <stdbool.h>

#define JSON_MAX_FIELDS 128

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

typedef struct JsonNode {
    JsonType type;
    // value payloads (union-ish; only one valid per type)
    double       num;      // NUMBER
    bool         boolean;  // BOOL
    const char  *str;      // STRING (interned)
    // OBJECT: key/val pairs; ARRAY: vals (key NULL)
    struct { const char *key; struct JsonNode *val; } fields[JSON_MAX_FIELDS];
    int size;
} JsonNode;

// parse a NUL-terminated document; NULL on any error (caller falls back)
JsonNode *JsonParse(const char *text);
// read a whole file; NULL if missing/unreadable; caller frees
char     *JsonReadFile(const char *path);

// arena lifetime: init before parsing, shutdown to free everything
void JsonInit(void);
void JsonShutdown(void);

// accessors (NULL-safe; invalid node types return fallbacks)
JsonNode *JsonGet(JsonNode *obj, const char *key);
JsonNode *JsonAt(JsonNode *arr, int idx);
int       JsonLen(JsonNode *arr);
const char *JsonStr(JsonNode *obj, const char *key, const char *def);
double    JsonNum(JsonNode *obj, const char *key, double def);
int       JsonInt(JsonNode *obj, const char *key, int def);
bool      JsonBool(JsonNode *obj, const char *key, bool def);
bool      JsonColor(JsonNode *obj, const char *key, Color *out);
bool      JsonVec3(JsonNode *obj, const char *key, Vector3 *out);

#endif
