#include "dubli.h"

static off_t wasted(const Group *g)
{
    return g->size * (off_t)(g->len - 1);
}

void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n", f); break;
        case '\t': fputs("\\t", f); break;
        default:
            if (*p < 0x20)
                fprintf(f, "\\u%04x", *p);
            else
                fputc(*p, f);
        }
    }
    fputc('"', f);
}

void report_text(FILE *f, const Entry *e, const GroupList *gl)
{
    char size[32], waste[32];

    for (size_t i = 0; i < gl->len; i++) {
        const Group *g = &gl->items[i];

        format_size(g->size, size, sizeof size);
        format_size(wasted(g), waste, sizeof waste);
        fprintf(f, "%s × %zu, лишнего %s\n", size, g->len, waste);
        for (size_t m = 0; m < g->len; m++)
            fprintf(f, "  %c %s\n", m == 0 ? '=' : '-', e[g->members[m]].path);
        fputc('\n', f);
    }
}

void report_json(FILE *f, const Entry *e, const GroupList *gl, const ScanStats *ss)
{
    size_t dups = 0;
    off_t total = 0;

    fprintf(f, "{\n  \"version\": \"%s\",\n", DUBLI_VERSION);
    fprintf(f, "  \"scanned\": {\"files\": %zu, \"dirs\": %zu, \"bytes\": %lld, \"hardlinks\": %zu, \"errors\": %zu},\n",
            ss->files, ss->dirs, (long long)ss->bytes, ss->hardlinks, ss->errors);
    fputs("  \"groups\": [", f);
    for (size_t i = 0; i < gl->len; i++) {
        const Group *g = &gl->items[i];

        dups += g->len - 1;
        total += wasted(g);
        fprintf(f, "%s\n    {\"size\": %lld, \"count\": %zu, \"wasted\": %lld, \"keep\": ",
                i ? "," : "", (long long)g->size, g->len, (long long)wasted(g));
        json_string(f, e[g->members[0]].path);
        fputs(", \"duplicates\": [", f);
        for (size_t m = 1; m < g->len; m++) {
            if (m > 1)
                fputs(", ", f);
            json_string(f, e[g->members[m]].path);
        }
        fputs("]}", f);
    }
    fprintf(f, "%s],\n", gl->len ? "\n  " : "");
    fprintf(f, "  \"total\": {\"groups\": %zu, \"duplicates\": %zu, \"wasted\": %lld}\n}\n",
            gl->len, dups, (long long)total);
}

static void csv_field(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"')
            fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

void report_csv(FILE *f, const Entry *e, const GroupList *gl)
{
    fputs("group,size,role,path\n", f);
    for (size_t i = 0; i < gl->len; i++) {
        const Group *g = &gl->items[i];
        for (size_t m = 0; m < g->len; m++) {
            fprintf(f, "%zu,%lld,%s,", i + 1, (long long)g->size, m == 0 ? "keep" : "duplicate");
            csv_field(f, e[g->members[m]].path);
            fputc('\n', f);
        }
    }
}

void report_summary(FILE *f, const GroupList *gl, const ScanStats *ss, const FindStats *fs)
{
    char buf[32];
    size_t dups = 0;
    off_t total = 0;

    for (size_t i = 0; i < gl->len; i++) {
        dups += gl->items[i].len - 1;
        total += wasted(&gl->items[i]);
    }

    format_size(ss->bytes, buf, sizeof buf);
    fprintf(f, "Просмотрено: %zu %s в %zu %s, %s\n",
            ss->files, plural_ru(ss->files, "файл", "файла", "файлов"),
            ss->dirs, plural_ru(ss->dirs, "папке", "папках", "папках"), buf);
    if (ss->hardlinks)
        fprintf(f, "Жёстких ссылок пропущено: %zu\n", ss->hardlinks);
    fprintf(f, "Отбор: по размеру %zu → по началу %zu → по содержимому %zu\n",
            fs->by_size, fs->by_head, fs->by_full);
    if (fs->collisions)
        fprintf(f, "Совпадений хэша с разным содержимым: %zu\n", fs->collisions);
    if (ss->errors + fs->errors)
        fprintf(f, "Не удалось прочитать: %zu\n", ss->errors + fs->errors);

    if (gl->len == 0) {
        fputs("Дубликатов не найдено.\n", f);
        return;
    }
    format_size(total, buf, sizeof buf);
    fprintf(f, "Групп дубликатов: %zu, лишних файлов: %zu, можно освободить: %s\n",
            gl->len, dups, buf);
}
