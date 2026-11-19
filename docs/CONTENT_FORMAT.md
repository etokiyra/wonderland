# Wonderland — Content Pack Format

Status: implemented in Milestone 6 (`src/content/pack.c`). Core packs live in
`packs/core/`; the example pack remains in `packs/examples/`. The notes marked
"M6 subset" below record the parts of this specification that are accepted
today (D-0097, D-0102).

## 1. Goals and non-goals

Goals:

* human-readable and editable with `vi` and `nano`;
* parseable with ~600 lines of C17 and libc, no JSON/TOML/YAML library;
* line-oriented, so diagnostics point at a line number;
* impossible to make a pack *execute* anything;
* explicit provenance for every line, so the loader can refuse to treat
  unverified material as canonical.

Non-goals for format v1: includes/nesting, conditional content, scripting,
localisation catalogs, binary blobs, external references beyond a source
string, and any format that needs a parser generator.

JSON was rejected because it needs an escaping layer, allows deeply nested
data that buys nothing here, and produces unreadable diffs. TOML was rejected
because a correct subset still needs a value grammar for tables and arrays that
this data does not use. A constrained INI-like format with here-doc text is
enough.

## 2. Lexical structure

* Encoding: bytes, normally UTF-8. Invalid UTF-8 is accepted and passed
  through (the terminal decides), except NUL, which is rejected.
* Line ending: LF; CRLF is normalised to LF before parsing.
* Comments: `#` at the start of a line (after optional spaces). `#` inside a
  value is literal.
* Blank lines separate records and end a multi-line value.
* Sections: `[name]` or `[name argument]` at column 0.
* Keys: `key = value`, one per line. Leading/trailing spaces around the key and
  around `=` are ignored; trailing spaces in the value are stripped (use a
  here-doc if trailing spaces matter).
* Multi-line values: `key <<SENTINEL` on the key line; every following line up
  to a line that is exactly `SENTINEL` is the value, byte for byte (LF
  preserved, no escape processing). A missing sentinel is a fatal error.
* There is no escape syntax inside values. A literal backslash is a backslash.
  Substitutions use `#{name}` placeholders (§6), which is a separate,
  enumerated mechanism.
* File size limits: 8 MiB per pack, 20 000 lines per pack, 8 KiB per scalar
  value, 64 KiB per here-doc value, 256 bytes per id.

## 3. Records

```ini
[pack]
id = wonderland.examples.alice
format = 1
name = Alice — example lines
content_license = unspecified
description = Original example lines only. No canonical, no quotations.

[persona alice]
display_name = Alice
class = main
verbosity = low
frequency = 0.12
voice = curious, literal, polite; unsettled by rules she cannot see
worldview = A place has house rules. She would like to know them.
silence_bias = 10
speaks_on = post_exec, fail, dir_change, session_start, exit

[line alice.post_exec.0001]
event = post_exec
status = original
category = original
author = Wonderland contributors
text = The door closes behind you. That is all a door can do.

[line alice.fail.0001]
event = fail
status = original
category = original
author = Wonderland contributors
text = Something refused you. Do you know its name yet?
```

Design rules that make the format hard to misuse:

* `[pack]` appears exactly once and must be the first section.
* **M6 subset:** one persona per pack. Line ids must be
  `<persona>.<name>` and refer to that pack's persona. Add-on packs for
  another pack's persona are rejected.
* Personas and lines are separate sections so a line can be added without
  touching persona metadata, and a line can be moved between personas by
  editing one field.
* Section order is irrelevant after `[pack]`; line order is irrelevant because
  the loader sorts deterministically.
* IDs are `[a-z0-9._-]+`, must start with a letter or digit, and are globally
  unique across all loaded packs. `line` ids are free-form but conventionally
  `<persona>.<event>.<serial>`.

## 4. Fields

### 4.1 `[pack]`

| Field | Required | Meaning |
|-------|----------|---------|
| `id` | yes | pack identity, dotted lower case |
| `format` | yes | must be `1`; any other value rejects the pack |
| `name` | yes | human name |
| `description` | no | one paragraph |
| `content_license` | yes | SPDX id, or the literal `unspecified`. `unspecified` packs load but are marked non-redistributable in `possess -l` |
| `requires` | no | reserved; must be absent in v1 |

### 4.2 `[persona <id>]`

| Field | Required | Meaning |
|-------|----------|---------|
| `display_name` | yes | shown to the user |
| `class` | yes | `main` \| `major` \| `secondary` \| `guest` |
| `verbosity` | no | `low` \| `normal` \| `high` (default `normal`); accepted and currently ignored by the engine |
| `default` | no | `true` for the persona that should be active at startup; Alice is the core default |
| `frequency` | no | 0.0–1.0, default from class (see `docs/PERSONAS.md`) |
| `silence_bias` | no | 0–100 |
| `voice`, `worldview`, `description` | no | authoring notes, also usable by `possess` |
| `speaks_on` | no | comma-separated event names; restricts where this persona may speak. Unknown event ⇒ fatal |
| `lines` | no | reserved; lines are associated by the `line` section's persona prefix, not by a pattern |

### 4.3 `[line <persona>.<anything>]`

| Field | Required | Meaning |
|-------|----------|---------|
| `event` | yes | one of the event names in `docs/PERSONAS.md` §5 |
| `status` | yes | `verified` \| `unverified` \| `original` |
| `category` | yes | `canonical` \| `quotation` \| `original` |
| `author` | conditional | required for `quotation` and `original`; for `canonical` it is the work's credited author if known |
| `work` | conditional | required for `canonical` and `quotation` |
| `source` | conditional | required for `canonical` and `quotation`: page, act, scene, episode, edition, or URL |
| `translation` | conditional | required for `quotation` unless the quoted language is the original and `translation = none` is stated explicitly |
| `verified_by`, `verified_on` | conditional | required when `status = verified`; who checked it and when |
| `text` | yes | the line itself |
| `tags` | no | comma-separated free tags for authoring (`slow`, `rare`, `cwd`); accepted and currently ignored |

## 5. Validation rules

Validation is a separate pass after parsing. The rules are normative:

1. `format` must be exactly `1`. Unknown format ⇒ **fatal**, pack rejected.
2. Unknown section name ⇒ **fatal** (catches typos like `[persone alice]`).
   Unknown key inside a known section ⇒ **warning**, key ignored.
3. Unknown persona `class`, `event`, `status` or `category` ⇒
   **fatal** for the record; because a persona's lines are meaningless without
   the persona, a bad persona section rejects the pack.
4. `category = canonical` **requires** `status = verified`, `work`, `source`,
   `author`, and `verified_by`. Anything else ⇒ the record is **rejected and
   the pack fails to load**. This is the structural implementation of
   amendment 5: *unverified text can never be loaded as canon*.
5. `category = quotation` requires `author`, `work`, `source`, `translation`
   (or `translation = none`) and `status` ∈ {verified, unverified}. Missing
   provenance ⇒ **fatal**.
6. `category = original` requires `status = original` and `author`; the
   validator additionally rejects `author` values that look like a real
   historical person's name when the pack declares the line original? No —
   that is not mechanically decidable. Instead: `original` lines must not carry
   `work`, `source`, or `translation` fields; those fields on an original line
   are a **fatal** error, because they are the mechanism by which original
   writing would be dressed up as sourced material.
7. `status = verified` without `verified_by` and `verified_on` ⇒ fatal.
8. Duplicate line id across the pack set ⇒ fatal for the pack that repeats it.
9. Every persona must have at least one line, and at least one line for one of
   its `speaks_on` events, or the persona is **rejected with a warning** (a
   mute persona is a content bug, not a crash).
10. Text must be non-empty after trimming, must not contain NUL, must not
    contain raw ANSI escape (`ESC`) bytes, and must not exceed 64 KiB.
    Escapes are rejected because they are how a pack would move the cursor or
    forge prompt text.
11. Placeholders are restricted to `#{cwd_base}`, `#{cmd}`, `#{status}`,
    `#{cycle}`, `#{persona}`. Any other `#{...}` ⇒ fatal for the line (typo
    protection, and no `printf` surface).
12. `frequency` must parse as a decimal in [0,1]; out of range ⇒ fatal for the
    persona.
13. Total lines per persona, per pack: 20 000; total packs: 64. Beyond that,
    the extra pack is rejected rather than silently truncated.

## 6. Placeholders

`#{name}` substitution is performed by the narrative engine at render time, on
the persona's text, not by any format-string function. Each placeholder maps to
a shell-owned, sanitized value:

| Placeholder | Value |
|-------------|-------|
| `#{cwd_base}` | basename of `$PWD`, control characters replaced with `?` |
| `#{cmd}` | first word of the executed command line, truncated to 32 bytes |
| `#{status}` | decimal exit status, e.g. `2` |
| `#{cycle}` | Alice's Library cycle number |
| `#{persona}` | display name of the active persona |

No other syntax is honoured. `#{` followed by anything else is a validation
error, so a pack author learns immediately.

## 7. Loading, atomicity, and malformed packs

```
for each pack path (sorted, deduplicated):
    parse  → candidate records + parse diagnostics
    validate(candidate, already-loaded id set)
    on any fatal error:
        free the candidate
        print exactly one line to stderr:
            wow: pack /path/x.pack: not loaded: line 42: category 'canonical' requires status 'verified'
        continue with the next pack
    on success:
        freeze candidate into the session arena
        register persona/line ids
```

Guarantees:

* A malformed pack never prevents startup, never crashes, and never removes
  personas that loaded earlier.
* Errors are reported once at startup, not on every prompt.
* Failed packs are remembered by `(path, size, mtime)` so a reload is explicit
  (`possess --reload`), not automatic.
* No pack can reference another pack in v1, so there is no include cycle, no
  path traversal, and no network access.
* Unknown-format packs (`format = 2`) are rejected, not guessed at.

## 8. Determinism

Packs are loaded in sorted path order; within a pack, lines are sorted by
`(event, id)`. Selection uses a seeded PRNG whose seed is recorded in the
debug log, so a surprising line can be reproduced. This matters for tests and
for the "why did it say that" question.

## 9. Distribution rules for this repository

* `packs/examples/` contains only `status = original` lines written for
  Wonderland (amendment 5). No Black Souls dialogue, no philosopher
  quotations, no translations.
* `content_license` of the example pack is `unspecified` until the project's
  content license is decided (open decision, see `docs/DECISIONS.md`). Until
  then the example pack is not offered for redistribution outside this
  repository.
* Users may point `WONDERLAND_PACKS` at their own directories. A pack that
  quotes copyrighted translations is the user's responsibility; the loader
  records provenance precisely so that responsibility is auditable.

## 9a. M6 subset and core packs

* Supported event names are exactly the eight the shell emits today:
  `post_exec`, `success`, `fail`, `session_start`, `dir_change`, `job_start`,
  `job_finish`, `exit`. The other events in `docs/PERSONAS.md` are rejected
  until M7 emits them.
* One persona per pack (above); no includes, no localisation, no per-line
  weights.
* `packs/core/` ships Alice (12 lines), Grimm (14), Nietzsche (13), Kafka (13)
  and Cioran (13), all `status = original` / `category = original`, with
  `content_license = unspecified` until OQ-1/OQ-9 are decided.
* `make install` copies core packs to
  `$(PREFIX)/share/wonderland/packs/core`; discovery additionally honours
  `WONDERLAND_PACKS` and the XDG data directory.

## 10. Open questions

* Whether `[line ...]` should allow a `weight` field for author-level
  prioritisation, or whether weighting belongs entirely to persona prefs.
* Whether verified quotations need a hash of the source text to detect silent
  edits.
* Whether packs should be validated at build time (`make check-packs`) in
  addition to load time. Proposed: yes, it is cheap and catches typos in CI.
* Whether `content_license = unspecified` should be a warning or a hard error
  once the project license exists.
