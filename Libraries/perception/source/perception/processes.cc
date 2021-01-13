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

#include "perception/threads.h"

#include <functional>

#ifndef PERCEPTION
#include <iostream>
#include <sched.h>
#include <sstream>
#include <stdlib.h>
#include <thread>
#endif

namespace perception {

ProcessId GetProcessId() {
#ifdef PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 39;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
#else
	return 0;
#endif
}

void TerminateProcess() {
#ifdef PERCEPTION
	register size_t syscall_num asm ("rdi") = 6;
	__asm__ ("syscall\n"::"r"(syscall_num): "rcx", "r11");
#else
	exit(0);
#endif
}

bool GetFirstProcessWithName(std::string_view name, ProcessId& pid) {
	if (name.size() > 88)
		return false;

	size_t process_name[kMaximumProcessNameLength / 8];
	memcpy(process_name, name.c_str(), std::min(kMaximumProcessNameLength, name.size()));

	volatile register size_t syscall asm ("rdi") = 22;
	volatile register size_t pid_r asm ("rbp") = 0;
	volatile register size_t name_1 asm ("rax") = process_name[0];
	volatile register size_t name_2 asm ("rbx") = process_name[1];
	volatile register size_t name_3 asm ("rdx") = process_name[2];
	volatile register size_t name_4 asm ("rsi") = process_name[3];
	volatile register size_t name_5 asm ("r8") = process_name[4];
	volatile register size_t name_6 asm ("r9") = process_name[5];
	volatile register size_t name_7 asm ("r10") = process_name[6];
	volatile register size_t name_8 asm ("r12") = process_name[7];
	volatile register size_t name_9 asm ("r13") = process_name[8];
	volatile register size_t name_10 asm ("r14") = process_name[9];
	volatile register size_t name_10 asm ("r15") = process_name[10];
	
	// TODO..
	volatile register size_t return_val asm ("rax");

}

void ForEachProcessWithName(std::string_view,
	const std::function<void(ProcessId)>& on_each_process) {
	if (name.size() > 88)
		return false;

}

}

