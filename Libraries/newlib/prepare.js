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

const newlib_git_repository = 'git://sourceware.org/git/newlib-cygwin.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading newlib');
	// Grab it from github.
	const command = 'git clone ' + newlib_git_repository + ' third_party';
	try {
		child_process.execSync(command, {stdio: 'pipe'});
	} catch (exp) {
		console.log('Error downloading newlib: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update newlib');
	// Try to update it.
	const command = 'git pull ' + newlib_git_repository;
	try {
		child_process.execSync(command, {cwd: 'third_party', stdio: 'pipe'});
	} catch (exp) {
		console.log('Error updating newlib: ' + exp);
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
	'public/sys/dirent.h': true,
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


copyFilesInDirectory('third_party/newlib/libc/machine/x86_64/sys', 'public/sys');
filesToIgnore['public/sys/fenv.h'] = true;

copyFilesInDirectory('third_party/newlib/libc/argz', 'source/libc/argz');
copyFilesInDirectory('third_party/newlib/libc/ctype', 'source/libc/ctype');
copyFilesInDirectory('third_party/newlib/libc/errno', 'source/libc/errno');
copyFilesInDirectory('third_party/newlib/libc/iconv/ccs', 'source/libc/iconv/ccs');
copyFilesInDirectory('third_party/newlib/libc/iconv/ces', 'source/libc/iconv/ces');
copyFilesInDirectory('third_party/newlib/libc/iconv/lib', 'source/libc/iconv/lib');
copyFilesInDirectory('third_party/newlib/libc/include', 'public');
copyFilesInDirectory('third_party/newlib/libc/locale', 'source/libc/locale');
copyFilesInDirectory('third_party/newlib/libc/machine/x86_64', 'source/libc/machine/x86_64');
copyFilesInDirectory('third_party/newlib/libc/misc', 'source/libc/misc');
// copyFilesInDirectory('third_party/newlib/libc/posix', 'source/libc/posix');
copyFilesInDirectory('third_party/newlib/libc/reent', 'source/libc/reent');
copyFilesInDirectory('third_party/newlib/libc/search', 'source/libc/search');
copyFilesInDirectory('third_party/newlib/libc/signal', 'source/libc/signal');
copyFilesInDirectory('third_party/newlib/libc/ssp', 'source/libc/ssp');
copyFilesInDirectory('third_party/newlib/libc/stdio', 'source/libc/stdio');
copyFilesInDirectory('third_party/newlib/libc/stdio64', 'source/libc/stdio'); // Depends on local headers in stdio
copyFilesInDirectory('third_party/newlib/libc/stdlib', 'source/libc/stdlib');
copyFilesInDirectory('third_party/newlib/libc/string', 'source/libc/string');
copyFilesInDirectory('third_party/newlib/libc/syscalls', 'source/libc/syscalls');
copyFilesInDirectory('third_party/newlib/libc/time', 'source/libc/time');
copyFilesInDirectory('third_party/newlib/libc/xdr', 'source/libc/xdr');
copyFilesInDirectory('third_party/newlib/libm/common', 'source/libm/common');
copyFilesInDirectory('third_party/newlib/libm/fenv', 'source/libm/fenv');
copyFilesInDirectory('third_party/newlib/libm/machine/x86_64', 'source/libm/machine/x86_64');
copyFilesInDirectory('third_party/newlib/libm/math', 'source/libm/math');
copyFilesInDirectory('third_party/newlib/libm/mathfp', 'source/libm/mathfp');

replaceInFile('source/libc/time/gettzinfo.c', '#include <local.h>', '#include "local.h"');
replaceInFile('source/libc/string/local.h', '#include <../ctype/local.h>', '#include "../ctype/local.h"')
replaceInFile('source/libc/stdio/fiprintf.c', '_vfiprintf_r', '_vfprintf_r');
replaceInFile('source/libc/stdio/vfprintf.c',
	'#else /* !STRING_ONLY */\n#ifdef INTEGER_ONLY\n\n#ifndef _FVWRITE_IN_STREAMIO',
	'#else /* !STRING_ONLY */\n#ifndef _FVWRITE_IN_STREAMIO');
replaceInFile('source/libc/stdio/vfprintf.c',
	'#else /* !INTEGER_ONLY */\n#ifndef _FVWRITE_IN_STREAMIO\nint __sfputs_r (struct _reent *, FILE *, const char *buf, size_t);\n#endif\nint __sprint_r (struct _reent *, FILE *, register struct __suio *);\n#endif /* !INTEGER_ONLY */',
	'');
replaceInFile('source/libc/stdio/fseeko64.c',
	'fp->_flags &= ~__SEOF;\n  _funlockfile(fp);\n  return 0;',
	'fp->_flags &= ~__SEOF;\n  _newlib_flockfile_end(fp);\n  return 0;',
	)
replaceInFile('public/sys/stat.h',
	'};\n\n#if !(defined(__svr4__) && !defined(__PPC__) && !defined(__sun__)) && !defined(__cris__)',
	'};\n\nstruct stat64 {};\n\n#if !(defined(__svr4__) && !defined(__PPC__) && !defined(__sun__)) && !defined(__cris__)');

// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		fs.unlinkSync(filePath)
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));


