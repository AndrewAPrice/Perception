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

const musl_git_repository = 'git://git.musl-libc.org/musl';

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

filesToIgnore['include/syscall_arch.h'] = true;

copyFilesInDirectory('third_party/include', 'include');
copyFilesInDirectory('third_party/arch/generic', 'include');
copyFilesInDirectory('third_party/arch/x86_64', 'include');
copyFilesInDirectory('third_party/src/aio', 'source/aio');
copyFilesInDirectory('third_party/src/complex', 'source/complex');
copyFilesInDirectory('third_party/src/conf', 'source/conf');
copyFilesInDirectory('third_party/src/crypt', 'source/crypt');
copyFilesInDirectory('third_party/src/ctype', 'source/ctype');
copyFilesInDirectory('third_party/src/dirent', 'source/dirent');
copyFilesInDirectory('third_party/src/complex', 'source/complex');
copyFilesInDirectory('third_party/src/env', 'source/env');
copyFilesInDirectory('third_party/src/errno', 'source/errno');
copyFilesInDirectory('third_party/src/exit', 'source/exit', true);
copyFilesInDirectory('third_party/src/fcntl', 'source/fctnl');
copyFilesInDirectory('third_party/src/fenv', 'source/fenv', true);
copyFilesInDirectory('third_party/src/fenv/x86_64', 'source/fenv');
copyFilesInDirectory('third_party/src/include', 'source/include');
copyFilesInDirectory('third_party/src/internal', 'source/internal');
copyFilesInDirectory('third_party/src/ipc', 'source/ipc');
copyFilesInDirectory('third_party/src/ldso', 'source/ldso', true);
copyFilesInDirectory('third_party/src/ldso/x86_64', 'source/ldso');
copyFilesInDirectory('third_party/src/legacy', 'source/legacy');
copyFilesInDirectory('third_party/src/linux', 'source/linux');
copyFilesInDirectory('third_party/src/locale', 'source/locale');
copyFilesInDirectory('third_party/src/malloc', 'source/malloc');
copyFilesInDirectory('third_party/src/locale', 'source/locale');
copyFilesInDirectory('third_party/src/math', 'source/math', true);
rename('source/math/fma.c', 'source/math/fma.c.inl');
rename('source/math/fmaf.c', 'source/math/fmaf.c.inl');
copyFilesInDirectory('third_party/src/math/x86_64', 'source/math');
copyFilesInDirectory('third_party/src/misc', 'source/misc');
copyFilesInDirectory('third_party/src/mman', 'source/mman');
copyFilesInDirectory('third_party/src/mq', 'source/mq');
copyFilesInDirectory('third_party/src/multibyte', 'source/multibyte');
copyFilesInDirectory('third_party/src/network', 'source/network');
copyFilesInDirectory('third_party/src/passwd', 'source/passwd');
copyFilesInDirectory('third_party/src/prng', 'source/prng');
copyFilesInDirectory('third_party/src/process', 'source/process', true);
copyFilesInDirectory('third_party/src/process/x86_64', 'source/process');
copyFilesInDirectory('third_party/src/regex', 'source/regex');
copyFilesInDirectory('third_party/src/sched', 'source/sched');
copyFilesInDirectory('third_party/src/search', 'source/search');
copyFilesInDirectory('third_party/src/select', 'source/select');
// Needs syscalls:
// copyFilesInDirectory('third_party/src/setjmp/x86_64', 'source/setjmp');
copyFilesInDirectory('third_party/src/signal', 'source/signal', true);
// Needs syscalls:
// copyFilesInDirectory('third_party/src/signal/x86_64', 'source/signal');
copyFilesInDirectory('third_party/src/stat', 'source/stat');
copyFilesInDirectory('third_party/src/stdio', 'source/stdio');
copyFilesInDirectory('third_party/src/stdlib', 'source/stdlib');
copyFilesInDirectory('third_party/src/string', 'source/string', true);
copyFilesInDirectory('third_party/src/string/x86_64', 'source/string');
copyFilesInDirectory('third_party/src/temp', 'source/temp');
copyFilesInDirectory('third_party/src/termios', 'source/termios');
copyFilesInDirectory('third_party/src/temp', 'source/temp');
copyFilesInDirectory('third_party/src/thread', 'source/thread', true);
// Needs syscalls:
// copyFilesInDirectory('third_party/src/thread/x86_64', 'source/thread');
copyFilesInDirectory('third_party/src/time', 'source/time');
copyFilesInDirectory('third_party/src/unistd', 'source/unistd', true);
// copyFilesInDirectory('third_party/compat', 'source/compat');


// SED alltypes.h
{
	let rebuildAllTypes = false;
	if (!fs.existsSync('include/bits/alltypes.h')) {
		rebuildAllTypes = true;
	} else {
		const destUpdateTime = fs.lstatSync('include/bits/alltypes.h').mtimeMs;
		const src1UpdateTime = fs.lstatSync('include/bits/alltypes.h.in').mtimeMs;
		const src2UpdateTime = fs.lstatSync('include/alltypes.h.in').mtimeMs;

		if (src1UpdateTime > destUpdateTime || src2UpdateTime > destUpdateTime) {
			rebuildAllTypes = true;
		}
	}
	if (rebuildAllTypes) {
		console.log('Generating include/alltypes.h');
		try {
			child_process.execSync('sed -f third_party/tools/mkalltypes.sed include/bits/alltypes.h.in ' +
				'include/alltypes.h.in > include/bits/alltypes.h', {stdio: 'inherit'});
		} catch {}
	}
}


// SED syscall.h
{
	let rebuildSyscall = false;
	if (!fs.existsSync('include/bits/syscall.h.in')) {
		rebuildSyscall = true;
	} else {
		const destUpdateTime = fs.lstatSync('include/bits/syscall.h').mtimeMs;
		const srcUpdateTime = fs.lstatSync('include/bits/syscall.h.in').mtimeMs;

		if (srcUpdateTime > destUpdateTime) {
			rebuildSyscall = true;
		}
	}
	if (rebuildSyscall) {
		console.log('Generating include/syscall.h');
		try {
			child_process.execSync('sed -e s/__NR_/SYS_/p include/bits/syscall.h.in '+
				'> include/bits/syscall.h', {stdio: 'inherit'});
		} catch {}
	}
}


/* obj/include/bits/syscall.h: $(srcdir)/arch/$(ARCH)/bits/syscall.h.in
	cp $< $@
	sed -n -e s/__NR_/SYS_/p < $< >> $@
*/
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
