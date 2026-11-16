/* plume-shell — petit lecteur JSON (assez pour les réponses de sway). */
#include "shell.h"

#include <stdlib.h>
#include <string.h>

struct parser {
	const char *p;
};

static void ws(struct parser *ps)
{
	while (*ps->p == ' ' || *ps->p == '\n' || *ps->p == '\r' || *ps->p == '\t')
		ps->p++;
}

static struct json *parse_value(struct parser *ps, int depth);

static void put_utf8(char **o, unsigned cp)
{
	char *d = *o;
	if (cp < 0x80) {
		*d++ = cp;
	} else if (cp < 0x800) {
		*d++ = 0xC0 | (cp >> 6);
		*d++ = 0x80 | (cp & 0x3F);
	} else if (cp < 0x10000) {
		*d++ = 0xE0 | (cp >> 12);
		*d++ = 0x80 | ((cp >> 6) & 0x3F);
		*d++ = 0x80 | (cp & 0x3F);
	} else {
		*d++ = 0xF0 | (cp >> 18);
		*d++ = 0x80 | ((cp >> 12) & 0x3F);
		*d++ = 0x80 | ((cp >> 6) & 0x3F);
		*d++ = 0x80 | (cp & 0x3F);
	}
	*o = d;
}

static unsigned hex4(const char *s)
{
	unsigned v = 0;
	for (int i = 0; i < 4; i++) {
		char c = s[i];
		v <<= 4;
		if (c >= '0' && c <= '9') v |= c - '0';
		else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
		else return 0xFFFD;
	}
	return v;
}

static char *parse_string(struct parser *ps)
{
	if (*ps->p != '"')
		return NULL;
	ps->p++;
	const char *start = ps->p;
	size_t len = 0;
	while (start[len] && start[len] != '"') {
		if (start[len] == '\\' && start[len + 1])
			len++;
		len++;
	}
	char *out = malloc(len * 2 + 4), *o = out;
	while (*ps->p && *ps->p != '"') {
		char c = *ps->p++;
		if (c != '\\') {
			*o++ = c;
			continue;
		}
		c = *ps->p++;
		switch (c) {
		case 'n': *o++ = '\n'; break;
		case 't': *o++ = '\t'; break;
		case 'r': *o++ = '\r'; break;
		case 'b': *o++ = '\b'; break;
		case 'f': *o++ = '\f'; break;
		case 'u': {
			unsigned cp = hex4(ps->p);
			ps->p += 4;
			if (cp >= 0xD800 && cp < 0xDC00 && ps->p[0] == '\\' && ps->p[1] == 'u') {
				unsigned lo = hex4(ps->p + 2);
				ps->p += 6;
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
			}
			put_utf8(&o, cp);
			break;
		}
		case '\0': ps->p--; break;
		default: *o++ = c;
		}
	}
	if (*ps->p == '"')
		ps->p++;
	*o = '\0';
	return out;
}

static struct json *parse_container(struct parser *ps, int depth, bool obj)
{
	struct json *j = calloc(1, sizeof *j), **tail = &j->child;
	j->t = obj ? J_OBJ : J_ARR;
	ps->p++;
	ws(ps);
	if (*ps->p == (obj ? '}' : ']')) {
		ps->p++;
		return j;
	}
	while (*ps->p) {
		char *key = NULL;
		if (obj) {
			ws(ps);
			key = parse_string(ps);
			ws(ps);
			if (*ps->p == ':')
				ps->p++;
		}
		struct json *v = parse_value(ps, depth + 1);
		if (!v) {
			free(key);
			break;
		}
		v->key = key;
		*tail = v;
		tail = &v->next;
		ws(ps);
		if (*ps->p == ',') {
			ps->p++;
			continue;
		}
		if (*ps->p == (obj ? '}' : ']'))
			ps->p++;
		break;
	}
	return j;
}

static struct json *parse_value(struct parser *ps, int depth)
{
	if (depth > 128)
		return NULL;
	ws(ps);
	struct json *j;
	switch (*ps->p) {
	case '{': return parse_container(ps, depth, true);
	case '[': return parse_container(ps, depth, false);
	case '"':
		j = calloc(1, sizeof *j);
		j->t = J_STR;
		j->s = parse_string(ps);
		return j;
	case 't':
	case 'f':
		j = calloc(1, sizeof *j);
		j->t = J_BOOL;
		j->b = *ps->p == 't';
		ps->p += j->b ? 4 : 5;
		return j;
	case 'n':
		j = calloc(1, sizeof *j);
		ps->p += 4;
		return j;
	default:
		if ((*ps->p >= '0' && *ps->p <= '9') || *ps->p == '-') {
			j = calloc(1, sizeof *j);
			j->t = J_NUM;
			char *end;
			j->n = strtod(ps->p, &end);
			ps->p = end;
			return j;
		}
		return NULL;
	}
}

struct json *json_parse(const char *s)
{
	if (!s)
		return NULL;
	struct parser ps = { s };
	return parse_value(&ps, 0);
}

void json_free(struct json *j)
{
	while (j) {
		struct json *n = j->next;
		json_free(j->child);
		free(j->key);
		free(j->s);
		free(j);
		j = n;
	}
}

struct json *json_get(struct json *obj, const char *key)
{
	if (!obj || obj->t != J_OBJ)
		return NULL;
	for (struct json *c = obj->child; c; c = c->next)
		if (c->key && !strcmp(c->key, key))
			return c;
	return NULL;
}

const char *json_str(struct json *obj, const char *key)
{
	struct json *v = json_get(obj, key);
	return v && v->t == J_STR && v->s ? v->s : "";
}

bool json_bool(struct json *obj, const char *key)
{
	struct json *v = json_get(obj, key);
	return v && v->t == J_BOOL && v->b;
}

double json_num(struct json *obj, const char *key)
{
	struct json *v = json_get(obj, key);
	return v && v->t == J_NUM ? v->n : 0;
}
