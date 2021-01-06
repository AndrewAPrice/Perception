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

#include "perception/debug.h"

#ifndef PERCEPTION
#include <iostream>
#endif

namespace perception {

DebugPrinter& DebugPrinter::operator<< (char c) {
#ifdef PERCEPTION
	register unsigned long long int syscall_num asm ("rdi") = 0;
	register unsigned long long int param asm ("rax") = c;

	__asm__ ("syscall\n"::"r"(syscall_num), "r"(param): "rcx", "r11");
#else
	std::cout << c;
#endif

	return *this;
}

DebugPrinter& DebugPrinter::operator<< (size_t number) {
	if(number == 0) {
		*this << '0';
		return *this;
	}

	/* maximum value is 18,446,744,073,709,551,615
	                    01123445677891111111111111
	                                 0012334566789*/
	char temp[20];
	size_t first_char = 20;

	while(number > 0) {
		first_char--;
		temp[first_char] = '0' + (char)(number % 10);
		number /= 10;
	}

	size_t i;
	for(i = first_char; i < 20; i++) {
		*this << temp[i];
		if(i == 1 || i == 4 || i == 7 || i == 10 || i == 13 || i == 16)
			*this << ',';

	}
	return *this;
}

DebugPrinter& DebugPrinter::operator<< (int64 number) {
	if (number < 0) {
		*this << '-';
		number *= -1;
	}
	*this << (size_t)number;
	return *this;
}


DebugPrinter& DebugPrinter::operator<< (const char* str) {
	const char *c = (const char *)str;
	while(*c) {
		*this << *c;
		c++;
	}
	return *this;
}
DebugPrinter& DebugPrinter::operator<< (bool b) {
	*this << (b ? "true" : "false");
	return *this;
}

DebugPrinter DebugPrinterSingleton;

extern "C" void DebugPrint(char *str) {
	DebugPrinterSingleton << str;
}

extern "C" void DebugNumber(long l) {
	DebugPrinterSingleton << (size_t)l;
}

}
