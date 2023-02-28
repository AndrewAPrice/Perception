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
const { forgetAlreadyBuiltLibraries } = require('./build');
const { getTempDirectory, getOutputPath, getFileSystemDirectory } = require('./config');
const { forgetAllLastModifiedTimestamps } = require('./file_timestamps');

// Tries to delete a file or directory, if it exists.
function maybeDelete(path) {
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

// Removes all built files.
function clean() {
  forgetAlreadyBuiltLibraries();
  forgetAllLastModifiedTimestamps();

  if (fs.existsSync(getTempDirectory()))
    fs.rmSync(getTempDirectory(), { recursive: true });

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