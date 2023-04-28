// Copyright 2023 Google LLC
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

const { getPackageBuildDirectory } = require('./package_directory');

// Libraries already built on this run, therefore we shouldn't have to build
// them again.
let alreadyBuiltLibraries = {};
let librariesWithObjectFiles = {};
let librariesGoingToBeBuilt = {};

function forgetAlreadyBuiltLibraries() {
    alreadyBuiltLibraries = {};
    librariesWithObjectFiles = {};
    librariesGoingToBeBuilt = {};
}

function libraryBinaryPath(packageType, packageName) {
    const packageDirectory =
        getPackageBuildDirectory(packageType, packageName);
    return packageDirectory + packageName + '.lib';
}

module.exports = {
    alreadyBuiltLibraries: alreadyBuiltLibraries,
    librariesWithObjectFiles: librariesWithObjectFiles,
    librariesGoingToBeBuilt: librariesGoingToBeBuilt,
    forgetAlreadyBuiltLibraries: forgetAlreadyBuiltLibraries,
    libraryBinaryPath: libraryBinaryPath
};