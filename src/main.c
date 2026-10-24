#include "dubli.h"

#include <getopt.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum { OPT_FAST = 256 };

static void usage(FILE *f)
{
    fputs(
        "Использование: dubli [опции] ПУТЬ...\n"
        "Ищет файлы с одинаковым содержимым: размер → хэш начала → хэш файла → побайтовое сравнение.\n"
        "\n"
        "Отбор файлов:\n"
        "  -m, --min-size РАЗМЕР   не меньше РАЗМЕРА (1, 10K, 5M, 1G), по умолчанию 1 байт\n"
        "  -e, --empty             учитывать пустые файлы\n"
        "  -x, --exclude ШАБЛОН    пропускать файлы и папки по маске (можно повторять)\n"
        "  -a, --hidden            заходить в скрытые файлы и папки\n"
        "\n"
        "Вывод:\n"
        "  -f, --format ФОРМАТ     text (по умолчанию), json или csv\n"
        "  -s, --summary           только итог, без списка групп\n"
        "  -k, --keep ПРАВИЛО      какой файл в группе оставить: first (первый путь\n"
        "                          из аргументов), oldest, newest, shortest\n"
        "\n"
        "Действия:\n"
        "  -d, --delete            удалить дубликаты, оставив по одному файлу\n"
        "  -l, --link              заменить дубликаты жёсткими ссылками на оставленный файл\n"
        "  -n, --dry-run           показать, что будет сделано, ничего не меняя\n"
        "      --fast              не сравнивать побайтово, доверять 64-битному хэшу\n"
        "\n"
        "  -h, --help              эта справка\n"
        "  -V, --version           версия\n",
        f);
}

static void die_usage(const char *fmt, ...)
{
    va_list ap;

    fputs("dubli: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\nПодробнее: dubli --help\n", stderr);
    exit(2);
}

static Keep parse_keep(const char *s)
{
    if (strcmp(s, "first") == 0) return KEEP_FIRST;
    if (strcmp(s, "oldest") == 0) return KEEP_OLDEST;
    if (strcmp(s, "newest") == 0) return KEEP_NEWEST;
    if (strcmp(s, "shortest") == 0) return KEEP_SHORTEST;
    die_usage("неизвестное правило --keep: %s", s);
    return KEEP_FIRST;
}

static Format parse_format(const char *s)
{
    if (strcmp(s, "text") == 0) return FMT_TEXT;
    if (strcmp(s, "json") == 0) return FMT_JSON;
    if (strcmp(s, "csv") == 0) return FMT_CSV;
    die_usage("неизвестный формат: %s", s);
    return FMT_TEXT;
}

static void print_action_summary(FILE *f, Action act, int dry_run, const ActionStats *as)
{
    char buf[32];

    format_size(as->freed, buf, sizeof buf);
    fprintf(f, "%s%s: %zu, освобождено: %s",
            dry_run ? "[проба] " : "",
            act == ACT_DELETE ? "Удалено" : "Заменено ссылками",
            as->done, buf);
    if (as->skipped)
        fprintf(f, ", пропущено: %zu", as->skipped);
    if (as->failed)
        fprintf(f, ", ошибок: %zu", as->failed);
    fputc('\n', f);
}

int main(int argc, char **argv)
{
    static const struct option longopts[] = {
        { "min-size", required_argument, NULL, 'm' },
        { "empty",    no_argument,       NULL, 'e' },
        { "exclude",  required_argument, NULL, 'x' },
        { "hidden",   no_argument,       NULL, 'a' },
        { "format",   required_argument, NULL, 'f' },
        { "summary",  no_argument,       NULL, 's' },
        { "keep",     required_argument, NULL, 'k' },
        { "delete",   no_argument,       NULL, 'd' },
        { "link",     no_argument,       NULL, 'l' },
        { "dry-run",  no_argument,       NULL, 'n' },
        { "fast",     no_argument,       NULL, OPT_FAST },
        { "help",     no_argument,       NULL, 'h' },
        { "version",  no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 }
    };
    ScanOptions so = { 1, 0, NULL, 0 };
    ScanStats ss = { 0 };
    FindStats fs = { 0 };
    ActionStats as = { 0 };
    EntryList entries = { 0 };
    GroupList groups = { 0 };
    Format fmt = FMT_TEXT;
    Keep keep = KEEP_FIRST;
    Action act = ACT_NONE;
    int dry_run = 0, verify = 1, summary_only = 0, roots_ok = 0, c;
    FILE *log;

    so.excludes = xmalloc((size_t)argc * sizeof *so.excludes);

    while ((c = getopt_long(argc, argv, "m:ex:af:sk:dlnhV", longopts, NULL)) != -1) {
        switch (c) {
        case 'm':
            if (parse_size(optarg, &so.min_size) != 0)
                die_usage("неверный размер: %s", optarg);
            break;
        case 'e': so.min_size = 0; break;
        case 'x': so.excludes[so.n_excludes++] = optarg; break;
        case 'a': so.hidden = 1; break;
        case 'f': fmt = parse_format(optarg); break;
        case 's': summary_only = 1; break;
        case 'k': keep = parse_keep(optarg); break;
        case 'd':
            if (act == ACT_LINK)
                die_usage("--delete и --link нельзя использовать вместе");
            act = ACT_DELETE;
            break;
        case 'l':
            if (act == ACT_DELETE)
                die_usage("--delete и --link нельзя использовать вместе");
            act = ACT_LINK;
            break;
        case 'n': dry_run = 1; break;
        case OPT_FAST: verify = 0; break;
        case 'h':
            usage(stdout);
            free(so.excludes);
            return 0;
        case 'V':
            printf("dubli %s\n", DUBLI_VERSION);
            free(so.excludes);
            return 0;
        default:
            fputs("Подробнее: dubli --help\n", stderr);
            free(so.excludes);
            return 2;
        }
    }
    if (optind >= argc) {
        usage(stderr);
        free(so.excludes);
        return 2;
    }
    if (dry_run && act == ACT_NONE)
        die_usage("--dry-run имеет смысл только с --delete или --link");

    for (int i = optind; i < argc; i++)
        roots_ok += scan_root(argv[i], i - optind, &so, &entries, &ss) == 0;
    if (!roots_ok) {
        free(so.excludes);
        return 1;
    }

    ss.hardlinks = drop_hardlinks(&entries);
    find_duplicates(&entries, verify, &groups, &fs);
    finalize_groups(entries.items, &groups, keep);

    if (fmt == FMT_JSON)
        report_json(stdout, entries.items, &groups, &ss);
    else if (fmt == FMT_CSV)
        report_csv(stdout, entries.items, &groups);
    else if (!summary_only)
        report_text(stdout, entries.items, &groups);

    /* Machine-readable output stays clean: in json/csv mode everything else goes to stderr. */
    log = fmt == FMT_TEXT ? stdout : stderr;
    if (act != ACT_NONE)
        apply_action(entries.items, &groups, act, dry_run, log, &as);
    if (fmt == FMT_TEXT)
        report_summary(stdout, &groups, &ss, &fs);
    if (act != ACT_NONE)
        print_action_summary(log, act, dry_run, &as);

    groups_free(&groups);
    entries_free(&entries);
    free(so.excludes);
    return as.failed ? 1 : 0;
}
