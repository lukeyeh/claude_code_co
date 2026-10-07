load("@rules_cc//cc:cc_library.bzl", "cc_library")

# For rules_nixpkgs, which reads the revision of bedrock from it.
exports_files(["flake.lock"])

# The whole library: start Claude Code (cli/cli.h), talk to it
# (session/session.h), and understand what it says (protocol/message.h).
cc_library(
    name = "claude_code",
    visibility = ["//visibility:public"],
    deps = [
        "//cli",
        "//protocol",
        "//session",
    ],
)

alias(
    name = "compile_commands",
    actual = "@wolfd_bazel_compile_commands//:generate_compile_commands",
)
