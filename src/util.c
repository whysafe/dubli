#include "dubli.h"

#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("dubli: не хватает памяти\n", stderr);
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fputs("dubli: не хватает памяти\n", stderr);
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

void warn_path(const char *path, const char *what)
{
    fprintf(stderr, "dubli: %s: %s: %s\n", path, what, strerror(errno));
}

int parse_size(const char *s, off_t *out)
{
    static const char units[] = "kmgt";
    char *end;
    unsigned long long value, mul = 1;
    const char *unit;

    if (!isdigit((unsigned char)*s))
        return -1;
    errno = 0;
    value = strtoull(s, &end, 10);
    if (errno == ERANGE)
        return -1;

    unit = *end ? strchr(units, tolower((unsigned char)*end)) : NULL;
    if (unit) {
        mul = 1ULL << (10 * (unit - units + 1));
        end++;
        if (tolower((unsigned char)*end) == 'i')
            end++;
    }
    if (tolower((unsigned char)*end) == 'b')
        end++;
    if (*end)
        return -1;
    if (value > (unsigned long long)INT64_MAX / mul)
        return -1;

    *out = (off_t)(value * mul);
    return 0;
}

void format_size(off_t bytes, char *buf, size_t n)
{
    static const char *units[] = { "КиБ", "МиБ", "ГиБ", "ТиБ", "ПиБ" };
    double v = (double)bytes;
    int u = -1;

    if (bytes < 1024) {
        snprintf(buf, n, "%lld Б", (long long)bytes);
        return;
    }
    while (v >= 1024 && u < 4) {
        v /= 1024;
        u++;
    }
    snprintf(buf, n, "%.1f %s", v, units[u]);
}

const char *plural_ru(unsigned long long n, const char *one, const char *few, const char *many)
{
    unsigned long long m10 = n % 10, m100 = n % 100;

    if (m10 == 1 && m100 != 11)
        return one;
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
        return few;
    return many;
}

/* Patterns with a slash are matched against the whole path, the rest against the name. */
int is_excluded(const char *name, const char *path, const char **patterns, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const char *subject = strchr(patterns[i], '/') ? path : name;
        if (fnmatch(patterns[i], subject, 0) == 0)
            return 1;
    }
    return 0;
}

char *path_join(const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    int slash = dl > 0 && dir[dl - 1] != '/';
    char *p = xmalloc(dl + slash + nl + 1);

    memcpy(p, dir, dl);
    if (slash)
        p[dl] = '/';
    memcpy(p + dl + slash, name, nl + 1);
    return p;
}

void entry_push(EntryList *l, Entry e)
{
    if (l->len == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->items = xrealloc(l->items, l->cap * sizeof *l->items);
    }
    l->items[l->len++] = e;
}

void group_push(GroupList *l, Group g)
{
    if (l->len == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 32;
        l->items = xrealloc(l->items, l->cap * sizeof *l->items);
    }
    l->items[l->len++] = g;
}

void entries_free(EntryList *l)
{
    for (size_t i = 0; i < l->len; i++)
        free(l->items[i].path);
    free(l->items);
    l->items = NULL;
    l->len = l->cap = 0;
}

void groups_free(GroupList *l)
{
    for (size_t i = 0; i < l->len; i++)
        free(l->items[i].members);
    free(l->items);
    l->items = NULL;
    l->len = l->cap = 0;
}
