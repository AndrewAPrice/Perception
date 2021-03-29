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
const path = require('path');
const process = require('process');

const musl_git_repository = 'https://github.com/memononen/nanosvg.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading musl');
	// Grab it from github.
	const command = 'git clone ' + musl_git_repository + ' third_party';
	try {
		child_process.execSync(command, {stdio: 'inherit'});
	} catch (exp) {
		console.log('Error downloading musl: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update musl');
	// Try to update it.
	const command = 'git pull ' + musl_git_repository;
	try {
		child_process.execSync(command, {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error updating musl: ' + exp);
		process.exit(1);
	}
}

// Keep track of third party files we copied from our last build, so we can remove any
// that no longer exist.
let third_party_files_from_last_run = {};
if (fs.existsSync('third_party_files.json')) {
	third_party_files_from_last_run = JSON.parse(fs.readFileSync('third_party_files.json'));
}
let third_party_files = {};

function createDirectoryIfItDoesntExist(dirPath) {
	if (fs.existsSync(dirPath)) {
		return;
	}
	// Make sure parent exists.
	const parentPath = path.dirname(dirPath);
	createDirectoryIfItDoesntExist(parentPath);
	fs.mkdirSync(dirPath);
}

function removeDirectoryIfEmpty(dirPath) {
	if (fs.readdirSync(dirPath).length >= 1) {
		// There are files in this directory.
		return;
	}

	fs.rmdirSync(dirPath);
	const parentPath = path.dirname(dirPath);
	removeDirectoryIfEmpty(parentPath);
}

const destinationFilesBySourceFiles = {};

function copyFile(fromPath, toPath, fromFileStats) {
	destinationFilesBySourceFiles[toPath] = fromPath;
}

const filesToIgnore = {};

function copyFilesInDirectory(from, to, noSubDirs) {
	const filesInDirectory = fs.readdirSync(from);
	for (let i = 0; i < filesInDirectory.length; i++) {
		const entryName = filesInDirectory[i];
		const fromPath = from + '/' + entryName;
		const toPath = to + '/' + entryName;

		if (filesToIgnore[toPath]) {
			continue;
		}

		const fileStats = fs.lstatSync(fromPath);
		if (fileStats.isDirectory()) {
			if (!noSubDirs) {
				copyFilesInDirectory(fromPath, toPath);
			}
		} else {
			copyFile(fromPath, toPath, fileStats);
		}
	}
}

function doCopyFile(fromPath, toPath) {
	if (third_party_files_from_last_run[toPath]) {
		delete third_party_files_from_last_run[toPath];
	}
	third_party_files[toPath] = true;
	createDirectoryIfItDoesntExist(path.dirname(toPath));

	if (fs.existsSync(toPath)) {
		const fromUpdateTime = fs.lstatSync(fromPath).mtimeMs;
		const toUpdateTime = fs.lstatSync(toPath).mtimeMs;

		if (fromUpdateTime <= toUpdateTime) {
			// File hasn't changed.
			return;
		}
	}

	console.log('Copying ' + toPath);
	try {
		fs.copyFileSync(fromPath, toPath)
	} catch {}
}

function removeDuplicateAssemblyCFiles() {
	const keysToRemove = [];
	Object.keys(destinationFilesBySourceFiles).forEach((toPath) => {
		if (path.extname(toPath) == '.s') {
			// Check if there is a matching .cc.
			const matchingCPath = toPath.substr(0, toPath.length - 2) + '.c';
			if (destinationFilesBySourceFiles[matchingCPath]) {
				keysToRemove.push(matchingCPath);
			}
		}
	});

	for (let i = 0; i < keysToRemove.length; i++) {
		delete destinationFilesBySourceFiles[keysToRemove[i]];
	}
}

function doCopy() {
	removeDuplicateAssemblyCFiles();
	Object.keys(destinationFilesBySourceFiles).forEach((toPath) => {
		doCopyFile(destinationFilesBySourceFiles[toPath], toPath);
	});
}

function replaceInFile(filename, needle, replaceWith) {
	let fileContents = fs.readFileSync(filename, 'utf8');
	fileContents = fileContents.split(needle).join(replaceWith);
	fs.writeFileSync(filename, fileContents);
}

function rename(fromPath, toPath) {
	destinationFilesBySourceFiles[toPath] = destinationFilesBySourceFiles[fromPath];
	delete destinationFilesBySourceFiles[fromPath];
}

copyFilesInDirectory('third_party/src', 'public');

doCopy();

// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		try {
			fs.unlinkSync(filePath);
		} catch {}
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));
