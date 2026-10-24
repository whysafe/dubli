#include "dubli.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void consider_file(const char *path, const struct stat *sb, int root,
                          const ScanOptions *o, EntryList *out, ScanStats *st)
{
    Entry e;

    st->files++;
    st->bytes += sb->st_size;
    if (sb->st_size < o->min_size)
        return;

    memset(&e, 0, sizeof e);
    e.path = xstrdup(path);
    e.size = sb->st_size;
    e.dev = sb->st_dev;
    e.ino = sb->st_ino;
    e.mtime = sb->st_mtime;
    e.root = root;
    entry_push(out, e);
}

/* Symlinks and special files are skipped: following links could escape the tree or loop. */
static void walk_dir(const char *dir, int root, const ScanOptions *o, EntryList *out, ScanStats *st)
{
    DIR *d = opendir(dir);
    struct dirent *de;

    if (!d) {
        warn_path(dir, "не удалось открыть папку");
        st->errors++;
        return;
    }
    st->dirs++;

    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        struct stat sb;
        char *child;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (!o->hidden && name[0] == '.')
            continue;

        child = path_join(dir, name);
        if (is_excluded(name, child, o->excludes, o->n_excludes)) {
            free(child);
            continue;
        }
        if (lstat(child, &sb) != 0) {
            warn_path(child, "нет доступа");
            st->errors++;
        } else if (S_ISDIR(sb.st_mode)) {
            walk_dir(child, root, o, out, st);
        } else if (S_ISREG(sb.st_mode)) {
            consider_file(child, &sb, root, o, out, st);
        }
        free(child);
    }
    closedir(d);
}

/* Roots are resolved with stat, so a symlink given on the command line is followed. */
int scan_root(const char *path, int root, const ScanOptions *o, EntryList *out, ScanStats *st)
{
    struct stat sb;

    if (stat(path, &sb) != 0) {
        warn_path(path, "не найдено");
        st->errors++;
        return -1;
    }
    if (S_ISDIR(sb.st_mode)) {
        walk_dir(path, root, o, out, st);
        return 0;
    }
    if (S_ISREG(sb.st_mode)) {
        consider_file(path, &sb, root, o, out, st);
        return 0;
    }
    fprintf(stderr, "dubli: %s: не файл и не папка, пропущено\n", path);
    st->errors++;
    return -1;
}

static int cmp_inode(const void *a, const void *b)
{
    const Entry *x = a, *y = b;

    if (x->dev != y->dev)
        return x->dev < y->dev ? -1 : 1;
    if (x->ino != y->ino)
        return x->ino < y->ino ? -1 : 1;
    if (x->root != y->root)
        return x->root < y->root ? -1 : 1;
    return strcmp(x->path, y->path);
}

/*
 * Hard links and paths reached twice point at one inode: removing one of them frees nothing,
 * so only the first path of every inode takes part in the search.
 */
size_t drop_hardlinks(EntryList *l)
{
    size_t kept = 0, dropped = 0;

    if (l->len == 0)
        return 0;
    qsort(l->items, l->len, sizeof *l->items, cmp_inode);
    for (size_t i = 0; i < l->len; i++) {
        Entry *prev = kept ? &l->items[kept - 1] : NULL;
        if (prev && prev->dev == l->items[i].dev && prev->ino == l->items[i].ino) {
            free(l->items[i].path);
            dropped++;
            continue;
        }
        l->items[kept++] = l->items[i];
    }
    l->len = kept;
    return dropped;
}
