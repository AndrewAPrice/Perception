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
const {BuildResult} = require('./build_result');
const {PackageType} = require('./package_type');
const {getToolPath} = require('./tools');
const {escapePath} = require('./escape_path');
const {foreachSourceFile} = require('./source_files');
const {getFileLastModifiedTimestamp} = require('./file_timestamps');
const {getBuildCommand, getLinkerCommand} = require('./build_commands');
const {getPackageDirectory} = require('./package_directory');

// Libraries already built on this run, therefore we shouldn't have to build them again.
const alreadyBuiltLibraries = {};

// Builds a package.
async function build(packageType, packageName, librariesToLink, parentPublicIncludeDirs,
	librariesToBuild, defines) {
	if (librariesToLink == undefined) {
		librariesToLink = [];
	}
	if (parentPublicIncludeDirs == undefined) {
		parentPublicIncludeDirs = [];
	}

	if (librariesToBuild == undefined) {
		librariesToBuild = {};
	}
	if (defines == undefined) {
		defines = {};
	}

	const packageDirectory = getPackageDirectory(packageType, packageName);
	if (!fs.existsSync(packageDirectory + 'metadata.json')) {
		fs.writeFileSync(packageDirectory + 'metadata.json', JSON.stringify({ dependencies: [] }));
	}
	const metadata = JSON.parse(fs.readFileSync(packageDirectory + 'metadata.json'));

	if (metadata.public_include) {
		for (let i = 0; i < metadata.public_include.length; i++) {
			parentPublicIncludeDirs.push(escapePath(packageDirectory) + metadata.public_include[i]);
		}
	} else {
		parentPublicIncludeDirs.push(escapePath(packageDirectory) + 'public');
	}

	if (packageType == PackageType.LIBRARY) {
		if (librariesToBuild[packageName] != undefined) {
			// Library is already being built. Just add it's public directory and return.
			// This supports recursive dependencies between libraries.
			Object.keys(librariesToBuild[packageName]).forEach((define) => {
				defines[define] = true;
			});
			return BuildResult.COMPILED;
		}
		if (alreadyBuiltLibraries[packageName] != undefined) {
			librariesToLink.push(packageDirectory + 'library.lib');
			const alreadyBuiltLibrary = alreadyBuiltLibraries[packageName];

			Object.keys(alreadyBuiltLibrary.defines).forEach((define) => {
				defines[define] = true;
			});

			return alreadyBuiltLibrary.status;
		}
	}

	if (metadata.public_define) {
		metadata.public_define.forEach((define) => {
			defines[define] = true;
		});
	}
	librariesToBuild[packageName] = defines;

	if (metadata.third_party) {
		if (fs.existsSync(packageDirectory + 'prepare.js') && !fs.existsSync(packageDirectory + 'third_party_files.json')) {
			console.log('Preparing ' + packageName);
			try {
				child_process.execSync('node prepare', {cwd: packageDirectory, stdio: 'inherit'});
			} catch (exp) {
				return ERROR;
			}
		}
	}

	let forceRelinkIfApplication = false;

	const publicIncludeDirs = [];

	if (packageType != PackageType.KERNEL) {
		// The kernel can't depend on anything other than itself.
		for (let i = 0; i < metadata.dependencies.length; i++) {
			const dependency = metadata.dependencies[i];
			// console.log('Depends on ' + dependency);
			const childDefines = {};
			const success = await build(PackageType.LIBRARY, dependency, librariesToLink, publicIncludeDirs, librariesToBuild,
				childDefines);
			if (!success) {
				console.log('Dependency ' + dependency + ' failed to build.');
				return BuildResult.FAILED;
			};
			Object.keys(childDefines).forEach((define) => {
				defines[define] = true;
			});
		}
	}

	const filesToIgnore = {};
	if (metadata.ignore) {
		metadata.ignore.forEach((fileToIgnore) => {
			filesToIgnore[packageDirectory + fileToIgnore] = true;
		});
	}

	const depsFile = packageDirectory + 'dependencies.json';
	let dependenciesPerFile = fs.existsSync(depsFile) ? JSON.parse(fs.readFileSync(depsFile)) : {};

	let params = '';

	if (metadata.include) {
		metadata.include.forEach((includeDir) => {
			params += ' -isystem ' + packageDirectory + includeDir;
		});
	}

	if (metadata.public_include) {
		for (let i = 0; i < metadata.public_include.length; i++) {
			params += ' -isystem '+ escapePath(packageDirectory) + metadata.public_include[i];
		}
	} else {
		params += ' -isystem '+ escapePath(packageDirectory) + 'public';
	}

	// Public dirs exported by each of our dependencies.
	publicIncludeDirs.forEach((includeDir) => {
		params += ' -isystem ' + includeDir; // Pre-escaped.
		parentPublicIncludeDirs.push(includeDir);
	});
	params += ' -D__perception__';

	Object.keys(defines).forEach((define) => {
		params += ' -D' + define;
	});

	if (metadata.define) {
		metadata.define.forEach((define) => {
			params += ' -D' + define;
		});
	}

	let objectFiles = [];
	let errors = false;
	let anythingChanged = false;

    await foreachSourceFile(packageDirectory,
		async function (file, buildPath) {
			if (filesToIgnore[file]) {
				return;
			}
			const buildCommand = getBuildCommand(file, packageType, params);
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
					for (let i = 0; i < deps.length; i++) {
						if (getFileLastModifiedTimestamp(deps[i]) >= objectFileTimestamp) {
							// Compile because one of the dependencies is newer.
							shouldCompileFile = true;
							break;
						}
					}
				}
			}

			if (shouldCompileFile) {
				anythingChanged = true;
				console.log("Compiling " + file);
				const command = buildCommand + ' -o ' + escapePath(objectFile) + ' ' + escapePath(file);
				//console.log(" with command: " + command);
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
		if (packageType == PackageType.LIBRARY) {
			alreadyBuiltLibraries[packageName] = {
				defines: {},
				status: BuildResult.FAILED
			};
		}
		return BuildResult.FAILED;
	} else if (objectFiles.length == 0) {
		console.log('Nothing was compiled.');
		if (packageType == PackageType.LIBRARY) {
			alreadyBuiltLibraries[packageName] = {
				defines: {},
				status: BuildResult.FAILED
			};
		}
		return BuildResult.FAILED;
	} else if (packageType == PackageType.LIBRARY) {
		librariesToLink.push(packageDirectory + 'library.lib');
		if (!anythingChanged && fs.existsSync(packageDirectory + 'library.lib')) {
			const status = forceRelinkIfApplication ? BuildResult.COMPILED : BuildResult.ALREADY_BUILT;
			alreadyBuiltLibraries[packageName] = {
				defines: defines,
				status: status
			};
			return status;
		}
		if (fs.existsSync(packageDirectory + 'library.lib')) {
			fs.unlinkSync(packageDirectory + 'library.lib');
		}

		console.log("Building library " + packageName);
		const command = getLinkerCommand(
			PackageType.LIBRARY, escapePath(packageDirectory) + 'library.lib',
			objectFiles.join(' '));
		//console.log(command);
		try {
			child_process.execSync(command);
			compiled = true;
		} catch (exp) {
			alreadyBuiltLibraries[packageName] = {
				defines: {},
				status: BuildResult.FAILED
			};
			return BuildResult.FAILED;
		}

		alreadyBuiltLibraries[packageName] = {
			defines: defines,
			status: BuildResult.COMPILED
		};
		return BuildResult.COMPILED;
	} else if (packageType == PackageType.APPLICATION) {
		if (!anythingChanged && fs.existsSync(packageDirectory + 'application.app')
			&& !forceRelinkIfApplication) {
			// We already exist. Check if any libraries are newer that us (even if they didn't recompile.)
			const ourTimestamp = getFileLastModifiedTimestamp(packageDirectory + 'application.app');
			for (let i = 0; i < librariesToLink.length && !forceRelinkIfApplication; i++) {
				const libraryTimestamp = getFileLastModifiedTimestamp(librariesToLink[i]);
				if (libraryTimestamp > ourTimestamp) { // The library is newer than us, relink.
					forceRelinkIfApplication = true;
				}
			}

			if (!forceRelinkIfApplication) {
				return BuildResult.ALREADY_BUILT;
			}
		}
		if (fs.existsSync(packageDirectory + 'application.app')) {
			fs.unlinkSync(packageDirectory + 'application.app');
		}
		console.log("Building application " + packageName);
		let linkerInput = objectFiles.join(' ') /* pre-escaped */;
		librariesToLink.forEach((libraryToLink) => {
			linkerInput += ' ' + escapePath(libraryToLink);
		});

		let command =  getLinkerCommand(
			PackageType.APPLICATION, escapePath(packageDirectory) + 'application.app',
			linkerInput);
		// console.log(command);
		try {
			child_process.execSync(command);
			compiled = true;
		} catch (exp) {
			return BuildResult.FAILED;
		}

		return BuildResult.COMPILED;
	} else if (packageType == PackageType.KERNEL) {
		if (!anythingChanged && fs.existsSync(packageDirectory + 'kernel.app')) {
			return BuildResult.ALREADY_BUILT;
		}
		if (fs.existsSync(packageDirectory + 'kernel.app')) {
			fs.unlinkSync(packageDirectory + 'kernel.app');
		}

		let command = getLinkerCommand(
			PackageType.KERNEL, escapePath(packageDirectory) + 'kernel.app',
			objectFiles.join(' ') /* pre-escaped */);
		try {
			child_process.execSync(command);
			compiled = true;
		} catch (exp) {
			return BuildResult.FAILED;
		}

		return BuildResult.COMPILED;
	} else {
		console.log('Unknown project type.')
		return BuildResult.FAILED;
	}
};

module.exports = {
	build: build
};

