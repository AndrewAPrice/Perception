// Copyright 2023 Google LLC
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
const { alreadyBuiltLibraries, librariesWithObjectFiles, librariesGoingToBeBuilt, libraryBinaryPath } = require('./libraries');
const { PackageType, getNameOfPackage, maybePrefixPackageName } = require('./package_type');
const { getMetadata } = require('./metadata');
const { escapePath, forEachIfDefined } = require('./utils');
const { STAGE, deferCommand } = require('./deferred_commands');
const { getTestLinkerCommand } = require('./build_commands');

const deferredTestsByPackageName = {};

function deferTestsForPackage(packageType, packageName, testObjectFiles) {
    deferredTestsByPackageName[maybePrefixPackageName(packageType, packageName)] = {
        packageType: packageType,
        packageName: packageName,
        testObjectFiles: testObjectFiles
    };
}

async function queueDeferredTests(deferredTests) {
    const packageType = deferredTests.packageType;
    const packageName = deferredTests.packageName;


    const librariesToLink = [];
    const metadata = getMetadata(packageType, packageName);
    forEachIfDefined(metadata.libraries, library => {
        if (librariesWithObjectFiles[library]) {
            librariesToLink.push(libraryBinaryPath(PackageType.LIBRARY, library));
        }
    });

    let libraryFiles = libraryBinaryPath(packageType, packageName);
    librariesToLink.forEach(libraryToLink => {
        libraryFiles += ' ' + escapePath(libraryToLink);
    });

    deferredTests.testObjectFiles.forEach(testObjectFile => {
        const binaryPath = testObjectFile.objectFile + '.test';
        if (fs.existsSync(binaryPath)) {
            fs.unlinkSync(binaryPath);
        }

        let command = getTestLinkerCommand(
            escapePath(binaryPath), escapePath(testObjectFile.objectFile), libraryFiles);

        deferCommand(
            STAGE.LINK_APPLICATION, command, 'Linking test ' + testObjectFile.source,
            metadata.output_warnings);

        deferCommand(
            STAGE.RUN_TEST, escapePath(binaryPath), 'Running test ' + testObjectFile.source,
            metadata.output_warnings);
    });
}

async function maybeQueueAllDeferredTests() {
    const packagesWithDeferredTests = Object.keys(deferredTestsByPackageName);
    for (let i = 0; i < packagesWithDeferredTests.length; i++) {
        await queueDeferredTests(deferredTestsByPackageName[packagesWithDeferredTests[i]]);
    }
}

async function maybeQueueAllDeferredTestsForPackage(packageType, packageName) {
    const deferredTests = deferredTestsByPackageName[maybePrefixPackageName(packageType, packageName)];
    if (!deferredTests) {
        console.log(getNameOfPackage(packageType, packageName) + ' has no tests.');
        return;
    }

    await queueDeferredTests(deferredTests);
}


module.exports = {
    deferTestsForPackage: deferTestsForPackage,
    maybeQueueAllDeferredTests: maybeQueueAllDeferredTests,
    maybeQueueAllDeferredTestsForPackage: maybeQueueAllDeferredTestsForPackage
};
