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

// Calls 'foreachFunc' for each file in the package directory.
async function foreachSourceFile (packageDir, foreachFunc) {
	await foreachSourceFileInSourceDir(
			slashEndingPath(packageDir + 'source'),
			slashEndingPath(packageDir + 'build'),
			foreachFunc);
}

// Calls 'foreachFunc' for each file in the source directory.
async function foreachSourceFileInSourceDir (dir, buildDir, foreachFunc) {
	if (!fs.existsSync(buildDir)) {
		fs.mkdirSync(buildDir);
	}

	let files = fs.readdirSync(dir);

	for (let i = 0; i < files.length; i++) {
		const fullPath = dir + files[i];
		const buildPath = buildDir + files[i];
		const fileStats = fs.lstatSync(fullPath);
		if (fileStats.isDirectory()) {
			await foreachSourceFileInSourceDir(fullPath + '/', buildPath + '/', foreachFunc)
		} else if (fileStats.isFile()) {
				await foreachFunc(fullPath, buildPath);
		}
	}
}

// Returns a path that ends in a slash.
function slashEndingPath(path) {
	return path.endsWith('/') ? path : path + '/';
}

// Returns a path that doesn't end in a slash.
function stripLastSlash(path) {
	return path.endsWith('/') ? path.substring(0, path.length - 1) : path;
}

module.exports = {
	foreachSourceFile: foreachSourceFile
};
