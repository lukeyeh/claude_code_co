#include "session/session.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "async/status_macros.h"
#include "async/task.h"
#include "net/stream.h"
#include "protocol/message.h"

namespace claude_code {
namespace {

// The longest line Claude Code is allowed to send. Lines are long when a
// tool reads a large file or an image, but not this long.
constexpr size_t kMaxLineBytes = size_t{256} * 1024 * 1024;

}  // namespace

Session::Session(std::unique_ptr<net::Stream> stream)
    : stream_(std::move(stream)), reader_(stream_.get()) {}

Task<absl::Status> Session::Send(std::string_view prompt) {
  const std::string line = EncodePrompt(prompt);

  co_return co_await stream_->Write(line);
}

Task<absl::StatusOr<Message>> Session::Next(net::Deadline deadline) {
  for (;;) {
    CO_ASSIGN_OR_RETURN(
        const std::string_view line,
        co_await reader_.ReadUntil("\n", kMaxLineBytes, deadline),
        _.SetPrepend() << "waiting for Claude Code: ");

    // Nothing the caller could do about a line that makes no sense, and the
    // lines after it are still good.
    absl::StatusOr<std::optional<Message>> message = Decode(line);
    if (!message.ok()) {
      LOG(WARNING) << "skipping a line: " << message.status();
      continue;
    }

    if (message->has_value()) co_return std::move(**message);
  }
}

Task<absl::Status> Session::Interrupt() {
  const std::string line =
      EncodeInterrupt(absl::StrCat("interrupt-", ++interruptions_));

  co_return co_await stream_->Write(line);
}

Task<absl::Status> Session::Allow(const PermissionRequest& request) {
  const std::string line = EncodeAllow(request);

  co_return co_await stream_->Write(line);
}

Task<absl::Status> Session::Deny(const PermissionRequest& request,
                                 std::string_view reason) {
  const std::string line = EncodeDeny(request, reason);

  co_return co_await stream_->Write(line);
}

}  // namespace claude_code
