# Wonderland — Personas

Status: design. No persona code, no packs beyond a single example pack with
original lines. This document is the contract for Milestones 3.5 and 6.

## 1. What a persona is

A persona is a read-only inhabitant of the session. It owns:

* an identity (name, category, description, worldview, voice),
* a set of narrative lines tagged by event, status and category,
* presentation preferences (verbosity, frequency, silence bias),
* optional character-specific mechanics that are, without exception,
  observational or thin builtins owned by the shell.

A persona does **not** own: the prompt's exit status, the command line, the
environment, the job table, the cycle counter, the checkpoint, or the choice to
exit. Those belong to `core` and to the builtins `possess`, `rest`,
`go-to-alice` and `exit` (amendment 3).

## 2. Persona record

```c
struct wl_persona {
    const char *id;            /* stable key, e.g. "alice"            */
    const char *display_name;
    enum wl_pclass class;      /* MAIN, MAJOR, SECONDARY, GUEST        */
    const char *description;   /* one paragraph, shown by `possess -l` */
    const char *worldview;     /* what they believe; used by authors   */
    const char *voice;         /* how they speak; used by authors      */
    enum wl_verbosity verbosity;
    unsigned char frequency;   /* 0..100: base chance to speak, per mille */
    unsigned char silence_bias;/* 0..100: extra chance to stay quiet    */
    struct wl_event_pref prefs[WL_EVENT_COUNT]; /* weight per event */
    struct wl_line_ref *lines; size_t nlines;   /* sorted by event      */
    unsigned flags;            /* e.g. WL_PF_NO_AUTO_SELECT            */
};
```

Everything is `const` after load. Selection state (last line id, cooldown
deadlines, recent history) lives in `struct wl_persona_state`, owned by the
shell and never exposed to pack data.

## 3. Cast

The brief's list is the long-term pool. Amendment 6 fixes the order: five
maximally distinct voices first, prove them, then scale.

### Wave 1 — the five (delivered in M6)

| Persona | Class | Axes |
|---------|-------|------|
| **Alice** | main | curious, literal-minded, polite, unsettled by rules she cannot see; asks questions; short sentences; no philosophy jargon |
| **Grimm** | main | collector of cruelties told as folk wisdom; archaic, concrete, faintly threatening; never explains the moral |
| **Nietzsche** | major | aphoristic, imperative, contemptuous of comfort; second person; metaphors of ascent, hardness, solitude |
| **Kafka** | major | bureaucratic dread, meticulous qualification, passive constructions; the failure is always procedural, never cosmic |
| **Cioran** | major | exhausted lucidity, elegant pessimism, dry self-mockery; short, terminal sentences; refuses consolation |

### Wave 2 — planned (M7+)

Wonderland: Red Hood, the Cheshire figure, the Duchess, the White Rabbit,
the Queen, the Hatter (each must earn a distinct *function*, not just a name).
Philosophy/literature: Stirner, Dostoevsky, Kierkegaard, Camus, Schopenhauer,
Socrates, plus later additions chosen for contrast rather than prestige.

Rule for adding a persona: write 20 lines first, blind-test them against the
existing cast, and only then add the record. A persona that cannot be told
apart from an existing one is not added.

Implementation status (2026-11-19, M7): `packs/core/` contains Alice (12
lines, default), Grimm (14), Nietzsche (13), Kafka (13), Cioran (13). All are
original and cover success/fail/session_start/dir_change/exit; Grimm,
Nietzsche, Kafka and Cioran also have job lines, now emitted by the Black
Rabbit hooks. Alice's Library persists the cycle and persona; `rest`/`respawn`
restore cwd, `PATH`, `HOME`, `PWD`, `OLDPWD`, `SHELL` and the persona, and
never claim to restore processes. The death screen is opt-in
(`WONDERLAND_DEATH_SCREEN=1`), mirror mode is a prompt fragment (default off),
and `go-to-alice` shares `exit` with an interactive confirmation that
`WONDERLAND_CONFIRM_EXIT=0` disables. Scaling toward 20–30 personas and the
100–300-line targets remains content work; the engine and format already
support it.

## 4. Voice differentiation

The requirement is stronger than "different topics": a line must be
attributable with the name hidden. These are authoring constraints, not
descriptions.

### Alice
* **Does**: asks; notices the wrong thing; uses plain nouns; apologises; counts
  things; treats the terminal as a place with house rules.
* **Never**: aphorism, irony about existence, archaic diction, cruelty.
* **Shape**: a question or an observation, then a small admission of confusion.
* **Length**: 5–15 words, occasionally two clauses.

### Grimm
* **Does**: speaks in collected sayings; uses concrete rural images (bread,
  teeth, snow, hinges); addresses the reader as "you" without warmth; ends
  without a moral.
* **Never**: modern technical vocabulary, self-pity, explanation.
* **Shape**: one image, one consequence, stop.
* **Length**: 8–25 words.

### Nietzsche
* **Does**: commands; uses em-dash apposition; names the temptation
  (comfort, pity, the herd); reframes failure as a test; speaks of height,
  hardness, distance.
* **Never**: bureaucratic detail, hedging, question-asking, consolation.
* **Shape**: imperative or aphorism, with a turn in the second half.
* **Length**: 6–20 words, dense.

### Kafka
* **Does**: qualifies; refers to procedure, forms, offices, the correct
  department; passive voice; the failure is administrative; mild, exhausted
  politeness.
* **Never**: triumph, prophecy, rural imagery, exclamation.
* **Shape**: a statement of what was attempted, then the obstruction, then a
  resigned clause.
* **Length**: 15–40 words, subordinate clauses allowed.

### Cioran
* **Does**: states the end first; short declaratives; dry wit about exhaustion;
  refuses hope without drama.
* **Never**: exclamation, encouragement, long sentences, bureaucratic diction.
* **Shape**: two short sentences, the second narrowing the first.
* **Length**: 4–14 words.

### Blind attribution test (per batch)

1. Strip persona names and event tags from 20 lines.
2. Give them to a reader who knows the cast.
3. Require ≥ 80% correct attribution; a line attributed to the wrong persona
   twice is rewritten or cut.
4. Any line that could belong to *any* persona is cut, regardless of quality.

This test is cheap, human, and mandatory before a persona is enabled by
default. It goes in the PR description, not in an automated test.

## 5. Events

Events are observational records. The shell constructs them; narrative reads
them; nothing else can.

```c
enum wl_event_kind {
    WL_EV_SESSION_START,      /* new interactive session                */
    WL_EV_PRE_EXEC,           /* before a command runs (no status yet)  */
    WL_EV_POST_EXEC,          /* command finished, status final         */
    WL_EV_SUCCESS,            /* post_exec with status == 0             */
    WL_EV_FAIL,               /* post_exec with status != 0             */
    WL_EV_NOT_FOUND,          /* exec failed: ENOENT                    */
    WL_EV_PERMISSION,         /* exec/redirection failed: EACCES        */
    WL_EV_SIGNAL,             /* child died from a signal               */
    WL_EV_DIR_CHANGE,         /* cd/pushd changed cwd                   */
    WL_EV_JOB_START,          /* background job created                 */
    WL_EV_JOB_FINISH,         /* background job reaped                  */
    WL_EV_IDLE,               /* no input for N seconds                 */
    WL_EV_REST,               /* `rest` checkpoint written              */
    WL_EV_RESPAWN,            /* checkpoint restored                    */
    WL_EV_EXIT,               /* session ending (not fatal)             */
    WL_EV_CYCLE,              /* Alice's Library cycle note             */
    WL_EV_COUNT
};

struct wl_event {
    enum wl_event_kind kind;
    int      status;        /* shell status, already final          */
    int      term_signal;   /* 0 if not signal-terminated           */
    int      err;           /* errno captured at failure, else 0    */
    pid_t    job_pid;       /* JOB_* only                          */
    int      job_id;
    long     duration_ms;   /* POST_EXEC only; -1 if unknown        */
    unsigned cycle;         /* Alice's Library cycle number         */
    const char *cmdline;    /* borrowed, NUL-terminated, sanitized  */
    const char *argv0;      /* borrowed, may be NULL                */
    const char *cwd;        /* borrowed                            */
};
```

The brief lists `on_success`, `on_fail`, `on_exit`, `on_idle`,
`on_job_start`, `on_job_finish`, `on_directory_change`, `on_rest`,
`on_respawn`, `pre_exec`, `post_exec`. The enum above adds `NOT_FOUND`,
`PERMISSION`, `SIGNAL`, `SESSION_START` and `CYCLE` because the brief's
"situations" list (missing executable, permission denied, background job
completion, session start) needs dedicated events to be selected reliably.
Amendments in `docs/DECISIONS.md`.

`cmdline` is truncated to 200 bytes on a character boundary and stripped of
control characters before the event is built; narrative never sees raw bytes
that could move the cursor.

Pack spelling is the enum name lowercased with the `WL_EV_` prefix removed:
`session_start`, `pre_exec`, `post_exec`, `success`, `fail`, `not_found`,
`permission`, `signal`, `dir_change`, `job_start`, `job_finish`, `idle`,
`rest`, `respawn`, `exit`, `cycle`. These strings are the only accepted values
for a pack's `event` field (`docs/CONTENT_FORMAT.md` §4.3).

### Priority

| Priority | Events |
|----------|--------|
| high | `FAIL` (repeated), `REST`, `RESPAWN`, `CYCLE`, `EXIT` |
| normal | `SUCCESS`, `NOT_FOUND`, `PERMISSION`, `SIGNAL`, `DIR_CHANGE`, `JOB_FINISH` |
| low | `POST_EXEC`, `JOB_START`, `IDLE`, `SESSION_START`, `PRE_EXEC` |

High-priority events bypass the base frequency roll once, then obey cooldowns.
Low-priority events are the first to be suppressed when the session has been
noisy.

## 6. Frequency, cooldowns, repetition

Design target: a typical interactive minute produces **0–2 lines**, not one
per command.

Per event dispatch:

```
1. gate:      interactive && world enabled && !plain && persona != NULL
2. gate:      event kind enabled in config
3. budget:    session budget (default 12 lines / 10 min) exhausted?
4. cooldown:  now < persona.cooldown_until[kind]?
5. priority:  high → skip roll; normal/low → roll against effective chance
6. repeat:    candidate id in last-N (N=25) or line text similar to last 3?
              → drop candidate
7. select:    weighted random among remaining candidates, weights from
              persona prefs[kind] × recency penalty
8. if none:   stay silent (this is a normal outcome)
9. render:    substitute placeholders (#{cwd_base}, #{cmd}, #{status})
10. record:   push id to recent ring, set per-kind cooldown
```

Concrete defaults:

| Parameter | Default | Notes |
|-----------|---------|-------|
| base chance (`post_exec`) | 12% | configurable per persona/pack |
| `fail` chance | 45% | failure is more interesting than success |
| `dir_change` chance | 8% | |
| `session_start` | 100% | one short greeting max |
| `exit` | 60% | |
| per-kind cooldown | 90 s | `fail` 45 s, `rest`/`respawn` 5 min |
| global cooldown | 15 s | nothing twice within 15 s |
| recent-line memory | 25 ids | session-scoped |
| near-duplicate window | 3 lines | token Jaccard ≥ 0.8 ⇒ suppress |
| session budget | 12 / 10 min | hard cap, then silence rest of window |
| `on_idle` threshold | 20 s | configurable, off by default |

All of it is configuration, and every default above must be recorded in
`docs/DECISIONS.md` before it is implemented, because defaults are opinions.

Placeholders are deliberately tiny (`#{cwd_base}`, `#{cmd}`, `#{status}`,
`#{cycle}`). No shell evaluation, no format strings from packs, no
`printf`-style specifiers: packs cannot execute anything (see
`docs/CONTENT_FORMAT.md`).

## 7. Selection of the active persona

* `possess <name>` switches explicitly. `possess` with no argument prints the
  current persona. `possess -l` lists loaded personas with class and line
  counts.
* Automatic changes: off by default. If enabled, a *manual* `possess` disables
  auto-selection for the session (the user's choice wins), and re-enabling is
  explicit.
* A persona is never selected if it has no lines for the events that can fire;
  the engine prefers personas with coverage over personas that would be mute.
* `possess` writes no narrative itself beyond a one-line acknowledgement, and
  it never changes the exit status of the previous command (it sets its own,
  like any builtin).

## 8. `rest` and `respawn` scope (design boundary)

`rest` stores, per amendment 9 and the brief: cwd, a whitelisted set of shell
variables, active persona id, narrative state (cycle, recent-line ids,
cooldowns), and a format version. It never claims to store: processes, open
file descriptors, jobs, terminal state, history position, or environment of
running children. `respawn` restores only those fields and says so on stderr
when it cannot restore something it expected.

## 9. Character mechanics (all observational or thin builtins)

* **Alice's Library** — cycle counter; a session is a cycle. Persistent,
  optional, corrupt file ⇒ cycle restarts at 1 with one stderr note.
* **Death screen** — presentation on non-zero status, owned by the shell,
  suppressed by plain mode, never terminates the shell, never alters status.
* **Black Rabbit** — background jobs get a short "it went down the hole" line
  and a `jobs` marker. Job control semantics are untouched.
* **Mirror mode** — may alter only Wonderland-owned presentation (prompt
  fragment, persona text, status glyph). Never piped through arbitrary command
  output; never applied when stdout is not a tty; off by default.
* **`go-to-alice`** — a real builtin sharing `exit`'s logic, with confirmation
  that can be disabled (`WONDERLAND_CONFIRM_EXIT=0` or config `confirm_exit =
  false`). `exit` itself may show a one-line narrative, but is never gated
  behind confirmation when non-interactive.

## 10. Line and pack targets

| Class | Target lines | Wave-1 target |
|-------|--------------|---------------|
| Main characters | 200–300+ | 12 (vertical slice, M3.5) → 60 by M6 |
| Major philosophers | 100–200+ | 12 (slice) → 40 by M6 |
| Secondary | 50–100+ | after M7 |

The vertical slice is deliberately tiny: one persona (Alice), ~12 lines, the
`post_exec` hook only, proving the wiring end to end. Quality rules apply from
line one; no filler, no "generated 300 variants" packs in this repository.

## 11. Content status interaction

Every line carries `status` ∈ {verified, unverified, original} and `category` ∈
{canonical, quotation, original}. Persona selection treats them as follows:

* `original` — always eligible.
* `quotation` + `verified` — eligible; author/work may be shown on request.
* `quotation` + `unverified` — eligible only when `allow_unverified = true`
  (default false). Never labelled as canonical, never shown as a verified
  quote.
* `canonical` — only ever with `status = verified`; a pack that claims
  otherwise is rejected at load time (amendment 5). No canonical lines ship in
  this repository.

## 12. Open questions

* Whether `possess` should accept pack-qualified ids (`alice@core`) from day
  one, or ids stay globally unique.
* Whether the idle timer belongs to the line editor or the main loop.
* Whether a persona may react to *another* persona's presence in M6 or only
  after the cast is larger.
* Whether session budget should be wall-clock or prompt-count based (wall
  clock is proposed).
* Whether `exit`'s narrative line can be disabled separately from `exit`
  confirmation.
