#include "dubli.h"

#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond) do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            fprintf(stderr, "%s:%d: проверка не прошла: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

static void test_fnv1a(void)
{
    CHECK(fnv1a(FNV_OFFSET, "", 0) == 0xcbf29ce484222325ULL);
    CHECK(fnv1a(FNV_OFFSET, "a", 1) == 0xaf63dc4c8601ec8cULL);
    CHECK(fnv1a(FNV_OFFSET, "foobar", 6) == 0x85944171f73967e8ULL);
    /* Hashing in pieces must equal hashing at once: hash_file relies on it. */
    CHECK(fnv1a(fnv1a(FNV_OFFSET, "foo", 3), "bar", 3) == 0x85944171f73967e8ULL);
}

static void test_parse_size(void)
{
    off_t v = -1;

    CHECK(parse_size("0", &v) == 0 && v == 0);
    CHECK(parse_size("123", &v) == 0 && v == 123);
    CHECK(parse_size("10K", &v) == 0 && v == 10240);
    CHECK(parse_size("10k", &v) == 0 && v == 10240);
    CHECK(parse_size("5MiB", &v) == 0 && v == 5 * 1024 * 1024);
    CHECK(parse_size("2G", &v) == 0 && v == 2LL * 1024 * 1024 * 1024);
    CHECK(parse_size("1t", &v) == 0 && v == 1LL << 40);
    CHECK(parse_size("64b", &v) == 0 && v == 64);
    CHECK(parse_size("", &v) != 0);
    CHECK(parse_size("-1", &v) != 0);
    CHECK(parse_size("K", &v) != 0);
    CHECK(parse_size("10X", &v) != 0);
    CHECK(parse_size("10KBx", &v) != 0);
    CHECK(parse_size("99999999999999999999", &v) != 0);
    CHECK(parse_size("99999999T", &v) != 0);
}

static void test_format_size(void)
{
    char buf[32];

    format_size(0, buf, sizeof buf);
    CHECK(strcmp(buf, "0 Б") == 0);
    format_size(1023, buf, sizeof buf);
    CHECK(strcmp(buf, "1023 Б") == 0);
    format_size(1024, buf, sizeof buf);
    CHECK(strcmp(buf, "1.0 КиБ") == 0);
    format_size(1536, buf, sizeof buf);
    CHECK(strcmp(buf, "1.5 КиБ") == 0);
    format_size(10000, buf, sizeof buf);
    CHECK(strcmp(buf, "9.8 КиБ") == 0);
    format_size(3LL * 1024 * 1024 * 1024, buf, sizeof buf);
    CHECK(strcmp(buf, "3.0 ГиБ") == 0);
}

static void test_plural(void)
{
    CHECK(strcmp(plural_ru(1, "файл", "файла", "файлов"), "файл") == 0);
    CHECK(strcmp(plural_ru(3, "файл", "файла", "файлов"), "файла") == 0);
    CHECK(strcmp(plural_ru(5, "файл", "файла", "файлов"), "файлов") == 0);
    CHECK(strcmp(plural_ru(11, "файл", "файла", "файлов"), "файлов") == 0);
    CHECK(strcmp(plural_ru(12, "файл", "файла", "файлов"), "файлов") == 0);
    CHECK(strcmp(plural_ru(21, "файл", "файла", "файлов"), "файл") == 0);
    CHECK(strcmp(plural_ru(104, "файл", "файла", "файлов"), "файла") == 0);
    CHECK(strcmp(plural_ru(111, "файл", "файла", "файлов"), "файлов") == 0);
}

static void test_exclude_and_join(void)
{
    const char *patterns[] = { "*.tmp", "node_modules", "*/cache/*" };
    char *p;

    CHECK(is_excluded("a.tmp", "x/a.tmp", patterns, 3));
    CHECK(is_excluded("node_modules", "proj/node_modules", patterns, 3));
    CHECK(is_excluded("img.png", "home/cache/img.png", patterns, 3));
    CHECK(!is_excluded("a.txt", "x/a.txt", patterns, 3));
    CHECK(!is_excluded("cache", "home/cache", patterns, 3));

    p = path_join("dir", "file");
    CHECK(strcmp(p, "dir/file") == 0);
    free(p);
    p = path_join("dir/", "file");
    CHECK(strcmp(p, "dir/file") == 0);
    free(p);
    p = path_join("/", "etc");
    CHECK(strcmp(p, "/etc") == 0);
    free(p);
}

static Entry fake(const char *path, int root, time_t mtime)
{
    Entry e;

    memset(&e, 0, sizeof e);
    e.path = (char *)path;
    e.root = root;
    e.mtime = mtime;
    e.size = 100;
    return e;
}

static void test_keep_order(void)
{
    Entry e[4];
    size_t members[4] = { 0, 1, 2, 3 };
    Group g = { members, 4, 100, NULL };

    e[0] = fake("backup/2024/photo.jpg", 1, 300);
    e[1] = fake("photos/photo.jpg", 0, 200);
    e[2] = fake("photos/a/photo.jpg", 0, 400);
    e[3] = fake("p.jpg", 2, 100);

    order_group(e, &g, KEEP_FIRST);
    CHECK(strcmp(g.key, "photos/a/photo.jpg") == 0);
    CHECK(members[1] == 1 && members[2] == 0 && members[3] == 3);

    order_group(e, &g, KEEP_OLDEST);
    CHECK(strcmp(g.key, "p.jpg") == 0);

    order_group(e, &g, KEEP_NEWEST);
    CHECK(strcmp(g.key, "photos/a/photo.jpg") == 0);
    CHECK(members[3] == 3);

    order_group(e, &g, KEEP_SHORTEST);
    CHECK(strcmp(g.key, "p.jpg") == 0);
    CHECK(members[1] == 1);
}

static void test_group_order(void)
{
    Entry e[5];
    size_t a[2] = { 0, 1 }, b[3] = { 2, 3, 4 };
    GroupList gl = { NULL, 0, 0 };
    Group ga = { NULL, 2, 1000, NULL }, gb = { NULL, 3, 100, NULL };

    e[0] = fake("z1", 0, 0);
    e[1] = fake("z2", 0, 0);
    e[2] = fake("a1", 0, 0);
    e[3] = fake("a2", 0, 0);
    e[4] = fake("a3", 0, 0);
    ga.members = a;
    gb.members = b;
    group_push(&gl, gb);
    group_push(&gl, ga);

    finalize_groups(e, &gl, KEEP_FIRST);
    /* 1000 wasted bytes outrank 200, whatever the names. */
    CHECK(gl.items[0].size == 1000);
    CHECK(strcmp(gl.items[1].key, "a1") == 0);
    free(gl.items);
}

static void test_json_string(void)
{
    FILE *f = tmpfile();
    char buf[128] = { 0 };
    size_t n;

    if (!f) {
        CHECK(0 && "tmpfile");
        return;
    }
    json_string(f, "a\"b\\c\nd\x01 файл");
    rewind(f);
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    CHECK(strcmp(buf, "\"a\\\"b\\\\c\\nd\\u0001 файл\"") == 0);
}

int main(void)
{
    test_fnv1a();
    test_parse_size();
    test_format_size();
    test_plural();
    test_exclude_and_join();
    test_keep_order();
    test_group_order();
    test_json_string();

    if (failures) {
        fprintf(stderr, "Провалено проверок: %d из %d\n", failures, checks);
        return 1;
    }
    printf("Модульные тесты: %d проверок пройдено\n", checks);
    return 0;
}
