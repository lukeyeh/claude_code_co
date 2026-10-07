#include "cli/cli.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "net/process.h"
#include "os/process.h"
#include "session/session.h"

namespace claude_code {
namespace {

// The command line that has the program hold a conversation over its
// standard input and output, set up as `options` asks.
std::vector<std::string> CommandLine(const Options& options) {
  std::vector<std::string> arguments = {
      options.program,
      // Without a terminal: take prompts from the input, not a keyboard.
      "--print",
      "--input-format=stream-json",
      "--output-format=stream-json",
      // Everything that happens in a turn, not just how it ends. The
      // program insists on this when printing stream-json.
      "--verbose",
  };

  if (!options.model.empty()) arguments.push_back("--model=" + options.model);
  if (!options.instructions.empty()) {
    arguments.push_back("--append-system-prompt=" + options.instructions);
  }

  switch (options.permissions) {
    case Permissions::kDefault: break;
    case Permissions::kAsk:
      // "stdio" has the program ask over the conversation itself.
      arguments.emplace_back("--permission-prompt-tool=stdio");
      break;
    case Permissions::kAcceptEdits:
      arguments.emplace_back("--permission-mode=acceptEdits");
      break;
    case Permissions::kPlan:
      arguments.emplace_back("--permission-mode=plan");
      break;
    case Permissions::kBypass:
      arguments.emplace_back("--permission-mode=bypassPermissions");
      break;
  }

  if (!options.allowed_tools.empty()) {
    arguments.push_back("--allowedTools=" +
                        absl::StrJoin(options.allowed_tools, ","));
  }
  if (!options.resume.empty()) {
    arguments.push_back("--resume=" + options.resume);
  }
  if (options.text_deltas) arguments.emplace_back("--include-partial-messages");

  return arguments;
}

}  // namespace

absl::StatusOr<std::unique_ptr<Session>> Start(const Options& options) {
  ABSL_ASSIGN_OR_RETURN(std::unique_ptr<net::Process> program,
                        net::Process::Start(os::Program{
                            .arguments = CommandLine(options),
                            .directory = options.directory,
                        }),
                        _.SetPrepend() << "starting Claude Code: ");

  return std::make_unique<Session>(std::move(program));
}

}  // namespace claude_code
