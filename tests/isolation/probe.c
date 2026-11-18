/*
 * Link probe for libwl_narrative.a.
 *
 * The archive is linked with --whole-archive and --no-undefined, so every
 * object in it is pulled in and every unresolved symbol becomes an error.
 * If narrative code ever reaches into shell state, this executable stops
 * linking (docs/ARCHITECTURE.md §3.1).
 */

int main(void) {
    return 0;
}
