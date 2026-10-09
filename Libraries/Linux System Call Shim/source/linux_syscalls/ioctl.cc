// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "linux_syscalls/ioctl.h"

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "bits/ioctl.h"
#include "files.h"
#include "perception/debug.h"
#include "perception/processes.h"
#include "perception/terminal_service.h"

using perception::DebugPrinterSingleton;

namespace perception {
namespace linux_syscalls {
namespace {

// Default terminal row count when no terminal service is attached.
constexpr unsigned short kDefaultRows = 25;

// Default terminal column count when no terminal service is attached.
constexpr unsigned short kDefaultCols = 80;

// Default terminal width in pixels when no terminal service is attached.
constexpr unsigned short kDefaultWidthPixels = 640;

// Default terminal height in pixels when no terminal service is attached.
constexpr unsigned short kDefaultHeightPixels = 400;

// Default interrupt control character (Ctrl+C).
constexpr cc_t kDefaultVintr = 3;

// Default end-of-file control character (Ctrl+D).
constexpr cc_t kDefaultVeof = 4;

// Default erase control character (DEL).
constexpr cc_t kDefaultVerase = 0x7f;

// Default minimum number of characters for noncanonical read.
constexpr cc_t kDefaultVmin = 1;

// Default timeout in deciseconds for noncanonical read.
constexpr cc_t kDefaultVtime = 0;

}  // namespace

long ioctl(long file_descriptor, long request, long arg) {
  auto desc = GetFileDescriptor(file_descriptor);

  switch (request) {
    case TIOCGWINSZ: {
      if (!desc) {
        if (file_descriptor == 1 || file_descriptor == 2) {
          struct winsize* wsz = reinterpret_cast<struct winsize*>(arg);
          wsz->ws_row = kDefaultRows;
          wsz->ws_col = kDefaultCols;
          wsz->ws_xpixel = kDefaultWidthPixels;
          wsz->ws_ypixel = kDefaultHeightPixels;
          return 0;
        }
        return -EBADF;
      }
      if (desc->type == FileDescriptor::PIPE && desc->pipe.pipe) {
        auto terminal_service = desc->pipe.pipe->GetTerminalService();
        if (!terminal_service.has_value())
          return -ENOTTY;
        auto status_or_size = terminal_service->GetWindowSize();
        if (!status_or_size.Ok())
          return -ENOTTY;
        struct winsize* wsz = reinterpret_cast<struct winsize*>(arg);
        wsz->ws_row = status_or_size->rows;
        wsz->ws_col = status_or_size->cols;
        wsz->ws_xpixel = status_or_size->width_pixels;
        wsz->ws_ypixel = status_or_size->height_pixels;
        return 0;
      }
      return -ENOTTY;
    }
    case TIOCSWINSZ: {
      if (desc && desc->type == FileDescriptor::PIPE && desc->pipe.pipe) {
        auto terminal_service = desc->pipe.pipe->GetTerminalService();
        if (terminal_service.has_value()) {
          const struct winsize* wsz =
              reinterpret_cast<const struct winsize*>(arg);
          TerminalWindowSize req;
          req.rows = wsz->ws_row;
          req.cols = wsz->ws_col;
          req.width_pixels = wsz->ws_xpixel;
          req.height_pixels = wsz->ws_ypixel;
          terminal_service->SetWindowSize(req);
          return 0;
        }
      }
      return -ENOTTY;
    }
    case TCGETS: {
      if (desc && desc->type == FileDescriptor::PIPE && desc->pipe.pipe) {
        auto terminal_service = desc->pipe.pipe->GetTerminalService();
        if (terminal_service.has_value()) {
          auto status_or_attr = terminal_service->GetAttributes();
          struct termios* tio = reinterpret_cast<struct termios*>(arg);
          memset(tio, 0, sizeof(struct termios));
          if (status_or_attr.Ok()) {
            tio->c_iflag = status_or_attr->c_iflag;
            tio->c_oflag = status_or_attr->c_oflag;
            tio->c_cflag = status_or_attr->c_cflag;
            tio->c_lflag = status_or_attr->c_lflag;
          } else {
            tio->c_iflag = ICRNL | IXON;
            tio->c_oflag = OPOST | ONLCR;
            tio->c_cflag = CS8 | CREAD;
            tio->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
          }
          tio->c_cc[VINTR] = kDefaultVintr;
          tio->c_cc[VEOF] = kDefaultVeof;
          tio->c_cc[VERASE] = kDefaultVerase;
          tio->c_cc[VMIN] = kDefaultVmin;
          tio->c_cc[VTIME] = kDefaultVtime;
          return 0;
        }
      }
      return -ENOTTY;
    }
    case TCSETS:
    case TCSETSW:
    case TCSETSF: {
      if (desc && desc->type == FileDescriptor::PIPE && desc->pipe.pipe) {
        auto terminal_service = desc->pipe.pipe->GetTerminalService();
        if (terminal_service.has_value()) {
          const struct termios* tio =
              reinterpret_cast<const struct termios*>(arg);
          TerminalAttributes attr;
          attr.c_iflag = tio->c_iflag;
          attr.c_oflag = tio->c_oflag;
          attr.c_cflag = tio->c_cflag;
          attr.c_lflag = tio->c_lflag;
          terminal_service->SetAttributes(attr);
          return 0;
        }
      }
      return -ENOTTY;
    }
    case TIOCGPGRP: {
      if (desc && desc->type == FileDescriptor::PIPE && desc->pipe.pipe &&
          desc->pipe.pipe->HasTerminalService()) {
        *reinterpret_cast<int*>(arg) = static_cast<int>(GetProcessId());
        return 0;
      }
      return -ENOTTY;
    }
    case TIOCSPGRP: {
      if (desc && desc->type == FileDescriptor::PIPE && desc->pipe.pipe &&
          desc->pipe.pipe->HasTerminalService())
        return 0;
      return -ENOTTY;
    }
    case FIONREAD: {
      if (desc && desc->type == FileDescriptor::PIPE && desc->pipe.pipe) {
        *reinterpret_cast<int*>(arg) =
            static_cast<int>(desc->pipe.pipe->BufferedBytes());
        return 0;
      }
      return -ENOTTY;
    }
    case FIONBIO: {
      if (desc && desc->type == FileDescriptor::PIPE) {
        desc->pipe.non_blocking = (*reinterpret_cast<int*>(arg) != 0);
        return 0;
      }
      if (desc && desc->type == FileDescriptor::SOCKET) {
        desc->socket.non_blocking = (*reinterpret_cast<int*>(arg) != 0);
        return 0;
      }
      return -ENOTTY;
    }
    default:
      DebugPrinterSingleton << "Unhandled ioctl request " << (size_t)request
                            << ", arg: " << (size_t)arg << "\n";
      return -ENOTTY;
  }
}

}  // namespace linux_syscalls
}  // namespace perception
