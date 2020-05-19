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

#pragma once

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

#ifdef __cplusplus
extern "C" {
#endif

extern long __syscall0(long n);
extern long __syscall1(long n, long a1);
extern long __syscall2(long n, long a1, long a2);
extern long __syscall3(long n, long a1, long a2, long a3);
extern long __syscall4(long n, long a1, long a2, long a3, long a4);
extern long __syscall5(long n, long a1, long a2, long a3, long a4, long a5);
extern long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6);

#ifdef __cplusplus
}
#endif

#define VDSO_USEFUL
#define VDSO_CGT_SYM "__vdso_clock_gettime"
#define VDSO_CGT_VER "PERCEPTION"
#define VDSO_GETCPU_SYM "__vdso_getcpu"
#define VDSO_GETCPU_VER "PERCEPTION"

#define IPC_64 0
