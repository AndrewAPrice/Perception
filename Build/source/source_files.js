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
const {buildPrefix} = require('./build_commands');

// Calls 'foreachFunc' for each file in the package directory.
async function foreachSourceFile (packageDir, buildSettings, foreachFunc) {
	await foreachSourceFileInSourceDir(
			packageDir + 'source/',
			packageDir + 'build/' + buildPrefix(buildSettings) + '/',
			true,
			foreachFunc);

	if (fs.existsSync(packageDir + 'generated/source/')) {
		await foreachSourceFileInSourceDir(
				packageDir + 'generated/source/',
				packageDir + 'generated/build/' + buildPrefix(buildSettings) + '/',
				true,
				foreachFunc);
	}
}

// Calls 'foreachFunc' for each permebuf source file.
async function foreachPermebufSourceFile (packageDir, foreachFunc) {
	const permebufDir = packageDir + 'permebuf/'
	if (!fs.existsSync(permebufDir)) {
		// This package doesn't have any permebuf files.
		return;
	}

	await foreachSourceFileInSourceDir(permebufDir, '', false, foreachFunc);
}

// Calls 'foreachFunc' for each file in the source directory.
async function foreachSourceFileInSourceDir (dir, secondaryDir, makeSecondaryDir, foreachFunc) {
	if (makeSecondaryDir && !fs.existsSync(secondaryDir)) {
		fs.mkdirSync(secondaryDir, {recursive: true});
	}

	let files = fs.readdirSync(dir);

	for (let i = 0; i < files.length; i++) {
		const fullPath = dir + files[i];
		const secondaryPath = secondaryDir + files[i];
		const fileStats = fs.lstatSync(fullPath);
		if (fileStats.isDirectory()) {
			await foreachSourceFileInSourceDir(fullPath + '/', secondaryPath + '/', makeSecondaryDir, foreachFunc);
		} else if (fileStats.isFile()) {
			await foreachFunc(fullPath, secondaryPath);
		}
	}
}

module.exports = {
	foreachSourceFile: foreachSourceFile,
	foreachPermebufSourceFile: foreachPermebufSourceFile
};
