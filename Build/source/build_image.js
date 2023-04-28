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
const path = require('path');
const child_process = require('child_process');
const { build } = require('./build');
const { buildSettings } = require('./build_commands');
const { getToolPath, getFileSystemDirectory, getOutputPath } = require('./config');
const { escapePath } = require('./utils');
const { getPackageBuildDirectory, getAllLibraries, getAllApplications, getPackageDirectory } = require('./package_directory');
const { getFileLastModifiedTimestamp } = require('./file_timestamps');
const { PackageType } = require('./package_type');
const { applicationsWithAssets, librariesWithAssets } = require('./assets');
const { runDeferredCommands } = require('./deferred_commands');
const { maybeQueueAllDeferredTests } = require('./test')

const GRUB_MKRESCUE_COMMAND = getToolPath('grub-mkrescue') + ' -o ' +
  escapePath(getOutputPath()) + ' ' + escapePath(getFileSystemDirectory());

function copyIfNewer(source, destination) {
  if (!fs.existsSync(source)) return false;

  if (fs.existsSync(destination)) {
    // Don't copy if the source older or same age as the destination.
    if (getFileLastModifiedTimestamp(source) <=
      getFileLastModifiedTimestamp(destination))
      return false;

    fs.unlinkSync(destination);
  }

  const destinationDir = path.parse(destination).dir;
  if (!fs.existsSync(destinationDir)) {
    fs.mkdirSync(destinationDir, { recursive: true });
  }

  console.log('Copying ' + destination);
  fs.copyFileSync(source, destination);
  return true;
}

function copyDirectoryRecursively(sourceDir, destinationDir) {
  if (!fs.existsSync(sourceDir)) return false;
  if (!fs.existsSync(destinationDir)) {
    fs.mkdirSync(destinationDir, { recursive: true });
  }

  // Copy each file in the source.
  let files = fs.readdirSync(sourceDir);

  for (let i = 0; i < files.length; i++) {
    const sourcePath = sourceDir + files[i];
    const destinationPath = destinationDir + files[i];
    const fileStats = fs.lstatSync(sourcePath);
    if (fileStats.isDirectory()) {
      copyDirectoryRecursively(sourcePath + '/', destinationPath + '/');
    } else if (fileStats.isFile()) {
      if (fs.existsSync(destinationPath)) {
        // Don't copy if the source is older or the same as as the destination.
        if (fileStats.mtimeMs <=
          getFileLastModifiedTimestamp(destinationPath)) {
          continue;
        }

        fs.unlinkSync(destinationPath);
      }

      console.log('Copying ' + destinationPath);
      fs.copyFileSync(sourcePath, destinationPath);
    }
  }
}

// Builds everything and turns it into an image.
async function buildImage() {
  let somethingChanged = false;
  const isLocalBuild = buildSettings.os != 'Perception';

  const libraries = getAllLibraries();
  const applications = getAllApplications();

  if (isLocalBuild) {
    // Build every library locally.
    for (let i = 0; i < libraries.length; i++) {
      const libraryName = libraries[i];
      success = await build(PackageType.LIBRARY, libraryName);
      if (!success) {
        return false;
      }
    }

    // Build every application locally.
    for (let i = 0; i < applications.length; i++) {
      const applicationName = applications[i];
      success = await build(PackageType.APPLICATION, applicationName);
      if (!success) {
        return false;
      }
    }

    await maybeQueueAllDeferredTests();

    return await runDeferredCommands();
  } else {
    // Build everything into an image.
    if (!(await build(PackageType.KERNEL, 'kernel'))) {
      return false;
    }

    // Build all applications.
    for (let i = 0; i < applications.length; i++) {
      const applicationName = applications[i];
      if (!(await build(PackageType.APPLICATION, applicationName))) {
        return false;
      }
    }

    if (!(await runDeferredCommands())) {
      return false;
    }

    somethingChanged |= copyIfNewer(
      getPackageBuildDirectory(PackageType.KERNEL, '') + 'kernel.app',
      getFileSystemDirectory() + '/boot/kernel.app');

    // Copy all applications
    for (let i = 0; i < applications.length; i++) {
      const applicationName = applications[i];
      const buildDirectory =
        getPackageBuildDirectory(PackageType.APPLICATION, applicationName);
      const fullPath = buildDirectory + applicationName + '.app';
      const packageDirectory = getPackageDirectory(PackageType.APPLICATION, applicationName);
      somethingChanged |= copyIfNewer(
        fullPath,
        getFileSystemDirectory() + '/Applications/' + applicationName + '/' +
        applicationName + '.app');

      somethingChanged |= copyIfNewer(
        packageDirectory + '/icon.svg',
        getFileSystemDirectory() + '/Applications/' + applicationName + '/icon.svg');

      somethingChanged |= copyIfNewer(
        packageDirectory + '/launcher.json',
        getFileSystemDirectory() + '/Applications/' + applicationName +
        '/launcher.json');
    }

    // Copy assets.
    Object.keys(applicationsWithAssets).forEach((application) => {
      copyDirectoryRecursively(
        getPackageDirectory(PackageType.APPLICATION, application) + '/assets/',
        getFileSystemDirectory() + '/Applications/' + application + '/assets/');
    });
    Object.keys(librariesWithAssets).forEach((library) => {
      copyDirectoryRecursively(
        getPackageDirectory(PackageType.LIBRARY, library) + '/assets/',
        getFileSystemDirectory() + '/Libraries/' + library + '/assets/');
    });


    if (!buildSettings.compile) {
      console.log('Nothing was compiled, so no image is built.');
      return false;
    }

    if (somethingChanged || !fs.existsSync(getOutputPath())) {
      let command = GRUB_MKRESCUE_COMMAND;
      try {
        child_process.execSync(command);
      } catch (exp) {
        return false;
      }
    }

    return true;
  }
}

module.exports = {
  buildImage: buildImage
};
