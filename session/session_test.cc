// claude_code::Session by example: a conversation with Claude Code, in which
// the test plays Claude Code at the other end of a loopback connection.

#include "session/session.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "async/task.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "json/json.h"
#include "net/event_loop.h"
#include "net/reader.h"
#include "net/stream.h"
#include "protocol/message.h"

namespace {

using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;
using claude_code::Message;

void RunOnEventLoop(Task<> test) {
  absl::StatusOr<EventLoop> loop = EventLoop::Create();
  ABSL_ASSERT_OK(loop);
  loop->Run(std::move(test));
}

net::Deadline Soon() { return net::After(std::chrono::seconds(5)); }

// The session under test, and the stream the test plays Claude Code on.
struct Conversation {
  std::unique_ptr<claude_code::Session> session;
  std::unique_ptr<net::Stream> claude;
};

Task<Conversation> Connect() {
  const absl::StatusOr<net::Listener> listener = net::Listener::OnLoopback();
  ABSL_EXPECT_OK(listener);
  absl::StatusOr<std::unique_ptr<net::Stream>> ours =
      co_await net::Dial(listener->address(), Soon());
  ABSL_EXPECT_OK(ours);
  absl::StatusOr<std::unique_ptr<net::Stream>> theirs =
      co_await listener->Accept();
  ABSL_EXPECT_OK(theirs);

  co_return Conversation{
      .session = std::make_unique<claude_code::Session>(std::move(*ours)),
      .claude = std::move(*theirs),
  };
}

// The next line the session wrote, as Claude Code would read it.
Task<json::Value> ReadLine(net::Stream& claude) {
  net::Reader reader(&claude);
  const absl::StatusOr<std::string_view> line =
      co_await reader.ReadUntil("\n", 10000, Soon());
  ABSL_EXPECT_OK(line);
  if (!line.ok()) co_return json::Value();

  const absl::StatusOr<json::Value> document = json::Parse(*line);
  ABSL_EXPECT_OK(document);
  co_return document.ok() ? *document : json::Value();
}

constexpr std::string_view kResult =
    R"({"type":"result","subtype":"success","is_error":false,"result":"4"})"
    "\n";

// Matches a message that ends a turn with the answer `text`.
MATCHER_P(IsResult, text, "") {
  return arg.ok() && arg->kind() == Message::Kind::kResult &&
         arg->result().text == text;
}

// A turn: a prompt goes out, and what Claude does about it comes back one
// message at a time, ending with a Result.
TEST(SessionTest, SendsAPromptAndYieldsTheTurn) {
  RunOnEventLoop([]() -> Task<> {
    const Conversation conversation = co_await Connect();

    ABSL_EXPECT_OK(co_await conversation.session->Send("What is 2 + 2?"));
    EXPECT_EQ((co_await ReadLine(*conversation.claude))["message"]["content"]
                  .AsString(),
              "What is 2 + 2?");

    // However Claude Code's writes happen to be split, a message is a line.
    ABSL_EXPECT_OK(co_await conversation.claude->Write(
        R"({"type":"system","subtype":"init","session_id":"abc"})"
        "\n"
        R"({"type":"assistant","message":{"content":[{"type":"te)"));
    ABSL_EXPECT_OK(co_await conversation.claude->Write(R"(xt","text":"4"}]}})"
                                                       "\n"));
    ABSL_EXPECT_OK(co_await conversation.claude->Write(kResult));

    EXPECT_THAT(co_await conversation.session->Next(Soon()),
                IsOkAndHolds(Message(claude_code::Init{
                    .session_id = "abc",
                })));
    EXPECT_THAT(co_await conversation.session->Next(Soon()),
                IsOkAndHolds(Message(claude_code::Assistant{
                    .text = "4",
                })));
    EXPECT_THAT(co_await conversation.session->Next(Soon()), IsResult("4"));
  }());
}

// Waiting for Claude costs no more than the deadline, and a message that
// arrives later is not lost.
TEST(SessionTest, NextGivesUpAtTheDeadlineAndStaysUsable) {
  RunOnEventLoop([]() -> Task<> {
    const Conversation conversation = co_await Connect();

    EXPECT_THAT(co_await conversation.session->Next(
                    net::After(std::chrono::milliseconds(10))),
                StatusIs(absl::StatusCode::kDeadlineExceeded));

    ABSL_EXPECT_OK(co_await conversation.claude->Write(kResult));
    EXPECT_THAT(co_await conversation.session->Next(Soon()), IsResult("4"));
  }());
}

// Lines that are not messages, and Claude Code's acknowledgements, never
// reach the caller: Next goes on to the next real message.
TEST(SessionTest, NextPassesOverWhatIsNotNews) {
  RunOnEventLoop([]() -> Task<> {
    const Conversation conversation = co_await Connect();

    ABSL_EXPECT_OK(co_await conversation.claude->Write(
        "\n"
        "Warning: this is not JSON\n"
        R"({"type":"control_response","response":{"subtype":"success"}})"
        "\n"));
    ABSL_EXPECT_OK(co_await conversation.claude->Write(kResult));

    EXPECT_THAT(co_await conversation.session->Next(Soon()), IsResult("4"));
  }());
}

// Waits for the end of the turn, on behalf of a test that is busy sending.
Task<> AwaitResult(claude_code::Session& session,
                   absl::StatusOr<Message>& result, bool& done) {
  result = co_await session.Next(Soon());
  done = true;
}

// The two directions are independent: a task waiting for Claude does not
// stop another from interrupting it. The interrupted turn ends as any turn
// does, with a Result.
TEST(SessionTest, InterruptsATurnThatIsBeingWaitedOn) {
  RunOnEventLoop([]() -> Task<> {
    const Conversation conversation = co_await Connect();
    absl::StatusOr<Message> result = absl::UnknownError("not yet");
    bool done = false;

    // Nothing has arrived, so this task is left waiting in Next.
    Spawn(AwaitResult(*conversation.session, result, done));
    EXPECT_FALSE(done);

    ABSL_EXPECT_OK(co_await conversation.session->Interrupt());
    const json::Value interruption = co_await ReadLine(*conversation.claude);
    EXPECT_EQ(interruption["type"].AsString(), "control_request");
    EXPECT_EQ(interruption["request"]["subtype"].AsString(), "interrupt");

    // Claude Code acknowledges, and then winds the turn up.
    ABSL_EXPECT_OK(co_await conversation.claude->Write(
        R"({"type":"control_response","response":{"subtype":"success"}})"
        "\n"
        R"({"type":"result","subtype":"error_during_execution",)"
        R"("is_error":true})"
        "\n"));
    // NOLINTNEXTLINE(bugprone-infinite-loop): set by the spawned task.
    while (!done) co_await Sleep(std::chrono::milliseconds(1));

    EXPECT_THAT(result, IsOkAndHolds(Message(claude_code::Result{
                            .is_error = true,
                        })));
  }());
}

// Each interruption is named differently, so that Claude Code can tell a
// second from a repeat of the first.
TEST(SessionTest, NamesEachInterruption) {
  RunOnEventLoop([]() -> Task<> {
    const Conversation conversation = co_await Connect();

    ABSL_EXPECT_OK(co_await conversation.session->Interrupt());
    const json::Value first = co_await ReadLine(*conversation.claude);
    ABSL_EXPECT_OK(co_await conversation.session->Interrupt());
    const json::Value second = co_await ReadLine(*conversation.claude);

    EXPECT_NE(first["request_id"].AsString(), "");
    EXPECT_NE(first["request_id"], second["request_id"]);
  }());
}

// Claude Code asks before using a tool, and waits. The answer names the
// request it is an answer to.
TEST(SessionTest, AnswersRequestsForPermission) {
  RunOnEventLoop([]() -> Task<> {
    const Conversation conversation = co_await Connect();

    ABSL_EXPECT_OK(co_await conversation.claude->Write(
        R"({"type":"control_request","request_id":"req_1","request":{)"
        R"("subtype":"can_use_tool","tool_name":"Bash",)"
        R"("tool_use_id":"use_1","input":{"command":"ls"}}})"
        "\n"));
    const absl::StatusOr<Message> message =
        co_await conversation.session->Next(Soon());
    ABSL_EXPECT_OK(message);
    if (!message.ok()) co_return;

    EXPECT_EQ(message->kind(), Message::Kind::kPermissionRequest);
    if (message->kind() != Message::Kind::kPermissionRequest) co_return;
    const claude_code::PermissionRequest& request =
        message->permission_request();
    EXPECT_EQ(request.tool_use.name, "Bash");
    EXPECT_EQ(request.tool_use.input["command"].AsString(), "ls");

    ABSL_EXPECT_OK(co_await conversation.session->Allow(request));
    const json::Value allowed = co_await ReadLine(*conversation.claude);
    EXPECT_EQ(allowed["response"]["request_id"].AsString(), "req_1");
    EXPECT_EQ(allowed["response"]["response"]["behavior"].AsString(), "allow");

    ABSL_EXPECT_OK(co_await conversation.session->Deny(request, "not now"));
    const json::Value denied = co_await ReadLine(*conversation.claude);
    EXPECT_EQ(denied["response"]["request_id"].AsString(), "req_1");
    EXPECT_EQ(denied["response"]["response"]["behavior"].AsString(), "deny");
    EXPECT_EQ(denied["response"]["response"]["message"].AsString(), "not now");
  }());
}

// Claude Code going away, as it does when it cannot start or is killed, is
// Unavailable: the conversation is over.
TEST(SessionTest, FailsOnceClaudeCodeHasGone) {
  RunOnEventLoop([]() -> Task<> {
    Conversation conversation = co_await Connect();

    conversation.claude.reset();

    EXPECT_THAT(co_await conversation.session->Next(Soon()),
                StatusIs(absl::StatusCode::kUnavailable));
  }());
}

// -----------------------------------------------------------------------------
// Benchmarks
// -----------------------------------------------------------------------------
//
//   bazel run -c opt //session:session_test -- --benchmark_filter=all

Task<> PromptAndResult(benchmark::State& state) {
  const Conversation conversation = co_await Connect();
  net::Reader prompts(conversation.claude.get());

  for (auto _ : state) {
    (co_await conversation.session->Send("What is 2 + 2?")).IgnoreError();
    benchmark::DoNotOptimize(co_await prompts.ReadUntil("\n", 10000, Soon()));

    (co_await conversation.claude->Write(kResult)).IgnoreError();
    benchmark::DoNotOptimize(co_await conversation.session->Next(Soon()));
  }
}

// The shortest possible turn, a prompt and its result: what the session
// itself adds to a conversation, which should be nothing beside Claude.
void BM_PromptAndResult(benchmark::State& state) {
  EventLoop::Create()->Run(PromptAndResult(state));
}
BENCHMARK(BM_PromptAndResult);

}  // namespace
