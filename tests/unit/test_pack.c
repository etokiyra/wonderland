#include "content/pack.h"
#include "narrative/persona.h"
#include "test.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/*
 * Pack loader: the rules that matter for honesty (canonical vs quotation vs
 * original), here-documents, duplicates, and atomic rejection.
 */

static char wl_dir[1024];

static int wl_make_dir(void) {
    char template[] = "/tmp/wl-pack-XXXXXX";

    if (mkdtemp(template) == NULL) {
        return 0;
    }
    (void)snprintf(wl_dir, sizeof wl_dir, "%s", template);
    return 1;
}

static char *wl_write_pack(const char *name, const char *content) {
    static char path[1200];
    FILE *file;

    (void)snprintf(path, sizeof path, "%s/%s", wl_dir, name);
    file = fopen(path, "w");
    if (file == NULL) {
        return NULL;
    }
    (void)fputs(content, file);
    (void)fclose(file);
    return path;
}

static void wl_cleanup(const char *first, const char *second) {
    char path[1200];

    if (first != NULL) {
        (void)snprintf(path, sizeof path, "%s/%s", wl_dir, first);
        (void)unlink(path);
    }
    if (second != NULL) {
        (void)snprintf(path, sizeof path, "%s/%s", wl_dir, second);
        (void)unlink(path);
    }
    (void)rmdir(wl_dir);
}

static int wl_saved_stderr = -1;

static void wl_silence(void) {
    int devnull;

    fflush(stderr);
    wl_saved_stderr = dup(STDERR_FILENO);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        (void)dup2(devnull, STDERR_FILENO);
        (void)close(devnull);
    }
}

static void wl_unsilence(void) {
    fflush(stderr);
    if (wl_saved_stderr >= 0) {
        (void)dup2(wl_saved_stderr, STDERR_FILENO);
        (void)close(wl_saved_stderr);
        wl_saved_stderr = -1;
    }
}

static const char *wl_valid_pack =
    "[pack]\n"
    "id = test.one\n"
    "format = 1\n"
    "name = Test\n"
    "content_license = unspecified\n"
    "\n"
    "[persona t]\n"
    "display_name = T\n"
    "class = main\n"
    "frequency = 0.5\n"
    "speaks_on = success, fail\n"
    "\n"
    "[line t.ok]\n"
    "event = success\n"
    "status = original\n"
    "category = original\n"
    "author = Test\n"
    "text = hello\n";

WL_TEST(loads_a_valid_pack) {
    struct wl_packset *packs;
    const struct wl_persona *persona;
    const char *scan[1];
    const char *path;

    WL_ASSERT(wl_make_dir());
    path = wl_write_pack("one.pack", wl_valid_pack);
    WL_ASSERT(path != NULL);
    scan[0] = wl_dir;
    packs = wl_packset_load(scan, 1);
    WL_ASSERT(packs != NULL);
    if (packs != NULL) {
        WL_ASSERT_INT_EQ(packs->count, 1);
        persona = wl_packset_find(packs, "t");
        WL_ASSERT(persona != NULL);
        if (persona != NULL) {
            WL_ASSERT_INT_EQ(persona->line_count, 1);
            WL_ASSERT_STR_EQ(persona->lines[0].text, "hello");
            WL_ASSERT_INT_EQ(persona->lines[0].status,
                             WL_CONTENT_STATUS_ORIGINAL);
        }
        wl_packset_free(packs);
    }
    wl_cleanup("one.pack", NULL);
}

static int wl_load_one(const char *content) {
    struct wl_packset *packs;
    const char *scan[1];

    if (!wl_make_dir()) {
        return -1;
    }
    if (wl_write_pack("bad.pack", content) == NULL) {
        return -1;
    }
    scan[0] = wl_dir;
    wl_silence();
    packs = wl_packset_load(scan, 1);
    {
        int count = (packs != NULL) ? (int)packs->count : -1;

        wl_unsilence();
        wl_packset_free(packs);
        wl_cleanup("bad.pack", NULL);
        return count;
    }
}

WL_TEST(canonical_without_verification_is_rejected) {
    const char *pack =
        "[pack]\nid = test.bad\nformat = 1\nname = Bad\n"
        "content_license = unspecified\n\n"
        "[persona t]\ndisplay_name = T\nclass = main\n"
        "speaks_on = success\n\n"
        "[line t.bad]\nevent = success\nstatus = original\n"
        "category = canonical\nauthor = Somebody\n"
        "work = A Work\nsource = p. 1\ntext = stolen\n";

    WL_ASSERT_INT_EQ(wl_load_one(pack), 0);
}

WL_TEST(quotation_without_translation_is_rejected) {
    const char *pack =
        "[pack]\nid = test.bad\nformat = 1\nname = Bad\n"
        "content_license = unspecified\n\n"
        "[persona t]\ndisplay_name = T\nclass = main\n"
        "speaks_on = success\n\n"
        "[line t.bad]\nevent = success\nstatus = verified\n"
        "category = quotation\nauthor = Somebody\nwork = A Work\n"
        "source = p. 1\nverified_by = Nobody\nverified_on = 2026-01-01\n"
        "text = quoted\n";

    WL_ASSERT_INT_EQ(wl_load_one(pack), 0);
}

WL_TEST(original_with_provenance_is_rejected) {
    const char *pack =
        "[pack]\nid = test.bad\nformat = 1\nname = Bad\n"
        "content_license = unspecified\n\n"
        "[persona t]\ndisplay_name = T\nclass = main\n"
        "speaks_on = success\n\n"
        "[line t.bad]\nevent = success\nstatus = original\n"
        "category = original\nauthor = Test\nwork = Fake\n"
        "text = pretending\n";

    WL_ASSERT_INT_EQ(wl_load_one(pack), 0);
}

WL_TEST(unknown_format_and_section_are_rejected) {
    const char *bad_format =
        "[pack]\nid = test.bad\nformat = 2\nname = Bad\n"
        "content_license = unspecified\n";
    const char *bad_section =
        "[pack]\nid = test.bad\nformat = 1\nname = Bad\n"
        "content_license = unspecified\n\n[persone t]\ndisplay_name = T\n";

    WL_ASSERT_INT_EQ(wl_load_one(bad_format), 0);
    WL_ASSERT_INT_EQ(wl_load_one(bad_section), 0);
}

WL_TEST(here_documents_and_placeholders) {
    struct wl_packset *packs;
    const char *scan[1];
    const char *path;
    const char *pack =
        "[pack]\nid = test.here\nformat = 1\nname = Here\n"
        "content_license = unspecified\n\n"
        "[persona t]\ndisplay_name = T\nclass = main\n"
        "speaks_on = success\n\n"
        "[line t.here]\nevent = success\nstatus = original\n"
        "category = original\nauthor = Test\n"
        "text <<END\nline one\nline two with #{cwd_base}\nEND\n";

    WL_ASSERT(wl_make_dir());
    path = wl_write_pack("here.pack", pack);
    WL_ASSERT(path != NULL);
    scan[0] = wl_dir;
    packs = wl_packset_load(scan, 1);
    WL_ASSERT(packs != NULL);
    if (packs != NULL) {
        WL_ASSERT_INT_EQ(packs->count, 1);
        if (packs->count > 0) {
            WL_ASSERT_STR_EQ(packs->personas[0].lines[0].text,
                             "line one\nline two with #{cwd_base}");
        }
        wl_packset_free(packs);
    }
    wl_cleanup("here.pack", NULL);

    {
        const char *bad =
            "[pack]\nid = test.bad\nformat = 1\nname = Bad\n"
            "content_license = unspecified\n\n"
            "[persona t]\ndisplay_name = T\nclass = main\n"
            "speaks_on = success\n\n"
            "[line t.bad]\nevent = success\nstatus = original\n"
            "category = original\nauthor = Test\n"
            "text = bad #{nope}\n";

        WL_ASSERT_INT_EQ(wl_load_one(bad), 0);
    }
}

WL_TEST(duplicate_line_and_persona_ids) {
    const char *two_lines =
        "[pack]\nid = test.dup\nformat = 1\nname = Dup\n"
        "content_license = unspecified\n\n"
        "[persona t]\ndisplay_name = T\nclass = main\n"
        "speaks_on = success\n\n"
        "[line t.same]\nevent = success\nstatus = original\n"
        "category = original\nauthor = Test\ntext = one\n"
        "[line t.same]\nevent = success\nstatus = original\n"
        "category = original\nauthor = Test\ntext = two\n";

    WL_ASSERT_INT_EQ(wl_load_one(two_lines), 0);
}

WL_TEST(directory_order_and_duplicate_personas) {
    struct wl_packset *packs;
    const char *scan[1];
    char second[1024];

    WL_ASSERT(wl_make_dir());
    WL_ASSERT(wl_write_pack("a.pack", wl_valid_pack) != NULL);
    (void)snprintf(second, sizeof second,
                   "[pack]\nid = test.two\nformat = 1\nname = Two\n"
                   "content_license = unspecified\n\n"
                   "[persona u]\ndisplay_name = U\nclass = major\n"
                   "speaks_on = success\n\n"
                   "[line u.ok]\nevent = success\nstatus = original\n"
                   "category = original\nauthor = Test\ntext = second\n");
    WL_ASSERT(wl_write_pack("b.pack", second) != NULL);

    scan[0] = wl_dir;
    packs = wl_packset_load(scan, 1);
    WL_ASSERT(packs != NULL);
    if (packs != NULL) {
        WL_ASSERT_INT_EQ(packs->count, 2);
        if (packs->count == 2) {
            WL_ASSERT_STR_EQ(packs->personas[0].id, "t");
            WL_ASSERT_STR_EQ(packs->personas[1].id, "u");
        }
        wl_packset_free(packs);
    }
    wl_cleanup("a.pack", "b.pack");
}

WL_TEST(loader_survives_random_bytes) {
    static const char alphabet[] =
        "[]=<># \n abcdefgh0123456789_-.{}$%";
    uint64_t rng = 0x123456789abcdefULL;
    char path[1024];

    WL_ASSERT(wl_make_dir());
    (void)snprintf(path, sizeof path, "%s/f.pack", wl_dir);
    wl_silence();
    for (unsigned iteration = 0; iteration < 400; iteration++) {
        FILE *file = fopen(path, "w");
        size_t length = (size_t)(rng % 220);
        struct wl_packset *packs;
        const char *scan[1];

        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        if (file == NULL) {
            break;
        }
        for (size_t index = 0; index < length; index++) {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            (void)fputc(alphabet[rng % (sizeof alphabet - 1)], file);
        }
        (void)fclose(file);
        scan[0] = wl_dir;
        packs = wl_packset_load(scan, 1);
        wl_packset_free(packs);
    }
    wl_unsilence();

    {
        FILE *file = fopen(path, "w");

        if (file != NULL) {
            (void)fclose(file);
        }
    }
    (void)unlink(path);
    (void)rmdir(wl_dir);
    WL_ASSERT(1);
}

WL_TEST(missing_paths_are_silent) {
    struct wl_packset *packs;
    const char *scan[1] = {"/tmp/wl-pack-does-not-exist"};

    packs = wl_packset_load(scan, 1);
    WL_ASSERT(packs != NULL);
    if (packs != NULL) {
        WL_ASSERT_INT_EQ(packs->count, 0);
        wl_packset_free(packs);
    }
}

void wl_suite_pack(void) {
    wl_test_register("pack: valid pack", loads_a_valid_pack);
    wl_test_register("pack: canonical needs verification",
                     canonical_without_verification_is_rejected);
    wl_test_register("pack: quotation needs translation",
                     quotation_without_translation_is_rejected);
    wl_test_register("pack: original cannot carry provenance",
                     original_with_provenance_is_rejected);
    wl_test_register("pack: bad format/section",
                     unknown_format_and_section_are_rejected);
    wl_test_register("pack: here-document and placeholders",
                     here_documents_and_placeholders);
    wl_test_register("pack: duplicate ids", duplicate_line_and_persona_ids);
    wl_test_register("pack: directory order", directory_order_and_duplicate_personas);
    wl_test_register("pack: fuzz", loader_survives_random_bytes);
    wl_test_register("pack: missing path", missing_paths_are_silent);
}
