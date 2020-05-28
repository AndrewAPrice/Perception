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
const {escapePath} = require('./escape_path');

function PopulateToolsJson() {
	let toolPaths = {};
	if (fs.existsSync('tools.json')) {
		toolPaths = JSON.parse(fs.readFileSync('tools.json'));
	}

	['ar', 'gas', 'nasm', 'gcc', 'grub-mkrescue', 'ld', 'qemu'].forEach((tool) => {
		if (!toolPaths[tool]) {
			toolPaths[tool] = '';
		}
	});

	fs.writeFileSync('tools.json', JSON.stringify(toolPaths));
};

// Loads the config file that contains the tools to use on this system.
if (!fs.existsSync('tools.json')) {
	console.log('Please fill in tools.json and read ../building.md.');
	PopulateToolsJson();
	process.exit(1);

}
const toolPaths = JSON.parse(fs.readFileSync('tools.json'));

function getToolPath(tool) {
	const toolPath = toolPaths[tool];
	if (!toolPath) {
		console.log('tools.json doesn\'t contain an entry for "' + tool + "'. Please read ../building.md.");
		PopulateToolsJson();
		process.exit(1);
	}
	return escapePath(toolPath);
}

module.exports = {
	getToolPath: getToolPath
};

