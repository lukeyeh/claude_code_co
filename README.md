# claude_code_co

Claude Code as a C++20 coroutine library: start the `claude` program and hold
a conversation with it as a stream of messages in both directions. Built on
[bedrock](https://github.com/lukeyeh/bedrock).

```cpp
Task<absl::StatusOr<std::string>> Ask(std::string prompt) {
  CO_ASSIGN_OR_RETURN(const std::unique_ptr<claude_code::Session> session,
                      claude_code::Start({.text_deltas = true}));
  CO_RETURN_IF_ERROR(co_await session->Send(prompt));

  for (;;) {
    CO_ASSIGN_OR_RETURN(
        const claude_code::Message message,
        co_await session->Next(net::After(std::chrono::minutes(10))));

    switch (message.kind()) {
      case claude_code::Message::Kind::kTextDelta:
        std::cout << message.text_delta().text << std::flush;
        break;
      case claude_code::Message::Kind::kResult:
        co_return message.result().text;
      default:
        break;
    }
  }
}
```

While one task waits in `Next`, another can `Send` more, `Interrupt` the
turn, or `Allow` / `Deny` a tool Claude Code asks about. `examples/ask.cc`
is a whole program: `bazel run //examples:ask -- --ask "Add a .gitignore"`.

## Layout

Each directory is a Bazel package, and each depends only on those below it.

| Directory | What it is |
| --- | --- |
| `cli/` | `claude_code::Start`: runs the `claude` program. The only code that knows its flags. |
| `session/` | `claude_code::Session`: the conversation, over any `net::Stream`. |
| `protocol/` | `claude_code::Message` and the stream-json lines it is read from. The only code that knows the format. |

Depend on `//:claude_code` for all three. Each file's `_test.cc` shows how it
is used.

## Setup

Needs [Nix](https://nixos.org) with flakes. Building and testing need nothing
else: the tests use a stand-in for `claude` and no network.

Using the library, and running the example, needs
[Claude Code](https://claude.com/claude-code) installed and logged in: it
runs the `claude` program it finds on `PATH`. The Nix shell does not provide
it. nixpkgs does package it, as `claude-code`, but under an unfree licence
and at whatever version `flake.lock` pins, so this project leaves it to be
installed the usual way.

```
nix develop                  # or direnv allow
bazel test //...
bazel test --config=epoll //...
bazel run :compile_commands  # compile_commands.json, for clangd
```

Run Bazel inside the Nix shell: the compiler comes from there. clangd needs
`--query-driver=/**/*` to find Nix's system headers.

Nix only sees files Git knows about. After adding a file, `git add -N .`
before the next `nix develop` or `bazel` command that reads the flake.

## Dependencies

Libraries come from nixpkgs through `nix/deps.nix`, not the Bazel registry,
and `flake.lock` pins them. To add one: add it to `nix/deps.nix`, then a
`nix_pkg.file` block in `MODULE.bazel` like the ones there. `nix flake update
bedrock` moves to bedrock's latest revision.
