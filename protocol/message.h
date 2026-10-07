// The messages exchanged with Claude Code, and the only code that knows how
// they are written: as lines of JSON, in the format the `claude` program
// calls stream-json.
//
// What Claude Code says is decoded into a Message, one per line. A message
// is one of several kinds, each with its own contents:
//
//   switch (message.kind()) {
//     case claude_code::Message::Kind::kTextDelta:
//       std::cout << message.text_delta().text;
//       break;
//     case claude_code::Message::Kind::kResult:
//       co_return message.result().text;
//     default:
//       break;
//   }
//
// What is said to Claude Code is encoded by the Encode functions, each of
// which gives one complete line.

#ifndef PROTOCOL_MESSAGE_H_
#define PROTOCOL_MESSAGE_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "json/json.h"

namespace claude_code {

// Claude asking to use a tool, such as "Bash" with a command to run.
struct ToolUse {
  // Names this use, so that its result can be matched to it.
  std::string id;
  std::string name;
  // The tool's arguments. Their shape depends on the tool.
  json::Value input;

  friend bool operator==(const ToolUse&, const ToolUse&) = default;
};

// A turn is beginning: Claude Code has taken a prompt and says how it is
// set up. Sent again at the start of every turn.
struct Init {
  // Identifies the conversation, which can be picked up later by this name.
  std::string session_id;
  std::string model;
  // The directory Claude Code is working in.
  std::string directory;
  // The names of the tools Claude may use.
  std::vector<std::string> tools;

  friend bool operator==(const Init&, const Init&) = default;
};

// The next few characters of what Claude is saying, as it says them. The
// whole text arrives again in an Assistant message once it is complete.
struct TextDelta {
  std::string text;

  friend bool operator==(const TextDelta&, const TextDelta&) = default;
};

// Something Claude said, asked to do, or both.
struct Assistant {
  std::string text;
  std::vector<ToolUse> tool_uses;

  friend bool operator==(const Assistant&, const Assistant&) = default;
};

// What came of one use of a tool.
struct ToolResult {
  // The ToolUse this answers.
  std::string tool_use_id;
  std::string output;
  // The tool failed, or was not allowed to run; `output` says why.
  bool is_error = false;

  friend bool operator==(const ToolResult&, const ToolResult&) = default;
};

// The results of tools Claude used, as they are handed back to it.
struct ToolResults {
  std::vector<ToolResult> results;

  friend bool operator==(const ToolResults&, const ToolResults&) = default;
};

// Claude Code asking whether Claude may use a tool. Claude Code waits until
// it is answered, with EncodeAllow or EncodeDeny.
struct PermissionRequest {
  // Names this request, for the answer.
  std::string id;
  ToolUse tool_use;

  friend bool operator==(const PermissionRequest&,
                         const PermissionRequest&) = default;
};

// The turn is over, and Claude Code is ready for the next prompt.
struct Result {
  // Claude's final answer, or why there is none.
  std::string text;
  // The turn did not end well: it was interrupted, say, or ran out of turns.
  bool is_error = false;
  std::string session_id;
  // What the conversation has cost so far, in US dollars.
  double cost_usd = 0;
  // How many times Claude was called on in this turn.
  int64_t turns = 0;
  std::chrono::milliseconds duration{0};

  friend bool operator==(const Result&, const Result&) = default;
};

// Anything else Claude Code says, of which there is a good deal: progress
// reports, hooks running, rate limits. Safe to ignore.
struct Other {
  // What kind of message this is, such as "system" or "rate_limit_event",
  // and for some kinds which sort of that, such as "status".
  std::string type;
  std::string subtype;
  // The whole message.
  json::Value raw;

  friend bool operator==(const Other&, const Other&) = default;
};

// One thing Claude Code said: exactly one of the above.
class Message {
 public:
  // Which one. Each kind is named for the struct that holds its contents.
  enum class Kind : uint8_t {
    kInit,
    kTextDelta,
    kAssistant,
    kToolResults,
    kPermissionRequest,
    kResult,
    kOther,
  };

  explicit Message(Init init) : contents_(std::move(init)) {}
  explicit Message(TextDelta delta) : contents_(std::move(delta)) {}
  explicit Message(Assistant assistant) : contents_(std::move(assistant)) {}
  explicit Message(ToolResults results) : contents_(std::move(results)) {}
  explicit Message(PermissionRequest request) : contents_(std::move(request)) {}
  explicit Message(Result result) : contents_(std::move(result)) {}
  explicit Message(Other other) : contents_(std::move(other)) {}

  Kind kind() const { return static_cast<Kind>(contents_.index()); }

  // The contents, by kind. Asking for a kind the message is not is a
  // mistake in the program, and stops it: look at kind() first.
  const Init& init() const;
  const TextDelta& text_delta() const;
  const Assistant& assistant() const;
  const ToolResults& tool_results() const;
  const PermissionRequest& permission_request() const;
  const Result& result() const;
  const Other& other() const;

  friend bool operator==(const Message&, const Message&) = default;

 private:
  // In the order of Kind, which is how kind() tells them apart.
  std::variant<Init, TextDelta, Assistant, ToolResults, PermissionRequest,
               Result, Other>
      contents_;
};

// Decodes one line from Claude Code. Evaluates to nothing for a line that
// carries no news, such as the acknowledgement of an interruption. Fails
// with InvalidArgument if `line` is not a message at all.
absl::StatusOr<std::optional<Message>> Decode(std::string_view line);

// What the user says to Claude: starts a turn, or adds to the one under way.
std::string EncodePrompt(std::string_view prompt);

// Asks Claude Code to stop the turn under way. `request_id` names the
// request, and must differ from one interruption to the next.
std::string EncodeInterrupt(std::string_view request_id);

// Answers `request`: the tool may be used as asked.
std::string EncodeAllow(const PermissionRequest& request);

// Answers `request`: the tool may not be used. Claude is told `reason`.
std::string EncodeDeny(const PermissionRequest& request,
                       std::string_view reason);

}  // namespace claude_code

#endif  // PROTOCOL_MESSAGE_H_
