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
#include "perception/messages.h"
#include "perception/processes.h"
#include "perception/threads.h"

#include "permebuf/Libraries/perception/helloworld.permebuf.h"

#include <iostream>
#include <string>

class Something {
public:
	Something() {
		std::cout << "Constructing something." << std::endl;
	}

	~Something() {
		std::cout << "Destructing something." << std::endl;
	}
};

void Child(std::unique_ptr<Something> something) {
	std::cout << "I now own something" << std::endl;
}

int main() {

	std::cout << "Hello " << "world " << (size_t)11 << '\n';
	{
		std::unique_ptr<Something> something = std::make_unique<Something>();
		Child(std::move(something));
		perception::DebugPrinterSingleton << "Hello world " << (size_t)12 << '\n';
	}

	std::cout << "Hello " << "world " << (size_t)13 << '\n';

	Permebuf<permebuf::perception::test::HelloWorld> hello_world;
	hello_world->SetBool1(true);
	hello_world->SetBool5(true);
	hello_world->SetBool9(true);
	hello_world->SetName("testy name");

	std::cout << "Bool1: " << hello_world->GetBool1() << std::endl;
	std::cout << "Bool5: " << hello_world->GetBool5() << std::endl;
	std::cout << "Bool9: " << hello_world->GetBool9() << std::endl;
	std::cout << "Name: " << *hello_world->GetName() << std::endl;
	std::cout << "My name is " << perception::GetProcessName() << std::endl;
	return 0;
}