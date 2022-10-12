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
const child_process = require('child_process');
const path = require('path');
const process = require('process');

const git_repository = 'https://github.com/freetype/freetype.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading freetype');
	// Grab it from github.
	const command = 'git clone ' + git_repository + ' third_party';
	try {
		child_process.execSync(command, {stdio: 'inherit'});
	} catch (exp) {
		console.log('Error downloading freetype: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update freetype');
	// Try to update it.
	const command = 'git pull ' + git_repository;
	try {
		child_process.execSync(command, {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error updating freetype: ' + exp);
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

const filesToIgnore = {};

function copyFile(fromPath, toPath, fromFileStats) {
	if (third_party_files_from_last_run[toPath]) {
		delete third_party_files_from_last_run[toPath];
	}
	third_party_files[toPath] = true;
	createDirectoryIfItDoesntExist(path.dirname(toPath));

	if (fs.existsSync(toPath)) {
		if (!fromFileStats) {
			fromFileStats = fs.lstatSync(fromPath);
		}
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

const acceptableExtensions = {'.c':1, '.cin':1, '.dat':1, '.hin':1, '.h':1};

function copyFilesInDirectory(from, to, extension) {
	const filesInDirectory = fs.readdirSync(from);
	for (let i = 0; i < filesInDirectory.length; i++) {
		const entryName = filesInDirectory[i];
		const fromPath = from + '/' + entryName;
		const toPath = to + '/' + entryName;

		if (filesToIgnore[toPath]) {
			continue;
		}

		const fileStats = fs.lstatSync(fromPath);
		const fileExtension = path.extname(entryName);
		if (!fileStats.isDirectory() &&
			(extension && extension == fileExtension) ||
			(!extension && acceptableExtensions[fileExtension])) {
			copyFile(fromPath, toPath, fileStats);
		}
	}
}

function replaceInFile(filename, needle, replaceWith) {
	let fileContents = fs.readFileSync(filename, 'utf8');
	fileContents = fileContents.split(needle).join(replaceWith);
	fs.writeFileSync(filename, fileContents);
}

filesToIgnore['source/autofit/autofit.c'] = true;
filesToIgnore['source/base/ftbase.c'] = true;
filesToIgnore['source/bdf/bdf.c'] = true;
filesToIgnore['source/cache/ftcache.c'] = true;
filesToIgnore['source/cff/cff.c'] = true;
filesToIgnore['source/cid/type1cid.c'] = true;
filesToIgnore['source/gxvalid/gxvalid.c'] = true;
filesToIgnore['source/otvalid/otvalid.c'] = true;
filesToIgnore['source/pcf/pcf.c'] = true;
filesToIgnore['source/pfr/pfr.c'] = true;
filesToIgnore['source/psaux/psaux.c'] = true;
filesToIgnore['source/pshinter/pshinter.c'] = true;
filesToIgnore['source/psnames/psnames.c'] = true;
filesToIgnore['source/raster/raster.c'] = true;
filesToIgnore['source/sdf/sdf.c'] = true;
filesToIgnore['source/sfnt/sfnt.c'] = true;
filesToIgnore['source/smooth/smooth.c'] = true;
filesToIgnore['source/svg/svg.c'] = true;
filesToIgnore['source/truetype/truetype.c'] = true;
filesToIgnore['source/type1/type1.c'] = true;
filesToIgnore['source/type42/type42.c'] = true;
filesToIgnore['source/base/ftsystem.c'] = true;

copyFilesInDirectory('third_party/include', 'public', '.h');
copyFilesInDirectory('third_party/include/freetype', 'public/freetype', '.h');
copyFilesInDirectory('third_party/include/freetype/config', 'public/freetype/config', '.h');
copyFilesInDirectory('third_party/include/freetype/internal', 'public/freetype/internal', '.h');
copyFilesInDirectory('third_party/include/freetype/internal/services', 'public/freetype/internal/services', '.h');
copyFilesInDirectory('third_party/src/autofit', 'source/autofit');
copyFilesInDirectory('third_party/src/base', 'source/base');
copyFilesInDirectory('third_party/src/bdf', 'source/bdf');
copyFilesInDirectory('third_party/src/bzip2', 'source/bzip2');
copyFilesInDirectory('third_party/src/cache', 'source/cache');
copyFilesInDirectory('third_party/src/cff', 'source/cff');
copyFilesInDirectory('third_party/src/cid', 'source/cid');
copyFilesInDirectory('third_party/src/dlg', 'source/dlg');
copyFilesInDirectory('third_party/src/gxvalid', 'source/gxvalid');
copyFile('third_party/src/gzip/ftgzip.c', 'source/gzip/ftgzip.c');
copyFile('third_party/src/gzip/ftzconf.h', 'source/gzip/ftzconf.h');
copyFilesInDirectory('third_party/src/lzw', 'source/lzw');
copyFilesInDirectory('third_party/src/otvalid', 'source/otvalid');
copyFilesInDirectory('third_party/src/pcf', 'source/pcf');
copyFilesInDirectory('third_party/src/pfr', 'source/pfr');
copyFilesInDirectory('third_party/src/psaux', 'source/psaux');
copyFilesInDirectory('third_party/src/bzip2', 'source/bzip2');
copyFilesInDirectory('third_party/src/pshinter', 'source/pshinter');
copyFilesInDirectory('third_party/src/psnames', 'source/psnames');
copyFilesInDirectory('third_party/src/raster', 'source/raster');
copyFilesInDirectory('third_party/src/sdf', 'source/sdf');
copyFilesInDirectory('third_party/src/bzip2', 'source/bzip2');
copyFilesInDirectory('third_party/src/sfnt', 'source/sfnt');
copyFilesInDirectory('third_party/src/smooth', 'source/smooth');
copyFilesInDirectory('third_party/src/bzip2', 'source/bzip2');
copyFilesInDirectory('third_party/src/svg', 'source/svg');
copyFilesInDirectory('third_party/src/truetype', 'source/truetype');
copyFilesInDirectory('third_party/src/type1', 'source/type1');
copyFilesInDirectory('third_party/src/type42', 'source/type42');
copyFilesInDirectory('third_party/src/winfonts', 'source/winfonts');
replaceInFile('source/truetype/ttobjs.h',
	'#include <freetype/internal/ftobjs.h>',
	'#include <freetype/internal/ftobjs.h>\n#include <freetype/internal/ftmmtypes.h>');
replaceInFile('public/freetype/internal/services/svmm.h',
	'#include <freetype/internal/ftserv.h>',	
	'#include <freetype/ftmm.h>\n#include <freetype/internal/ftserv.h>');
copyFile('third_party/builds/unix/ftsystem.c', 'source/base/ftsystem.c');


// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		fs.unlinkSync(filePath)
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));



