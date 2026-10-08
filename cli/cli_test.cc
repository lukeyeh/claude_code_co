// claude_code::Start by example. A shell script (fake_claude.sh) plays the
// `claude` program: it reports how it was started, and answers each prompt
// with a count of the prompts so far.

#include "cli/cli.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "async/task.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "json/json.h"
#include "net/event_loop.h"
#include "net/stream.h"
#include "protocol/message.h"
#include "session/session.h"

namespace {

using absl_testing::StatusIs;
using claude_code::Message;
using testing::ElementsAre;

void RunOnEventLoop(Task<> test) {
  absl::StatusOr<EventLoop> loop = EventLoop::Create();
  ABSL_ASSERT_OK(loop);
  loop->Run(std::move(test));
}

net::Deadline Soon() { return net::After(std::chrono::seconds(5)); }

// Where Bazel puts the script for the test to run.
std::string FakeClaude() {
  return std::string(std::getenv("TEST_SRCDIR")) + "/_main/cli/fake_claude.sh";
}

// How the fake program says it was started: the first thing it writes.
struct Started {
  std::vector<std::string> arguments;
  std::string directory;
};

Task<Started> StartFake(claude_code::Options options) {
  options.program = FakeClaude();
  const absl::StatusOr<std::unique_ptr<claude_code::Session>> session =
      claude_code::Start(options);
  ABSL_EXPECT_OK(session);
  if (!session.ok()) co_return Started{};

  const absl::StatusOr<Message> message = co_await (*session)->Next(Soon());
  ABSL_EXPECT_OK(message);
  if (!message.ok()) co_return Started{};
  EXPECT_EQ(message->kind(), Message::Kind::kOther);
  if (message->kind() != Message::Kind::kOther) co_return Started{};
  const json::Value& report = message->other().raw;

  Started started{
      .directory = report["directory"].AsString(),
  };
  for (const json::Value& argument : report["arguments"].items()) {
    started.arguments.push_back(argument.AsString());
  }
  co_return started;
}

// Matches a message that ends a turn with the answer `text`.
MATCHER_P(IsResult, text, "") {
  return arg.ok() && arg->kind() == Message::Kind::kResult &&
         arg->result().text == text;
}

// The session Start gives back is a conversation with the program, for as
// many turns as wanted.
TEST(StartTest, StartsAProgramThatCanBeTalkedTo) {
  RunOnEventLoop([]() -> Task<> {
    const absl::StatusOr<std::unique_ptr<claude_code::Session>> session =
        claude_code::Start({
            .program = FakeClaude(),
        });
    ABSL_EXPECT_OK(session);
    if (!session.ok()) co_return;
    // The fake's report of how it was started.
    ABSL_EXPECT_OK(co_await (*session)->Next(Soon()));

    ABSL_EXPECT_OK(co_await (*session)->Send("first"));
    EXPECT_THAT(co_await (*session)->Next(Soon()), IsResult("heard 1"));

    ABSL_EXPECT_OK(co_await (*session)->Send("second"));
    EXPECT_THAT(co_await (*session)->Next(Soon()), IsResult("heard 2"));
  }());
}

// With nothing asked for, the program is told only to hold a conversation
// in stream-json: everything else is left to the user's own settings.
TEST(StartTest, AsksForAConversationAndNothingElseByDefault) {
  RunOnEventLoop([]() -> Task<> {
    const Started started = co_await StartFake({});

    EXPECT_THAT(started.arguments,
                ElementsAre("--print", "--input-format=stream-json",
                            "--output-format=stream-json", "--verbose"));
    EXPECT_EQ(started.directory,
              std::filesystem::canonical(std::filesystem::current_path()));
  }());
}

// Each option becomes the flag the program knows it by.
TEST(StartTest, TurnsOptionsIntoFlags) {
  RunOnEventLoop([]() -> Task<> {
    const Started started = co_await StartFake({
        .model = "haiku",
        .instructions = "Be brief.",
        .permissions = claude_code::Permissions::kAsk,
        .allowed_tools =
            {
                "Read",
                "Bash(git log:*)",
            },
        .resume = "abc",
        .text_deltas = true,
    });

    EXPECT_THAT(started.arguments,
                ElementsAre("--print", "--input-format=stream-json",
                            "--output-format=stream-json", "--verbose",
                            "--model=haiku", "--append-system-prompt=Be brief.",
                            "--permission-prompt-tool=stdio",
                            "--allowedTools=Read,Bash(git log:*)",
                            "--resume=abc", "--include-partial-messages"));
  }());
}

// The other permissions are modes of the program's own.
TEST(StartTest, NamesThePermissionMode) {
  RunOnEventLoop([]() -> Task<> {
    EXPECT_EQ((co_await StartFake({
                   .permissions = claude_code::Permissions::kAuto,
               }))
                  .arguments.back(),
              "--permission-mode=auto");
    EXPECT_EQ((co_await StartFake({
                   .permissions = claude_code::Permissions::kAcceptEdits,
               }))
                  .arguments.back(),
              "--permission-mode=acceptEdits");
    EXPECT_EQ((co_await StartFake({
                   .permissions = claude_code::Permissions::kPlan,
               }))
                  .arguments.back(),
              "--permission-mode=plan");
    EXPECT_EQ((co_await StartFake({
                   .permissions = claude_code::Permissions::kBypass,
               }))
                  .arguments.back(),
              "--permission-mode=bypassPermissions");
  }());
}

// Claude works in the directory it is started in.
TEST(StartTest, StartsInTheGivenDirectory) {
  RunOnEventLoop([]() -> Task<> {
    const std::filesystem::path directory =
        std::filesystem::canonical(std::getenv("TEST_TMPDIR"));

    const Started started = co_await StartFake({
        .directory = directory,
    });

    EXPECT_EQ(started.directory, directory);
  }());
}

// A program that is not installed is known at once.
TEST(StartTest, FailsWhenTheProgramIsNotThere) {
  RunOnEventLoop([]() -> Task<> {
    EXPECT_THAT(claude_code::Start({
                    .program = "no-such-claude-anywhere",
                }),
                StatusIs(absl::StatusCode::kNotFound));
    co_return;
  }());
}

// A program that starts and then gives up, as the real one does when it is
// not logged in, shows as a conversation that is over.
TEST(StartTest, AProgramThatGivesUpEndsTheConversation) {
  RunOnEventLoop([]() -> Task<> {
    const absl::StatusOr<std::unique_ptr<claude_code::Session>> session =
        claude_code::Start({
            .program = "false",
        });
    ABSL_EXPECT_OK(session);
    if (!session.ok()) co_return;

    EXPECT_THAT(co_await (*session)->Next(Soon()),
                StatusIs(absl::StatusCode::kUnavailable));
  }());
}

// -----------------------------------------------------------------------------
// Benchmarks
// -----------------------------------------------------------------------------
//
//   bazel run -c opt //cli:cli_test -- --benchmark_filter=all

Task<> StartAndHear(benchmark::State& state) {
  for (auto _ : state) {
    const absl::StatusOr<std::unique_ptr<claude_code::Session>> session =
        claude_code::Start({
            .program = "true",
        });
    if (!session.ok()) {
      state.SkipWithError("cannot run a program");
      co_return;
    }

    // The program exiting, which is as soon as it can be heard from.
    benchmark::DoNotOptimize(co_await (*session)->Next(Soon()));
  }
}

// Starting a program and seeing it end: what this library adds to however
// long `claude` itself takes to start.
void BM_StartAProgram(benchmark::State& state) {
  EventLoop::Create()->Run(StartAndHear(state));
}
BENCHMARK(BM_StartAProgram);

}  // namespace
