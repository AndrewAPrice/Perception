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

function generateBuildCommand(language, buildSettings) {
	let cParams = ' -D' + buildSettings.os.toUpperCase().replace(/-/g, '_') + ' ' +
					' -D' + buildSettings.build + '_BUILD_ '
					 + '-fdata-sections -ffunction-sections ';
	if (buildSettings.build == 'optimized')
		cParams += '-g -O3 ';
	else if (buildSettings.build == 'debug')
		cParams += '-g -Og ';

	const isLocalBuild = buildSettings.os != 'Perception';

	if (language == 'C++') {
		let command = '';
		if (isLocalBuild)
			return getToolPath('local-gcc') + ' -c -std=c++17 -MD -MF temp.d' + cParams;
		else
			return getToolPath('gcc') + ' -fverbose-asm -m64 -ffreestanding -nostdlib ' +
				'-nostdinc++ -mno-red-zone -c -std=c++17 -MD -MF temp.d' + cParams;
	} else if (language == 'C') {
		if (isLocalBuild)
			return getToolPath('local-gcc') + ' -c -MD -MF temp.d' + cParams;
		else
			return getToolPath('gcc') + ' -D PERCEPTION -m64 -ffreestanding -nostdlib ' +
			'-mno-red-zone -c -MD -MF temp.d' + cParams;
	} else if (language == 'Kernel C') {
		return  getToolPath('gcc') + ' -m64 -mcmodel=kernel ' +
			'-ffreestanding -fno-builtin -nostdlib -nostdinc -mno-red-zone  -c ' +
			'-msoft-float -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-avx ' +
			'-mno-avx2 -MD -MF temp.d  -O3 -isystem ' + escapePath(rootDirectory) + 'Kernel/source ' + cParams;
	} else if (language == 'Intel ASM') {
		if (isLocalBuild)
			return '';
		else
			return getToolPath('nasm') + ' -felf64 -dPERCEPTION';
	} else if (language == 'AT&T ASM') {
		return getToolPath('gcc') + ' -c';
	} else {
		console.log('Unhandled language: ' + language);
		return '';
	}
}

// Cached build commands.
const buildCommands = {};

// Gets the build command to use for a file.
function getBuildCommand(filePath, packageType, cParams, buildSettings) {
	let language = '';
	let addCParams = false;
	if (filePath.endsWith('.cc') || filePath.endsWith('.cpp')) {
		language = 'C++';
		addCParams = true;
	} else if (filePath.endsWith('.c')) {
		language = packageType == PackageType.KERNEL ? 'Kernel C' : 'C';
		addCParams = true;
	} else if (filePath.endsWith('.asm')) {
		language = 'Intel ASM';
	} else if (filePath.endsWith('.s') || filePath.endsWith('.S')) {
		language = 'AT&T ASM';
	}

	if (language == '')
		return '';

	if (!buildCommands[language])
		buildCommands[language] = generateBuildCommand(language, buildSettings);

	return addCParams ? buildCommands[language] + cParams : buildCommands[language];
}

function getLinkerCommand(packageType, outputFile, inputFiles, buildSettings) {
	switch(packageType) {
		case PackageType.KERNEL: {
			let extras = '';
			if (buildSettings.build == 'optimized') {
				 extras = ' -s ';
			} else {
				extras += ' -g ';
			}
			return getToolPath('ld') + ' -nodefaultlibs ' + extras + ' -T ' +
				escapePath(rootDirectory) + 'Kernel/source/linker.ld -o ' +
				outputFile + ' ' + inputFiles;
			// getToolPath('gcc') + ' -nostdlib -nodefaultlibs -T ' + rootDirectory + 'Kernel/source/linker.ld '
		}
		case PackageType.APPLICATION: {
			let extras = ' -Wl,--gc-sections ';
			if (buildSettings.build == 'optimized') {
				extras += ' -O3 -g -s';
			} else {
				extras += ' -g ';

			}
			if (buildSettings.os == 'Perception')
				return getToolPath('gcc') + extras + ' -nostdlib  -nodefaultlibs ' +
					' -nolibc -nostartfiles -z max-page-size=1 -T userland.ld -o ' + outputFile +
					' -Wl,--start-group ' + inputFiles + ' -Wl,--end-group -Wl,-lgcc';
			else {
				// whole-archive/no-whole-archive is less efficient than --start-group/--end-group because it links in dead code, but
				// the LD that comes with MacOS doesn't support --start-group/--end-group.
				// let startGroup = buildSettings.os == 'Darwin' ? '--whole-archive' : '--start-group';
				// let endGroup = buildSettings.os == 'Darwin' ? '--no-whole-archive' : '--end-group';
				// return getToolPath('local-gcc') + extras + ' -o ' + outputFile + ' -Wl,' + startGroup + ' ' +
				//	inputFiles + ' -Wl,' + endGroup;
				return getToolPath('local-gcc') + extras + ' -o ' + outputFile + ' ' + inputFiles;
			}
		}
		case PackageType.LIBRARY:
		    if (buildSettings.os == 'Perception') {
				return getToolPath('ar') + ' rvs -o ' + outputFile + ' '  + inputFiles;
			} else {
				return getToolPath('local-ar') + ' -rvs ' + outputFile + ' '  + inputFiles;
			}
	}
}

function buildPrefix(buildSettings) {
	if (buildSettings.test)
		return 'test';
	else
		return buildSettings.os + '-' + buildSettings.build;
}

module.exports = {
	getBuildCommand: getBuildCommand,
	getLinkerCommand: getLinkerCommand,
	buildPrefix: buildPrefix
};