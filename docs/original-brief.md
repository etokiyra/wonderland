WONDERLAND

Project Design & Architecture Brief

Project: Wonderland
Type: Linux Unix shell
Language: C
Target: Linux
Implementation philosophy: libc-only, low-level, native execution
Status: Design phase

⸻

1. Vision

Wonderland is a real, daily-drivable Unix shell written in C.

It must function as a practical shell first. The user should be able to use it for normal Linux work: navigating directories, running programs, managing processes, writing scripts, building software, using Git, and working with the system.

However, Wonderland has a second identity.

Inspired by the atmosphere, characters, and dark fairy-tale themes of Black Souls, it presents the terminal as a strange, inhabited world. The shell has personalities, memories, recurring characters, philosophical commentary, and subtle narrative elements.

The terminal should feel like entering a world rather than opening another command interpreter.

The important distinction:

Wonderland is not a terminal simulator or a game pretending to be a shell. It is a real shell with an additional narrative layer.

The narrative layer must never compromise command execution, compatibility, performance, or reliability.

⸻

2. Technical Foundation

Language and dependencies

* Written in C.
* Target C17 or newer.
* libc-only dependency policy.
* POSIX interfaces are acceptable and necessary.
* No external scripting runtime.
* No mandatory database or external service.
* Must run natively on Linux.

The implementation should remain relatively small, understandable, modular, and maintainable.

Architecture

The shell should be separated into independent subsystems:

1. Terminal interface
2. Input handling and line editor
3. Lexer
4. Parser
5. Abstract Syntax Tree (AST)
6. Expansion engine
7. Command execution engine
8. Process and job control
9. Shell state and environment
10. Builtins
11. History and completion
12. Persona engine
13. Narrative event system
14. Content and persona pack loader
15. Configuration system

The narrative system must be independent of the execution engine.

A persona must never be able to alter a command, its arguments, its environment, its exit status, or its execution flow.

⸻

3. Reuse of PercyLang Infrastructure

I have an existing programming-language project:

https://github.com/etokiyra/percylang

Before implementing Wonderland’s infrastructure from scratch, inspect PercyLang and identify reusable components and design patterns.

Potentially reusable areas:

* Lexer architecture and token representation
* Source positions and source spans
* Diagnostic and error-reporting infrastructure
* AST node conventions and allocation strategies
* Parser architecture and recursive-descent techniques
* AST traversal conventions
* Testing infrastructure
* General memory-management patterns

Do not blindly copy or force PercyLang’s language-specific implementation into Wonderland.

A shell has different grammatical requirements, especially around quoting, expansion, redirection, pipelines, command substitution, and here-documents.

The goal is to reuse proven infrastructure where appropriate while designing a shell-specific grammar and AST.

Before implementation, provide a short reuse audit:

* What can be reused directly?
* What should be adapted?
* What should be redesigned?
* What should not be reused?
* Are there licensing implications of copying existing code?

Do not claim that a component is reusable without actually inspecting its implementation.

⸻

4. Shell Functionality

Wonderland must be a genuine interactive shell.

Essential functionality

Command execution

* Execute external programs using fork/exec.
* Search PATH and resolve executables.
* Preserve normal exit codes.
* Handle environment variables.
* Support shell variables.
* Support $?.
* Support cd, exit, and other essential builtins.

Parsing

Implement a proper lexer and parser with an AST.

Support:

* Single and double quotes
* Escaping
* Variable expansion
* Command substitution $(...)
* Globbing
* Pipes |
* Input redirection <
* Output redirection >
* Append redirection >>
* Standard error redirection
* File descriptor duplication such as 2>&1
* Command chaining with &&, ||, and ;
* Background execution with &
* Here-documents

Do not build the shell around string splitting or simplistic tokenization.

Quoting and expansion must preserve the distinction between quoted and unquoted content.

Process and job control

Support:

* Foreground and background processes
* jobs
* fg
* bg
* Ctrl+C
* Ctrl+Z
* Process groups
* Terminal ownership
* Signal handling
* Proper child-process reaping

Use appropriate POSIX process and terminal interfaces.

Signal handlers should perform minimal work. More complicated state updates should happen in the main event loop.

Interactive experience

Implement:

* Raw-mode input handling
* Command history
* Arrow-key navigation
* Tab completion
* Basic line editing
* Configurable prompt
* Terminal resize handling
* Proper restoration of terminal settings

The shell must remain usable over SSH and in ordinary terminal emulators.

⸻

5. Compatibility Strategy

Compatibility should be approached incrementally.

Phase 1: POSIX-style shell

Implement a solid POSIX-oriented command language.

Phase 2: Selected Bash extensions

Once the foundation is reliable, consider:

* [[ ... ]]
* Arrays
* Extended parameter expansion, including ${var//a/b}
* Process substitution
* Other commonly used Bash conveniences

Do not sacrifice architectural quality for premature compatibility.

Scripts beginning with:

#!/bin/bash

must run under the actual Bash interpreter when invoked normally. Wonderland should not pretend to be Bash or silently claim compatibility it does not have.

Build a shell test suite covering parsing, expansion, execution, redirection, job control, and compatibility.

Where practical, compare expected behavior against established shells.

⸻

6. Wonderland’s Identity

The shell’s atmosphere draws inspiration from:

* Black Souls
* Alice in Wonderland
* Dark fairy tales
* Philosophical literature
* Existentialism
* Psychological horror
* The idea of a world that remembers its inhabitants

The aesthetic should be dark, mysterious, elegant, and restrained.

Avoid excessive ASCII art, constant roleplay, childish fantasy language, and unnecessary theatrical messages.

The user should feel that the terminal has a personality, not that it is constantly trying to entertain them.

The lore must remain optional.

Support:

* --plain
* ABGRUND_PLAIN=1 should be replaced with WONDERLAND_PLAIN=1

Plain mode must disable all narrative output and decorative behavior.

The shell must also detect whether stdout is a TTY. Narrative output must never contaminate pipelines, redirected output, or scripts.

⸻

7. The Persona System

This is one of the most important parts of Wonderland.

I do not want a shell that occasionally prints one of five random quotes.

I want a substantial cast of distinct personalities, each with a recognizable worldview, voice, behavior, vocabulary, and large collection of contextual reactions.

The personas should feel like inhabitants of Wonderland.

Initial personas

The following are starting examples, not a final restricted list.

Wonderland characters

* Alice
* Grimm
* Red Hood
* Other characters inspired by the world and atmosphere of Black Souls

Philosophers and literary figures

* Friedrich Nietzsche
* Max Stirner
* Fyodor Dostoyevsky
* Franz Kafka
* Søren Kierkegaard
* Albert Camus
* Emil Cioran
* Arthur Schopenhauer
* Socrates
* Other suitable philosophers and writers

The system should eventually support 20–30 or more personas.

The selection should be deliberate. Each character needs a distinct identity rather than simply having a different name attached to generic text.

Persona identity

Every persona should have:

* Name
* Description
* Personality
* Worldview
* Vocabulary and speaking style
* Mood tendencies
* Preferred situations
* Reactions to success and failure
* Reactions to specific command categories
* Commentary frequency
* Verbosity preference
* Collection of narrative lines
* Optional character-specific mechanics

For example:

Nietzsche should not react like Kafka.

Alice should not sound like Dostoyevsky.

Cioran should not sound like Grimm.

The distinction must be recognizable even without displaying the persona’s name.

Many variations per persona

This is a strict design requirement.

Each persona needs a substantial collection of lines and variations.

I want hundreds of lines for important personas eventually, not a handful of generic responses.

However, content should be developed in meaningful batches rather than generated as enormous collections of repetitive filler.

Suggested long-term content targets:

Persona category	Target lines per persona
Main characters	200–300+
Major philosophers	100–200+
Secondary characters	50–100+

These are targets, not requirements to fill the database with low-quality text.

Every line should have context and purpose.

Examples of situations:

* Successful command execution
* Failed command execution
* Missing executable
* Permission denied
* Broken build
* Successful compilation
* Long-running process
* Background job completion
* Repeated failures
* Entering a new directory
* Starting a new shell session
* Exiting Wonderland
* Restoring a checkpoint
* Idle periods

The same event should produce different reactions depending on the active persona.

For example, after a failed compilation:

* Nietzsche might interpret the failure as an opportunity to overcome oneself.
* Kafka might describe the error as another absurd bureaucratic obstacle.
* Cioran might express pessimism about the entire undertaking.
* Alice might react with curiosity or confusion.
* Grimm might offer something unsettling and cryptic.

These are examples of intended differences, not final dialogue.

⸻

8. Canonical, Historical, and Original Content

The content system must distinguish three categories.

1. Canonical content

Actual dialogue or material from Black Souls.

* Must be authentic.
* Must have a known source where possible.
* Must not be fabricated and presented as canon.
* Must respect copyright and redistribution restrictions.

2. Historical and literary quotations

Actual quotations from philosophers and authors.

Each quotation should include:

* Author
* Original work
* Source information
* Translation information where relevant
* Whether the quotation is verified

Do not attribute invented quotations to real philosophers.

Be careful with modern translations of public-domain works, as translations can have their own copyright.

3. Original content

Newly written lines inspired by a character’s worldview or atmosphere.

These should be clearly marked internally as original.

Do not present original writing as an authentic quotation from Nietzsche, Kafka, Alice, or any other source.

The system should support all three categories independently.

⸻

9. Persona Behavior and Commentary

Personas should react to events through a narrative event system.

Potential events:

* pre_exec
* post_exec
* on_success
* on_fail
* on_exit
* on_idle
* on_job_start
* on_job_finish
* on_directory_change
* on_rest
* on_respawn

These events must be observational.

They may read event information and produce commentary, but they must never change shell execution.

Commentary frequency

Do not print something after every command.

That would quickly become irritating.

Instead:

* Use contextual reactions.
* Introduce cooldowns.
* Avoid repeating recently displayed lines.
* Consider recent shell history when selecting reactions.
* Give some events higher narrative priority than others.
* Allow personas to remain silent.
* Make commentary frequency configurable.

A persona should feel alive, not noisy.

Persona selection

Introduce a command such as:

possess <name>

This changes the active persona.

Potential future behavior:

* Personas can be selected manually.
* A persona can occasionally change according to configured behavior.
* Some personas may react to another persona’s presence.
* Certain events may trigger rare character-specific commentary.

The user should always be able to disable automatic changes.

⸻

10. Persona Content Packs

Do not hardcode all narrative content directly into the C source.

Create an extensible content-pack system.

Users should eventually be able to:

* Add personas
* Remove personas
* Create their own personas
* Add new lines
* Add new reactions
* Define event preferences
* Configure commentary frequency
* Create custom persona collections

The format should be human-readable and easy to edit.

JSON, TOML, or another appropriate format may be evaluated, but avoid unnecessary dependencies under the libc-only policy. A small custom format or a simple text-based format may be preferable.

The loader must validate content and handle malformed packs safely.

Keep persona data separate from the shell’s execution code.

⸻

11. Special Wonderland Mechanics

These mechanics are thematic additions. They must never interfere with normal shell behavior.

Alice’s Library

A shell session represents a cycle in Wonderland.

The shell can maintain a persistent cycle counter.

The world remembers previous sessions, even if the user does not.

Potential features:

* Session count
* Previous session information
* Persistent narrative state
* Rare events based on previous cycles

Persistence should be simple, local, and optional.

Death and failure

A nonzero exit status may trigger a narrative death-screen-style presentation.

However:

* The shell must not terminate because a command failed.
* The real exit status must remain unchanged.
* The presentation must be configurable.
* Plain mode must suppress it.

A failed command is not a failed shell session.

rest

A checkpoint mechanic.

The command:

rest

should save a snapshot of selected shell state, such as:

* Current working directory
* Relevant shell variables
* Session information
* Active persona
* Narrative state

A future respawn mechanic may restore that snapshot.

Do not promise restoration of arbitrary processes, open file descriptors, or complete system state. This is a shell-state checkpoint, not process resurrection.

Black Rabbit

Background jobs can be represented thematically as independent activities in Wonderland.

The persona system may provide special commentary or visual treatment when background jobs start or finish.

The actual job-control behavior must remain standard.

Mirror mode

A mirror mode may reverse or distort shell-owned presentation.

Do not blindly reverse arbitrary command output.

That would break text interfaces, programs, scripts, and interactive applications.

If implemented, it should operate only on controlled Wonderland presentation.

Exiting Wonderland

A themed exit command may be called:

go-to-alice

The ordinary exit command may also trigger a configurable confirmation or narrative event.

Do not implement this as a fragile alias. It should be handled by the appropriate builtin logic.

The user must always be able to exit normally, with a way to disable confirmation.

⸻

12. Visual Design

The terminal should feel like a dark, elegant fairy-tale environment.

Suggested direction:

* Deep charcoal and near-black backgrounds
* Muted crimson
* Ivory or pale text
* Restrained decorative symbols
* Subtle character indicators
* Elegant prompt design
* Minimal but distinctive status information

The prompt should remain practical and readable.

Do not let the aesthetic interfere with command output or terminal applications.

The design should work in ordinary terminal emulators without requiring a custom terminal.

⸻

13. Reliability and Safety Requirements

These are non-negotiable.

1. Narrative code must never modify execution behavior.
2. Persona hooks must never change exit codes.
3. Narrative output must never contaminate pipelines or redirected output.
4. Plain mode must genuinely disable narrative behavior.
5. The shell must restore terminal settings after interruptions and abnormal exits where possible.
6. Child processes must be managed correctly.
7. Signal handling must not perform unsafe complex operations.
8. Malformed persona packs must not crash the shell.
9. Persistent state must be recoverable or safely ignored if corrupted.
10. Interactive applications must receive proper terminal control.
11. Memory ownership must be explicit and carefully managed.
12. Errors must be reported clearly.
13. The shell must not silently claim compatibility it does not have.

⸻

14. Development Milestones

Development should happen incrementally.

Do not attempt to implement the entire project in one enormous step.

Milestone 1: Foundation

* Project structure
* Build system
* Terminal initialization
* Main loop
* Basic prompt
* Builtin cd and exit
* Basic external command execution

Milestone 2: Lexer, Parser, and AST

* Tokenization
* Quoting
* Source positions
* Parser
* AST representation
* Diagnostics
* Parser tests

Inspect PercyLang before deciding how much infrastructure can be reused.

Milestone 3: Execution

* Variable expansion
* PATH resolution
* Pipes
* Redirections
* Command chaining
* Background execution
* Command substitution
* Globbing

Milestone 4: Job Control

* Process groups
* Foreground/background jobs
* jobs
* fg
* bg
* Signal handling
* Terminal ownership

Milestone 5: Interactive Experience

* Raw-mode editor
* History
* Completion
* Keyboard handling
* Prompt configuration

Milestone 6: Narrative Foundation

* Event system
* Persona representation
* Persona selection
* Content-pack loader
* Commentary selection
* Cooldowns and repetition avoidance
* Plain mode

Milestone 7: Wonderland Identity

* Alice’s Library
* Cycle tracking
* rest
* Respawn
* Black Rabbit
* Mirror presentation
* Character-specific events

Milestone 8: Compatibility and Polish

* Selected Bash extensions
* Extensive test suite
* Interactive testing
* Memory and resource audits
* Documentation
* Installation
* Configuration examples
* Example persona packs

⸻

15. Working Method

I want this project developed carefully.

For each milestone:

1. Inspect the existing repository and architecture.
2. Explain the proposed implementation briefly.
3. Implement one coherent feature at a time.
4. Add tests for the feature.
5. Run the relevant test suite.
6. Check for regressions.
7. Review memory management and error handling.
8. Report changed files and architectural decisions.
9. Report known limitations honestly.
10. Provide a clear handoff before moving to the next milestone.

Do not implement speculative features just because they sound interesting.

Do not create unnecessary abstractions.

Do not sacrifice shell reliability for the narrative layer.

The goal is a shell that can genuinely replace a normal interactive shell for everyday Linux work, while providing an experience that feels unmistakably like Wonderland.

⸻

16. First Task

Do not begin implementing the entire shell yet.

Start with an architectural and repository audit.

Specifically:

1. Inspect PercyLang and identify reusable infrastructure.
2. Propose Wonderland’s repository structure.
3. Propose the shell’s AST and grammar architecture.
4. Explain how execution and narrative systems will remain isolated.
5. Propose the persona/content-pack data model.
6. Identify the biggest technical risks.
7. Recommend a realistic development order.

Present the findings before making large architectural changes.

The central principle: Wonderland must be a proper shell first, and a living, inhabited world second.
