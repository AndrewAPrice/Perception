#include "linux_syscalls/clock_gettime.h"

#include "perception/time.h"
#include <errno.h>

namespace perception {
namespace linux_syscalls {

long clock_gettime(clockid_t clk_id, struct timespec *tp) {
  if (tp == nullptr) {
    return -EFAULT;
  }
  auto microseconds = ::perception::GetTimeSinceKernelStarted();
  tp->tv_sec = microseconds.count() / 1000000;
  tp->tv_nsec = (microseconds.count() % 1000000) * 1000;
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception

