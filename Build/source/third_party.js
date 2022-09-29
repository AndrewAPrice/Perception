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
const {getPackageDirectory} = require('./package_directory');
const {PackageType, getPackageTypeDirectoryName} = require('./package_type');
const {getStandaloneApplicationMetadata, getStandaloneLibraryMetadata} = require('./metadata');

const loadedThirdPartyApplications = {};
const loadedThirdPartyLibraries = {};

function loadThirdParty(packageName, packageType, metadata) {
	if (!metadata.third_party) {
		// Not a third party package. This should never trigger.
		console.log('Warning: ' + packageName + ' is not a third party package. Not sure why third_party.js#loadThirdParty is being called.');
		return true;
	}

	const packageDirectory = getPackageDirectory(packageType, packageName);
	if (fs.existsSync(packageDirectory + 'prepare.js') && !fs.existsSync(packageDirectory + 'third_party_files.json')) {
		console.log('Preparing ' + packageName);
		try {
			child_process.execSync('node prepare', {cwd: packageDirectory, stdio: 'inherit'});
		} catch (exp) {
			console.log(exp);
			return false;
		}
	}
	return true;
}

function makeSureThirdPartyIsLoaded(packageName, packageType) {
	if (packageType == PackageType.LIBRARY) {
		if (loadedThirdPartyLibraries[packageName] == undefined) {
			loadedThirdPartyLibraries[packageName] =
				loadThirdParty(packageName, packageType,
					getStandaloneLibraryMetadata(packageName));
		}
		return loadedThirdPartyLibraries[packageName];
	} else if (packageType == PackageType.APPLICATION) {
		if (loadedThirdPartyApplications[packageName] == undefined) {
			loadedThirdPartyApplications[packageName] =
				loadThirdParty(packageName, packageType,
					getStandaloneApplicationMetadata(packageName));
		}
		return loadedThirdPartyApplications[packageName];
	}
	return false;
}

module.exports = {
	makeSureThirdPartyIsLoaded: makeSureThirdPartyIsLoaded
};
