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
const {rootDirectory} = require('./root_directory');

// Tries to delete a file or directory, if it exists.
function maybeDelete(path) {
	if (!fs.existsSync(path)) {
		return;
	}
	const fileStats = fs.lstatSync(path);
	console.log('Removing ' + path);
	if (fileStats.isDirectory()) {
		fs.rmdirSync(path, { recursive: true });
	} else {
		fs.unlinkSync(path);
	}
}

// Removes all built files.
function clean() {
	// Clean up kernel.
	maybeDelete(rootDirectory + 'Kernel/build');
	maybeDelete(rootDirectory + 'Kernel/kernel.app');
	maybeDelete(rootDirectory + 'fs/boot/kernel.app');

	// Clean up libraries.
	let libraries = fs.readdirSync(rootDirectory + 'Libraries/');
	for (let i = 0; i < libraries.length; i++) {
		const libraryName = libraries[i];
		const fullPath = rootDirectory + 'Libraries/' + libraryName;
		const fileStats = fs.lstatSync(fullPath);
		if (fileStats.isDirectory()) {
			maybeDelete(fullPath + '/build');
			maybeDelete(fullPath + '/generated');
		}
	}

	// Clean up applications.
	let applications = fs.readdirSync(rootDirectory + 'Applications/');
	for (let i = 0; i < applications.length; i++) {
		const applicationName = applications[i];
		const fullPath = rootDirectory + 'Applications/' + applicationName;
		const fileStats = fs.lstatSync(fullPath);
		if (fileStats.isDirectory()) {
			maybeDelete(fullPath + '/build');
			maybeDelete(fullPath + '/generated');
			maybeDelete(rootDirectory + 'fs/' + applicationName + '.app');
		}
	}

	// Clean up ISO image.
	maybeDelete(rootDirectory + 'Perception.iso');
}

module.exports = {
	clean: clean
};