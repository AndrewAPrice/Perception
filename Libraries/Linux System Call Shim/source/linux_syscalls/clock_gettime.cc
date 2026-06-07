#include "linux_syscalls/clock_gettime.h"

#include <errno.h>

#include "linux_syscalls/time_helper.h"

namespace perception {
namespace linux_syscalls {

long clock_gettime(clockid_t clk_id, struct timespec *tp) {
  if (tp == nullptr) return -EFAULT;
  uint64 microseconds = GetUtcTimeInMicroseconds();
  tp->tv_sec = microseconds / 1000000;
  tp->tv_nsec = (microseconds % 1000000) * 1000;
  return 0;
}

}  // namespace linux_syscalls
}  // namespace perception

