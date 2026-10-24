#ifndef DUBLI_H
#define DUBLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#define DUBLI_VERSION "1.0.0"

/* Files that share size are first compared by a hash of this many leading bytes. */
#define HEAD_BYTES 4096

typedef struct {
    char *path;
    off_t size;
    dev_t dev;
    ino_t ino;
    time_t mtime;
    int root;     /* index of the command-line path the file was found under */
    uint64_t head;
    uint64_t full;
    int failed;   /* could not be read; never placed into a group */
} Entry;

typedef struct {
    Entry *items;
    size_t len, cap;
} EntryList;

/* Members are indexes into the EntryList; members[0] is the file to keep. */
typedef struct {
    size_t *members;
    size_t len;
    off_t size;
    const char *key;
} Group;

typedef struct {
    Group *items;
    size_t len, cap;
} GroupList;

typedef enum { KEEP_FIRST, KEEP_OLDEST, KEEP_NEWEST, KEEP_SHORTEST } Keep;
typedef enum { FMT_TEXT, FMT_JSON, FMT_CSV } Format;
typedef enum { ACT_NONE, ACT_DELETE, ACT_LINK } Action;

typedef struct {
    off_t min_size;
    int hidden;
    const char **excludes;
    size_t n_excludes;
} ScanOptions;

typedef struct {
    size_t files, dirs, errors, hardlinks;
    off_t bytes;
} ScanStats;

typedef struct {
    size_t by_size, by_head, by_full, collisions, errors;
} FindStats;

typedef struct {
    size_t done, skipped, failed;
    off_t freed;
} ActionStats;

/* util.c */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
void warn_path(const char *path, const char *what);
int parse_size(const char *s, off_t *out);
void format_size(off_t bytes, char *buf, size_t n);
const char *plural_ru(unsigned long long n, const char *one, const char *few, const char *many);
int is_excluded(const char *name, const char *path, const char **patterns, size_t n);
char *path_join(const char *dir, const char *name);
void entry_push(EntryList *l, Entry e);
void group_push(GroupList *l, Group g);
void entries_free(EntryList *l);
void groups_free(GroupList *l);

/* hash.c */
#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL
uint64_t fnv1a(uint64_t h, const void *data, size_t n);
int hash_file(const char *path, off_t limit, uint64_t *out);
int files_equal(const char *a, const char *b);

/* scan.c */
int scan_root(const char *path, int root, const ScanOptions *o, EntryList *out, ScanStats *st);
size_t drop_hardlinks(EntryList *l);

/* group.c */
void find_duplicates(EntryList *l, int verify, GroupList *out, FindStats *st);
int keep_cmp(const Entry *x, const Entry *y, Keep keep);
void order_group(const Entry *e, Group *g, Keep keep);
void finalize_groups(const Entry *e, GroupList *gl, Keep keep);

/* report.c */
void json_string(FILE *f, const char *s);
void report_text(FILE *f, const Entry *e, const GroupList *gl);
void report_json(FILE *f, const Entry *e, const GroupList *gl, const ScanStats *ss);
void report_csv(FILE *f, const Entry *e, const GroupList *gl);
void report_summary(FILE *f, const GroupList *gl, const ScanStats *ss, const FindStats *fs);

/* actions.c */
void apply_action(const Entry *e, const GroupList *gl, Action act, int dry_run, FILE *out, ActionStats *st);

#endif
