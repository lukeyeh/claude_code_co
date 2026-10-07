load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# The messages exchanged with Claude Code and how they are written. Nothing
# here waits or does I/O.
package(default_visibility = [
    "//:__pkg__",
    "//cli:__pkg__",
    "//session:__pkg__",
])

cc_library(
    name = "protocol",
    srcs = ["message.cc"],
    hdrs = ["message.h"],
    deps = [
        "@abseil-cpp",
        "@bedrock//:json",
    ],
)

cc_test(
    name = "message_test",
    size = "small",
    srcs = ["message_test.cc"],
    deps = [
        ":protocol",
        "@abseil-cpp",
        "@bedrock//:json",
        "@bedrock//:testing_main",
        "@gbenchmark",
        "@gtest",
    ],
)
