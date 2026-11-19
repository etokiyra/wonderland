/*
 * realpath(3) is XSI, not POSIX base; the pack loader uses it to deduplicate
 * the same pack found through two search paths.
 */
#define _XOPEN_SOURCE 700

#include "content/pack.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define WL_PACK_MAX_BYTES (8u * 1024u * 1024u)
#define WL_PACK_MAX_LINES 20000
#define WL_PACK_MAX_SCALAR 8192
#define WL_PACK_MAX_TEXT (64u * 1024u)

enum section {
    SEC_NONE,
    SEC_PACK,
    SEC_PERSONA,
    SEC_LINE
};

struct builder {
    const char *path;
    unsigned line_no;
    bool failed;

    /* [pack] */
    char *pack_id;
    long pack_format;
    bool have_format;
    char *pack_name;
    char *pack_license;
    char *pack_description;

    /* [persona] */
    char *persona_id;
    char *display_name;
    char *class_name;
    char *voice;
    char *worldview;
    char *description;
    unsigned frequency;
    bool have_frequency;
    unsigned silence_bias;
    bool have_silence;
    bool default_persona;
    bool have_speaks_on;
    unsigned speaks_mask;

    struct wl_content_line *lines;
    size_t line_count;
    size_t line_capacity;
    unsigned event_mask;

    /* [line] */
    char *line_id;
    char *event_name;
    char *status_name;
    char *category_name;
    char *author;
    char *work;
    char *source;
    char *translation;
    char *verified_by;
    char *verified_on;
    char *text;
    size_t text_len;
    bool have_text;

    /* here-document collection */
    bool in_heredoc;
    char *heredoc_sentinel;
};

static void *wl_alloc(size_t size) {
    return calloc(1, size);
}

static char *wl_strdup_n(const char *text, size_t length) {
    char *copy = malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static bool wl_set_string(char **slot, const char *text, size_t length) {
    char *copy = wl_strdup_n(text, length);

    if (copy == NULL) {
        return false;
    }
    free(*slot);
    *slot = copy;
    return true;
}

static void wl_free_string(char **slot) {
    free(*slot);
    *slot = NULL;
}

static bool wl_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static void wl_trim(const char **start, size_t *length) {
    while (*length > 0 && wl_is_space(**start)) {
        (*start)++;
        (*length)--;
    }
    while (*length > 0 && wl_is_space((*start)[*length - 1])) {
        (*length)--;
    }
}

static bool wl_valid_id(const char *text) {
    size_t index = 0;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    if (!(isalnum((unsigned char)text[0]))) {
        return false;
    }
    for (index = 0; text[index] != '\0'; index++) {
        char c = text[index];

        if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

static int wl_event_from_name(const char *name) {
    static const char *const names[] = {
        "post_exec", "success",  "fail",   "session_start",
        "dir_change", "job_start", "job_finish", "exit",
    };

    for (unsigned index = 0; index < sizeof names / sizeof names[0]; index++) {
        if (strcmp(name, names[index]) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static bool wl_status_from_name(const char *name, enum wl_content_status *out) {
    if (strcmp(name, "original") == 0) {
        *out = WL_CONTENT_STATUS_ORIGINAL;
        return true;
    }
    if (strcmp(name, "verified") == 0) {
        *out = WL_CONTENT_STATUS_VERIFIED;
        return true;
    }
    if (strcmp(name, "unverified") == 0) {
        *out = WL_CONTENT_STATUS_UNVERIFIED;
        return true;
    }
    return false;
}

static bool wl_category_from_name(const char *name,
                                  enum wl_content_category *out) {
    if (strcmp(name, "original") == 0) {
        *out = WL_CONTENT_CATEGORY_ORIGINAL;
        return true;
    }
    if (strcmp(name, "canonical") == 0) {
        *out = WL_CONTENT_CATEGORY_CANONICAL;
        return true;
    }
    if (strcmp(name, "quotation") == 0) {
        *out = WL_CONTENT_CATEGORY_QUOTATION;
        return true;
    }
    return false;
}

static bool wl_placeholders_valid(const char *text) {
    const char *cursor = text;

    while ((cursor = strstr(cursor, "#{")) != NULL) {
        const char *close = strchr(cursor + 2, '}');
        size_t length;

        if (close == NULL) {
            return false;
        }
        length = (size_t)(close - (cursor + 2));
        if (!((length == 8 && memcmp(cursor + 2, "cwd_base", 8) == 0) ||
              (length == 3 && memcmp(cursor + 2, "cmd", 3) == 0) ||
              (length == 6 && memcmp(cursor + 2, "status", 6) == 0) ||
              (length == 5 && memcmp(cursor + 2, "cycle", 5) == 0) ||
              (length == 7 && memcmp(cursor + 2, "persona", 7) == 0))) {
            return false;
        }
        cursor = close + 1;
    }
    return true;
}

static bool wl_text_valid(const char *text, size_t length) {
    if (length == 0 || length > WL_PACK_MAX_TEXT) {
        return false;
    }
    for (size_t index = 0; index < length; index++) {
        unsigned char c = (unsigned char)text[index];

        if (c == 0 || c == 0x1b) {
            return false;
        }
    }
    return wl_placeholders_valid(text);
}

static void wl_builder_fail(struct builder *builder, unsigned line,
                            const char *message) {
    if (!builder->failed) {
        fprintf(stderr, "wonderland: pack %s: line %u: %s\n", builder->path,
                line, message);
    }
    builder->failed = true;
}

static void wl_free_line(struct wl_content_line *line) {
    wl_free_string((char **)&line->id);
    wl_free_string((char **)&line->text);
    wl_free_string((char **)&line->author);
    wl_free_string((char **)&line->work);
    wl_free_string((char **)&line->source);
    wl_free_string((char **)&line->translation);
    wl_free_string((char **)&line->verified_by);
    wl_free_string((char **)&line->verified_on);
}

static void wl_builder_free(struct builder *builder) {
    for (size_t index = 0; index < builder->line_count; index++) {
        wl_free_line(&builder->lines[index]);
    }
    free(builder->lines);
    wl_free_string(&builder->pack_id);
    wl_free_string(&builder->pack_name);
    wl_free_string(&builder->pack_license);
    wl_free_string(&builder->pack_description);
    wl_free_string(&builder->persona_id);
    wl_free_string(&builder->display_name);
    wl_free_string(&builder->class_name);
    wl_free_string(&builder->voice);
    wl_free_string(&builder->worldview);
    wl_free_string(&builder->description);
    wl_free_string(&builder->line_id);
    wl_free_string(&builder->event_name);
    wl_free_string(&builder->status_name);
    wl_free_string(&builder->category_name);
    wl_free_string(&builder->author);
    wl_free_string(&builder->work);
    wl_free_string(&builder->source);
    wl_free_string(&builder->translation);
    wl_free_string(&builder->verified_by);
    wl_free_string(&builder->verified_on);
    wl_free_string(&builder->text);
    wl_free_string(&builder->heredoc_sentinel);
}

static void wl_reset_line(struct builder *builder) {
    wl_free_string(&builder->line_id);
    wl_free_string(&builder->event_name);
    wl_free_string(&builder->status_name);
    wl_free_string(&builder->category_name);
    wl_free_string(&builder->author);
    wl_free_string(&builder->work);
    wl_free_string(&builder->source);
    wl_free_string(&builder->translation);
    wl_free_string(&builder->verified_by);
    wl_free_string(&builder->verified_on);
    wl_free_string(&builder->text);
    builder->text_len = 0;
    builder->have_text = false;
}

static bool wl_finalize_line(struct builder *builder) {
    struct wl_content_line line;
    unsigned mask;
    int event;

    if (builder->line_id == NULL) {
        return true;
    }
    memset(&line, 0, sizeof line);

    if (!builder->have_text) {
        wl_builder_fail(builder, builder->line_no, "line has no text");
        return false;
    }
    if (!wl_text_valid(builder->text, builder->text_len)) {
        wl_builder_fail(builder, builder->line_no,
                        "text is empty, too long, contains ESC/NUL, or uses "
                        "an unknown #{placeholder}");
        return false;
    }
    if (builder->persona_id == NULL) {
        wl_builder_fail(builder, builder->line_no,
                        "line appears before any [persona] section");
        return false;
    }
    {
        size_t id_len = strlen(builder->line_id);
        size_t persona_len = strlen(builder->persona_id);

        if (id_len <= persona_len + 1 ||
            strncmp(builder->line_id, builder->persona_id, persona_len) != 0 ||
            builder->line_id[persona_len] != '.') {
            wl_builder_fail(builder, builder->line_no,
                            "line id must be '<persona>.<name>'");
            return false;
        }
    }
    if (!wl_valid_id(builder->line_id)) {
        wl_builder_fail(builder, builder->line_no, "invalid line id");
        return false;
    }
    for (size_t index = 0; index < builder->line_count; index++) {
        if (strcmp(builder->lines[index].id, builder->line_id) == 0) {
            wl_builder_fail(builder, builder->line_no, "duplicate line id");
            return false;
        }
    }
    event = wl_event_from_name(builder->event_name != NULL
                                   ? builder->event_name
                                   : "");
    if (event < 0) {
        wl_builder_fail(builder, builder->line_no,
                        "unknown or missing event");
        return false;
    }
    mask = WL_EVENT_BIT((enum wl_event_kind)event);
    if (builder->have_speaks_on && (builder->speaks_mask & mask) == 0) {
        wl_builder_fail(builder, builder->line_no,
                        "event is not listed in the persona's speaks_on");
        return false;
    }
    if (!wl_status_from_name(builder->status_name != NULL
                                 ? builder->status_name
                                 : "",
                             &line.status)) {
        wl_builder_fail(builder, builder->line_no,
                        "status must be original, verified or unverified");
        return false;
    }
    if (!wl_category_from_name(builder->category_name != NULL
                                   ? builder->category_name
                                   : "",
                               &line.category)) {
        wl_builder_fail(builder, builder->line_no,
                        "category must be original, canonical or quotation");
        return false;
    }

    if (line.category == WL_CONTENT_CATEGORY_CANONICAL) {
        if (line.status != WL_CONTENT_STATUS_VERIFIED) {
            wl_builder_fail(builder, builder->line_no,
                            "canonical text requires status verified");
            return false;
        }
        if (builder->author == NULL || builder->work == NULL ||
            builder->source == NULL || builder->verified_by == NULL) {
            wl_builder_fail(builder, builder->line_no,
                            "canonical text requires author, work, source and "
                            "verified_by");
            return false;
        }
    } else if (line.category == WL_CONTENT_CATEGORY_QUOTATION) {
        if (builder->author == NULL || builder->work == NULL ||
            builder->source == NULL || builder->translation == NULL) {
            wl_builder_fail(builder, builder->line_no,
                            "quotation requires author, work, source and "
                            "translation (use 'none' for the original "
                            "language)");
            return false;
        }
        if (line.status == WL_CONTENT_STATUS_ORIGINAL) {
            wl_builder_fail(builder, builder->line_no,
                            "quotation cannot have status original");
            return false;
        }
    } else {
        if (line.status != WL_CONTENT_STATUS_ORIGINAL) {
            wl_builder_fail(builder, builder->line_no,
                            "original text requires status original");
            return false;
        }
        if (builder->author == NULL) {
            wl_builder_fail(builder, builder->line_no,
                            "original text requires an author");
            return false;
        }
        if (builder->work != NULL || builder->source != NULL ||
            builder->translation != NULL || builder->verified_by != NULL ||
            builder->verified_on != NULL) {
            wl_builder_fail(builder, builder->line_no,
                            "original text must not carry work/source/"
                            "translation/verification fields");
            return false;
        }
    }
    if (line.status == WL_CONTENT_STATUS_VERIFIED &&
        (builder->verified_by == NULL || builder->verified_on == NULL)) {
        wl_builder_fail(builder, builder->line_no,
                        "status verified requires verified_by and verified_on");
        return false;
    }

    line.id = builder->line_id;
    builder->line_id = NULL;
    line.text = builder->text;
    builder->text = NULL;
    line.len = builder->text_len;
    line.event_mask = mask;
    line.author = builder->author;
    builder->author = NULL;
    line.work = builder->work;
    builder->work = NULL;
    line.source = builder->source;
    builder->source = NULL;
    line.translation = builder->translation;
    builder->translation = NULL;
    line.verified_by = builder->verified_by;
    builder->verified_by = NULL;
    line.verified_on = builder->verified_on;
    builder->verified_on = NULL;

    if (builder->line_count == builder->line_capacity) {
        size_t capacity =
            (builder->line_capacity > 0) ? builder->line_capacity * 2 : 16;
        struct wl_content_line *grown =
            realloc(builder->lines, capacity * sizeof *grown);

        if (grown == NULL) {
            wl_free_line(&line);
            wl_builder_fail(builder, builder->line_no, "out of memory");
            return false;
        }
        builder->lines = grown;
        builder->line_capacity = capacity;
    }
    builder->lines[builder->line_count++] = line;
    builder->event_mask |= mask;
    return true;
}

static bool wl_finalize_pack(struct builder *builder,
                             struct wl_persona **out_persona) {
    struct wl_persona persona;

    *out_persona = NULL;
    if (builder->pack_id == NULL || !builder->have_format ||
        builder->pack_format != 1 || builder->pack_name == NULL ||
        builder->pack_license == NULL) {
        wl_builder_fail(builder, builder->line_no,
                        "pack requires id, format = 1, name and "
                        "content_license");
        return false;
    }
    if (!wl_valid_id(builder->pack_id)) {
        wl_builder_fail(builder, builder->line_no, "invalid pack id");
        return false;
    }
    if (builder->persona_id == NULL) {
        wl_builder_fail(builder, builder->line_no,
                        "pack has no [persona] section");
        return false;
    }
    if (!wl_valid_id(builder->persona_id)) {
        wl_builder_fail(builder, builder->line_no, "invalid persona id");
        return false;
    }
    if (builder->display_name == NULL) {
        wl_builder_fail(builder, builder->line_no,
                        "persona requires display_name");
        return false;
    }
    if (builder->class_name == NULL ||
        (strcmp(builder->class_name, "main") != 0 &&
         strcmp(builder->class_name, "major") != 0 &&
         strcmp(builder->class_name, "secondary") != 0 &&
         strcmp(builder->class_name, "guest") != 0)) {
        wl_builder_fail(builder, builder->line_no,
                        "persona class must be main, major, secondary or "
                        "guest");
        return false;
    }
    if (builder->line_count == 0) {
        wl_builder_fail(builder, builder->line_no,
                        "persona has no lines");
        return false;
    }

    memset(&persona, 0, sizeof persona);
    persona.id = builder->persona_id;
    builder->persona_id = NULL;
    persona.display_name = builder->display_name;
    builder->display_name = NULL;
    persona.class_name = builder->class_name;
    builder->class_name = NULL;
    persona.voice = builder->voice;
    builder->voice = NULL;
    persona.worldview = builder->worldview;
    builder->worldview = NULL;
    persona.description = builder->description;
    builder->description = NULL;
    persona.frequency =
        builder->have_frequency ? builder->frequency : 120;
    persona.silence_bias =
        builder->have_silence ? builder->silence_bias : 10;
    persona.default_persona = builder->default_persona;
    persona.event_mask = builder->event_mask;
    persona.lines = builder->lines;
    persona.line_count = builder->line_count;
    builder->lines = NULL;
    builder->line_count = 0;

    *out_persona = wl_alloc(sizeof **out_persona);
    if (*out_persona == NULL) {
        fprintf(stderr, "wonderland: pack %s: out of memory\n", builder->path);
        free((char *)persona.id);
        free((char *)persona.display_name);
        free((char *)persona.class_name);
        free((char *)persona.voice);
        free((char *)persona.worldview);
        free((char *)persona.description);
        for (size_t index = 0; index < persona.line_count; index++) {
            wl_free_line((struct wl_content_line *)&persona.lines[index]);
        }
        free((struct wl_content_line *)persona.lines);
        return false;
    }
    **out_persona = persona;
    return true;
}

static bool wl_parse_bool(const char *text, bool *out) {
    if (strcmp(text, "true") == 0 || strcmp(text, "yes") == 0 ||
        strcmp(text, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(text, "false") == 0 || strcmp(text, "no") == 0 ||
        strcmp(text, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool wl_parse_frequency(const char *text, unsigned *out) {
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value < 0.0 ||
        value > 1.0) {
        return false;
    }
    *out = (unsigned)(value * 1000.0 + 0.5);
    return true;
}

static bool wl_parse_bias(const char *text, unsigned *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 ||
        value > 100) {
        return false;
    }
    *out = (unsigned)value;
    return true;
}

static bool wl_parse_speaks_on(struct builder *builder, const char *text) {
    const char *cursor = text;
    unsigned mask = 0;

    while (*cursor != '\0') {
        const char *comma = strchr(cursor, ',');
        size_t length = (comma != NULL) ? (size_t)(comma - cursor)
                                        : strlen(cursor);
        const char *start = cursor;
        char name[64];
        int event;

        while (length > 0 && wl_is_space(*start)) {
            start++;
            length--;
        }
        while (length > 0 && wl_is_space(start[length - 1])) {
            length--;
        }
        if (length == 0 || length >= sizeof name) {
            return false;
        }
        memcpy(name, start, length);
        name[length] = '\0';
        event = wl_event_from_name(name);
        if (event < 0) {
            return false;
        }
        mask |= WL_EVENT_BIT((enum wl_event_kind)event);
        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
    }
    builder->speaks_mask = mask;
    builder->have_speaks_on = true;
    return true;
}

static bool wl_apply_key(struct builder *builder, enum section section,
                         const char *key, const char *value) {
    if (section == SEC_PACK) {
        if (strcmp(key, "id") == 0) {
            return wl_set_string(&builder->pack_id, value, strlen(value));
        }
        if (strcmp(key, "format") == 0) {
            char *end = NULL;

            builder->pack_format = strtol(value, &end, 10);
            builder->have_format =
                end != value && *end == '\0' && builder->pack_format == 1;
            if (!builder->have_format) {
                wl_builder_fail(builder, builder->line_no,
                                "unsupported format (only 1 is accepted)");
                return false;
            }
            return true;
        }
        if (strcmp(key, "name") == 0) {
            return wl_set_string(&builder->pack_name, value, strlen(value));
        }
        if (strcmp(key, "content_license") == 0) {
            return wl_set_string(&builder->pack_license, value,
                                 strlen(value));
        }
        if (strcmp(key, "description") == 0) {
            return wl_set_string(&builder->pack_description, value,
                                 strlen(value));
        }
        fprintf(stderr, "wonderland: pack %s: warning: unknown key '%s'\n",
                builder->path, key);
        return true;
    }

    if (section == SEC_PERSONA) {
        if (strcmp(key, "display_name") == 0) {
            return wl_set_string(&builder->display_name, value,
                                 strlen(value));
        }
        if (strcmp(key, "class") == 0) {
            return wl_set_string(&builder->class_name, value, strlen(value));
        }
        if (strcmp(key, "voice") == 0) {
            return wl_set_string(&builder->voice, value, strlen(value));
        }
        if (strcmp(key, "worldview") == 0) {
            return wl_set_string(&builder->worldview, value, strlen(value));
        }
        if (strcmp(key, "description") == 0) {
            return wl_set_string(&builder->description, value,
                                 strlen(value));
        }
        if (strcmp(key, "frequency") == 0) {
            builder->have_frequency =
                wl_parse_frequency(value, &builder->frequency);
            if (!builder->have_frequency) {
                wl_builder_fail(builder, builder->line_no,
                                "frequency must be between 0 and 1");
                return false;
            }
            return true;
        }
        if (strcmp(key, "silence_bias") == 0) {
            builder->have_silence =
                wl_parse_bias(value, &builder->silence_bias);
            if (!builder->have_silence) {
                wl_builder_fail(builder, builder->line_no,
                                "silence_bias must be between 0 and 100");
                return false;
            }
            return true;
        }
        if (strcmp(key, "default") == 0) {
            if (!wl_parse_bool(value, &builder->default_persona)) {
                wl_builder_fail(builder, builder->line_no,
                                "default must be true or false");
                return false;
            }
            return true;
        }
        if (strcmp(key, "verbosity") == 0) {
            if (strcmp(value, "low") != 0 && strcmp(value, "normal") != 0 &&
                strcmp(value, "high") != 0) {
                wl_builder_fail(builder, builder->line_no,
                                "verbosity must be low, normal or high");
                return false;
            }
            return true;
        }
        if (strcmp(key, "lines") == 0) {
            /* Reserved by the format; lines are discovered by section. */
            return true;
        }
        if (strcmp(key, "speaks_on") == 0) {
            if (!wl_parse_speaks_on(builder, value)) {
                wl_builder_fail(builder, builder->line_no,
                                "speaks_on contains an unknown event");
                return false;
            }
            return true;
        }
        fprintf(stderr, "wonderland: pack %s: warning: unknown key '%s'\n",
                builder->path, key);
        return true;
    }

    if (section == SEC_LINE) {
        if (strcmp(key, "event") == 0) {
            return wl_set_string(&builder->event_name, value, strlen(value));
        }
        if (strcmp(key, "status") == 0) {
            return wl_set_string(&builder->status_name, value, strlen(value));
        }
        if (strcmp(key, "category") == 0) {
            return wl_set_string(&builder->category_name, value,
                                 strlen(value));
        }
        if (strcmp(key, "author") == 0) {
            return wl_set_string(&builder->author, value, strlen(value));
        }
        if (strcmp(key, "work") == 0) {
            return wl_set_string(&builder->work, value, strlen(value));
        }
        if (strcmp(key, "source") == 0) {
            return wl_set_string(&builder->source, value, strlen(value));
        }
        if (strcmp(key, "translation") == 0) {
            return wl_set_string(&builder->translation, value,
                                 strlen(value));
        }
        if (strcmp(key, "verified_by") == 0) {
            return wl_set_string(&builder->verified_by, value,
                                 strlen(value));
        }
        if (strcmp(key, "verified_on") == 0) {
            return wl_set_string(&builder->verified_on, value,
                                 strlen(value));
        }
        if (strcmp(key, "tags") == 0) {
            /* Authoring metadata; not used by the M6 engine. */
            return true;
        }
        if (strcmp(key, "text") == 0) {
            builder->text_len = strlen(value);
            builder->have_text = true;
            return wl_set_string(&builder->text, value, builder->text_len);
        }
        fprintf(stderr, "wonderland: pack %s: warning: unknown key '%s'\n",
                builder->path, key);
        return true;
    }

    wl_builder_fail(builder, builder->line_no,
                    "key outside of a section");
    return false;
}

static bool wl_append_text(struct builder *builder, const char *text,
                           size_t length, bool first) {
    size_t needed = builder->text_len + length + (first ? 0 : 1);
    char *grown;

    if (needed > WL_PACK_MAX_TEXT) {
        return false;
    }
    grown = realloc(builder->text, needed + 1);
    if (grown == NULL) {
        return false;
    }
    builder->text = grown;
    if (!first) {
        builder->text[builder->text_len++] = '\n';
    }
    memcpy(builder->text + builder->text_len, text, length);
    builder->text_len += length;
    builder->text[builder->text_len] = '\0';
    return true;
}

static bool wl_process_line(struct builder *builder, const char *line,
                            size_t length, enum section *section) {
    const char *start = line;
    size_t trimmed = length;

    builder->line_no++;
    wl_trim(&start, &trimmed);

    if (builder->in_heredoc) {
        if (trimmed == strlen(builder->heredoc_sentinel) &&
            memcmp(start, builder->heredoc_sentinel, trimmed) == 0) {
            builder->in_heredoc = false;
            builder->have_text = true;
            wl_free_string(&builder->heredoc_sentinel);
            return true;
        }
        if (!wl_append_text(builder, start, trimmed, builder->text == NULL)) {
            wl_builder_fail(builder, builder->line_no,
                            "here-document too large or out of memory");
            return false;
        }
        return true;
    }

    if (trimmed == 0 || start[0] == '#') {
        return true;
    }

    if (start[0] == '[') {
        const char *close = memchr(start, ']', trimmed);
        char header[256];
        size_t header_len;

        if (close == NULL) {
            wl_builder_fail(builder, builder->line_no,
                            "unterminated section header");
            return false;
        }
        header_len = (size_t)(close - start - 1);
        if (header_len >= sizeof header) {
            wl_builder_fail(builder, builder->line_no,
                            "section header too long");
            return false;
        }
        memcpy(header, start + 1, header_len);
        header[header_len] = '\0';

        if (strcmp(header, "pack") == 0) {
            if (*section == SEC_PERSONA || *section == SEC_LINE) {
                if (!wl_finalize_line(builder)) {
                    return false;
                }
            }
            if (*section != SEC_NONE) {
                wl_builder_fail(builder, builder->line_no,
                                "[pack] must appear once, first");
                return false;
            }
            *section = SEC_PACK;
            return true;
        }
        if (strncmp(header, "persona ", 8) == 0) {
            const char *id = header + 8;
            size_t id_len;

            while (*id == ' ') {
                id++;
            }
            id_len = strlen(id);
            wl_trim(&id, &id_len);
            if (builder->persona_id != NULL) {
                wl_builder_fail(builder, builder->line_no,
                                "M6 supports one persona per pack");
                return false;
            }
            if (!wl_finalize_line(builder)) {
                return false;
            }
            if (!wl_set_string(&builder->persona_id, id, id_len)) {
                wl_builder_fail(builder, builder->line_no, "out of memory");
                return false;
            }
            *section = SEC_PERSONA;
            return true;
        }
        if (strncmp(header, "line ", 5) == 0) {
            const char *id = header + 5;
            size_t id_len;

            while (*id == ' ') {
                id++;
            }
            id_len = strlen(id);
            wl_trim(&id, &id_len);
            if (!wl_finalize_line(builder)) {
                return false;
            }
            wl_reset_line(builder);
            if (!wl_set_string(&builder->line_id, id, id_len)) {
                wl_builder_fail(builder, builder->line_no, "out of memory");
                return false;
            }
            *section = SEC_LINE;
            return true;
        }
        wl_builder_fail(builder, builder->line_no, "unknown section");
        return false;
    }

    if (*section == SEC_NONE) {
        wl_builder_fail(builder, builder->line_no,
                        "content outside a section");
        return false;
    }

    {
        const char *arrow = NULL;
        const char *equals;
        const char *key = start;
        size_t key_len;
        const char *value;
        size_t value_len;
        char key_buf[128];
        char *value_buf;
        bool ok;
        size_t scan;

        for (scan = 0; scan + 1 < trimmed; scan++) {
            if (start[scan] == '<' && start[scan + 1] == '<') {
                arrow = start + scan;
                break;
            }
        }
        equals = memchr(start, '=', trimmed);

        /*
         * Here-document form: "key <<SENTINEL". It must be recognised before
         * the generic 'key = value' split, because it contains no '='.
         */
        if (arrow != NULL && (equals == NULL || arrow < equals)) {
            const char *sentinel;
            size_t sentinel_len;

            key_len = (size_t)(arrow - start);
            wl_trim(&key, &key_len);
            if (key_len == 0 || key_len >= sizeof key_buf) {
                wl_builder_fail(builder, builder->line_no, "invalid key");
                return false;
            }
            memcpy(key_buf, key, key_len);
            key_buf[key_len] = '\0';

            sentinel = arrow + 2;
            sentinel_len = trimmed - (size_t)(sentinel - start);
            wl_trim(&sentinel, &sentinel_len);

            if (strcmp(key_buf, "text") != 0 || *section != SEC_LINE ||
                sentinel_len == 0) {
                wl_builder_fail(builder, builder->line_no,
                                "here-document is only valid for text in a "
                                "[line] section");
                return false;
            }
            if (!wl_set_string(&builder->heredoc_sentinel, sentinel,
                               sentinel_len)) {
                wl_builder_fail(builder, builder->line_no, "out of memory");
                return false;
            }
            builder->in_heredoc = true;
            builder->have_text = false;
            wl_free_string(&builder->text);
            builder->text_len = 0;
            return true;
        }

        if (equals == NULL) {
            wl_builder_fail(builder, builder->line_no,
                            "expected 'key = value' or 'key <<SENTINEL'");
            return false;
        }
        key_len = (size_t)(equals - start);
        wl_trim(&key, &key_len);
        if (key_len == 0 || key_len >= sizeof key_buf) {
            wl_builder_fail(builder, builder->line_no, "invalid key");
            return false;
        }
        memcpy(key_buf, key, key_len);
        key_buf[key_len] = '\0';

        value = equals + 1;
        value_len = trimmed - (size_t)(value - start);
        wl_trim(&value, &value_len);
        if (value_len > WL_PACK_MAX_SCALAR) {
            wl_builder_fail(builder, builder->line_no, "value too long");
            return false;
        }
        value_buf = wl_strdup_n(value, value_len);
        if (value_buf == NULL) {
            wl_builder_fail(builder, builder->line_no, "out of memory");
            return false;
        }

        ok = wl_apply_key(builder, *section, key_buf, value_buf);
        free(value_buf);
        return ok;
    }
}

static struct wl_persona *wl_load_pack(const char *path) {
    FILE *file;
    char *data;
    size_t size = 0;
    size_t capacity = 0;
    struct builder builder;
    struct wl_persona *persona = NULL;
    enum section section = SEC_NONE;
    size_t at = 0;

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "wonderland: pack %s: %s\n", path, strerror(errno));
        return NULL;
    }
    capacity = 4096;
    data = malloc(capacity);
    if (data == NULL) {
        fprintf(stderr, "wonderland: pack %s: out of memory\n", path);
        (void)fclose(file);
        return NULL;
    }
    for (;;) {
        size_t count;

        if (size == capacity) {
            size_t grown_capacity = capacity * 2;
            char *grown;

            if (grown_capacity > WL_PACK_MAX_BYTES) {
                fprintf(stderr, "wonderland: pack %s: file is too large\n",
                        path);
                free(data);
                (void)fclose(file);
                return NULL;
            }
            grown = realloc(data, grown_capacity);
            if (grown == NULL) {
                fprintf(stderr, "wonderland: pack %s: out of memory\n", path);
                free(data);
                (void)fclose(file);
                return NULL;
            }
            data = grown;
            capacity = grown_capacity;
        }
        count = fread(data + size, 1, capacity - size, file);
        size += count;
        if (count == 0) {
            break;
        }
    }
    (void)fclose(file);

    memset(&builder, 0, sizeof builder);
    builder.path = path;

    while (at < size) {
        size_t start = at;

        while (at < size && data[at] != '\n') {
            at++;
        }
        {
            size_t length = at - start;

            if (length > 0 && data[start + length - 1] == '\r') {
                length--;
            }
            if (!wl_process_line(&builder, data + start, length, &section)) {
                wl_builder_free(&builder);
                free(data);
                return NULL;
            }
        }
        if (at < size && data[at] == '\n') {
            at++;
        }
    }
    free(data);

    if (builder.in_heredoc) {
        fprintf(stderr, "wonderland: pack %s: unterminated here-document\n",
                path);
        wl_builder_free(&builder);
        return NULL;
    }
    if (!wl_finalize_line(&builder)) {
        wl_builder_free(&builder);
        return NULL;
    }
    if (!wl_finalize_pack(&builder, &persona)) {
        wl_builder_free(&builder);
        return NULL;
    }
    wl_builder_free(&builder);
    return persona;
}

static bool wl_persona_id_taken(const struct wl_persona *personas,
                                size_t count, const char *id) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(personas[index].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static bool wl_line_ids_taken(const struct wl_persona *personas, size_t count,
                              const struct wl_persona *candidate) {
    for (size_t index = 0; index < count; index++) {
        for (size_t outer = 0; outer < personas[index].line_count; outer++) {
            for (size_t inner = 0; inner < candidate->line_count; inner++) {
                if (strcmp(personas[index].lines[outer].id,
                           candidate->lines[inner].id) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

static int wl_compare_strings(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static bool wl_collect_directory(const char *directory, char ***paths,
                                 size_t *count, size_t *capacity) {
    DIR *dir = opendir(directory);
    struct dirent *entry;

    if (dir == NULL) {
        fprintf(stderr, "wonderland: pack %s: %s\n", directory,
                strerror(errno));
        return false;
    }
    while ((entry = readdir(dir)) != NULL) {
        size_t length = strlen(entry->d_name);
        char *joined;

        if (length < 6 || strcmp(entry->d_name + length - 5, ".pack") != 0) {
            continue;
        }
        if (*count == *capacity) {
            size_t grown_capacity = (*capacity > 0) ? *capacity * 2 : 8;
            char **grown = realloc(*paths, grown_capacity * sizeof *grown);

            if (grown == NULL) {
                (void)closedir(dir);
                return false;
            }
            *paths = grown;
            *capacity = grown_capacity;
        }
        {
            size_t needed = strlen(directory) + length + 2;

            joined = malloc(needed);
            if (joined == NULL) {
                (void)closedir(dir);
                return false;
            }
            (void)snprintf(joined, needed, "%s/%s", directory, entry->d_name);
        }
        (*paths)[(*count)++] = joined;
    }
    (void)closedir(dir);
    return true;
}

struct wl_packset *wl_packset_load(const char *const *paths, size_t count) {
    struct wl_packset *packs;
    struct wl_persona *personas = NULL;
    size_t persona_count = 0;
    size_t persona_capacity = 0;
    char **files = NULL;
    size_t file_count = 0;
    size_t file_capacity = 0;

    if (paths == NULL) {
        count = 0;
    }
    for (size_t index = 0; index < count; index++) {
        struct stat info;

        if (paths[index] == NULL || paths[index][0] == '\0') {
            continue;
        }
        if (stat(paths[index], &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            if (!wl_collect_directory(paths[index], &files, &file_count,
                                      &file_capacity)) {
                /* keep going with what was collected */
            }
        } else {
            if (file_count == file_capacity) {
                size_t grown_capacity =
                    (file_capacity > 0) ? file_capacity * 2 : 8;
                char **grown =
                    realloc(files, grown_capacity * sizeof *grown);

                if (grown == NULL) {
                    break;
                }
                files = grown;
                file_capacity = grown_capacity;
            }
            files[file_count] = strdup(paths[index]);
            if (files[file_count] != NULL) {
                file_count++;
            }
        }
    }

    if (file_count > 1) {
        qsort(files, file_count, sizeof *files, wl_compare_strings);
    }

    {
        char **seen = calloc(file_count, sizeof *seen);
        size_t seen_count = 0;

        for (size_t index = 0; index < file_count; index++) {
            struct wl_persona *persona;
            char *resolved = realpath(files[index], NULL);
            const char *key = (resolved != NULL) ? resolved : files[index];
            bool duplicate = false;

            if (seen != NULL) {
                for (size_t previous = 0; previous < seen_count; previous++) {
                    if (strcmp(seen[previous], key) == 0) {
                        duplicate = true;
                        break;
                    }
                }
            }
            if (duplicate) {
                free(resolved);
                free(files[index]);
                continue;
            }
            if (seen != NULL) {
                seen[seen_count] = strdup(key);
                if (seen[seen_count] != NULL) {
                    seen_count++;
                }
            }
            free(resolved);
            persona = wl_load_pack(files[index]);
            if (persona == NULL) {
                free(files[index]);
                continue;
            }
        if (wl_persona_id_taken(personas, persona_count, persona->id)) {
            fprintf(stderr,
                    "wonderland: pack %s: duplicate persona id '%s'\n",
                    files[index], persona->id);
            free((char *)persona->id);
            free((char *)persona->display_name);
            free((char *)persona->class_name);
            free((char *)persona->voice);
            free((char *)persona->worldview);
            free((char *)persona->description);
            for (size_t line = 0; line < persona->line_count; line++) {
                wl_free_line((struct wl_content_line *)&persona->lines[line]);
            }
            free((struct wl_content_line *)persona->lines);
            free(persona);
            free(files[index]);
            continue;
        }
        if (wl_line_ids_taken(personas, persona_count, persona)) {
            fprintf(stderr,
                    "wonderland: pack %s: duplicate line id\n",
                    files[index]);
            free((char *)persona->id);
            free((char *)persona->display_name);
            free((char *)persona->class_name);
            free((char *)persona->voice);
            free((char *)persona->worldview);
            free((char *)persona->description);
            for (size_t line = 0; line < persona->line_count; line++) {
                wl_free_line((struct wl_content_line *)&persona->lines[line]);
            }
            free((struct wl_content_line *)persona->lines);
            free(persona);
            free(files[index]);
            continue;
        }
        if (persona_count == persona_capacity) {
            size_t grown_capacity =
                (persona_capacity > 0) ? persona_capacity * 2 : 8;
            struct wl_persona *grown =
                realloc(personas, grown_capacity * sizeof *grown);

            if (grown == NULL) {
                free(files[index]);
                break;
            }
            personas = grown;
            persona_capacity = grown_capacity;
        }
            personas[persona_count++] = *persona;
            free(persona);
            free(files[index]);
        }
        if (seen != NULL) {
            for (size_t index = 0; index < seen_count; index++) {
                free(seen[index]);
            }
            free(seen);
        }
    }
    free(files);

    packs = wl_alloc(sizeof *packs);
    if (packs == NULL) {
        fprintf(stderr, "wonderland: out of memory loading packs\n");
        return NULL;
    }
    packs->personas = personas;
    packs->count = persona_count;
    return packs;
}

void wl_packset_free(struct wl_packset *packs) {
    if (packs == NULL) {
        return;
    }
    for (size_t index = 0; index < packs->count; index++) {
        struct wl_persona *persona = (struct wl_persona *)&packs->personas[index];

        free((char *)persona->id);
        free((char *)persona->display_name);
        free((char *)persona->class_name);
        free((char *)persona->voice);
        free((char *)persona->worldview);
        free((char *)persona->description);
        for (size_t line = 0; line < persona->line_count; line++) {
            wl_free_line((struct wl_content_line *)&persona->lines[line]);
        }
        free((struct wl_content_line *)persona->lines);
    }
    free((struct wl_persona *)packs->personas);
    free(packs);
}

const struct wl_persona *wl_packset_find(const struct wl_packset *packs,
                                         const char *id) {
    if (packs == NULL || id == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < packs->count; index++) {
        if (strcmp(packs->personas[index].id, id) == 0) {
            return &packs->personas[index];
        }
    }
    return NULL;
}
