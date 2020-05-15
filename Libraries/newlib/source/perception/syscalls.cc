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

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/times.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "perception/debug.h"
#include "perception/liballoc.h"
#include "perception/threads.h"

extern "C" {

void _exit() {
	perception::TerminateProcess();
}

int _close(int file) {
	::perception::DebugPrinterSingleton << "close(" << (size_t)file << ")\n";
	return 0;
}

char **environ; /* pointer to array of char * strings that define the current environment variables */
int _execve(char *name, char **argv, char **env) {
	::perception::DebugPrinterSingleton << "TODO: Implement execve()\n";
	return 0;
}

int _fork() {
	::perception::DebugPrinterSingleton << "TODO: Implement fork()\n";
	return 0;
}

int _fstat(int file, struct stat *st) {
	::perception::DebugPrinterSingleton << "TODO: Implement fstat()\n";
	return 0;
}

int _getpid() {
	::perception::DebugPrinterSingleton << "TODO: Implement getpid()\n";
	return 0;
}
int _isatty(int file) {
	::perception::DebugPrinterSingleton << "TODO: Implement isatty()\n";
	return 0;
}

int _kill(int pid, int sig) {
	::perception::DebugPrinterSingleton << "TODO: Implement kill()\n";
	return 0;
}

int _link(char *old, char *new_) {
	::perception::DebugPrinterSingleton << "TODO: Implement link()\n";
	return 0;
}

int _lseek(int file, int ptr, int dir) {
	::perception::DebugPrinterSingleton << "TODO: Implement lseek()\n";
	return 0;
}

int _lseek64(int file, _off64_t __ptr, int dir) {
	::perception::DebugPrinterSingleton << "TODO: Implement lseek64()\n";
	return 0;
}

int _open(const char *name, int flags, ...) {
	::perception::DebugPrinterSingleton << "TODO: Implement open()\n";
	return 0;
}

int _read(int file, char *ptr, int len) {
	::perception::DebugPrinterSingleton << "TODO: Implement read()\n";
	return 0;
}

void *_malloc_r (struct _reent *ptr, size_t size) {
  return malloc(size);
}

void *_calloc_r (struct _reent *ptr, size_t size, size_t len) {
  void *mem = malloc(size * len);
  memset(mem, 0, size * len);
  return mem;
}

void _free_r (struct _reent *ptr, void *mem) {
	free(mem);
}

int _stat(const char *file, struct stat *st) {
	::perception::DebugPrinterSingleton << "TODO: Implement stat()\n";
	return 0;
}

clock_t _times(struct tms *buf) {
	::perception::DebugPrinterSingleton << "TODO: Implement times()\n";
	return 0;
}

int _unlink(char *name) {
	::perception::DebugPrinterSingleton << "TODO: Implement unlink()\n";
	return 0;
}

int _wait(int *status) {
	::perception::DebugPrinterSingleton << "TODO: Implement wait()\n";
	return 0;
}

int _write(int file, char *ptr, int len) {
	// TODO: file != STDOUT
	while (len > 0) {
		::perception::DebugPrinterSingleton << *ptr;
		ptr++;
		len--;
	}
	return 0;
}

int _gettimeofday (struct timeval *__restrict __p, void *__restrict __tz) {
	::perception::DebugPrinterSingleton << "TODO: Implement gettimeofday()\n";
	return 0;
}

}