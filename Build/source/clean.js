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
const { forgetAlreadyBuiltLibraries } = require('./build');
const { getTempDirectory, getOutputPath, getFileSystemDirectory } = require('./config');
const { forgetAllLastModifiedTimestamps } = require('./file_timestamps');
const { getAllApplications, getPackageDirectory, getAllLibraries } = require('./package_directory');
const { PackageType } = require('./package_type');
const { removeDirectoryIfEmpty } = require('./utils');

// Tries to delete a file or directory, if it exists.
function maybeDelete(path) {
  console.log(path);
  if (!fs.existsSync(path)) {
    return;
  }
  const fileStats = fs.lstatSync(path);
  if (fileStats.isDirectory()) {
    fs.rmSync(path, { recursive: true });
  } else {
    fs.unlinkSync(path);
  }
}

function maybeDeleteAndRemoveParentIfEmpty(filePath) {
  maybeDelete(filePath);
  try {
    removeDirectoryIfEmpty(path.dirname(filePath));
  } catch {
    // Silently ignore errors such as the directory doesn't exist because it was
    // already cleaned up but still mentioned in third_party_files.json.
  }
}

function deepCleanPackage(packageDir) {
  const thirdPartyFilesPath = packageDir + 'third_party_files.json';
  if (fs.existsSync(thirdPartyFilesPath)) {
    try {
      const thirdPartyFiles = JSON.parse(fs.readFileSync(thirdPartyFilesPath));
      Object.keys(thirdPartyFiles).forEach(maybeDeleteAndRemoveParentIfEmpty);
      maybeDelete(thirdPartyFilesPath);
    } catch (exp) {
      console.log('Error deleting third party files in ' + packageDir + ' because: ' + exp);
    }
  }

  maybeDelete(packageDir + 'generated');
}

// Removes all built files.
function clean(deepClean) {
  forgetAlreadyBuiltLibraries();
  forgetAllLastModifiedTimestamps();

  if (deepClean) {
    // Delete all temp files including repositories.
    if (fs.existsSync(getTempDirectory())) {
      fs.rmSync(getTempDirectory(), { recursive: true });
    }

    // Delete all generated and third party files.
    getAllApplications().forEach(application => {
      deepCleanPackage(getPackageDirectory(PackageType.APPLICATION, application));
    });

    getAllLibraries().forEach(library => {
      deepCleanPackage(getPackageDirectory(PackageType.LIBRARY, library));
    });

  } else {
    // Don't delete repositories.
    if (fs.existsSync(getTempDirectory())) {
      let tempEntries = fs.readdirSync(getTempDirectory());
      tempEntries.forEach(tempEntry => {
        if (tempEntry == 'repositories')
          return;

        maybeDelete(getTempDirectory() + '/' + tempEntry);
      });
    }
  }

  // Clean up FS image.
  if (fs.existsSync(getFileSystemDirectory() + '/Applications'))
    fs.rmSync(getFileSystemDirectory() + '/Applications', { recursive: true });


  if (fs.existsSync(getFileSystemDirectory() + '/Libraries'))
    fs.rmSync(getFileSystemDirectory() + '/Libraries', { recursive: true });

  // Clean up ISO image.
  maybeDelete(getOutputPath());
}

module.exports = {
  clean: clean
};