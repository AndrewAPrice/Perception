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

function generateBuildCommand(language, cParams, inputFile, outputFile) {
  cParams += ' -D' + buildSettings.os.toUpperCase().replace(/-/g, '_') +
    ' --target=x86_64-unknown-none-elf ' +
    ' -D' + buildSettings.build + '_BUILD_ ' +
    '-fdata-sections -ffunction-sections -nostdinc ';
  if (buildSettings.build == 'optimized')
    cParams += '-g -O3  -fomit-frame-pointer -flto ';
  else if (buildSettings.build == 'debug') {
    cParams += '-g -Og ';
    if (buildSettings.test)
      cParams += '-D __TEST__ ';
  }

  const isLocalBuild = buildSettings.os != 'Perception';

  if (language == 'C++') {
    let command = '';
    if (isLocalBuild)
      return getToolPath('local-gcc') + ' -c -std=c++20 -MD -MF ${deps}' +
        cParams + ' -o ' + escapePath(outputFile) + ' ' + escapePath(inputFile);
    else
      return getToolPath('gcc') +
        ' -fverbose-asm -m64 -ffreestanding -nostdlibinc ' +
        '-nostdinc++ -mno-red-zone -c -std=c++20 -MD -MF ${deps}' + cParams + ' -o ' + escapePath(outputFile) + ' ' + escapePath(inputFile);
  } else if (language == 'C') {
    if (isLocalBuild)
      return getToolPath('local-gcc') + ' -c -std=c17 -MD -MF ${deps}' +
        cParams + ' -o ' + escapePath(outputFile) + ' ' + escapePath(inputFile);
    else
      return getToolPath('gcc') +
        ' -D PERCEPTION -std=c17 -m64 -ffreestanding ' +
        '-mno-red-zone -c -MD -MF ${deps}' + cParams + ' -o ' + escapePath(outputFile) + ' ' + escapePath(inputFile);
  } else if (language == 'Kernel C++') {
    return getToolPath('gcc') + ' -mcmodel=kernel ' +
      '-ffreestanding -fno-builtin  -c -std=c++20  -mno-sse ' +
      '-MD -MF ${deps}  -O3 -isystem ' + escapePath(getKernelDirectory() + '/source')
      + ' ' + cParams + ' -o ' + escapePath(outputFile) + ' ' + escapePath(inputFile);
  }  else if (language == 'Kernel C') {
    return getToolPath('gcc') + ' -mcmodel=kernel ' +
      '-ffreestanding -fno-builtin  -mno-sse -c ' +
      '-MD -MF ${deps}  -O3 -isystem ' + escapePath(getKernelDirectory() + '/source')
      + ' ' + cParams + ' -o ' + escapePath(outputFile) + ' ' + escapePath(inputFile);
  } else if (language == 'Intel ASM') {
    return getToolPath('nasm')
      + ' -i ' + escapePath(path.dirname(inputFile)) + ' -felf64 -dPERCEPTION -o ' + escapePath(outputFile) + ' ' + escapePath(inputFile);
  /* } else if (language == 'Intel ASM') {
    return getToolPath('gcc')
      + ' -masm=intel --language=assembler-with-cpp -isystem ' + escapePath(path.dirname(inputFile)) +
      ' ' + cParams + ' -c -o ' + escapePath(outputFile) + ' ' +
      escapePath(inputFile); */
  } else if (language == 'AT&T ASM') {
    return getToolPath('gcc')
      + ' -isystem ' + escapePath(path.dirname(inputFile)) +
      ' ' + cParams + ' -c -o ' + escapePath(outputFile) + ' ' +
      escapePath(inputFile);
  } else {
    console.log('Unhandled language: ' + language);
    return '';
  }
}

// Gets the build command to use for a file.
function getBuildCommand(filePath, packageType, cParams, outputFile) {
  let language = '';
  if (filePath.endsWith('.cc') || filePath.endsWith('.cpp')) {
    language = packageType == PackageType.KERNEL && !buildSettings.test ? 'Kernel C++' : 'C++';
  } else if (filePath.endsWith('.c')) {
    language = packageType == PackageType.KERNEL && !buildSettings.test ? 'Kernel C' : 'C';
  } else if (filePath.endsWith('.asm')) {
    language = 'Intel ASM';
  } else if (filePath.endsWith('.s') || filePath.endsWith('.S')) {
    language = 'AT&T ASM';
  }

  if (language == '') return '';
  return generateBuildCommand(language, cParams, filePath, outputFile);
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
      return getToolPath('ld') + ' -z max-page-size=4096' +
        // ' -flto ' + // -fwhole-program  -fuse-ld=gold ' +
        extras + ' -T ' + getKernelDirectory() +
        '/source/linker.ld -o ' + outputFile + ' ' + inputFiles;
    }
    case PackageType.APPLICATION: {
      let extras = '  ';
      if (buildSettings.build == 'optimized') {
        extras += ' -O3 -g -s';
      } else {
        extras += ' -g ';
      }
      if (buildSettings.os == 'Perception')
        return getToolPath('ld') + extras + ' -nostdlib ' +
          ' --gc-sections' +
          '  -z max-page-size=1 -o ' +
          outputFile + ' --start-group ' + inputFiles +
          ' --end-group';
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
        return getToolPath('ar') + ' rvs ' + outputFile + ' ' + inputFiles;
      } else {
        return getToolPath('local-ar') + ' -rvs ' + outputFile + ' ' +
          inputFiles;
      }
  }
}

function getTestLinkerCommand(outputFile, testObjectFile, libraryFiles) {
  return getToolPath('local-gcc') + ' -o ' + outputFile + ' ' +
    testObjectFile + ' ' + libraryFiles;
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
  getTestLinkerCommand: getTestLinkerCommand,
  buildPrefix: buildPrefix,
  buildSettings: buildSettings
};