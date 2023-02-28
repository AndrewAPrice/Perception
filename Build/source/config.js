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
const { escapePath } = require('./utils');

const configFile = 'config.json';
const localConfigFile = 'local-config.json';

function PopulateLocalConfigJson() {
  const toolsPaths = {};
  ['ar', 'gas', 'nasm', 'gcc', 'grub-mkrescue', 'ld', 'qemu'].forEach(
    (tool) => {
      if (!toolPaths[tool]) {
        toolPaths[tool] = '';
      }
    });

  fs.writeFileSync(
    localConfigFile, JSON.stringify({ 'tools': toolPaths, 'parallel_tasks': 1 }));
};

// Loads the config file that contains the tools to use on this system.
if (!fs.existsSync(localConfigFile)) {
  console.log('Please fill in ' + localConfigFile + ' and read ../building.md.');
  PopulateLocalConfigJson();
  process.exit(1);
}
const localConfig = JSON.parse(fs.readFileSync(localConfigFile));
const config = JSON.parse(fs.readFileSync(configFile));
const toolPaths = localConfig.tools;

function getToolPath(tool) {
  const toolPath = toolPaths[tool];
  if (!toolPath) {
    console.log(
      localConfigFile + ' doesn\'t contain an entry for "' + tool +
      '\'. Please read ../building.md.');
    PopulateToolsJson();
    process.exit(1);
  }
  return escapePath(toolPath);
}

function getParallelTasks() {
  return localConfig.parallel_tasks;
}

function setAndFlushParallelTasks(numTasks) {
  localConfig.parallel_tasks = numTasks;
  fs.writeFileSync(
    localConfigFile, JSON.stringify(localConfig));
}

function getKernelDirectory() {
  return config.repositories.kernel;
}

function getApplicationDirectories() {
  return config.repositories.applications;
}

function getLibraryDirectories() {
  return config.repositories.libraries;
}

function getTempDirectory() {
  return config.tempDirectory;
}

function getFileSystemDirectory() {
  return config.fileSystemDirectory;
}

function getOutputPath() {
  return config.outputPath;
}

module.exports = {
  getToolPath: getToolPath,
  getParallelTasks: getParallelTasks,
  setAndFlushParallelTasks: setAndFlushParallelTasks,
  getKernelDirectory: getKernelDirectory,
  getApplicationDirectories: getApplicationDirectories,
  getLibraryDirectories: getLibraryDirectories,
  getTempDirectory: getTempDirectory,
  getFileSystemDirectory: getFileSystemDirectory,
  getOutputPath: getOutputPath
};
