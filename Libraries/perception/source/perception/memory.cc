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

#include "perception/liballoc.h"
#include "perception/memory.h"

#include <iostream>

#ifndef PERCEPTION
#include <stdlib.h>
#include <unistd.h>
#endif

namespace perception {
namespace {
	size_t kOutOfMemory = 1;
}

void* AllocateMemoryPages(size_t number) {
#if PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 12;
	volatile register size_t param1 asm ("rax") = number;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num), "r"(param1): "rcx", "r11");
	if (return_val == kOutOfMemory)
		return nullptr;
	else
		return (void*)return_val;
#else
	return malloc(kPageSize * number);
#endif
}

void ReleaseMemoryPages(void* ptr, size_t number) {
#if PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 13;
	volatile register size_t param1 asm ("rax") = (size_t)ptr;
	volatile register size_t param2 asm ("rbx") = number;

	__asm__ __volatile__ ("syscall\n"::"r"(syscall_num), "r"(param1), "r"(param2): "rcx", "r11");
#else
	return free(ptr);
#endif
}

// Maps physical memory into this process's address space. Only drivers
// may call this.
void* MapPhysicalMemory(size_t physical_address, size_t pages) {
#if PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 41;
	volatile register size_t physical_address_r asm ("rax") = physical_address;
	volatile register size_t pages_r asm ("rbx") = pages;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num),
		"r"(physical_address_r), "r"(pages_r): "rcx", "r11");
	if (return_val == kOutOfMemory)
			return nullptr;
	else
		return (void*)return_val;
#else
	return nullptr;
#endif
}

bool MaybeResizePages(void** ptr, size_t current_number, size_t new_number) {
#if PERCEPTION
	std::cout << "Implement MaybeResizePages." << std::endl;
	return false;
#else
	void* maybe_new_ptr = realloc(*ptr, new_number * kPageSize);
	if (maybe_new_ptr == nullptr)
		return false;
	
	*ptr = maybe_new_ptr;
	return true;
#endif
}

size_t GetFreeSystemMemory() {
#if PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 14;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
#else
	return 0;
#endif
}

size_t GetTotalSystemMemory() {
#if PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 16;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
#else
	long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return pages * page_size;
#endif
}

size_t GetMemoryUsedByProcess() {
#if PERCEPTION
	volatile register size_t syscall_num asm ("rdi") = 15;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
#else
	return 0;
#endif
}

}

#ifdef PERCEPTION
// Function that runs if a virtual function implementation is missing. Should never be called but
// needs to exist.
// extern "C" void __cxa_pure_virtual() {}

// Functions to support new/delete.
void *operator new(long unsigned int size) {
    return malloc(size);
}
 
void *operator new[](long unsigned int size) {
    return malloc(size);
}
 
void operator delete(void *address, long unsigned int size) {
    free(address);
}
 
void operator delete[](void *address, long unsigned int size) {
    free(address);
}
#endif
