load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# A conversation with Claude Code over a stream. Nothing here knows how
# Claude Code is started, or how its messages are written.
package(default_visibility = [
    "//:__pkg__",
    "//cli:__pkg__",
])

cc_library(
    name = "session",
    srcs = ["session.cc"],
    hdrs = ["session.h"],
    deps = [
        "//protocol",
        "@abseil-cpp",
        "@bedrock//:async",
        "@bedrock//:net",
    ],
)

cc_test(
    name = "session_test",
    size = "small",
    srcs = ["session_test.cc"],
    deps = [
        ":session",
        "//protocol",
        "@abseil-cpp",
        "@bedrock//:async",
        "@bedrock//:json",
        "@bedrock//:net",
        "@bedrock//:testing_main",
        "@gbenchmark",
        "@gtest",
    ],
)
