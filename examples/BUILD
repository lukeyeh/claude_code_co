load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

# Programs that show the library in use.

cc_binary(
    name = "ask",
    srcs = ["ask.cc"],
    deps = [
        "//:claude_code",
        "@abseil-cpp",
        "@bedrock//:async",
        "@bedrock//:json",
        "@bedrock//:net",
    ],
)
