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
const {PackageType, getPackageTypeDirectoryName} = require('./package_type');
const {getToolPath} = require('./tools');
const {escapePath} = require('./escape_path');
const {foreachSourceFile, foreachPermebufSourceFile} = require('./source_files');
const {forgetFileLastModifiedTimestamp, getFileLastModifiedTimestamp} = require('./file_timestamps');
const {getBuildCommand, getLinkerCommand, buildPrefix} = require('./build_commands');
const {getPackageDirectory} = require('./package_directory');
const {compilePermebufToCpp} = require('./permebufs');

// Libraries already built on this run, therefore we shouldn't have to build them again.
const alreadyBuiltLibraries = {};

// A map of the generated source file back to the original file, so if there's an error
// building the generated file, it's better to show the original file that is broken.
const generatedFilenameMap = {};

// Returns the filename we should display, respecting generated files that should remap
// back to the original file.
function getDisplayFilename(filename) {
	const remappedFileName = generatedFilenameMap[filename];
	return remappedFileName ? remappedFileName : filename;
}

// Builds a package.
async function build(packageType, packageName, buildSettings, librariesToLink, parentPublicIncludeDirs,
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

	if (buildSettings.os != 'Perception' && (metadata.skip_local || packageType == PackageType.KERNEL)) {
		return BuildResult.COMPILED;
	}

	if (metadata.public_include) {
		for (let i = 0; i < metadata.public_include.length; i++) {
			parentPublicIncludeDirs.push(escapePath(packageDirectory) + metadata.public_include[i]);
		}
	} else {
		parentPublicIncludeDirs.push(escapePath(packageDirectory) + 'public');
	}

	// Include generated header files.
	let alreadyAddedGeneratedInclude = false;
	if (fs.existsSync(packageDirectory + 'generated/include')) {
		const generatedIncludeDir = escapePath(packageDirectory + 'generated/include');
		parentPublicIncludeDirs.push(generatedIncludeDir);
		alreadyAddedGeneratedInclude = true;
	}

	if (packageType == PackageType.LIBRARY) {
		if (librariesToBuild[packageName] != undefined) {
			// Library is already being built. Just add its public directory and return.
			// This supports recursive dependencies between libraries.
			Object.keys(librariesToBuild[packageName]).forEach((define) => {
				defines[define] = true;
			});
			return BuildResult.COMPILED;
		}
		if (alreadyBuiltLibraries[packageName] != undefined) {
			librariesToLink.push(packageDirectory + 'build/' + buildPrefix(buildSettings) + '.lib');
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
	let errors = false;

	const publicIncludeDirs = [];

	const filesToIgnore = {};
	if (metadata.ignore) {
		metadata.ignore.forEach((fileToIgnore) => {
			filesToIgnore[packageDirectory + fileToIgnore] = true;
		});
	}

	if (!fs.existsSync(packageDirectory + 'build/' + buildPrefix(buildSettings)))
		fs.mkdirSync(packageDirectory + 'build/' + buildPrefix(buildSettings), {recursive: true});

	const depsFile = packageDirectory + 'build/' + buildPrefix(buildSettings) + '/dependencies.json';
	let dependenciesPerFile = fs.existsSync(depsFile) ? JSON.parse(fs.readFileSync(depsFile)) : {};

	if (packageType != PackageType.KERNEL) {
		// Construct generated files.
		await foreachPermebufSourceFile(packageDirectory,
			async function (fullPath, localPath) {
				if (filesToIgnore[fullPath]) {
					return;
				}

				if (!localPath.endsWith('.permebuf')) {
					// Not a permebuf file.
					return;
				}

				let shouldCompileFile = false;

				// We just need one of the generated files to tell if the source file is newer or not.
				const outputFile = packageDirectory + 'generated/source/permebuf/' + getPackageTypeDirectoryName(packageType) +
					'/' + packageName + '/' + localPath + '.cc';
				generatedFilenameMap[outputFile] = fullPath;

				if (!fs.existsSync(outputFile)) {
					// Compile if the output file doesn't exist.
					shouldCompileFile = true;
				} else {
					const outputFileTimestamp = fs.lstatSync(outputFile).mtimeMs;

					const deps = dependenciesPerFile[fullPath];
					if (deps == null) {
						// Compile because we don't know the dependencies.
						shouldCompileFile = true;
					} else {
						for (let i = 0; i < deps.length; i++) {
							if (getFileLastModifiedTimestamp(deps[i]) >= outputFileTimestamp) {
								// Compile because one of the dependencies is newer.
								shouldCompileFile = true;
								break;
							}
						}
					}
				}

				if (shouldCompileFile) {
					console.log('Compiling ' + fullPath + ' to C++');
					deps = [];
					if (!compilePermebufToCpp(localPath,
						packageName,
						packageType,
						deps)) {
						errors = true;
					}
					dependenciesPerFile[fullPath] = deps;
				}
			});

		// If this package is compiled as a unit test, the dependencies can't be, because they aren't what are being tested.
		let childBuildSettings = buildSettings;
		if (buildSettings.test) {
			childBuildSettings = Object.assign({}, buildSettings);
			childBuildSettings.test = false;
		}

		// The kernel can't depend on anything other than itself.
		for (let i = 0; i < metadata.dependencies.length; i++) {
			const dependency = metadata.dependencies[i];
			// console.log('Depends on ' + dependency);
			const childDefines = {};
			const success = await build(PackageType.LIBRARY, dependency, childBuildSettings, librariesToLink, publicIncludeDirs, librariesToBuild,
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

	// Include generated header files.
	if (fs.existsSync(packageDirectory + 'generated/include')) {
		const generatedIncludeDir = escapePath(packageDirectory + 'generated/include');
		params += ' -isystem ' + generatedIncludeDir;
		if (!alreadyAddedGeneratedInclude)
			parentPublicIncludeDirs.push(generatedIncludeDir);
	}

	// Public dirs exported by each of our dependencies.
	publicIncludeDirs.forEach((includeDir) => {
		params += ' -isystem ' + includeDir; // Pre-escaped.
		parentPublicIncludeDirs.push(includeDir);
	});

	params += ' -isystem '+ escapePath(packageDirectory) + 'source';

	Object.keys(defines).forEach((define) => {
		params += ' -D' + define;
	});

	if (metadata.define) {
		metadata.define.forEach((define) => {
			params += ' -D' + define;
		});
	}

	let objectFiles = [];
	let anythingChanged = false;

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
		const binaryPath = packageDirectory + 'build/' + buildPrefix(buildSettings) + '.lib';
		librariesToLink.push(binaryPath);
		if (!anythingChanged && fs.existsSync(binaryPath)) {
			const status = forceRelinkIfApplication ? BuildResult.COMPILED : BuildResult.ALREADY_BUILT;
			alreadyBuiltLibraries[packageName] = {
				defines: defines,
				status: status
			};
			return status;
		}
		if (fs.existsSync(binaryPath)) {
			fs.unlinkSync(binaryPath);
		}

		console.log("Building library " + packageName);
		const command = getLinkerCommand(
			PackageType.LIBRARY, escapePath(binaryPath),
			objectFiles.join(' '), buildSettings);
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
		const binaryPath = packageDirectory + 'build/' + buildPrefix(buildSettings) + '.app';
		if (!anythingChanged && fs.existsSync(binaryPath)
			&& !forceRelinkIfApplication) {
			// We already exist. Check if any libraries are newer that us (even if they didn't recompile.)
			const ourTimestamp = getFileLastModifiedTimestamp(binaryPath);
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
		if (fs.existsSync(binaryPath)) {
			fs.unlinkSync(binaryPath);
		}
		console.log("Building application " + packageName);
		let linkerInput = objectFiles.join(' ') /* pre-escaped */;
		librariesToLink.forEach((libraryToLink) => {
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
			objectFiles.join(' ') /* pre-escaped */, buildSettings);
		try {
			child_process.execSync(command);
			compiled = true;
		} catch (exp) {
			return BuildResult.FAILED;
		}

		return BuildResult.COMPILED;
	} else {
		console.log('Unknown project type.');
		return BuildResult.FAILED;
	}
};

module.exports = {
	build: build
};

