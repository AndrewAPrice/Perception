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

const llvm_git_repository = 'https://github.com/llvm/llvm-project.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading libcxx/libcxxabi');
	// Grab it from github.
	// const command = 'git clone ' + llvm_git_repository + ' third_party';
	try {
		child_process.execSync('git clone --no-checkout --depth 1 git://github.com/llvm/llvm-project.git third_party', {stdio: 'inherit'});
		child_process.execSync('git config core.sparseCheckout true', {cwd: 'third_party', stdio: 'inherit'});
		child_process.execSync('git config pull.rebase true', {cwd: 'third_party', stdio: 'inherit'});
		fs.appendFileSync('third_party/.git/info/sparse-checkout', 'libcxx/\n', 'utf8');
		fs.appendFileSync('third_party/.git/info/sparse-checkout', 'libcxxabi/\n', 'utf8');
		fs.appendFileSync('third_party/.git/info/sparse-checkout', 'libunwind/\n', 'utf8');
		child_process.execSync('git checkout', {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error downloading libcxx/libcxxabi: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update libcxx/libcxxabi');
	// Try to update it.
	try {
		child_process.execSync('git checkout', {cwd: 'third_party', stdio: 'inherit'});
		child_process.execSync('git pull', {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error updating libcxx/libcxxabi: ' + exp);
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

function copyFile(fromPath, toPath, fromFileStats) {
	if (third_party_files_from_last_run[toPath]) {
		delete third_party_files_from_last_run[toPath];
	}
	third_party_files[toPath] = true;
	createDirectoryIfItDoesntExist(path.dirname(toPath));

	if (fs.existsSync(toPath)) {
		const fromUpdateTime = fromFileStats.mtimeMs;
		const toUpdateTime = fs.lstatSync(toPath).mtimeMs;

		if (fromUpdateTime <= toUpdateTime) {
			// File hasn't changed.
			return;
		}
	}

	console.log('Copying ' + toPath);
	fs.copyFileSync(fromPath, toPath);
}

const filesToIgnore = {
	'source/libc/stdio/nano-vfprintf.c': true,
	'source/libc/stdio/nano-vfprintf_float.c': true,
	'source/libc/stdio/nano-vfprintf_i.c': true,
	'source/libc/stdio/nano-vfprintf_local.c': true,
	'source/libc/stdio/nano-vfscanf.c': true,
	'source/libc/stdio/nano-vfscanf_float.c': true,
	'source/libc/stdio/nano-vfscanf_i.c': true,
	'source/libc/stdio/nano-vfscanf_local.c': true,
	'source/libc/stdio/nano-vfprintf_local.h': true,
	'source/libc/stdio/nano-vfscanf_local.h': true,
};

function copyFilesInDirectory(from, to) {
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
			copyFilesInDirectory(fromPath, toPath);
		} else {
			copyFile(fromPath, toPath, fileStats);
		}
	}
}

function replaceInFile(filename, needle, replaceWith) {
	let fileContents = fs.readFileSync(filename, 'utf8');
	fileContents = fileContents.split(needle).join(replaceWith);
	fs.writeFileSync(filename, fileContents);
}


copyFilesInDirectory('third_party/libcxx/include', 'public');
copyFilesInDirectory('third_party/libcxx/src', 'source');
copyFilesInDirectory('third_party/libcxxabi/include', 'public');
copyFilesInDirectory('third_party/libcxxabi/src', 'source');
copyFilesInDirectory('third_party/libunwind/include', 'public');
copyFilesInDirectory('third_party/libunwind/src', 'source');

// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		fs.unlinkSync(filePath)
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));



