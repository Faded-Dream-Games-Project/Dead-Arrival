// ============================================================================
//  DEAD ARRIVAL - json.c
//  Minimal JSON reader for the data files (guns.json, perks.json, maps/*.json).
//  Not a general-purpose library: parses one document into a lightweight
//  node tree, lookups are by key. No writes, no streaming, no unicode
//  escapes beyond the basics. Always succeeds structurally: on any error
//  the parser returns NULL and the caller falls back to built-in defaults.
// ============================================================================
#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static JsonNode *sNodePool = NULL;
static int        sPoolUsed = 0;
static char      *sStringPool = NULL;
static int        sStringUsed = 0;

#define MAX_NODES 20000
#define MAX_STRING_BYTES (256 * 1024)

void JsonInit(void) {
    if (!sNodePool) {
        sNodePool = calloc(MAX_NODES, sizeof(JsonNode));
        sStringPool = malloc(MAX_STRING_BYTES);
    }
    sPoolUsed = 0;
    sStringUsed = 0;
}

void JsonShutdown(void) {
    free(sNodePool); sNodePool = NULL;
    free(sStringPool); sStringPool = NULL;
    sPoolUsed = sStringUsed = 0;
}

static JsonNode *NewNode(JsonType type) {
    if (sPoolUsed >= MAX_NODES) return NULL;
    JsonNode *n = &sNodePool[sPoolUsed++];
    memset(n, 0, sizeof *n);
    n->type = type;
    return n;
}

// interning: keep parsed strings in one arena, NUL-terminated
static const char *PoolStr(const char *s, int len) {
    if (sStringUsed + len + 1 > MAX_STRING_BYTES) return "";
    char *dst = &sStringPool[sStringUsed];
    memcpy(dst, s, len);
    dst[len] = 0;
    sStringUsed += len + 1;
    return dst;
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------
typedef struct {
    const char *p;
    const char *end;
    int         depth;      // runaway guard
    bool        failed;
} Parser;

static void SkipWs(Parser *ps) {
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ps->p++;
        else if (c == '/' && ps->p + 1 < ps->end && ps->p[1] == '/') {
            while (ps->p < ps->end && *ps->p != '\n') ps->p++;
        } else if (c == '/' && ps->p + 1 < ps->end && ps->p[1] == '*') {
            ps->p += 2;
            while (ps->p + 1 < ps->end && !(ps->p[0] == '*' && ps->p[1] == '/')) ps->p++;
            if (ps->p + 1 < ps->end) ps->p += 2;
        } else break;
    }
}

static JsonNode *ParseValue(Parser *ps, int depth);

static bool ParseStringRaw(Parser *ps, const char **out, int *outLen) {
    if (ps->p >= ps->end || *ps->p != '"') { ps->failed = true; return false; }
    ps->p++;
    const char *start = ps->p;
    // fast scan (no escapes in our data files; handles \" \\ \n \t \/ \uXXXX copy)
    while (ps->p < ps->end && *ps->p != '"') {
        if (*ps->p == '\\' && ps->p + 1 < ps->end) ps->p++;   // skip escaped char
        ps->p++;
    }
    if (ps->p >= ps->end) { ps->failed = true; return false; }
    *out = start; *outLen = (int)(ps->p - start);
    ps->p++;   // closing quote
    return true;
}

static JsonNode *ParseObject(Parser *ps, int depth) {
    JsonNode *obj = NewNode(JSON_OBJECT);
    if (!obj) { ps->failed = true; return NULL; }
    obj->size = 0;
    ps->p++;   // {
    SkipWs(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
    for (;;) {
        SkipWs(ps);
        const char *key; int keyLen;
        if (!ParseStringRaw(ps, &key, &keyLen)) return NULL;
        SkipWs(ps);
        if (ps->p >= ps->end || *ps->p != ':') { ps->failed = true; return NULL; }
        ps->p++;
        SkipWs(ps);
        JsonNode *val = ParseValue(ps, depth + 1);
        if (!val) return NULL;
        if (obj->size < JSON_MAX_FIELDS) {
            obj->fields[obj->size].key = PoolStr(key, keyLen);
            obj->fields[obj->size].val = val;
            obj->size++;
        }
        SkipWs(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
        ps->failed = true; return NULL;
    }
}

static JsonNode *ParseArray(Parser *ps, int depth) {
    JsonNode *arr = NewNode(JSON_ARRAY);
    if (!arr) { ps->failed = true; return NULL; }
    arr->size = 0;
    ps->p++;   // [
    SkipWs(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
    for (;;) {
        SkipWs(ps);
        JsonNode *val = ParseValue(ps, depth + 1);
        if (!val) return NULL;
        if (arr->size < JSON_MAX_FIELDS) {
            arr->fields[arr->size].key = NULL;
            arr->fields[arr->size].val = val;
            arr->size++;
        }
        SkipWs(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
        ps->failed = true; return NULL;
    }
}

static JsonNode *ParseValue(Parser *ps, int depth) {
    if (depth > 32) { ps->failed = true; return NULL; }
    SkipWs(ps);
    if (ps->p >= ps->end) { ps->failed = true; return NULL; }
    char c = *ps->p;
    if (c == '{') return ParseObject(ps, depth);
    if (c == '[') return ParseArray(ps, depth);
    if (c == '"') {
        const char *s; int len;
        if (!ParseStringRaw(ps, &s, &len)) return NULL;
        JsonNode *n = NewNode(JSON_STRING);
        if (!n) { ps->failed = true; return NULL; }
        n->str = PoolStr(s, len);
        return n;
    }
    if (c == 't' && ps->end - ps->p >= 4 && !strncmp(ps->p, "true", 4)) {
        ps->p += 4;
        JsonNode *n = NewNode(JSON_BOOL);
        if (!n) { ps->failed = true; return NULL; }
        n->boolean = true;
        return n;
    }
    if (c == 'f' && ps->end - ps->p >= 5 && !strncmp(ps->p, "false", 5)) {
        ps->p += 5;
        JsonNode *n = NewNode(JSON_BOOL);
        if (!n) { ps->failed = true; return NULL; }
        n->boolean = false;
        return n;
    }
    if (c == 'n' && ps->end - ps->p >= 4 && !strncmp(ps->p, "null", 4)) {
        ps->p += 4;
        return NewNode(JSON_NULL);
    }
    // number
    {
        char *endptr = NULL;
        double v = strtod(ps->p, &endptr);
        if (endptr == ps->p) { ps->failed = true; return NULL; }
        JsonNode *n = NewNode(JSON_NUMBER);
        if (!n) { ps->failed = true; return NULL; }
        n->num = v;
        ps->p = endptr;
        return n;
    }
}

JsonNode *JsonParse(const char *text) {
    if (!text || !text[0]) return NULL;
    Parser ps = { text, text + strlen(text), 0, false };
    JsonNode *root = ParseValue(&ps, 0);
    if (ps.failed || !root) return NULL;
    SkipWs(&ps);
    if (ps.p != ps.end) return NULL;    // trailing garbage
    return root;
}

// ---------------------------------------------------------------------------
// File loading (whole file into a malloc'd string; caller frees)
// ---------------------------------------------------------------------------
char *JsonReadFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0 || size > 4 * 1024 * 1024) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return NULL; }
    buf[size] = 0;
    return buf;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
JsonNode *JsonGet(JsonNode *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->size; i++)
        if (!strcmp(obj->fields[i].key, key)) return obj->fields[i].val;
    return NULL;
}

JsonNode *JsonAt(JsonNode *arr, int idx) {
    if (!arr || arr->type != JSON_ARRAY || idx < 0 || idx >= arr->size) return NULL;
    return arr->fields[idx].val;
}

int JsonLen(JsonNode *arr) {
    return (arr && arr->type == JSON_ARRAY) ? arr->size : 0;
}

// typed getters with fallback (missing/invalid -> def, never crash)
const char *JsonStr(JsonNode *obj, const char *key, const char *def) {
    JsonNode *n = JsonGet(obj, key);
    return (n && n->type == JSON_STRING && n->str[0]) ? n->str : def;
}
double JsonNum(JsonNode *obj, const char *key, double def) {
    JsonNode *n = JsonGet(obj, key);
    return (n && n->type == JSON_NUMBER) ? n->num : def;
}
int JsonInt(JsonNode *obj, const char *key, int def) {
    JsonNode *n = JsonGet(obj, key);
    if (!n) return def;
    if (n->type == JSON_NUMBER) return (int)n->num;
    if (n->type == JSON_BOOL) return n->boolean ? 1 : 0;
    return def;
}
bool JsonBool(JsonNode *obj, const char *key, bool def) {
    JsonNode *n = JsonGet(obj, key);
    if (!n) return def;
    if (n->type == JSON_BOOL) return n->boolean;
    if (n->type == JSON_NUMBER) return n->num != 0;
    return def;
}
// color: [r,g,b] or [r,g,b,a] arrays (0-255)
bool JsonColor(JsonNode *obj, const char *key, Color *out) {
    JsonNode *n = JsonGet(obj, key);
    if (!n || n->type != JSON_ARRAY || n->size < 3) return false;
    int c[4] = { 0, 0, 0, 255 };
    for (int i = 0; i < n->size && i < 4; i++) {
        if (n->fields[i].val->type != JSON_NUMBER) return false;
        double v = n->fields[i].val->num;
        if (v < 0) v = 0; if (v > 255) v = 255;
        c[i] = (int)v;
    }
    out->r = (unsigned char)c[0]; out->g = (unsigned char)c[1];
    out->b = (unsigned char)c[2]; out->a = (unsigned char)c[3];
    return true;
}
// vector: [x,y,z]
bool JsonVec3(JsonNode *obj, const char *key, Vector3 *out) {
    JsonNode *n = JsonGet(obj, key);
    if (!n || n->type != JSON_ARRAY || n->size < 3) return false;
    if (n->fields[0].val->type != JSON_NUMBER) return false;
    out->x = (float)n->fields[0].val->num;
    out->y = (float)n->fields[1].val->num;
    out->z = (float)n->fields[2].val->num;
    return true;
}
