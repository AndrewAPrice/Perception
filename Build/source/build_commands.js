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

const path = require('path');
const { getToolPath, getParallelTasks, getKernelDirectory } = require('./config');
const { escapePath } = require('./utils');
const { PackageType } = require('./package_type');

const buildSettings = {
  os: 'Perception',
  build: 'optimized',
  compile: true,
  test: false,
  parallelTasks: getParallelTasks()
};

function generateBuildCommand(language, filePath) {
  let cParams = ' -D' + buildSettings.os.toUpperCase().replace(/-/g, '_') +
    ' ' +
    ' -D' + buildSettings.build + '_BUILD_ ' +
    '-fdata-sections -ffunction-sections ';
  if (buildSettings.build == 'optimized')
    cParams += '-g -O3  -fomit-frame-pointer ';
  else if (buildSettings.build == 'debug')
    cParams += '-g -Og ';

  const isLocalBuild = buildSettings.os != 'Perception';

  if (language == 'C++') {
    let command = '';
    if (isLocalBuild)
      return getToolPath('local-gcc') + ' -c -std=c++20 -MD -MF ${deps}' +
        cParams;
    else
      return getToolPath('gcc') +
        ' -fverbose-asm -m64 -ffreestanding -nostdlib ' +
        '-nostdinc++ -mno-red-zone -c -std=c++20 -MD -MF ${deps}' + cParams;
  } else if (language == 'C') {
    if (isLocalBuild)
      return getToolPath('local-gcc') + ' -c -std=c17 -MD -MF ${deps}' +
        cParams;
    else
      return getToolPath('gcc') +
        ' -D PERCEPTION -std=c17 -m64 -ffreestanding -nostdlib ' +
        '-mno-red-zone -c -MD -MF ${deps}' + cParams;
  } else if (language == 'Kernel C') {
    return getToolPath('gcc') + ' -m64 -mcmodel=kernel ' +
      '-ffreestanding -fno-builtin -nostdlib -nostdinc -mno-red-zone  -c ' +
      '-msoft-float -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-avx ' +
      '-mno-avx2 -MD -MF ${deps}  -O3 -isystem ' + escapePath(getKernelDirectory() + '/source')
      + ' ' + cParams;
  } else if (language == 'Intel ASM') {
    if (isLocalBuild)
      return '';
    else {
      return getToolPath('nasm') + ' -felf64 -dPERCEPTION';
    }
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

    addCParams = true;
    cParams = ' -i ' + escapePath(path.dirname(filePath));
  } else if (filePath.endsWith('.s') || filePath.endsWith('.S')) {
    language = 'AT&T ASM';
  }

  if (language == '') return '';

  if (!buildCommands[language])
    buildCommands[language] = generateBuildCommand(language, filePath);

  return addCParams ? buildCommands[language] + cParams :
    buildCommands[language];
}

function getLinkerCommand(packageType, outputFile, inputFiles) {
  switch (packageType) {
    case PackageType.KERNEL: {
      let extras = '';
      if (buildSettings.build == 'optimized') {
        extras = ' -s ';
      } else {
        extras += ' -g ';
      }
      return getToolPath('ld') + ' -z nodefaultlibs -z max-page-size=4096 ' +
        extras + ' -T ' + getKernelDirectory() +
        '/source/linker.ld -o ' + outputFile + ' ' + inputFiles;
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
          ' -nolibc -nostartfiles -z max-page-size=1 -T userland.ld -o ' +
          outputFile + ' -Wl,--start-group ' + inputFiles +
          ' -Wl,--end-group -Wl,-lgcc';
      else {
        // whole-archive/no-whole-archive is less efficient than
        // --start-group/--end-group because it links in dead code, but the LD
        // that comes with MacOS doesn't support --start-group/--end-group. let
        // startGroup = buildSettings.os == 'Darwin' ? '--whole-archive' :
        // '--start-group'; let endGroup = buildSettings.os == 'Darwin' ?
        // '--no-whole-archive' : '--end-group'; return getToolPath('local-gcc')
        // + extras + ' -o ' + outputFile + ' -Wl,' + startGroup + ' ' +
        //  inputFiles + ' -Wl,' + endGroup;
        return getToolPath('local-gcc') + extras + ' -o ' + outputFile + ' ' +
          inputFiles;
      }
    }
    case PackageType.LIBRARY:
      if (buildSettings.os == 'Perception') {
        return getToolPath('ar') + ' rvs -o ' + outputFile + ' ' + inputFiles;
      } else {
        return getToolPath('local-ar') + ' -rvs ' + outputFile + ' ' +
          inputFiles;
      }
  }
}

function buildPrefix() {
  if (buildSettings.test)
    return 'test';
  else
    return buildSettings.os + '-' + buildSettings.build;
}

module.exports = {
  getBuildCommand: getBuildCommand,
  getLinkerCommand: getLinkerCommand,
  buildPrefix: buildPrefix,
  buildSettings: buildSettings
};