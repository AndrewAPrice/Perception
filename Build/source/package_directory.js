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

const { getKernelDirectory, getApplicationDirectories, getLibraryDirectories, getTempDirectory } = require('./config');
const { PackageType } = require('./package_type');
const { buildPrefix, buildSettings } = require('./build_commands');

const fs = require('fs');

let cachedApplicationDirectories = null;
let cachedLibraryDirectories = null;
let allApplications = null;
let allLibraries = null;

function scanForApplications() {
  cachedApplicationDirectories = {};
  getApplicationDirectories().forEach(applicationDirectory => {
    let applicationsFileEntries = fs.readdirSync(applicationDirectory);
    for (let i = 0; i < applicationsFileEntries.length; i++) {
      const applicationName = applicationsFileEntries[i];
      const applicationPath = applicationDirectory + '/' + applicationName;
      const fileStats = fs.lstatSync(applicationPath);
      if (fileStats.isDirectory()) {
        cachedApplicationDirectories[applicationName] = applicationPath + '/';
      }
    }
  });

  allApplications = Object.keys(cachedApplicationDirectories);
}

function scanForLibraries() {
  cachedLibraryDirectories = {};
  getLibraryDirectories().forEach(libraryDirectory => {
    let libraryFileEntries = fs.readdirSync(libraryDirectory);
    for (let i = 0; i < libraryFileEntries.length; i++) {
      const libraryName = libraryFileEntries[i];
      const libraryPath = libraryDirectory + '/' + libraryName;
      const fileStats = fs.lstatSync(libraryPath);
      if (fileStats.isDirectory()) {
        cachedLibraryDirectories[libraryName] = libraryPath + '/';
      }
    }
  });

  allLibraries = Object.keys(cachedLibraryDirectories);
}

// Gets the directory for a package type and package name.
function getPackageDirectory(packageType, packageName) {
  switch (packageType) {
    case PackageType.APPLICATION: {
      if (!allApplications) scanForApplications();
      if (cachedApplicationDirectories[packageName])
        return cachedApplicationDirectories[packageName];

      console.log('Cannot find application ' + packageName);
      return '';
    }
    case PackageType.LIBRARY: {
      if (!allLibraries) scanForLibraries();
      if (cachedLibraryDirectories[packageName])
        return cachedLibraryDirectories[packageName];

      console.log('Cannot find library ' + packageName);
      return '';
    }
    case PackageType.KERNEL:
      return getKernelDirectory() + '/';
    default:
      return '';
  }
}

function getBuildPackageDirectorySuffix(packageType, packageName) {
  switch (packageType) {
    case PackageType.APPLICATION:
      return 'Applications/' + packageName + '/';
    case PackageType.LIBRARY:
      return 'Libraries/' + packageName + '/';
    case PackageType.KERNEL:
      return 'Kernel' + '/';
    default:
      return '';
  }
}

function getPackageBuildDirectory(packageType, packageName) {
  return getTempDirectory() + '/' + buildPrefix() + '/' +
    getBuildPackageDirectorySuffix(packageType, packageName);
}

function getPackageBinary(packageType, packageName) {
  switch (packageType) {
    case PackageType.APPLICATION:
      break;
    case PackageType.LIBRARY:
      return libraryBinaryPath(packageType, packageName);
    case PackageType.KERNEL:
      packageName = 'kernel';
      break;
  }
  return getPackageBuildDirectory(packageType, packageName) + packageName + '.app';
}

function getAllApplications() {
  if (!allApplications) scanForApplications();
  return allApplications;
}

function getAllLibraries() {
  if (!allLibraries) scanForLibraries();
  return allLibraries;
}

module.exports = {
  getPackageBinary: getPackageBinary,
  getPackageDirectory: getPackageDirectory,
  getPackageBuildDirectory: getPackageBuildDirectory,
  getAllApplications: getAllApplications,
  getAllLibraries: getAllLibraries
};
