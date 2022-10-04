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
const process = require('process');
const {PackageType, getPackageTypeDirectoryName} = require('./package_type');
const {getToolPath} = require('./tools');
const {foreachSourceFile} = require('./source_files');
const {forgetFileLastModifiedTimestamp, getFileLastModifiedTimestamp} = require('./file_timestamps');
const {getBuildCommand, getLinkerCommand, buildPrefix} = require('./build_commands');
const {getPackageDirectory} = require('./package_directory');
const {transpilePermebufToCppForPackage} = require('./permebufs');
const {getMetadata} = require('./metadata');
const {makeSureThirdPartyIsLoaded} = require('./third_party');
const {getDisplayFilename} = require('./generated_filename_map');
const {constructIncludeAndDefineParams} = require('./build_parameters');
const {escapePath, forEachIfDefined} = require('./utils');
const {maybeGenerateClangCompleteFilesForProject} = require('./clang_complete');

// Libraries already built on this run, therefore we shouldn't have to build them again.
const alreadyBuiltLibraries = {};

async function build(packageType, packageName, buildSettings) {
	if (packageType == PackageType.LIBRARY) {
		if (alreadyBuiltLibraries[packageName] == undefined) {
			alreadyBuiltLibraries[packageName] = forceBuild(packageType, packageName, buildSettings);
		}
		return alreadyBuiltLibraries[packageName];
	}

	return forceBuild(packageType, packageName, buildSettings);
}

async function compileAndLink(packageType, packageName, packageDirectory, buildSettings, metadata) {
	const filesToIgnore = {};
	forEachIfDefined(metadata.ignore, (fileToIgnore) => {
		filesToIgnore[fileToIgnore] = true;
	});

	// Make sure the path exists where we're going to put our outputs.
	if (!fs.existsSync(packageDirectory + 'build/' + buildPrefix(buildSettings)))
		fs.mkdirSync(packageDirectory + 'build/' + buildPrefix(buildSettings), {recursive: true});

	const depsFile = packageDirectory + 'build/' + buildPrefix(buildSettings) + '/dependencies.json';
	let dependenciesPerFile = fs.existsSync(depsFile) ? JSON.parse(fs.readFileSync(depsFile)) : {};

	let anythingChanged = false;
	let params = ' ' + constructIncludeAndDefineParams(packageDirectory,
		metadata, 'source', true, false).join(' ');

	let objectFiles = [];
	let errors = false;

    await foreachSourceFile(packageDirectory,
    	buildSettings,
		async function (file, buildPath) {
			if (filesToIgnore[file]) {
				return;
			}
			const buildCommand = getBuildCommand(file, packageType, params, buildSettings);
			if (buildCommand == '') {
				// Skip this file.
				return;
			}

			let shouldCompileFile = false;
			const objectFile = buildPath + /*sourceFileToObjectFile(packageDirectory, file) + */ '.obj';
			objectFiles.push(escapePath(objectFile));

			if (!fs.existsSync(objectFile)) {
				// Compile if the output file doesn't exist.
				shouldCompileFile = true;
			} else {
				const objectFileTimestamp = fs.lstatSync(objectFile).mtimeMs;

				const deps = dependenciesPerFile[file];
				if (deps == null) {
					// Compile because we don't know the dependencies.
					shouldCompileFile = true;
				} else {
					if (metadata.metadata_updated >= objectFileTimestamp) {
						shouldCompileFile = true;
					} else {
						for (let i = 0; i < deps.length; i++) {
							if (getFileLastModifiedTimestamp(deps[i]) >= objectFileTimestamp) {
								// Compile because one of the dependencies is newer.
								shouldCompileFile = true;
								break;
							}
						}
					}
				}
			}

			if (shouldCompileFile) {
				anythingChanged = true;
				console.log('Compiling ' + getDisplayFilename(file));
				const command = buildCommand + ' -o ' + escapePath(objectFile) + ' ' + escapePath(file);

				// console.log(" with command: " + command);
				var compiled = false;
				try {
					child_process.execSync(command);
					compiled = true;
				} catch (exp) {
					errors = true;
				}

				if (!compiled) { return; }

				// Update "dependencies".
				if (file.endsWith('.asm') || file.endsWith('.s')) {
					// Assembly files don't include anything other than themselves.
					dependenciesPerFile[file] = [file];
				} else {
					let depsStr = fs.readFileSync('temp.d').toString('utf8');
					let separator = depsStr.indexOf(':');
					if (separator == -1) {
						throw "Error parsing dependencies: " + deps;
					}
					let newDeps = [];
					depsStr.substring(separator + 1).split("\\\n").forEach(s1 => {
						// Pipe is an invalid path character, so we'll temporarily replace '\ '
						// (a space in the path) into '|' before splitting the files that are
						// seperated by spaces.
						s1.replace(/\\ /g, '|').split(' ').forEach(s2 => {
							s2 = s2.replace(/\|/g, ' ').trim();
							if (s2.length > 1) {
								newDeps.push(s2);
							}
						});
					});
					dependenciesPerFile[file] = newDeps;
				}
			}
		});

    if (anythingChanged) {
		fs.writeFileSync(depsFile, JSON.stringify(dependenciesPerFile));
	}

	if (errors) {
		console.log('There were errors compiling.');
		return false;
	}

	return link(packageType, packageName, packageDirectory, objectFiles, anythingChanged, buildSettings,
		metadata);
}

function libraryBinaryPath(packageName, buildSettings) {
	const packageDirectory = getPackageDirectory(PackageType.LIBRARY, packageName);
	return packageDirectory + 'build/' + buildPrefix(buildSettings) + '.lib';
}

async function link(packageType, packageName, packageDirectory, objectFiles, anythingChanged, buildSettings,
	metadata) {
	if (objectFiles.length == 0) {
		// Nothing to link. This might be a header only library, but an error for non-libraries.
		return packageType == PackageType.LIBRARY;
	} else if (packageType == PackageType.LIBRARY) {
		// Link library.
		const binaryPath = libraryBinaryPath(packageName, buildSettings);
		if (!anythingChanged && fs.existsSync(binaryPath)) {
			// Nothing changed and the file exists.
			return true;
		}
		if (fs.existsSync(binaryPath)) {
			fs.unlinkSync(binaryPath);
		}

		console.log("Building library " + packageName);
		const command = getLinkerCommand(
			PackageType.LIBRARY, escapePath(binaryPath),
			objectFiles.join(' '), buildSettings);
		try {
			child_process.execSync(command);
		} catch (exp) {
			return false;
		}

		return true;
	} else if (packageType == PackageType.APPLICATION) {
		// Link application.
		const binaryPath = packageDirectory + 'build/' + buildPrefix(buildSettings) + '.app';
		const librariesToLink = [];

		forEachIfDefined(metadata.libraries, library => {
			const libraryPath = libraryBinaryPath(library, buildSettings);
			if (fs.existsSync(libraryPath)) {
				// There can be header only libraries that didn't build anything.
				librariesToLink.push(libraryPath);
			}
		});

		if (!anythingChanged && fs.existsSync(binaryPath)) {
			// We already exist. Check if any libraries are newer that us (even if they didn't recompile.)
			const ourTimestamp = getFileLastModifiedTimestamp(binaryPath);

			if (librariesToLink.find(
				libraryToLink => (getFileLastModifiedTimestamp(libraryToLink) > ourTimestamp))
				== undefined) {
				// No libraries are newer than us, no need to relink.
				return true;
			}
		}
		if (fs.existsSync(binaryPath)) {
			fs.unlinkSync(binaryPath);
		}
		console.log("Building application " + packageName);
		let linkerInput = objectFiles.join(' ') /* pre-escaped */;
		librariesToLink.forEach(libraryToLink => {
			linkerInput += ' ' + escapePath(libraryToLink);
		});

		let command = getLinkerCommand(
			PackageType.APPLICATION, escapePath(binaryPath),
			linkerInput, buildSettings);
		try {
			child_process.execSync(command);
			compiled = true;
			forgetFileLastModifiedTimestamp(binaryPath);
		} catch (exp) {
			return false;
		}

		return true;
	} else if (packageType == PackageType.KERNEL) {
		// Link kernel.
		if (!anythingChanged && fs.existsSync(packageDirectory + 'kernel.app')) {
			return true;
		}
		if (fs.existsSync(packageDirectory + 'kernel.app')) {
			fs.unlinkSync(packageDirectory + 'kernel.app');
		}

		let command = getLinkerCommand(
			PackageType.KERNEL, escapePath(packageDirectory) + 'kernel.app',
			objectFiles.join(' ') /* pre-escaped */, buildSettings);
		try {
			child_process.execSync(command);
			compiled = true;
		} catch (exp) {
			return false;
		}

		return true;
	} else {
		console.log('Unknown project type.');
		return false;
	}
}

// Builds a package.
async function forceBuild(packageType, packageName, buildSettings) {
	const isLocal = buildSettings.os != 'Perception';
	const metadata = getMetadata(packageName, packageType, isLocal);

	if (isLocal && (metadata.skip_local || packageType == PackageType.KERNEL)) {
		return true;
	}

	let error = false;

	// If there are any third party libraries, make sure they're loaded.
	forEachIfDefined(metadata.third_party_libraries, third_party_library => {
		error |= !makeSureThirdPartyIsLoaded(third_party_library, PackageType.LIBRARY);
	});

	// If we are third party, make sure we're loaded.
	if (metadata.third_party) {
		error |= !makeSureThirdPartyIsLoaded(packageName, packageType);
	}
	if (error) return false;

	// If anything we dependend on have Permebufs, transpile them.
	forEachIfDefined(metadata.libraries_with_permebufs, library_with_permebufs => {
		error |= !transpilePermebufToCppForPackage(library_with_permebufs, PackageType.LIBRARY, buildSettings);
	});

	// If we have Permebufs, transpile them.
	if (metadata.has_permebufs) {
		error |= !transpilePermebufToCppForPackage(packageName, packageType, buildSettings);
	}
	if (error) return false;

	// We have dependencies, build them before us.
	if (packageType == PackageType.APPLICATION && metadata.libraries) {
		for (let i = 0; i < metadata.libraries.length; i++) {
			const dependency = metadata.libraries[i];
			const childDefines = {};
			const success = await build(PackageType.LIBRARY, dependency, buildSettings);
			if (!success) {
				console.log('Dependency ' + dependency + ' failed to build.');
				return false;
			}
		}
	}

	// Update our .clang_complete files so that auto-complete works in editors.
	const packageDirectory = getPackageDirectory(packageType, packageName);
	maybeGenerateClangCompleteFilesForProject(packageDirectory, metadata);

	// All of the prework is done. The only thing left to do is to build this
	// package.
	if (buildSettings.compile) {
		return compileAndLink(packageType, packageName, packageDirectory,
			buildSettings, metadata);
	} else {
		return true;
	}
};

module.exports = {
	build: build
};


