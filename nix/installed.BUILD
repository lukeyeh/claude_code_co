# claude_code_co as a project that uses it sees it: one library. Depend on
# "@claude_code_co//:claude_code" and include "cli/cli.h",
# "session/session.h" and "protocol/message.h".
#
# This file is installed with the package (see package.nix), and is the BUILD
# file of the repository that rules_nixpkgs makes of it. It is evaluated
# there, where other repositories are not known by their short names, so it
# names them canonically. That fixes what the using project must call them in
# its MODULE.bazel: "abseil-cpp" and "bedrock".
load("@@rules_cc+//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

BEDROCK = "@@rules_nixpkgs_core++nix_pkg+bedrock//"

cc_library(
    name = "claude_code",
    srcs = ["lib/libclaude_code.a"],
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    deps = [
        BEDROCK + ":async",
        BEDROCK + ":json",
        BEDROCK + ":net",
        BEDROCK + ":os",
        "@@rules_nixpkgs_core++nix_pkg+abseil-cpp//:abseil-cpp",
    ],
)
