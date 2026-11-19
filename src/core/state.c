#include "core/state.h"

#include "core/buf.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * State file format (all values after the magic are line-oriented):
 *
 *   wonderland-state 1
 *   cycle 3
 *   persona alice
 *   rest 1
 *   rest_cwd <hex>
 *   rest_time 1700000000
 *   var PATH <hex>
 *   checksum <16 hex digits of everything above, including newlines>
 *
 * Hex encoding keeps arbitrary bytes (spaces, newlines) unambiguous without an
 * escaping grammar. The checksum makes a truncated or edited file visibly
 * wrong instead of quietly plausible.
 */

#define WL_STATE_MAGIC "wonderland-state 1"

/* The file is disposable; a larger one is treated as corrupt, not parsed. */
#define WL_STATE_MAX_BYTES (256u * 1024u)
#define WL_STATE_LINE_MAX 16384u

static bool wl_state_valid_persona(const char *id);
static bool wl_state_valid_name(const char *name);

static uint64_t wl_fnv1a(const char *data, size_t length) {
    uint64_t hash = 14695981039346656037ULL;

    for (size_t index = 0; index < length; index++) {
        hash ^= (unsigned char)data[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void wl_state_init(struct wl_state *state) {
    memset(state, 0, sizeof *state);
}

void wl_state_free(struct wl_state *state) {
    free(state->persona);
    free(state->rest_cwd);
    for (size_t index = 0; index < state->var_count; index++) {
        free(state->vars[index].name);
        free(state->vars[index].value);
    }
    free(state->vars);
    memset(state, 0, sizeof *state);
}

void wl_state_clear_rest(struct wl_state *state) {
    state->has_rest = false;
    free(state->rest_cwd);
    state->rest_cwd = NULL;
    state->rest_time = 0;
    for (size_t index = 0; index < state->var_count; index++) {
        free(state->vars[index].name);
        free(state->vars[index].value);
    }
    free(state->vars);
    state->vars = NULL;
    state->var_count = 0;
    state->var_capacity = 0;
}

bool wl_state_set_persona(struct wl_state *state, const char *id) {
    char *copy;

    if (id != NULL && !wl_state_valid_persona(id)) {
        return false;
    }
    copy = (id != NULL) ? strdup(id) : NULL;
    if (id != NULL && copy == NULL) {
        return false;
    }
    free(state->persona);
    state->persona = copy;
    return true;
}

bool wl_state_set_var(struct wl_state *state, const char *name,
                      const char *value) {
    char *name_copy;
    char *value_copy;

    if (!wl_state_valid_name(name)) {
        return false;
    }
    name_copy = strdup(name);
    value_copy = strdup(value);
    if (name_copy == NULL || value_copy == NULL) {
        free(name_copy);
        free(value_copy);
        return false;
    }
    for (size_t index = 0; index < state->var_count; index++) {
        if (strcmp(state->vars[index].name, name) == 0) {
            free(state->vars[index].value);
            state->vars[index].value = value_copy;
            free(name_copy);
            return true;
        }
    }
    if (state->var_count == state->var_capacity) {
        size_t capacity =
            (state->var_capacity > 0) ? state->var_capacity * 2 : 8;
        struct wl_state_var *grown =
            realloc(state->vars, capacity * sizeof *grown);

        if (grown == NULL) {
            free(name_copy);
            free(value_copy);
            return false;
        }
        state->vars = grown;
        state->var_capacity = capacity;
    }
    state->vars[state->var_count].name = name_copy;
    state->vars[state->var_count].value = value_copy;
    state->var_count++;
    return true;
}

char *wl_state_default_path(void) {
    const char *xdg = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    char *path;
    size_t length;

    if (xdg != NULL && xdg[0] != '\0') {
        length = strlen(xdg) + strlen("/wonderland/state") + 1;
        path = malloc(length);
        if (path != NULL) {
            (void)snprintf(path, length, "%s/wonderland/state", xdg);
        }
        return path;
    }
    if (home == NULL || home[0] == '\0') {
        return NULL;
    }
    length = strlen(home) + strlen("/.local/state/wonderland/state") + 1;
    path = malloc(length);
    if (path != NULL) {
        (void)snprintf(path, length, "%s/.local/state/wonderland/state", home);
    }
    return path;
}

static bool wl_state_mkdirs(const char *path) {
    char *copy = strdup(path);

    if (copy == NULL) {
        return false;
    }
    for (char *cursor = copy + 1; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
                free(copy);
                return false;
            }
            *cursor = '/';
        }
    }
    free(copy);
    return true;
}

static bool wl_state_append_hex(struct wl_buf *buffer, const char *text) {
    static const char digits[] = "0123456789abcdef";

    for (size_t index = 0; text[index] != '\0'; index++) {
        char pair[2];

        pair[0] = digits[((unsigned char)text[index]) >> 4];
        pair[1] = digits[((unsigned char)text[index]) & 0x0f];
        if (!wl_buf_append(buffer, pair, 2)) {
            return false;
        }
    }
    return true;
}

static bool wl_state_valid_persona(const char *id) {
    if (id[0] == '\0') {
        return false;
    }
    for (const char *cursor = id; *cursor != '\0'; cursor++) {
        char c = *cursor;

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

static bool wl_state_valid_name(const char *name) {
    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_')) {
        return false;
    }
    for (const char *cursor = name + 1; *cursor != '\0'; cursor++) {
        char c = *cursor;

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static int wl_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static char *wl_state_decode_hex(const char *text) {
    size_t length = strlen(text);
    char *decoded;

    if (length % 2 != 0) {
        return NULL;
    }
    decoded = malloc(length / 2 + 1);
    if (decoded == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < length / 2; index++) {
        int high = wl_hex_digit(text[index * 2]);
        int low = wl_hex_digit(text[index * 2 + 1]);

        if (high < 0 || low < 0) {
            free(decoded);
            return NULL;
        }
        decoded[index] = (char)((high << 4) | low);
    }
    decoded[length / 2] = '\0';
    return decoded;
}

bool wl_state_save(const char *path, const struct wl_state *state) {
    struct wl_buf content;
    char number[64];
    char checksum[64];
    char *temp_path;
    size_t temp_length;
    FILE *file;
    bool ok;

    if (path == NULL) {
        return false;
    }
    wl_buf_init(&content);

    if (!wl_buf_append_str(&content, WL_STATE_MAGIC "\n")) {
        goto fail;
    }
    (void)snprintf(number, sizeof number, "cycle %u\n", state->cycle);
    if (!wl_buf_append_str(&content, number)) {
        goto fail;
    }
    if (state->persona != NULL) {
        if (!wl_buf_append_str(&content, "persona ") ||
            !wl_buf_append_str(&content, state->persona) ||
            !wl_buf_append_str(&content, "\n")) {
            goto fail;
        }
    }
    if (state->has_rest) {
        if (!wl_buf_append_str(&content, "rest 1\nrest_cwd ")) {
            goto fail;
        }
        if (!wl_state_append_hex(&content,
                                 (state->rest_cwd != NULL) ? state->rest_cwd
                                                           : "")) {
            goto fail;
        }
        (void)snprintf(number, sizeof number, "\nrest_time %ld\n",
                       (long)state->rest_time);
        if (!wl_buf_append_str(&content, number)) {
            goto fail;
        }
        for (size_t index = 0; index < state->var_count; index++) {
            if (!wl_buf_append_str(&content, "var ") ||
                !wl_buf_append_str(&content, state->vars[index].name) ||
                !wl_buf_append_str(&content, " ")) {
                goto fail;
            }
            if (!wl_state_append_hex(&content, state->vars[index].value) ||
                !wl_buf_append_str(&content, "\n")) {
                goto fail;
            }
        }
    }

    (void)snprintf(checksum, sizeof checksum, "checksum %016llx\n",
                   (unsigned long long)wl_fnv1a(content.data, content.len));
    if (!wl_buf_append_str(&content, checksum)) {
        goto fail;
    }
    if (content.len > WL_STATE_MAX_BYTES) {
        goto fail;
    }

    if (!wl_state_mkdirs(path)) {
        goto fail;
    }
    temp_length = strlen(path) + 5;
    temp_path = malloc(temp_length);
    if (temp_path == NULL) {
        goto fail;
    }
    (void)snprintf(temp_path, temp_length, "%s.tmp", path);

    file = fopen(temp_path, "w");
    if (file == NULL) {
        free(temp_path);
        goto fail;
    }
    (void)chmod(temp_path, 0600);
    ok = fwrite(content.data, 1, content.len, file) == content.len;
    if (ok) {
        ok = fflush(file) == 0;
    }
    if (ok) {
        ok = fsync(fileno(file)) == 0;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (ok && rename(temp_path, path) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)unlink(temp_path);
    }
    free(temp_path);
    wl_buf_free(&content);
    return ok;

fail:
    wl_buf_free(&content);
    return false;
}

bool wl_state_load(const char *path, struct wl_state *state, bool *corrupt) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t count;
    unsigned parsed_cycle = 0;
    char *parsed_persona = NULL;
    char *parsed_cwd = NULL;
    long parsed_time = 0;
    bool parsed_rest = false;
    struct wl_state_var *parsed_vars = NULL;
    size_t parsed_var_count = 0;
    size_t parsed_var_capacity = 0;
    size_t checksum_offset = 0;
    uint64_t stored_checksum = 0;
    bool have_checksum = false;
    bool have_magic = false;
    char *file_data = NULL;
    size_t file_length = 0;
    struct wl_buf raw;
    bool too_big = false;
    bool ok = false;

    if (corrupt != NULL) {
        *corrupt = false;
    }
    if (path == NULL) {
        return false;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    wl_buf_init(&raw);
    while ((count = getline(&line, &capacity, file)) != -1) {
        if (raw.len + (size_t)count > WL_STATE_MAX_BYTES) {
            too_big = true;
            break;
        }
        /* Keep the exact bytes for checksum verification. */
        if (!wl_buf_append(&raw, line, (size_t)count)) {
            break;
        }
    }
    free(line);
    (void)fclose(file);
    file_data = raw.data;
    file_length = raw.len;
    if (file_data == NULL) {
        file_data = "";
    }
    if (too_big || file_length > WL_STATE_MAX_BYTES) {
        goto invalid;
    }

    {
        size_t at = 0;

        while (at < file_length) {
            size_t start = at;

            while (at < file_length && file_data[at] != '\n') {
                at++;
            }
            {
                size_t length = at - start;
                char local[WL_STATE_LINE_MAX];
                char *value;

                if (length >= sizeof local) {
                    goto invalid;
                }
                memcpy(local, file_data + start, length);
                local[length] = '\0';
                if (at < file_length && file_data[at] == '\n') {
                    at++;
                }

                if (start == 0) {
                    if (strcmp(local, WL_STATE_MAGIC) != 0) {
                        goto invalid;
                    }
                    have_magic = true;
                    continue;
                }
                if (!have_magic) {
                    goto invalid;
                }
                if (strncmp(local, "checksum ", 9) == 0) {
                    char *end = NULL;

                    stored_checksum = strtoull(local + 9, &end, 16);
                    if (end == local + 9 || (end != NULL && *end != '\0')) {
                        goto invalid;
                    }
                    checksum_offset = start;
                    have_checksum = true;
                    continue;
                }
                if (strncmp(local, "cycle ", 6) == 0) {
                    parsed_cycle = (unsigned)strtoul(local + 6, NULL, 10);
                } else if (strncmp(local, "persona ", 8) == 0) {
                    if (!wl_state_valid_persona(local + 8)) {
                        goto invalid;
                    }
                    free(parsed_persona);
                    parsed_persona = strdup(local + 8);
                    if (parsed_persona == NULL) {
                        goto invalid;
                    }
                } else if (strcmp(local, "rest 1") == 0) {
                    parsed_rest = true;
                } else if (strncmp(local, "rest_time ", 10) == 0) {
                    parsed_time = strtol(local + 10, NULL, 10);
                } else if (strncmp(local, "rest_cwd ", 9) == 0) {
                    free(parsed_cwd);
                    parsed_cwd = wl_state_decode_hex(local + 9);
                    if (parsed_cwd == NULL) {
                        goto invalid;
                    }
                } else if (strncmp(local, "var ", 4) == 0) {
                    char *space = strchr(local + 4, ' ');

                    if (space == NULL) {
                        goto invalid;
                    }
                    *space = '\0';
                    if (!wl_state_valid_name(local + 4)) {
                        goto invalid;
                    }
                    value = wl_state_decode_hex(space + 1);
                    if (value == NULL) {
                        goto invalid;
                    }
                    if (parsed_var_count == parsed_var_capacity) {
                        size_t grown_capacity =
                            (parsed_var_capacity > 0)
                                ? parsed_var_capacity * 2
                                : 8;
                        struct wl_state_var *grown = realloc(
                            parsed_vars, grown_capacity * sizeof *grown);

                        if (grown == NULL) {
                            free(value);
                            goto invalid;
                        }
                        parsed_vars = grown;
                        parsed_var_capacity = grown_capacity;
                    }
                    parsed_vars[parsed_var_count].name = strdup(local + 4);
                    parsed_vars[parsed_var_count].value = value;
                    if (parsed_vars[parsed_var_count].name == NULL) {
                        free(value);
                        goto invalid;
                    }
                    parsed_var_count++;
                }
                /* Unknown keys are ignored for forward compatibility. */
            }
        }
    }

    if (!have_magic || !have_checksum) {
        goto invalid;
    }
    if (wl_fnv1a(file_data, checksum_offset) != stored_checksum) {
        goto invalid;
    }

    state->cycle = parsed_cycle;
    free(state->persona);
    state->persona = parsed_persona;
    parsed_persona = NULL;
    state->has_rest = parsed_rest;
    free(state->rest_cwd);
    state->rest_cwd = parsed_cwd;
    parsed_cwd = NULL;
    state->rest_time = (time_t)parsed_time;
    for (size_t index = 0; index < state->var_count; index++) {
        free(state->vars[index].name);
        free(state->vars[index].value);
    }
    free(state->vars);
    state->vars = parsed_vars;
    state->var_count = parsed_var_count;
    state->var_capacity = parsed_var_capacity;
    parsed_vars = NULL;
    ok = true;
    goto done;

invalid:
    if (corrupt != NULL) {
        *corrupt = true;
    }
    ok = false;

done:
    free(parsed_persona);
    free(parsed_cwd);
    if (parsed_vars != NULL) {
        for (size_t index = 0; index < parsed_var_count; index++) {
            free(parsed_vars[index].name);
            free(parsed_vars[index].value);
        }
        free(parsed_vars);
    }
    wl_buf_free(&raw);
    return ok;
}
