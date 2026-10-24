#include "dubli.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define CHUNK (64 * 1024)

uint64_t fnv1a(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = data;

    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* Reads until `want` bytes or end of file, so short reads from pipes and NFS do not end a comparison early. */
static ssize_t read_full(int fd, unsigned char *buf, size_t want)
{
    size_t got = 0;

    while (got < want) {
        ssize_t r = read(fd, buf + got, want - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

/* Hashes the first `limit` bytes of a file, or all of it when limit is negative. */
int hash_file(const char *path, off_t limit, uint64_t *out)
{
    static unsigned char buf[CHUNK];
    uint64_t h = FNV_OFFSET;
    off_t left = limit;
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return -1;
    for (;;) {
        size_t want = CHUNK;
        ssize_t r;

        if (limit >= 0) {
            if (left == 0)
                break;
            if ((off_t)want > left)
                want = (size_t)left;
        }
        r = read_full(fd, buf, want);
        if (r < 0) {
            int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
        h = fnv1a(h, buf, (size_t)r);
        if (limit >= 0)
            left -= r;
        if ((size_t)r < want)
            break;
    }
    close(fd);
    *out = h;
    return 0;
}

/* 1 when contents are identical, 0 when they differ, -1 on read error. */
int files_equal(const char *a, const char *b)
{
    static unsigned char ba[CHUNK], bb[CHUNK];
    int result = 1;
    int fa, fb;

    fa = open(a, O_RDONLY);
    if (fa < 0)
        return -1;
    fb = open(b, O_RDONLY);
    if (fb < 0) {
        close(fa);
        return -1;
    }
    for (;;) {
        ssize_t ra = read_full(fa, ba, CHUNK);
        ssize_t rb = read_full(fb, bb, CHUNK);

        if (ra < 0 || rb < 0) {
            result = -1;
            break;
        }
        if (ra != rb || memcmp(ba, bb, (size_t)ra) != 0) {
            result = 0;
            break;
        }
        if ((size_t)ra < CHUNK)
            break;
    }
    close(fa);
    close(fb);
    return result;
}
