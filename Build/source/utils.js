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

// Escape spaces in a path so paths aren't split up when used as part of a
// command.
function escapePath(path) {
  return path.replace(/ /g, '\\ ');
}

function forEachIfDefined(array, onEach) {
  if (array) array.forEach(onEach);
}

function removeDirectoryIfEmpty(dirPath) {
	const dirEntries = fs.readdirSync(dirPath);
	if (dirEntries.length >= 1) {
		// There are files in this directory, but this is ok if the only file is '.clang_complete.'
		if (dirEntries.length != 1 || dirEntries[0] != '.clang_complete')
			return;
	}

	fs.rmdirSync(dirPath);
	const parentPath = path.dirname(dirPath);
	removeDirectoryIfEmpty(parentPath);
}

function createDirectoryIfItDoesntExist(dirPath) {
	if (fs.existsSync(dirPath)) {
		return;
	}
	// Make sure parent exists.
	const parentPath = path.dirname(dirPath);
	createDirectoryIfItDoesntExist(parentPath);
	fs.mkdirSync(dirPath);
}

module.exports = {
  escapePath : escapePath,
  forEachIfDefined : forEachIfDefined,
  removeDirectoryIfEmpty : removeDirectoryIfEmpty,
  createDirectoryIfItDoesntExist : createDirectoryIfItDoesntExist
};