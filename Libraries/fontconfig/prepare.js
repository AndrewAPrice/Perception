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

const git_repository = 'https://gitlab.freedesktop.org/fontconfig/fontconfig.git';

// Check that the third directory exists.
if (!fs.existsSync('third_party')) {
	console.log('Downloading fontconfig');
	// Grab it from github.
	const command = 'git clone ' + git_repository + ' third_party';
	try {
		child_process.execSync(command, {stdio: 'inherit'});
	} catch (exp) {
		console.log('Error downloading fontconfig: ' + exp);
		process.exit(1);
	}
} else {
	console.log('Attempting to update fontconfig');
	// Try to update it.
	const command = 'git pull ' + git_repository;
	try {
		child_process.execSync(command, {cwd: 'third_party', stdio: 'inherit'});
	} catch (exp) {
		console.log('Error updating fontconfig: ' + exp);
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
	if (!fromFileStats) {
		fromFileStats = fs.lstatSync(fromPath);
	}
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
copyFilesInDirectory('third_party/fontconfig', 'public/fontconfig', '.h');
copyFilesInDirectory('third_party/src', 'source/src');
copyFile('third_party/config-fixups.h', 'source/config-fixups.h');
copyFile('third_party/src/fcstdint.h.in', 'source/src/fcstdint.h');
copyFilesInDirectory('third_party/conf.d', 'assets/templates', '.conf');



try {
	child_process.execSync('third_party/src/makealias third_party/src ' +
		'source/src/fcalias.h source/src/fcaliastail.h ' +
		'third_party/fontconfig/fontconfig.h ' +
		'third_party/src/fcdeprecate.h ' +
		'third_party/fontconfig/fcprivate.h', {stdio: 'inherit'});
	third_party_files['source/fsrc/calias.h'] = true;
	third_party_files['source/src/fcaliastail.h'] = true;
} catch (exp) {
	console.log('Error running makealias: ' + exp);
	process.exit(1);
}


try {
	child_process.execSync('third_party/src/makealias third_party/src ' +
		'source/src/fcftalias.h source/src/fcftaliastail.h ' +
		'third_party/fontconfig/fcfreetype.h', {stdio: 'inherit'});
	third_party_files['source/src/fcftalias.h'] = true;
	third_party_files['source/src/fcftaliastail.h'] = true;
} catch (exp) {
	console.log('Error running makealias: ' + exp);
	process.exit(1);
}

{
	createDirectoryIfItDoesntExist('source/fc-lang');
	const orth_files = [];
	const filesInDirectory = fs.readdirSync('third_party/fc-lang');
	for (let i = 0; i < filesInDirectory.length; i++) {
		const entryName = filesInDirectory[i];
		const fileExtension = path.extname(entryName);
		if (fileExtension == '.orth') {
			orth_files.push(entryName);
		}
	}


	try {
		child_process.execSync('python3 third_party/fc-lang/fc-lang.py ' +
			orth_files.join(' ') +
			' --template third_party/fc-lang/fclang.tmpl.h ' +
			' --output source/fc-lang/fclang.h ' +
			' --directory third_party/fc-lang'
			, {stdio: 'inherit'});
		third_party_files['source/fc-lang/fclang.h'] = true;
	} catch (exp) {
		console.log('Error running fc-lang.py: ' + exp);
		process.exit(1);
	}
}

try {
	createDirectoryIfItDoesntExist('source/fc-case');
	child_process.execSync('python3 third_party/fc-case/fc-case.py ' +
		' third_party/fc-case/CaseFolding.txt' +
		' --template third_party/fc-case/fccase.tmpl.h ' +
		' --output source/fc-case/fccase.h'
		, {stdio: 'inherit'});
	third_party_files['source/fc-case/fccase.h'] = true;
} catch (exp) {
	console.log('Error running fc-case.py: ' + exp);
	process.exit(1);
}

copyFile('third_party/src/cutout.py', 'generated/cutout.py');
replaceInFile('generated/cutout.py',
	'    buildroot = args[0].buildroot\n'+
"    with open(os.path.join(buildroot, 'meson-info', 'intro-buildoptions.json')) as json_file:\n"+
'        bopts = json.load(json_file)\n'+
'        for opt in bopts:\n'+
"            if opt['name'] == 'c_args' and opt['section'] == 'compiler' and opt['machine'] == 'host':\n"+
"                host_cargs = opt['value']\n"+
'                break', '');

try {
	child_process.execSync('python3 generated/cutout.py' +
		' third_party/src/fcobjshash.gperf.h' +
		' generated/fcobjshash.gperf' +
		' third_party gcc -I third_party -I third_party/src -E -P'
		, {stdio: 'inherit'});
	third_party_files['generated/fcobjshash.gperf'] = true;
} catch (exp) {
	console.log('Error running cutout.py: ' + exp);
	process.exit(1);
}

try {
	child_process.execSync('gperf --pic -m 100 ' +
		' generated/fcobjshash.gperf' +
		' --output-file source/src/fcobjshash.h'
		, {stdio: 'inherit'});
	third_party_files['source/src/fcobjshash.h'] = true;
} catch (exp) {
	console.log('Error running gperf: ' + exp);
	process.exit(1);
}

replaceInFile('source/src/fcobjshash.h',
	'register unsigned int len',
	'register size_t len');

// Remove any third party files that don't exist in the latest build.
Object.keys(third_party_files_from_last_run).forEach(function (filePath) {
	if (fs.existsSync(filePath)) {
		console.log('Removing ' + filePath);
		fs.unlinkSync(filePath)
		removeDirectoryIfEmpty(path.dirname(filePath));
	};
});
fs.writeFileSync('third_party_files.json', JSON.stringify(third_party_files));
