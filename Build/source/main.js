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

const process = require('process');
const os = require('os');
const {benchmark} = require('./benchmark');
const {build} = require('./build');
const {buildSettings} = require('./build_commands');
const {runDeferredCommands} = require('./deferred_commands');
const {buildImage} = require('./build_image');
const {clean} = require('./clean');
const {PackageType} = require('./package_type');
const {run} = require('./run');
const {test} = require('./test');

// Parse the input.
let command = '';
let package = '';
for (let argIndex = 2; argIndex < process.argv.length; argIndex++) {
  const arg = process.argv[argIndex];
  switch (arg) {
    case '--local':
      buildSettings.os = os.type();
      break;
    case '--dbg':
      buildSettings.build = 'debug';
      break;
    case '--optimized':
      buildSettings.build = 'optimized';
      break;
    case '--fast':
      buildSettings.build = 'fast';
      break;
    case '--test':
      buildSettings.os = os.type();
      buildSettings.build = 'debug';
      buildSettings.test = true;
      break;
    case '--prepare':
      buildSettings.compile = false;
      break;
    default:
      if (command == '')
        command = arg;
      else
        package = arg;
      break;
  }
}

// Parses the input.
switch (command) {
  case 'application':
    build(PackageType.APPLICATION, package).then(() => runDeferredCommands());
    break;
  case 'library':
    build(PackageType.LIBRARY, package).then(() => runDeferredCommands());
    break;
  case 'kernel':
    build(PackageType.KERNEL, '').then(() => runDeferredCommands());
    break;
  case 'run':
    run(package);
    break;
  case 'deep-clean':
    clean(/*deepClean=*/true);
    break;
  case 'clean':
    clean(/*deepClean=*/false);
    break;
  case 'all':
    buildImage();
    break;
  case 'benchmark':
    benchmark();
  case 'help':
  case undefined:
    break;
  default:
    console.log('Don\'t know what to compile. Argument was ' + process.argv[2]);
    break;
}
