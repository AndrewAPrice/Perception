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

const git_repository = 'https://github.com/harfbuzz/harfbuzz.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading harfbuzz');
	// Grab it from github.
	const command = 'git clone ' + git_repository + ' third_party';
	try {
		child_process.execSync(command, {stdio: 'inherit'});
	} catch (exp) {
		console.log('Error downloading harfbuzz: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update harfbuzz');
	// Try to update it.
	const command = 'git pull ' + git_repository;
	try {
		child_process.execSync(command, {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error updating harfbuzz: ' + exp);
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

const acceptableExtensions = {'.c':1, '.cc':1, '.h':1, '.hh':1};

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

filesToIgnore['source/harfbuzz.cc'] = true;
filesToIgnore['source/harfbuzz-subset.cc'] = true;
filesToIgnore['source/main.cc'] = true;
filesToIgnore['source/test-algs.cc'] = true;
filesToIgnore['source/test-array.cc'] = true;
filesToIgnore['source/test-bimap.cc'] = true;
filesToIgnore['source/test-buffer-serialize.cc'] = true;
filesToIgnore['source/test-gpos-size-params.cc'] = true;
filesToIgnore['source/test-gsub-would-substitute.cc'] = true;
filesToIgnore['source/test-iter.cc'] = true;
filesToIgnore['source/test-machinery.cc'] = true;
filesToIgnore['source/test-map.cc'] = true;
filesToIgnore['source/test-number.cc'] = true;
filesToIgnore['source/test-ot-glyphname.cc'] = true;
filesToIgnore['source/test-ot-meta.cc'] = true;
filesToIgnore['source/test-ot-name.cc'] = true;
filesToIgnore['source/test-priority-queue.cc'] = true;
filesToIgnore['source/test-repacker.cc'] = true;
filesToIgnore['source/test-serialize.cc'] = true;
filesToIgnore['source/test-set.cc'] = true;
filesToIgnore['source/test-unicode-ranges.cc'] = true;
filesToIgnore['source/test-use-table.cc'] = true;
filesToIgnore['source/test-vector.cc'] = true;
filesToIgnore['source/test.cc'] = true;
filesToIgnore['public/graph/test-classdef-graph.cc'] = true;

copyFilesInDirectory('third_party/src', 'public', '.h');
copyFilesInDirectory('third_party/src', 'public', '.hh');
copyFilesInDirectory('third_party/src', 'source', '.c');
copyFilesInDirectory('third_party/src', 'source', '.cc');
copyFilesInDirectory('third_party/src/OT/glyf', 'public/OT/glyf');
copyFilesInDirectory('third_party/src/OT/Layout', 'public/OT/Layout');
copyFilesInDirectory('third_party/src/OT/Layout/Common', 'public/OT/Layout/Common');
copyFilesInDirectory('third_party/src/OT/Layout/GPOS', 'public/OT/Layout/GPOS');
copyFilesInDirectory('third_party/src/OT/Layout/GSUB', 'public/OT/Layout/GSUB');
copyFilesInDirectory('third_party/src/graph', 'public/graph');
/*
replaceInFile('source/graph/graph.hh', [
		['#include "../hb-set.hh"', '#include "hb-set.hh"'],
		['#include "../hb-priority-queue.hh"', '#include "hb-priority-queue.hh"'],
		['#include "../hb-serialize.hh""', '#include "hb-serialize.hh"']
		]);
replaceInFile('source/OT/Layout/GPOS/GPOS.hh', [['#include "../../../hb-ot-layout-common.hh"', '#include "hb-ot-layout-common.hh"']]);
replaceInFile('source/OT/glyf/glyf.hh', [['#include "../../hb-open-type.hh"', '#include "hb-open-type.hh"']]);
replaceInFile('source/OT/Layout/GSUB/GSUB.hh', [['#include "../../../hb-ot-layout-gsubgpos.hh"', '#include "hb-ot-layout-gsubgpos.hh"']]);
*/
// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		fs.unlinkSync(filePath)
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));
