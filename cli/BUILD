load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# Starting the `claude` program. The only code that knows its command line.
package(default_visibility = ["//:__pkg__"])

cc_library(
    name = "cli",
    srcs = ["cli.cc"],
    hdrs = ["cli.h"],
    deps = [
        "//session",
        "@abseil-cpp",
        "@bedrock//:net",
        "@bedrock//:os",
    ],
)

cc_test(
    name = "cli_test",
    size = "small",
    srcs = ["cli_test.cc"],
    data = ["fake_claude.sh"],
    deps = [
        ":cli",
        "//protocol",
        "//session",
        "@abseil-cpp",
        "@bedrock//:async",
        "@bedrock//:json",
        "@bedrock//:net",
        "@bedrock//:testing_main",
        "@gbenchmark",
        "@gtest",
    ],
)
