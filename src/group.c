#include "dubli.h"

#include <stdlib.h>
#include <string.h>

typedef int (*SameFn)(const Entry *, const Entry *);

static int same_size(const Entry *a, const Entry *b) { return a->size == b->size; }
static int same_head(const Entry *a, const Entry *b) { return !b->failed && a->head == b->head; }
static int same_full(const Entry *a, const Entry *b) { return !b->failed && a->full == b->full; }

static size_t run_end(const Entry *e, size_t i, size_t end, SameFn same)
{
    size_t j = i + 1;

    while (j < end && same(&e[i], &e[j]))
        j++;
    return j;
}

static int cmp_size_desc(const void *a, const void *b)
{
    const Entry *x = a, *y = b;

    if (x->size != y->size)
        return x->size > y->size ? -1 : 1;
    return strcmp(x->path, y->path);
}

/* Unreadable files sort last so the runs of equal hashes stay contiguous. */
static int cmp_head(const void *a, const void *b)
{
    const Entry *x = a, *y = b;

    if (x->failed != y->failed)
        return x->failed - y->failed;
    if (x->head != y->head)
        return x->head < y->head ? -1 : 1;
    return strcmp(x->path, y->path);
}

static int cmp_full(const void *a, const void *b)
{
    const Entry *x = a, *y = b;

    if (x->failed != y->failed)
        return x->failed - y->failed;
    if (x->full != y->full)
        return x->full < y->full ? -1 : 1;
    return strcmp(x->path, y->path);
}

static void mark_failed(Entry *e, FindStats *st)
{
    warn_path(e->path, "ошибка чтения");
    e->failed = 1;
    st->errors++;
}

static Group new_group(size_t capacity, off_t size)
{
    Group g;

    g.members = xmalloc(capacity * sizeof *g.members);
    g.len = 0;
    g.size = size;
    g.key = NULL;
    return g;
}

/* Hashes only narrow the candidates; a byte-by-byte pass rules out collisions entirely. */
static void split_by_bytes(Entry *e, size_t a, size_t b, GroupList *out, FindStats *st)
{
    size_t n = b - a, nc = 0;
    Group *classes = xmalloc(n * sizeof *classes);

    for (size_t k = a; k < b; k++) {
        size_t c;
        int failed = 0;

        for (c = 0; c < nc; c++) {
            int r = files_equal(e[classes[c].members[0]].path, e[k].path);
            if (r == 1)
                break;
            if (r < 0) {
                mark_failed(&e[k], st);
                failed = 1;
                break;
            }
        }
        if (failed)
            continue;
        if (c == nc)
            classes[nc++] = new_group(n, e[k].size);
        classes[c].members[classes[c].len++] = k;
    }

    if (nc > 1)
        st->collisions += nc - 1;
    for (size_t c = 0; c < nc; c++) {
        if (classes[c].len >= 2)
            group_push(out, classes[c]);
        else
            free(classes[c].members);
    }
    free(classes);
}

static void emit_all(size_t a, size_t b, off_t size, GroupList *out)
{
    Group g = new_group(b - a, size);

    for (size_t k = a; k < b; k++)
        g.members[g.len++] = k;
    group_push(out, g);
}

static void split_by_full(Entry *e, size_t a, size_t b, int verify, GroupList *out, FindStats *st)
{
    /* Small files were read completely by the head pass. */
    int whole = e[a].size <= HEAD_BYTES;

    st->by_head += b - a;
    for (size_t k = a; k < b; k++) {
        if (whole)
            e[k].full = e[k].head;
        else if (hash_file(e[k].path, -1, &e[k].full) != 0)
            mark_failed(&e[k], st);
    }
    qsort(e + a, b - a, sizeof *e, cmp_full);

    for (size_t i = a; i < b && !e[i].failed;) {
        size_t j = run_end(e, i, b, same_full);
        if (j - i >= 2) {
            st->by_full += j - i;
            if (verify)
                split_by_bytes(e, i, j, out, st);
            else
                emit_all(i, j, e[i].size, out);
        }
        i = j;
    }
}

static void split_by_head(Entry *e, size_t a, size_t b, int verify, GroupList *out, FindStats *st)
{
    st->by_size += b - a;
    for (size_t k = a; k < b; k++) {
        if (hash_file(e[k].path, HEAD_BYTES, &e[k].head) != 0)
            mark_failed(&e[k], st);
    }
    qsort(e + a, b - a, sizeof *e, cmp_head);

    for (size_t i = a; i < b && !e[i].failed;) {
        size_t j = run_end(e, i, b, same_head);
        if (j - i >= 2)
            split_by_full(e, i, j, verify, out, st);
        i = j;
    }
}

/*
 * Each stage sorts only the range handed to it, and a group is emitted after its range
 * is sorted for the last time, so the member indexes stay valid.
 */
void find_duplicates(EntryList *l, int verify, GroupList *out, FindStats *st)
{
    Entry *e = l->items;

    if (l->len == 0)
        return;
    qsort(e, l->len, sizeof *e, cmp_size_desc);
    for (size_t i = 0; i < l->len;) {
        size_t j = run_end(e, i, l->len, same_size);
        if (j - i >= 2)
            split_by_head(e, i, j, verify, out, st);
        i = j;
    }
}

int keep_cmp(const Entry *x, const Entry *y, Keep keep)
{
    int c = 0;

    switch (keep) {
    case KEEP_FIRST:
        c = (x->root > y->root) - (x->root < y->root);
        break;
    case KEEP_OLDEST:
        c = (x->mtime > y->mtime) - (x->mtime < y->mtime);
        break;
    case KEEP_NEWEST:
        c = (x->mtime < y->mtime) - (x->mtime > y->mtime);
        break;
    case KEEP_SHORTEST: {
        size_t a = strlen(x->path), b = strlen(y->path);
        c = (a > b) - (a < b);
        break;
    }
    }
    return c ? c : strcmp(x->path, y->path);
}

/* Groups are small, so an insertion sort that can see the entries beats a global qsort context. */
void order_group(const Entry *e, Group *g, Keep keep)
{
    for (size_t i = 1; i < g->len; i++) {
        size_t m = g->members[i], j = i;
        while (j > 0 && keep_cmp(&e[m], &e[g->members[j - 1]], keep) < 0) {
            g->members[j] = g->members[j - 1];
            j--;
        }
        g->members[j] = m;
    }
    g->key = e[g->members[0]].path;
}

static int cmp_group(const void *a, const void *b)
{
    const Group *x = a, *y = b;
    off_t wx = x->size * (off_t)(x->len - 1), wy = y->size * (off_t)(y->len - 1);

    if (wx != wy)
        return wx > wy ? -1 : 1;
    return strcmp(x->key, y->key);
}

void finalize_groups(const Entry *e, GroupList *gl, Keep keep)
{
    for (size_t i = 0; i < gl->len; i++)
        order_group(e, &gl->items[i], keep);
    if (gl->len > 1)
        qsort(gl->items, gl->len, sizeof *gl->items, cmp_group);
}
