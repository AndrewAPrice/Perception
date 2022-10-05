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

const git_repository = 'https://github.com/libjpeg-turbo/libjpeg-turbo.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading libjpeg-turbo');
	// Grab it from github.
	const command = 'git clone ' + git_repository + ' third_party';
	try {
		child_process.execSync(command, {stdio: 'inherit'});
	} catch (exp) {
		console.log('Error downloading libjpeg-turbo: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update libjpeg-turbo');
	// Try to update it.
	const command = 'git pull ' + git_repository;
	try {
		child_process.execSync(command, {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error updating libjpeg-turbo: ' + exp);
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
	if (!fromFileStats) {
		fromFileStats = fs.lstatSync(fromPath);
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

const acceptableExtensions = {'.asm':1, '.c':1, '.cpp':1, '.h':1, '.hh':1, '.inc': 1};

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

function replaceInFile(filename, needles) {
	let fileContents = fs.readFileSync(filename, 'utf8');
	needles.forEach(needle => {
		fileContents = fileContents.split(needle[0]).join(needle[1]);
	});
	fs.writeFileSync(filename, fileContents);
}

function prefixInFile(filename, prefix) {
	let fileContents = fs.readFileSync(filename, 'utf8');
	fs.writeFileSync(filename, prefix + fileContents);

}

function readFromFile(filename, regexes) {
	const results = [];
	let fileContents = fs.readFileSync(filename, 'utf8');
	regexes.forEach(regex => {
		results.push(regex.exec(fileContents));
	});
	return results;
}

filesToIgnore['source/jerror.h'] = true;
filesToIgnore['source/jpeglib.h'] = true;
filesToIgnore['source/jmorecfg.h'] = true;
filesToIgnore['source/turbojpeg-jni.c'] = true;
filesToIgnore['source/simd/x86_64/jsimdcfg.inc.h'] = true;
copyFilesInDirectory('third_party', 'source');
copyFilesInDirectory('third_party/simd', 'source/simd');
copyFilesInDirectory('third_party/simd/x86_64', 'source/simd/x86_64');
copyFilesInDirectory('third_party/simd/nasm', 'source/simd/x86_64');
copyFilesInDirectory('third_party/md5', 'source/md5');
copyFile('third_party/jconfig.h.in', 'public/jconfig.h');
copyFile('third_party/jerror.h', 'public/jerror.h');
copyFile('third_party/jpeglib.h', 'public/jpeglib.h');
copyFile('third_party/jmorecfg.h', 'public/jmorecfg.h');
copyFile('third_party/jconfigint.h.in', 'source/jconfigint.h');
copyFile('third_party/jversion.h.in', 'source/jversion.h');
copyFile('third_party/simd/nasm/jsimdcfg.inc.h', 'source/simd/jsimdcfg.inc.h');

const versions = readFromFile('third_party/CMakeLists.txt', [
	/set\(VERSION (\d+).(\d+).(\d+)\)/]);
const majorVersion = versions[0][1];
const minorVersion = versions[0][2];
const revisionVersion = versions[0][3];

replaceInFile('public/jconfig.h', [
	['@VERSION@', '"'+majorVersion + '.' + minorVersion + '.' + revisionVersion + '"'],
	['@LIBJPEG_TURBO_VERSION_NUMBER@', majorVersion * 100000 + minorVersion * 100 + revisionVersion],
	['@JPEG_LIB_VERSION@', '80'],
	['#cmakedefine RIGHT_SHIFT_IS_UNSIGNED 1', ''],
	['#cmakedefine', '#define'],
	['@BITS_IN_JSAMPLE@', '8']
	]);

replaceInFile('source/jconfigint.h', [
	['@BUILD@', '0'],
	['@INLINE@', '__inline__ __attribute__((always_inline))'],
	['@THREAD_LOCAL@', '__thread'],
	['@CMAKE_PROJECT_NAME@', 'turbo-jpeg'],
	['@VERSION@', majorVersion + '.' + minorVersion + '.' + revisionVersion],
	['@SIZE_T@', 8],
	['#cmakedefine', '#define']
	]);

replaceInFile('source/simd/x86_64/jsimd.c', [
	['#include "../../jpeglib.h"', '#include "jpeglib.h"']
	]);

replaceInFile('source/simd/jsimdcfg.inc.h', [
	['#include "../jpeglib.h"', '#include "jpeglib.h"'],
	['#include "../jconfig.h"', '#include "jconfig.h"'],
	['#include "../jmorecfg.h"', '#include "jmorecfg.h"']
	]);


try {
	child_process.execSync('gcc -I public -I source -E -P source/simd/jsimdcfg.inc.h | ' +
		' grep -E \'^[\\;%]|^\\ %\' | sed \'s%_cpp_protection_%%\' | ' +
  		' sed \'s@% define@%define@g\' > source/simd/x86_64/jsimdcfg.inc'
		, {stdio: 'inherit'});
	third_party_files['generated/fcobjshash.gperf'] = true;
} catch (exp) {
	console.log('Error running cutout.py: ' + exp);
	process.exit(1);
}


prefixInFile('source/simd/x86_64/jsimdext.inc', '%define __x86_64__\n%define ELF\n');



// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		fs.unlinkSync(filePath)
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));
