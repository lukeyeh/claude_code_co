// A conversation with Claude Code: a stream of messages in both directions.
// Prompts go one way and what Claude says and does comes back the other,
// while it happens, so a caller can follow a turn, answer the questions
// Claude Code asks about it, and cut it short.
//
//   CO_RETURN_IF_ERROR(co_await session.Send("What is in this directory?"));
//
//   for (;;) {
//     CO_ASSIGN_OR_RETURN(const claude_code::Message message,
//                         co_await session.Next(deadline));
//
//     if (message.kind() == claude_code::Message::Kind::kResult) {
//       co_return message.result().text;
//     }
//   }
//
// Most callers get a Session from claude_code::Start (cli/cli.h), which runs
// the `claude` program. A Session can be held over any stream that speaks
// the same protocol, which is how tests stand in for Claude Code.

#ifndef SESSION_SESSION_H_
#define SESSION_SESSION_H_

#include <cstdint>
#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "async/task.h"
#include "net/reader.h"
#include "net/stream.h"
#include "protocol/message.h"

namespace claude_code {

// One conversation. It lasts for as many turns as wanted: a turn starts with
// Send and is over when Next yields a Result.
//
// The two directions are independent, which is the point: while one task
// waits in Next, another may Send, Interrupt, Allow or Deny. At most one
// Next may be in progress at a time, and at most one of the others. Like
// the stream it is held over, a session belongs to the event loop of the
// thread that made it, and must not be destroyed while any of its
// operations is in progress.
//
// Destroying the session ends the conversation, and Claude Code with it.
class Session {
 public:
  // A session over `stream`, at the other end of which is Claude Code.
  explicit Session(std::unique_ptr<net::Stream> stream);

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Says `prompt` to Claude. If no turn is under way this starts one;
  // otherwise Claude takes it up when it next has the chance. Fails with
  // Unavailable if Claude Code has gone, which is final.
  Task<absl::Status> Send(std::string_view prompt);

  // Waits for the next message. Fails with DeadlineExceeded if none arrives
  // by `deadline`, after which the session is still good and Next can be
  // tried again with nothing lost. Any other failure is final: Unavailable
  // if Claude Code has gone, ResourceExhausted if it sent a message too
  // large to be believed.
  Task<absl::StatusOr<Message>> Next(net::Deadline deadline);

  // Asks Claude Code to stop the turn under way. The turn still ends in the
  // usual way, with a Result from Next. Fails as Send does.
  Task<absl::Status> Interrupt();

  // Answers `request`, which Next yielded: the tool may be used. Claude Code
  // asks only if it was started with permissions set to be asked for, and
  // then waits for one of these two before going on. Fails as Send does.
  Task<absl::Status> Allow(const PermissionRequest& request);

  // Answers `request`: the tool may not be used. Claude is told `reason`,
  // and carries on with the turn knowing it.
  Task<absl::Status> Deny(const PermissionRequest& request,
                          std::string_view reason);

 private:
  std::unique_ptr<net::Stream> stream_;
  net::Reader reader_;
  // How many times Interrupt has been called, to name each request.
  uint64_t interruptions_ = 0;
};

}  // namespace claude_code

#endif  // SESSION_SESSION_H_
