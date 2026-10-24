#include "dubli.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The tree may change between the scan and the action; never touch a file we did not compare. */
static int unchanged(const Entry *x)
{
    struct stat sb;

    if (lstat(x->path, &sb) != 0)
        return 0;
    return S_ISREG(sb.st_mode) && sb.st_size == x->size && sb.st_dev == x->dev
        && sb.st_ino == x->ino && sb.st_mtime == x->mtime;
}

/* Link to a temporary name first, then rename over the duplicate: the path is never missing. */
static int relink(const char *keep, const char *path)
{
    size_t n = strlen(path) + 32;
    char *tmp = xmalloc(n);
    int rc = -1;

    snprintf(tmp, n, "%s.dubli-%ld", path, (long)getpid());
    if (link(keep, tmp) == 0) {
        if (rename(tmp, path) == 0) {
            rc = 0;
        } else {
            int saved = errno;
            unlink(tmp);
            errno = saved;
        }
    }
    free(tmp);
    return rc;
}

static void act_on(const Entry *keep, const Entry *x, Action act, int dry_run, FILE *out, ActionStats *st)
{
    int rc;

    if (act == ACT_LINK && x->dev != keep->dev) {
        fprintf(stderr, "dubli: %s: другая файловая система, ссылку не создать\n", x->path);
        st->skipped++;
        return;
    }
    if (!unchanged(x)) {
        fprintf(stderr, "dubli: %s: файл изменился после проверки, пропущен\n", x->path);
        st->skipped++;
        return;
    }
    if (dry_run) {
        fprintf(out, "[проба] %s %s\n", act == ACT_DELETE ? "удалить" : "заменить ссылкой", x->path);
        st->done++;
        st->freed += x->size;
        return;
    }

    rc = act == ACT_DELETE ? unlink(x->path) : relink(keep->path, x->path);
    if (rc != 0) {
        warn_path(x->path, act == ACT_DELETE ? "не удалось удалить" : "не удалось создать ссылку");
        st->failed++;
        return;
    }
    if (act == ACT_DELETE)
        fprintf(out, "удалён: %s\n", x->path);
    else
        fprintf(out, "связан: %s → %s\n", x->path, keep->path);
    st->done++;
    st->freed += x->size;
}

void apply_action(const Entry *e, const GroupList *gl, Action act, int dry_run, FILE *out, ActionStats *st)
{
    for (size_t i = 0; i < gl->len; i++) {
        const Group *g = &gl->items[i];
        const Entry *keep = &e[g->members[0]];

        if (!unchanged(keep)) {
            fprintf(stderr, "dubli: %s: оригинал изменился, группа пропущена\n", keep->path);
            st->skipped += g->len - 1;
            continue;
        }
        for (size_t m = 1; m < g->len; m++)
            act_on(keep, &e[g->members[m]], act, dry_run, out, st);
    }
}
