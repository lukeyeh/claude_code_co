// Asks Claude Code one thing and prints the turn as it happens: what Claude
// says, the tools it uses, and what the turn cost.
//
//   bazel run //examples:ask -- "What does this project do?"
//   bazel run //examples:ask -- --directory=$PWD --ask "Add a .gitignore"
//
// With --ask, Claude Code asks before using a tool it has not been allowed,
// and the answer is read from the terminal.

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "async/status_macros.h"
#include "async/task.h"
#include "cli/cli.h"
#include "json/json.h"
#include "net/event_loop.h"
#include "net/stream.h"
#include "protocol/message.h"
#include "session/session.h"

ABSL_FLAG(std::string, directory, "", "The directory Claude works in.");
ABSL_FLAG(std::string, model, "", "The model to use; the default if empty.");
ABSL_FLAG(bool, ask, false, "Ask at the terminal before Claude uses a tool.");

namespace {

// How long Claude may go without saying anything at all.
constexpr std::chrono::minutes kPatience(10);

// Puts the question to whoever is at the terminal. This stops everything
// else on the thread while it waits, which is all right here because the
// only other thing going on is Claude Code waiting for this answer.
Task<absl::Status> Answer(claude_code::Session& session,
                          const claude_code::PermissionRequest& request) {
  std::cout << "\nAllow " << request.tool_use.name << " "
            << json::Serialize(request.tool_use.input) << "? [y/N] "
            << std::flush;
  std::string answer;
  std::getline(std::cin, answer);

  if (answer == "y") co_return co_await session.Allow(request);
  co_return co_await session.Deny(request, "The user said no.");
}

// One turn, start to finish.
Task<absl::Status> Ask(claude_code::Options options, std::string prompt) {
  CO_ASSIGN_OR_RETURN(const std::unique_ptr<claude_code::Session> session,
                      claude_code::Start(options));
  CO_RETURN_IF_ERROR(co_await session->Send(prompt));

  for (;;) {
    CO_ASSIGN_OR_RETURN(const claude_code::Message message,
                        co_await session->Next(net::After(kPatience)));

    switch (message.kind()) {
      case claude_code::Message::Kind::kTextDelta:
        std::cout << message.text_delta().text << std::flush;
        break;

      case claude_code::Message::Kind::kAssistant:
        // Its words have been printed already, as they came.
        for (const claude_code::ToolUse& use : message.assistant().tool_uses) {
          std::cout << "\n[" << use.name << " " << json::Serialize(use.input)
                    << "]\n";
        }
        break;

      case claude_code::Message::Kind::kPermissionRequest:
        CO_RETURN_IF_ERROR(
            co_await Answer(*session, message.permission_request()));
        break;

      case claude_code::Message::Kind::kResult: {
        const claude_code::Result& result = message.result();
        std::cout << "\n(" << result.turns << " turns, $" << result.cost_usd
                  << ")\n";

        if (result.is_error) co_return absl::AbortedError(result.text);
        co_return absl::OkStatus();
      }

      // The rest is Claude Code's own business.
      case claude_code::Message::Kind::kInit:
      case claude_code::Message::Kind::kToolResults:
      case claude_code::Message::Kind::kOther: break;
    }
  }
}

absl::Status Run(const std::vector<char*>& words) {
  if (words.size() < 2) {
    return absl::InvalidArgumentError("give a prompt for Claude");
  }
  ABSL_ASSIGN_OR_RETURN(EventLoop loop, EventLoop::Create());

  return loop.Run(Ask(
      claude_code::Options{
          .directory = absl::GetFlag(FLAGS_directory),
          .model = absl::GetFlag(FLAGS_model),
          .permissions = absl::GetFlag(FLAGS_ask)
                             ? claude_code::Permissions::kAsk
                             : claude_code::Permissions::kDefault,
          .text_deltas = true,
      },
      // Everything after the program's own name is the prompt.
      absl::StrJoin(words.begin() + 1, words.end(), " ")));
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<char*> words = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  const absl::Status status = Run(words);
  if (!status.ok()) std::cerr << status << "\n";
  return status.ok() ? 0 : 1;
}
