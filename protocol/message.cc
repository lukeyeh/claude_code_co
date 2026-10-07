#include "protocol/message.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "json/json.h"

namespace claude_code {
namespace {

// The text of `content`, which is either a string or a list of blocks, of
// which the ones of type "text" hold some.
std::string TextOf(const json::Value& content) {
  std::string text = content.AsString();
  for (const json::Value& block : content.items()) {
    if (block["type"].AsString() == "text") text += block["text"].AsString();
  }

  return text;
}

Init DecodeInit(const json::Value& document) {
  Init init{
      .session_id = document["session_id"].AsString(),
      .model = document["model"].AsString(),
      .directory = document["cwd"].AsString(),
  };
  for (const json::Value& tool : document["tools"].items()) {
    init.tools.push_back(tool.AsString());
  }

  return init;
}

Assistant DecodeAssistant(const json::Value& document) {
  Assistant assistant;
  for (const json::Value& block : document["message"]["content"].items()) {
    const std::string& type = block["type"].AsString();

    if (type == "text") {
      assistant.text += block["text"].AsString();
    } else if (type == "tool_use") {
      assistant.tool_uses.push_back(ToolUse{
          .id = block["id"].AsString(),
          .name = block["name"].AsString(),
          .input = block["input"],
      });
    }
  }

  return assistant;
}

ToolResults DecodeToolResults(const json::Value& document) {
  ToolResults results;
  for (const json::Value& block : document["message"]["content"].items()) {
    if (block["type"].AsString() != "tool_result") continue;

    results.results.push_back(ToolResult{
        .tool_use_id = block["tool_use_id"].AsString(),
        .output = TextOf(block["content"]),
        .is_error = block["is_error"].AsBool(),
    });
  }

  return results;
}

PermissionRequest DecodePermissionRequest(const json::Value& document) {
  const json::Value& request = document["request"];

  return PermissionRequest{
      .id = document["request_id"].AsString(),
      .tool_use =
          ToolUse{
              .id = request["tool_use_id"].AsString(),
              .name = request["tool_name"].AsString(),
              .input = request["input"],
          },
  };
}

Result DecodeResult(const json::Value& document) {
  return Result{
      .text = document["result"].AsString(),
      .is_error = document["is_error"].AsBool(),
      .session_id = document["session_id"].AsString(),
      .cost_usd = document["total_cost_usd"].AsDouble(),
      .turns = document["num_turns"].AsInt(),
      .duration = std::chrono::milliseconds(document["duration_ms"].AsInt()),
  };
}

// The contents of `contents` as a T, which they must be.
template <typename T, typename Contents>
const T& Get(const Contents& contents) {
  const T* const held = std::get_if<T>(&contents);
  CHECK(held != nullptr) << "the message is of another kind";

  return *held;
}

// A line: the document, and the newline that ends it.
std::string Line(const json::Value& document) {
  return json::Serialize(document) + "\n";
}

// The answer to `request`, which is `decision`.
std::string EncodeDecision(const PermissionRequest& request,
                           json::Value decision) {
  const json::Value response = json::Value()
                                   .Set("subtype", "success")
                                   .Set("request_id", request.id)
                                   .Set("response", std::move(decision));

  return Line(
      json::Value().Set("type", "control_response").Set("response", response));
}

}  // namespace

const Init& Message::init() const { return Get<Init>(contents_); }
const TextDelta& Message::text_delta() const {
  return Get<TextDelta>(contents_);
}
const Assistant& Message::assistant() const {
  return Get<Assistant>(contents_);
}
const ToolResults& Message::tool_results() const {
  return Get<ToolResults>(contents_);
}
const PermissionRequest& Message::permission_request() const {
  return Get<PermissionRequest>(contents_);
}
const Result& Message::result() const { return Get<Result>(contents_); }
const Other& Message::other() const { return Get<Other>(contents_); }

absl::StatusOr<std::optional<Message>> Decode(std::string_view line) {
  ABSL_ASSIGN_OR_RETURN(json::Value document, json::Parse(line),
                        _.SetPrepend() << "message from Claude Code: ");

  const std::string type = document["type"].AsString();
  if (type.empty()) {
    return absl::InvalidArgumentError(
        "message from Claude Code: it does not say what type it is");
  }
  std::string subtype = document["subtype"].AsString();

  // Claude Code having heard a request of ours, which is not news.
  if (type == "control_response") return std::nullopt;

  if (type == "system" && subtype == "init") {
    return Message(DecodeInit(document));
  }
  if (type == "assistant") return Message(DecodeAssistant(document));
  if (type == "user") return Message(DecodeToolResults(document));
  if (type == "result") return Message(DecodeResult(document));

  if (type == "stream_event") {
    const json::Value& delta = document["event"]["delta"];
    if (delta["type"].AsString() == "text_delta") {
      return Message(TextDelta{
          .text = delta["text"].AsString(),
      });
    }
  }

  if (type == "control_request") {
    subtype = document["request"]["subtype"].AsString();
    if (subtype == "can_use_tool") {
      return Message(DecodePermissionRequest(document));
    }
  }

  return Message(Other{
      .type = type,
      .subtype = std::move(subtype),
      .raw = std::move(document),
  });
}

std::string EncodePrompt(std::string_view prompt) {
  const json::Value message =
      json::Value().Set("role", "user").Set("content", prompt);

  return Line(json::Value().Set("type", "user").Set("message", message));
}

std::string EncodeInterrupt(std::string_view request_id) {
  const json::Value request = json::Value().Set("subtype", "interrupt");

  return Line(json::Value()
                  .Set("type", "control_request")
                  .Set("request_id", request_id)
                  .Set("request", request));
}

std::string EncodeAllow(const PermissionRequest& request) {
  // Claude Code wants the arguments back, since an answer may change them.
  return EncodeDecision(request,
                        json::Value()
                            .Set("behavior", "allow")
                            .Set("updatedInput", request.tool_use.input));
}

std::string EncodeDeny(const PermissionRequest& request,
                       std::string_view reason) {
  return EncodeDecision(
      request, json::Value().Set("behavior", "deny").Set("message", reason));
}

}  // namespace claude_code
