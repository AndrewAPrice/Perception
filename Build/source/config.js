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

const fs = require('fs');
const {escapePath} = require('./utils');

function PopulateConfigJson() {
  const toolsPaths = {};
  ['ar', 'gas', 'nasm', 'gcc', 'grub-mkrescue', 'ld', 'qemu'].forEach(
      (tool) => {
        if (!toolPaths[tool]) {
          toolPaths[tool] = '';
        }
      });

  fs.writeFileSync(
      'config.json', JSON.stringify({'tools': toolPaths, 'parallel_tasks': 1}));
};

// Loads the config file that contains the tools to use on this system.
if (!fs.existsSync('config.json')) {
  console.log('Please fill in config.json and read ../building.md.');
  PopulateConfigJson();
  process.exit(1);
}
const config = JSON.parse(fs.readFileSync('config.json'));
const toolPaths = config.tools;

function getToolPath(tool) {
  const toolPath = toolPaths[tool];
  if (!toolPath) {
    console.log(
        'config.json doesn\'t contain an entry for "' + tool +
        '\'. Please read ../building.md.');
    PopulateToolsJson();
    process.exit(1);
  }
  return escapePath(toolPath);
}

function getParallelTasks() {
  return config.parallel_tasks;
}

function setAndFlushParallelTasks(numTasks) {
  config.parallel_tasks = numTasks;
  fs.writeFileSync(
      'config.json', JSON.stringify(config));
}

module.exports = {
  getToolPath : getToolPath,
  getParallelTasks: getParallelTasks,
  setAndFlushParallelTasks: setAndFlushParallelTasks
};
