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
const child_process = require('child_process');
const {build} = require('./build');
const {getToolPath} = require('./tools');
const {rootDirectory} = require('./root_directory');
const {escapePath} = require('./escape_path');
const {getFileLastModifiedTimestamp} = require('./file_timestamps');
const {BuildResult} = require('./build_result');
const {PackageType} = require('./package_type');

const GRUB_MKRESCUE_COMMAND = getToolPath('grub-mkrescue') + ' -o ' + escapePath(rootDirectory) +
	 'Perception.iso' + ' ' + rootDirectory + 'fs';
	 
function copyIfNewer(source, destination) {
	if (fs.existsSync(destination)) {
		// Don't copy if the source older or same age as the destination.
		if (getFileLastModifiedTimestamp(source) <= getFileLastModifiedTimestamp(destination))
			return false;

		fs.unlinkSync(destination);
	}

	console.log('Copying ' + destination);
	fs.copyFileSync(source, destination);
	return true;
}

// Builds everything and turns it into an image.
async function buildImage() {
	let somethingChanged = false;

	let success = await build(PackageType.KERNEL, '');
	if (!success) {
		console.log('Build error. Stopping the world.');
		return BuildResult.FAILED;
	}

	somethingChanged |= copyIfNewer(rootDirectory + 'Kernel/kernel.app', rootDirectory + 'fs/boot/kernel.app');

	// Compile and copy all applications
	let applications = fs.readdirSync(rootDirectory + 'Applications/');
	for (let i = 0; i < applications.length; i++) {
		const applicationName = applications[i];
		const fullPath = rootDirectory + 'Applications/' + applicationName;
		const fileStats = fs.lstatSync(fullPath);
		if (fileStats.isDirectory()) {
			success = await build(PackageType.APPLICATION, applicationName);
			if (!success) {
				console.log('Compile error. Stopping the world.');
				return BuildResult.FAILED;
			}

			somethingChanged |= copyIfNewer(
				rootDirectory + 'Applications/' + applicationName + '/application.app',
				rootDirectory + 'fs/' + applicationName + '.app');
		}
	}

	if (somethingChanged || !fs.existsSync(rootDirectory + 'Perception.iso')) {
		let command = GRUB_MKRESCUE_COMMAND;
		try {
			child_process.execSync(command);
		} catch (exp) {
			return BuildResult.FAILED;
		}
	}

	return BuildResult.COMPILED;
}

module.exports = {
	buildImage: buildImage
};
