// Copyright 2022 Google LLC
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

const fs = require('fs');
const path = require('path');
const {performance} = require('node:perf_hooks');
const child_process = require('child_process');
const {build} = require('./build');
const {clean} = require('./clean');
const {buildSettings} = require('./build_commands');
const {setAndFlushParallelTasks} = require('./config');
const {rootDirectory} = require('./root_directory');
const {escapePath} = require('./utils');
const {getPackageBuildDirectory} = require('./package_directory');
const {getFileLastModifiedTimestamp} = require('./file_timestamps');
const {PackageType} = require('./package_type');
const {applicationsWithAssets, librariesWithAssets} = require('./assets');
const {runDeferredCommands} = require('./deferred_commands');

function quitWithMessage() {
	console.log(' ');
  console.log('Fix the build errors before benchmarking');
  process.exit(1);
}

async function benchmarkWithNumTasks(parallelTasks) {
  if (parallelTasks <= 0) {
    return Infinity;
  }


  process.stdout.write('Building with ' + parallelTasks + ': ');
  // Clean everything to start from scratch.
  clean();

  // Prepare the kernel and all applications.
  if (!(await build(PackageType.KERNEL, ''))) {
    quitWithMessage();
  }

  let applicationsFileEntries = fs.readdirSync(rootDirectory + 'Applications/');
  for (let i = 0; i < applicationsFileEntries.length; i++) {
    const applicationName = applicationsFileEntries[i];

    const fileStats =
        fs.lstatSync(rootDirectory + 'Applications/' + applicationName);
    if (fileStats.isDirectory()) {
      if (!(await build(PackageType.APPLICATION, applicationName))) {
        quitWithMessage();
      }
    }
  }


  buildSettings.parallelTasks = parallelTasks;
  const startTime = performance.now();
  // Measure how long it takes to run the deferred commands.
  if (!(await runDeferredCommands(/*silent=*/true))) {
    quitWithMessage();
  }
  const time = performance.now() - startTime;
  console.log(time);
  return time;
}

async function findFastestNumberOfTasks() {
  const taskTimes = {};
  async function getTimeWithNumTasks(tasks) {
    if (!taskTimes[tasks]) {
      taskTimes[tasks] = await benchmarkWithNumTasks(tasks);
    }
    return taskTimes[tasks];
  }
  // Start from 1 and keep doubling until we get slower.
  let aTime = await getTimeWithNumTasks(1);

  let bTime = await getTimeWithNumTasks(2);
  let maxTasks = 2;

  while (bTime <= aTime) {
    aTime = bTime;
    maxTasks *= 2;
    bTime = await getTimeWithNumTasks(maxTasks);
  }

  // Create a window from minTasks->maxTasks, and keep reducing it
  // until we reach a windowSize of 1.
  let middleTask = maxTasks / 2;
  let windowSize = maxTasks / 2;

  while (true) {
    let sizesToTest = [];
    if (windowSize == 1) {
      sizesToTest =
          [middleTask - windowSize, middleTask, middleTask + windowSize];
    } else {
      sizesToTest = [
        middleTask - windowSize, middleTask - windowSize / 2, middleTask,
        middleTask + windowSize / 2, middleTask + windowSize
      ];
    }

    let fastestTime = Infinity;
    let fastestTaskSize = 0;
    for (let i = 0; i < sizesToTest.length; i++) {
      const sizeToTest = sizesToTest[i];
      const time = await getTimeWithNumTasks(sizeToTest);
      if (time < fastestTime) {
        fastestTime = time;
        fastestTaskSize = sizeToTest;
      }
    }

    if (windowSize <= 2) {
      // We've tested all values in this window, so we can return the fastest
      // task size.
      return fastestTaskSize;
    } else {
      // Shrink the window around the middle task.
      middleTask = fastestTaskSize;
      windowSize /= 2;
    }
  }
}

async function benchmark() {
  const fastestNumberOfTasks = await findFastestNumberOfTasks();
  setAndFlushParallelTasks(fastestNumberOfTasks);
  console.log('We will now build with ' + fastestNumberOfTasks + ' task(s).');
}

module.exports = {
  benchmark : benchmark
};