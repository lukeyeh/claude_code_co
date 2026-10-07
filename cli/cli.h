// Claude Code itself: starts the `claude` program and gives back the
// conversation with it. This is the only code that knows the program's
// command line.
//
//   ABSL_ASSIGN_OR_RETURN(const std::unique_ptr<claude_code::Session> session,
//                         claude_code::Start({
//                             .directory = "/home/me/project",
//                             .permissions = claude_code::Permissions::kAsk,
//                         }));
//
// The program must be installed and logged in; it runs as the user, with the
// user's settings, as it would in a terminal.

#ifndef CLI_CLI_H_
#define CLI_CLI_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "session/session.h"

namespace claude_code {

// What Claude may do without being told it may.
enum class Permissions : uint8_t {
  // What the user's settings allow. Anything they would have Claude Code ask
  // about is refused, there being nobody to ask.
  kDefault,
  // As kDefault, but what would be refused is asked about instead: it comes
  // from Session::Next as a PermissionRequest.
  kAsk,
  // Editing files as well.
  kAcceptEdits,
  // Looking, but changing nothing: Claude makes a plan instead.
  kPlan,
  // Anything at all. For a sandbox.
  kBypass,
};

struct Options {
  // The program to run. One named without a slash is looked for in the
  // directories of PATH.
  std::string program = "claude";

  // The directory Claude works in. Empty means this process's own.
  std::string directory;

  // Which model to use, by name or alias ("opus", "sonnet", "haiku"). Empty
  // means the user's default.
  std::string model;

  // Instructions added to the ones Claude Code gives Claude itself.
  std::string instructions;

  Permissions permissions = Permissions::kDefault;

  // Tools Claude may use without asking, whatever `permissions` says, as
  // Claude Code writes them: "Read", "Bash(git log:*)".
  std::vector<std::string> allowed_tools;

  // The session id of an earlier conversation to carry on with, from its
  // Init or Result. Empty starts a new one.
  std::string resume;

  // Yield what Claude says as it says it, as TextDelta messages, as well as
  // whole once it has said it.
  bool text_deltas = false;
};

// Starts Claude Code. It says nothing until it is sent a prompt.
//
// Fails with NotFound if there is no such program or directory, and
// PermissionDenied if the program may not be run. A program that starts and
// then gives up, as `claude` does when it is not logged in, is found out by
// the session: its operations fail with Unavailable, and what the program
// had to say for itself is on this process's standard error.
//
// Must be called from a task running on an EventLoop.
absl::StatusOr<std::unique_ptr<Session>> Start(const Options& options);

}  // namespace claude_code

#endif  // CLI_CLI_H_
