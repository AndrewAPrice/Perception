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

const {getToolPath} = require('./tools');
const {escapePath} = require('./escape_path');
const {PackageType} = require('./package_type');
const {rootDirectory} = require('./root_directory');

// Commands for compiling.
const ASM_COMMAND = getToolPath('nasm') + ' -felf64';
const GAS_COMMAND = getToolPath('gcc') + ' -c';
const CC_KERNEL_COMMAND = getToolPath('gcc') + ' -m64 -mcmodel=kernel ' +
	'-ffreestanding -fno-builtin -nostdlib -nostdinc -mno-red-zone  -c ' +
	'-msoft-float -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-avx ' +
	'-mno-avx2 -MD -MF temp.d  -O3 -isystem ' + escapePath(rootDirectory) + 'Kernel/source ';
const CC_COMMAND = getToolPath('gcc') + ' -g -fverbose-asm -m64 -ffreestanding -nostdlib ' +
	'-nostdinc++ -mno-red-zone -c -std=c++17 -MD -MF temp.d ';
const C_COMMAND = getToolPath('gcc') + ' -g -m64 -ffreestanding -nostdlib ' +
	'-mno-red-zone -c -MD -MF temp.d ';

// Commands for linking.
const KERNEL_LINKER_COMMAND = getToolPath('ld') + ' -nodefaultlibs -T ' + escapePath(rootDirectory) + 'Kernel/source/linker.ld -o %o %i';
// const KERNEL_LINKER_COMMAND = getToolPath('gcc') + ' -nostdlib -nodefaultlibs -T ' + rootDirectory + 'Kernel/source/linker.ld ';
const LIBRARY_LINKER_COMMAND = getToolPath('ar') + ' rvs -o %o %i';
const APPLICATION_LINKER_COMMAND = getToolPath('gcc') + ' -nostdlib -nodefaultlibs -nolibc -nostartfiles -z max-page-size=1 -T userland.ld -o %o -Wl,--start-group %i -Wl,--end-group';

// Gets the build command to use for a file.
function getBuildCommand(filePath, packageType, cParams) {
	if (filePath.endsWith('.cc') || filePath.endsWith('.cpp'))
		return CC_COMMAND + cParams;
	else if (filePath.endsWith('.c'))
		return packageType == PackageType.KERNEL ? CC_KERNEL_COMMAND : C_COMMAND + cParams;
	else if (filePath.endsWith('.asm'))
		return ASM_COMMAND;
	else if (filePath.endsWith('.s') || filePath.endsWith('.S'))
		return GAS_COMMAND;
	else if (filePath.endsWith('.h') || filePath.endsWith('.inl') || filePath.endsWith('.ld') || filePath.endsWith('.txt')
		|| filePath.endsWith('.DS_Store'))
		return '';
	else {
		// console.log('Don\'t know how to build ' + filePath);
		return '';
	}
}

function getLinkerCommand(packageType, outputFile, inputFiles) {
	let command = '';
	switch(packageType) {
		case PackageType.KERNEL:
			command = KERNEL_LINKER_COMMAND;
			break;
		case PackageType.APPLICATION:
			command = APPLICATION_LINKER_COMMAND;
			break;
		case PackageType.LIBRARY:
			command = LIBRARY_LINKER_COMMAND;
	}

	return command.replace('%o', outputFile).replace('%i', inputFiles);
}

module.exports = {
	getBuildCommand: getBuildCommand,
	getLinkerCommand: getLinkerCommand
};