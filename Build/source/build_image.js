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
const {build} = require('./build');
const {buildPrefix} = require('./build_commands');
const {getToolPath} = require('./tools');
const {rootDirectory} = require('./root_directory');
const {escapePath} = require('./utils');
const {getFileLastModifiedTimestamp} = require('./file_timestamps');
const {PackageType} = require('./package_type');
const {applicationsWithAssets, librariesWithAssets} = require('./assets');

const GRUB_MKRESCUE_COMMAND = getToolPath('grub-mkrescue') + ' -o ' +
    escapePath(rootDirectory) + 'Perception.iso' +
    ' ' + rootDirectory + 'fs';

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
    fs.mkdirSync(destinationDir, {recursive: true});
  }

  console.log('Copying ' + destination);
  fs.copyFileSync(source, destination);
  return true;
}

function copyDirectoryRecursively(sourceDir, destinationDir) {
  if (!fs.existsSync(sourceDir)) return false;
  if (!fs.existsSync(destinationDir)) {
    fs.mkdirSync(destinationDir, {recursive: true});
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
async function buildImage(buildSettings) {
  let somethingChanged = false;
  const isLocalBuild = buildSettings.os != 'Perception';

  if (isLocalBuild) {
    // Build every library locally.
    let libraries = fs.readdirSync(rootDirectory + 'Libraries/');
    for (let i = 0; i < libraries.length; i++) {
      const libraryName = libraries[i];
      const fullPath = rootDirectory + 'Libraries/' + libraryName;
      const fileStats = fs.lstatSync(fullPath);
      if (fileStats.isDirectory()) {
        success = await build(PackageType.LIBRARY, libraryName, buildSettings);
        if (!success) {
          console.log('Compile error. Stopping the world.');
          return false;
        }
      }
    }

    // Build every application locally.
    let applications = fs.readdirSync(rootDirectory + 'Applications/');
    for (let i = 0; i < applications.length; i++) {
      const applicationName = applications[i];
      const fullPath = rootDirectory + 'Applications/' + applicationName;
      const fileStats = fs.lstatSync(fullPath);
      if (fileStats.isDirectory()) {
        success = await build(
            PackageType.APPLICATION, applicationName, buildSettings);
        if (!success) {
          console.log('Compile error. Stopping the world.');
          return false;
        }
      }
    }

    return true;
  } else {
    // Build everything into an image.
    let success = await build(PackageType.KERNEL, '', buildSettings);
    if (!success) {
      console.log('Build error. Stopping the world.');
      return false;
    }

    const prefix = buildPrefix(buildSettings);

    somethingChanged |= copyIfNewer(
        rootDirectory + 'Kernel/kernel.app',
        rootDirectory + 'fs/boot/kernel.app');

    // Compile and copy all applications
    let applications = fs.readdirSync(rootDirectory + 'Applications/');
    for (let i = 0; i < applications.length; i++) {
      const applicationName = applications[i];
      const fullPath = rootDirectory + 'Applications/' + applicationName;
      const fileStats = fs.lstatSync(fullPath);
      if (fileStats.isDirectory()) {
        success = await build(
            PackageType.APPLICATION, applicationName, buildSettings);
        if (!success) {
          console.log('Compile error. Stopping the world.');
          return false;
        }

        somethingChanged |= copyIfNewer(
            rootDirectory + 'Applications/' + applicationName + '/build/' +
                prefix + '.app',
            rootDirectory + 'fs/Applications/' + applicationName + '/' +
                applicationName + '.app');

        somethingChanged |= copyIfNewer(
            rootDirectory + 'Applications/' + applicationName + '/icon.svg',
            rootDirectory + 'fs/Applications/' + applicationName + '/icon.svg');

        somethingChanged |= copyIfNewer(
            rootDirectory + 'Applications/' + applicationName +
                '/launcher.json',
            rootDirectory + 'fs/Applications/' + applicationName +
                '/launcher.json');
      }
    }

    // Copy assets.
    Object.keys(applicationsWithAssets).forEach((application) => {
      copyDirectoryRecursively(
          rootDirectory + 'Applications/' + application + '/assets/',
          rootDirectory + 'fs/Applications/' + application + '/assets/');
    });
    Object.keys(librariesWithAssets).forEach((library) => {
      copyDirectoryRecursively(
          rootDirectory + 'Libraries/' + library + '/assets/',
          rootDirectory + 'fs/Libraries/' + library + '/assets/');
    });


    if (!buildSettings.compile) {
      console.log('Nothing was compiled, so no image is built.');
      return false;
    }

    if (somethingChanged || !fs.existsSync(rootDirectory + 'Perception.iso')) {
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
  buildImage : buildImage
};
