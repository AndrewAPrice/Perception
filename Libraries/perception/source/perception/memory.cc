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
namespace perception {

void* AllocateMemoryPages(size_t number) {
	volatile register size_t syscall_num asm ("rdi") = 12;
	volatile register size_t param1 asm ("rax") = number;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num), "r"(param1): "rcx", "r11");
	return (void*)return_val;
}

void ReleaseMemoryPages(void* ptr, size_t number) {
	volatile register size_t syscall_num asm ("rdi") = 13;
	volatile register size_t param1 asm ("rax") = (size_t)ptr;
	volatile register size_t param2 asm ("rbx") = number;

	__asm__ __volatile__ ("syscall\n"::"r"(syscall_num), "r"(param1), "r"(param2): "rcx", "r11");
}

bool MaybeResizePages(void** ptr, size_t current_number, size_t new_number) {
	// TODO
	return false;
}

size_t GetFreeSystemMemory() {
	volatile register size_t syscall_num asm ("rdi") = 14;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
}

size_t GetTotalSystemMemory() {
	volatile register size_t syscall_num asm ("rdi") = 16;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
}

size_t GetMemoryUsedByProcess() {
	volatile register size_t syscall_num asm ("rdi") = 15;
	volatile register size_t return_val asm ("rax");

	__asm__ __volatile__ ("syscall\n":"=r"(return_val):"r"(syscall_num): "rcx", "r11");
	return return_val;
}

}


// Function that runs if a virtual function implementation is missing. Should never be called but
// needs to exist.
extern "C" void __cxa_pure_virtual() {}

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