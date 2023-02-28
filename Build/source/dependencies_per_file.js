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
const {buildPrefix} = require('./build_commands');
const { getTempDirectory } = require('./config');

let dependenciesPerFile = undefined;
let anythingChanged = false;

function getDependenciesFilePath() {
  return getTempDirectory() + '/' + buildPrefix() + '/dependencies.json';
}

function loadDependenciesFile() {
  const depsFile = getDependenciesFilePath();
  dependenciesPerFile =
      fs.existsSync(depsFile) ? JSON.parse(fs.readFileSync(depsFile)) : {};
}

function getDependenciesOfFile(file) {
  if (!dependenciesPerFile) loadDependenciesFile();

  return dependenciesPerFile[file];
}

function setDependenciesOfFile(file, dependencies) {
  if (!dependenciesPerFile) loadDependenciesFile();

  dependenciesPerFile[file] = dependencies;
  anythingChanged = true;
}

function flushDependenciesForFile() {
  if (!dependenciesPerFile || !anythingChanged) return;

  if (anythingChanged) {
    fs.writeFileSync(
        getDependenciesFilePath(), JSON.stringify(dependenciesPerFile));
  }
}

module.exports = {
  getDependenciesOfFile : getDependenciesOfFile,
  setDependenciesOfFile : setDependenciesOfFile,
  flushDependenciesForFile : flushDependenciesForFile
}