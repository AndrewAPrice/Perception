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

const child_process = require('child_process');
const {buildImage} = require('./build_image');
const {getToolPath} = require('./tools');
const {escapePath} = require('./escape_path');
const {rootDirectory} = require('./root_directory');

const EMULATOR_COMMAND = getToolPath('qemu') + ' -boot d -cdrom ' + escapePath(rootDirectory) +
	'Perception.iso -m 512 -serial stdio';

// Builds everything and runs the emulator.
async function run() {
	const success = await buildImage();
	if (success) {
		console.log(EMULATOR_COMMAND);
		child_process.execSync(EMULATOR_COMMAND, {stdio: 'inherit'});
	}
}

module.exports = {
	run: run
};
