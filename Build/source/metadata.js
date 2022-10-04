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

const process = require('process');
const fs = require('fs');
const {forEachIfDefined} = require('./utils');
const {getPackageDirectory} = require('./package_directory');
const {PackageType, getPackageTypeDirectoryName} = require('./package_type');

const standaloneApplicationMetadatas = {};
const combinedApplicationMetadatas = {};
const standaloneLibraryMetadatas = {};
const combinedLibraryMetadatas = {};

function loadStandaloneMetadata(name, packageType) {
	const packageDirectory = getPackageDirectory(packageType, name);

	if (!fs.existsSync(packageDirectory + 'metadata.json')) {
		throw Error('No metadata file exists for ' +
			(packageType == PackageType.APPLICATION ? 'application' : 'library') +
			' "' + name + '". Expected the file to be at: '
			+ packageDirectory + 'metadata.json');
	}
	let metadata = {};
	try {
		metadata = JSON.parse(fs.readFileSync(packageDirectory + 'metadata.json'));
	} catch (exp) {
		console.log('Error reading ' + packageDirectory + 'metadata.json: ' + exp);
		process.exit(-1);
	}
	if (!metadata.public_include) {
		if (packageType == PackageType.APPLICATION) {
			metadata.public_include = [];
		} else {
			metadata.public_include = ['public'];
		}
	}

	if (fs.existsSync(packageDirectory + 'permebuf') &&
		fs.lstatSync(packageDirectory + 'permebuf').isDirectory()) {
		metadata.public_include.push('generated/include');
		metadata.has_permebufs = true;
	}
	metadata.package_directory = packageDirectory;
	metadata.metadata_updated = fs.lstatSync(packageDirectory + 'metadata.json').mtimeMs;
	return metadata;

}

function getStandaloneApplicationMetadata(name) {
	if (!standaloneApplicationMetadatas[name]) {
		standaloneApplicationMetadatas[name] = loadStandaloneMetadata(name, PackageType.APPLICATION);
	}

	return standaloneApplicationMetadatas[name];
}

function getStandaloneLibraryMetadata(name) {
	if (!standaloneLibraryMetadatas[name]) {
		standaloneLibraryMetadatas[name] = loadStandaloneMetadata(name, PackageType.LIBRARY);
	}

	return standaloneLibraryMetadatas[name];
}

function constructCombinedMetadata(metadata, name, packageType, isLocal) {

	// Start with merging in this library's metadata.
	let packageDirectory = getPackageDirectory(packageType, name);

	const combinedMetadata = {
		public_include: [],
		include: [],
		my_public_include: [],
		my_include: [],
		public_define: [],
		define: [],
		libraries: [],
		ignore: [],
		libraries_with_permebufs: [],
		third_party_libraries: [],
		has_permebufs: metadata.has_permebufs ? true : false,
		third_party: metadata.third_party ? true : false,
		skip_local: metadata.skip_local ? true : false,
		metadata_updated: metadata.metadata_updated
	};

	forEachIfDefined(metadata.public_include, publicInclude => {
			combinedMetadata.public_include.push(packageDirectory + publicInclude);
			combinedMetadata.my_public_include.push(publicInclude);
		});
	forEachIfDefined(metadata.ignore, ignore => {
			combinedMetadata.ignore.push(packageDirectory + ignore);
		});
	forEachIfDefined(metadata.include, include => {
			combinedMetadata.include.push(packageDirectory + include);
			combinedMetadata.my_include.push(include);
		});
	forEachIfDefined(metadata.define, define => {
			combinedMetadata.define.push(define);
		});
	forEachIfDefined(metadata.public_define, define => {
			combinedMetadata.public_define.push(define);
		});

	const dependenciesEncountered = {};
	const dependenciesToInclude = [];
	if (packageType == PackageType.LIBRARY) {
		dependenciesEncountered[name] = true;
	}
	function maybeAddDependency(dependency) {
		if (!dependenciesEncountered[dependency]) {
			dependenciesToInclude.push(dependency);
			combinedMetadata.libraries.push(dependency);
			dependenciesEncountered[dependency] = true;
		};
	}
	forEachIfDefined(metadata.dependencies, maybeAddDependency);

	const unprioritizedIncludesToAdd = [];
	const prioritizedIncludesToAdd = {}; 

	// Merge in each dependency.
	while (dependenciesToInclude.length > 0) {
		const thisDependency = dependenciesToInclude.pop();

		metadata = getStandaloneLibraryMetadata(thisDependency);
		if (metadata.skip_local && isLocal) continue;
		packageDirectory = getPackageDirectory(PackageType.LIBRARY, thisDependency);

		if (metadata.include_priority == undefined) {
			forEachIfDefined(metadata.public_include, publicInclude => {
				unprioritizedIncludesToAdd.push(packageDirectory + publicInclude);
			});
		} else {
			if(prioritizedIncludesToAdd[metadata.include_priority] == undefined) {
				prioritizedIncludesToAdd[metadata.include_priority] = [];
			}
			const includesToAdd = prioritizedIncludesToAdd[metadata.include_priority];
			forEachIfDefined(metadata.public_include, publicInclude => {
				includesToAdd.push(packageDirectory + publicInclude);
			});
		}

		forEachIfDefined(metadata.public_define, publicDefine => {
				combinedMetadata.public_define.push(publicDefine);
			});
		forEachIfDefined(metadata.dependencies, maybeAddDependency);
		if (metadata.metadata_updated > combinedMetadata.metadata_updated) {
			combinedMetadata.metadata_updated = metadata.metadata_updated;
		}
		if (metadata.has_permebufs) {
			combinedMetadata.libraries_with_permebufs.push(thisDependency);
		}
		if (metadata.third_party) {
			combinedMetadata.third_party_libraries.push(thisDependency);
		}
	}

	// Add the prioritized includes.
	Object.keys(prioritizedIncludesToAdd).sort().forEach(includePriority => {
		prioritizedIncludesToAdd[includePriority].forEach(include =>
			combinedMetadata.public_include.push (include));
	});
	// Add the unprioritized includes.
	unprioritizedIncludesToAdd.forEach(include => combinedMetadata.public_include.push(include));

	return combinedMetadata;
}

function getApplicationMetadata(name, isLocal) {
	if (!combinedApplicationMetadatas[name]) {
		combinedApplicationMetadatas[name] = constructCombinedMetadata(
		getStandaloneApplicationMetadata(name), name, PackageType.APPLICATION, isLocal);
	}
	return combinedApplicationMetadatas[name];
}

function getLibraryMetadata(name, isLocal) {
	if (!combinedLibraryMetadatas[name]) {
		combinedLibraryMetadatas[name] = constructCombinedMetadata(
		getStandaloneLibraryMetadata(name), name, PackageType.LIBRARY, isLocal);
	}
	return combinedLibraryMetadatas[name];
}

function getMetadata(name, packageType, isLocal) {
	switch (packageType) {
		case PackageType.APPLICATION:
			return getApplicationMetadata(name, isLocal);
		case PackageType.LIBRARY:
			return getLibraryMetadata(name, isLocal);
		default:
			return {};
	}
}

module.exports = {
	getStandaloneApplicationMetadata: getStandaloneApplicationMetadata,
	getStandaloneLibraryMetadata: getStandaloneLibraryMetadata,
	getApplicationMetadata: getApplicationMetadata,
	getLibraryMetadata: getLibraryMetadata,
	getMetadata, getMetadata
};
