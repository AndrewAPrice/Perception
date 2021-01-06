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

const process = require('process');
const child_process = require('child_process');
const {build} = require('./build');
const {buildImage} = require('./build_image');
const {buildPrefix} = require('./build_commands');
const {escapePath} = require('./escape_path');
const {PackageType} = require('./package_type');
const {rootDirectory} = require('./root_directory');
const {getToolPath} = require('./tools');

const EMULATOR_COMMAND = getToolPath('qemu') + ' -boot d -cdrom ' + escapePath(rootDirectory) +
	'Perception.iso -m 512 -serial stdio';

// Builds everything and runs the emulator.
async function run(package, buildSettings) {
	if (buildSettings.os == 'Perception') {
		if (package != '') {
			console.log('\'build run\' builds everything into a disk image. If you\'re trying to run a particular application locally, please pass --local.');
			process.exit(-1);
		}
		const success = await buildImage(buildSettings);
		if (success) {
			console.log(EMULATOR_COMMAND);
			child_process.execSync(EMULATOR_COMMAND, {stdio: 'inherit'});
		}
	} else {
		if (package == '') {
			console.log('When passing --local to \'build run\', you need to add the name of the application you want to run.');
			process.exit(-1);
		}
		build(PackageType.APPLICATION, package, buildSettings).then((res) => {
			if (res) {
				child_process.execSync(escapePath(rootDirectory + 'Applications/' + package + '/build/' +
					buildPrefix(buildSettings) + '.app'), {stdio: 'inherit'});
			} else {
				console.log("failed!");
			}
		}, (err) =>{
			console.log(err);
		});
	}
}

module.exports = {
	run: run
};
