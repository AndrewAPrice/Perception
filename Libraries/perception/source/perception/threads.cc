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

#ifndef PERCEPTION
#include <iostream>
#include <sched.h>
#include <sstream>
#include <stdlib.h>
#include <thread>
#endif

namespace perception {

ThreadId CreateThread(void (*entry_point)(void *), void* param) {
#ifdef PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 1;
	volatile register size_t param1 asm ("rax") = (size_t)entry_point;
	volatile register size_t param2 asm ("rbx") = (size_t)param;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num), "r"(param1), "r"(param2): "rcx", "r11");
	return return_val;
#else
	std::cout << "You shouldn't manually call perception::CreateThread. Please use std::thread." << std::endl;
	return 0;
#endif
}

ThreadId GetThreadId() {
#ifdef PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 2;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
#else
	std::stringstream ss;
	ss << std::this_thread::get_id();
	return (ThreadId)std::stoi(ss.str());
#endif
}

void TerminateThread() {
#ifdef PERCEPTION
	register size_t syscall_num asm ("rdi") = 4;
	__asm__ ("syscall\n"::"r"(syscall_num): "rcx", "r11");
#else
	std::cout << "You shouldn't manually call perception::TerminateThread. Please let the thread function return." << std::endl;
#endif
}

void TerminateThread(ThreadId tid) {
#ifdef PERCEPTION
	register size_t syscall_num asm ("rdi") = 5;
	register size_t param asm ("rax") = tid;

	__asm__ ("syscall\n"::"r"(syscall_num), "r"(param): "rcx", "r11");
#else
	std::cout << "You shouldn't manually call perception::TerminateThread. Please let the thread function return." << std::endl;
#endif
}

void Yield() {
#ifdef PERCEPTION
	register unsigned long long int syscall_num asm ("rdi") = 8;
	__asm__ ("syscall\n"::"r"(syscall_num): "rcx", "r11");
#else
	(void)sched_yield();
#endif
}

void SetThreadSegment(size_t segment_address) {
#ifdef PERCEPTION
	register size_t syscall_num asm ("rdi") = 27;
	register size_t param asm ("rax") = segment_address;

	__asm__ ("syscall\n"::"r"(syscall_num), "r"(param): "rcx", "r11");
#endif
}

void SetAddressToClearOnThreadTermination(size_t address_to_clear) {
#ifdef PERCEPTION
	register size_t syscall_num asm ("rdi") = 28;
	register size_t param asm ("rax") = address_to_clear;

	__asm__ ("syscall\n"::"r"(syscall_num), "r"(param): "rcx", "r11");
#endif
}

}

