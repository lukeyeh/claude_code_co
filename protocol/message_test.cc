// The stream-json format by example: what each line Claude Code writes means
// as a Message, and what the lines written to it look like.

#include "protocol/message.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <optional>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "json/json.h"

namespace {

using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;
using claude_code::Message;
using testing::Eq;
using testing::Optional;

// What a line written to Claude Code says, with its newline checked and
// taken off.
json::Value Written(std::string_view line) {
  EXPECT_TRUE(line.ends_with("\n"));
  EXPECT_EQ(line.find('\n'), line.size() - 1) << "more than one line";
  const absl::StatusOr<json::Value> document = json::Parse(line);
  ABSL_EXPECT_OK(document);

  return document.ok() ? *document : json::Value();
}

json::Value Json(std::string_view text) {
  const absl::StatusOr<json::Value> document = json::Parse(text);
  ABSL_EXPECT_OK(document);

  return document.ok() ? *document : json::Value();
}

// Every turn opens with how Claude Code is set up, including the session id
// that a later conversation can resume by.
TEST(DecodeTest, ReadsHowTheSessionIsSetUp) {
  EXPECT_THAT(
      claude_code::Decode(
          R"({"type":"system","subtype":"init","cwd":"/work",)"
          R"("session_id":"abc","tools":["Bash","Read"],"model":"haiku"})"),
      IsOkAndHolds(Optional(Message(claude_code::Init{
          .session_id = "abc",
          .model = "haiku",
          .directory = "/work",
          .tools =
              {
                  "Bash",
                  "Read",
              },
      }))));
}

// What Claude says and what it asks to do arrive together. Blocks that are
// neither, such as its thinking, are left out.
TEST(DecodeTest, ReadsWhatClaudeSaidAndAskedToDo) {
  EXPECT_THAT(claude_code::Decode(R"({"type":"assistant","message":{"content":[
        {"type":"thinking","thinking":"hm"},
        {"type":"text","text":"Let me look."},
        {"type":"tool_use","id":"use_1","name":"Bash",
         "input":{"command":"ls"}}
      ]}})"),
              IsOkAndHolds(Optional(Message(claude_code::Assistant{
                  .text = "Let me look.",
                  .tool_uses =
                      {
                          claude_code::ToolUse{
                              .id = "use_1",
                              .name = "Bash",
                              .input = Json(R"({"command":"ls"})"),
                          },
                      },
              }))));
}

// A tool's output comes back as plain text or as a list of blocks; either
// way it reads as text.
TEST(DecodeTest, ReadsToolResultsInEitherForm) {
  EXPECT_THAT(claude_code::Decode(R"({"type":"user","message":{"content":[
        {"type":"tool_result","tool_use_id":"use_1","content":"a.txt",
         "is_error":false},
        {"type":"tool_result","tool_use_id":"use_2","is_error":true,
         "content":[{"type":"text","text":"no such "},
                    {"type":"text","text":"file"}]}
      ]}})"),
              IsOkAndHolds(Optional(Message(claude_code::ToolResults{
                  .results =
                      {
                          claude_code::ToolResult{
                              .tool_use_id = "use_1",
                              .output = "a.txt",
                          },
                          claude_code::ToolResult{
                              .tool_use_id = "use_2",
                              .output = "no such file",
                              .is_error = true,
                          },
                      },
              }))));
}

// With partial messages switched on, Claude's words arrive as it says them.
TEST(DecodeTest, ReadsTextAsItIsSaid) {
  EXPECT_THAT(
      claude_code::Decode(
          R"({"type":"stream_event","event":{"type":"content_block_delta",)"
          R"("index":0,"delta":{"type":"text_delta","text":"Hel"}}})"),
      IsOkAndHolds(Optional(Message(claude_code::TextDelta{
          .text = "Hel",
      }))));
}

// The end of a turn carries the answer and what the conversation has cost.
TEST(DecodeTest, ReadsTheEndOfATurn) {
  EXPECT_THAT(claude_code::Decode(
                  R"({"type":"result","subtype":"success","is_error":false,)"
                  R"("result":"42","session_id":"abc","total_cost_usd":0.25,)"
                  R"("num_turns":3,"duration_ms":1500})"),
              IsOkAndHolds(Optional(Message(claude_code::Result{
                  .text = "42",
                  .is_error = false,
                  .session_id = "abc",
                  .cost_usd = 0.25,
                  .turns = 3,
                  .duration = std::chrono::milliseconds(1500),
              }))));
}

// Claude Code asking leave to use a tool names the request, for the answer,
// and says what would be done.
TEST(DecodeTest, ReadsARequestForPermission) {
  EXPECT_THAT(
      claude_code::Decode(
          R"({"type":"control_request","request_id":"req_7","request":{)"
          R"("subtype":"can_use_tool","tool_name":"Bash",)"
          R"("tool_use_id":"use_1","input":{"command":"rm -rf /"}}})"),
      IsOkAndHolds(Optional(Message(claude_code::PermissionRequest{
          .id = "req_7",
          .tool_use =
              claude_code::ToolUse{
                  .id = "use_1",
                  .name = "Bash",
                  .input = Json(R"({"command":"rm -rf /"})"),
              },
      }))));
}

// Claude Code says a good deal more than this library has names for. All of
// it is kept, for whoever wants it.
TEST(DecodeTest, KeepsWhatItHasNoNameFor) {
  const std::string_view line =
      R"({"type":"system","subtype":"status","status":"requesting"})";

  EXPECT_THAT(claude_code::Decode(line),
              IsOkAndHolds(Optional(Message(claude_code::Other{
                  .type = "system",
                  .subtype = "status",
                  .raw = Json(line),
              }))));
}

// The acknowledgement of a request made of Claude Code is not a message.
TEST(DecodeTest, HasNothingToSayOfAnAcknowledgement) {
  EXPECT_THAT(
      claude_code::Decode(
          R"({"type":"control_response","response":{"subtype":"success",)"
          R"("request_id":"interrupt-1"}})"),
      IsOkAndHolds(Eq(std::nullopt)));
}

// A message is one kind of thing, and says which: this is what a switch over
// the messages of a turn goes by.
TEST(MessageTest, SaysWhichKindItIs) {
  const Message message(claude_code::Result{
      .text = "42",
  });

  EXPECT_EQ(message.kind(), Message::Kind::kResult);
  EXPECT_EQ(message.result().text, "42");
}

// Asking a message for contents of a kind it is not is a mistake in the
// program, caught on the spot.
TEST(MessageDeathTest, StopsAProgramThatAsksForAnotherKind) {
  const Message message(claude_code::Result{});

  EXPECT_DEATH(message.assistant(), "another kind");
}

// A line that is not a message is an error, not a guess.
TEST(DecodeTest, RejectsWhatIsNotAMessage) {
  EXPECT_THAT(claude_code::Decode("Warning: something"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(claude_code::Decode(R"({"no":"type"})"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// A prompt is one line however many it holds itself: the newlines inside it
// are escaped.
TEST(EncodeTest, WritesAPromptAsOneLine) {
  EXPECT_EQ(Written(claude_code::EncodePrompt("first\nsecond \"quoted\"")),
            Json(R"({"type":"user","message":{
              "role":"user","content":"first\nsecond \"quoted\""}})"));
}

// An interruption is a request, named so that its acknowledgement can be
// told from another's.
TEST(EncodeTest, WritesAnInterruption) {
  EXPECT_EQ(Written(claude_code::EncodeInterrupt("interrupt-1")),
            Json(R"({"type":"control_request","request_id":"interrupt-1",
              "request":{"subtype":"interrupt"}})"));
}

// Allowing a tool hands its arguments back with the answer, and names the
// request being answered.
TEST(EncodeTest, WritesPermissionGranted) {
  const claude_code::PermissionRequest request{
      .id = "req_7",
      .tool_use =
          claude_code::ToolUse{
              .id = "use_1",
              .name = "Bash",
              .input = Json(R"({"command":"ls"})"),
          },
  };

  EXPECT_EQ(Written(claude_code::EncodeAllow(request)),
            Json(R"({"type":"control_response","response":{
              "subtype":"success","request_id":"req_7","response":{
                "behavior":"allow","updatedInput":{"command":"ls"}}}})"));
}

// Refusing one says why, which is what Claude is told.
TEST(EncodeTest, WritesPermissionRefused) {
  const claude_code::PermissionRequest request{
      .id = "req_7",
  };

  EXPECT_EQ(Written(claude_code::EncodeDeny(request, "not on my machine")),
            Json(R"({"type":"control_response","response":{
              "subtype":"success","request_id":"req_7","response":{
                "behavior":"deny","message":"not on my machine"}}})"));
}

// -----------------------------------------------------------------------------
// Benchmarks
// -----------------------------------------------------------------------------
//
//   bazel run -c opt //protocol:message_test -- --benchmark_filter=all

// Decoding the commonest line by far when partial messages are on: one is
// read for every few characters Claude says.
void BM_DecodeTextDelta(benchmark::State& state) {
  const std::string_view line =
      R"({"type":"stream_event","event":{"type":"content_block_delta",)"
      R"("index":0,"delta":{"type":"text_delta","text":"Hello there"}},)"
      R"("session_id":"9763a570-2762-467d-86dc-810c745731cd"})";

  for (auto _ : state) benchmark::DoNotOptimize(claude_code::Decode(line));
}
BENCHMARK(BM_DecodeTextDelta);

}  // namespace
