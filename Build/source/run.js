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
const child_process = require('child_process');
const { build } = require('./build');
const { buildImage } = require('./build_image');
const { buildSettings } = require('./build_commands');
const { escapePath } = require('./utils');
const { PackageType } = require('./package_type');
const { getToolPath, getOutputPath } = require('./config');
const { runDeferredCommands } = require('./deferred_commands');
const { monitorProcess } = require('./monitor');

const EMULATOR_COMMAND = getToolPath('qemu');

const EMULATOR_ARGS = [
  '-boot', 'd', '-cdrom',
  getOutputPath(),
  '-m', '512', '-serial', 'stdio'];

// For debugging the kernel, it's useful to add '-no-reboot -d int,cpu_reset'.

// Builds everything and runs the emulator.
async function run(package) {
  if (buildSettings.test) {
    console.log('Can\'t run with --test.');
    return;
  }
  if (buildSettings.os == 'Perception') {
    if (package != '') {
      console.log(
        '\'build run\' builds everything into a disk image. If you\'re trying to run a particular application locally, please pass --local.');
      process.exit(-1);
    }
    if (await buildImage()) {
      await monitorProcess(child_process.spawn(EMULATOR_COMMAND, EMULATOR_ARGS));
    }
  } else {
    if (package == '') {
      console.log(
        'When passing --local to \'build run\', you need to add the name of the application you want to run.');
      process.exit(-1);
    }
    if (!(await build(PackageType.APPLICATION, package))) {
      return;
    }

    if (!(await runDeferredCommands())) {
      return;
    }

    // TODO:
    /* child_process.execSync(
        escapePath(
            rootDirectory + 'Applications/' + package + '/build/' +
            buildPrefix() + '.app'),
        {stdio: 'inherit'});*/
  }
}

module.exports = {
  run: run
};
