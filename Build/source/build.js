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
const { PackageType, getNonCapitalizedNameOfPackage, getPackageTypeDirectoryName } = require('./package_type');
const { alreadyBuiltLibraries, librariesWithObjectFiles, librariesGoingToBeBuilt, libraryBinaryPath } = require('./libraries');
const { getToolPath } = require('./config');
const { foreachSourceFile } = require('./source_files');
const { forgetFileLastModifiedTimestamp, getFileLastModifiedTimestamp } =
  require('./file_timestamps');
const { getBuildCommand, getLinkerCommand, buildPrefix, buildSettings } =
  require('./build_commands');
const { getPackageDirectory, getPackageBuildDirectory } =
  require('./package_directory');
const { transpilePermebufToCppForPackage } = require('./permebufs');
const { getMetadata, getStandaloneLibraryMetadata } = require('./metadata');
const { makeSureThirdPartyIsLoaded } = require('./third_party');
const { getDisplayFilename } = require('./generated_filename_map');
const { constructIncludeAndDefineParams } = require('./build_parameters');
const { escapePath, forEachIfDefined } = require('./utils');
const { maybeGenerateClangCompleteFilesForProject } = require('./clang_complete');
const { applicationsWithAssets, librariesWithAssets } = require('./assets');
const { STAGE, deferCommand } = require('./deferred_commands');
const { getDependenciesOfFile } = require('./dependencies_per_file');
const { escape } = require('querystring');
const { deferTestsForPackage, maybeQueueAllDeferredTestsForPackage } = require('./test');


async function build(packageType, packageName) {
  if (packageType == PackageType.LIBRARY) {
    if (alreadyBuiltLibraries[packageName] == undefined) {
      alreadyBuiltLibraries[packageName] = forceBuild(packageType, packageName);
    }
    return alreadyBuiltLibraries[packageName];
  }

  return forceBuild(packageType, packageName);
}

async function buildAndMaybeRunTests(packageType, packageName) {
  await build(packageType, packageName);

  // Make sure all dependencies are also built.
  const isLocal = buildSettings.os != 'Perception';
  const dependencies = getMetadata(packageName, packageType, isLocal).dependencies;
  if (dependencies) {
    for (let i = 0; i < dependencies.length; i++) {
      await build(PackageType.LIBRARY, dependencies[i]);
    };
  }

  if (buildSettings.test) {
    await maybeQueueAllDeferredTestsForPackage(packageType, packageName);
  }
}

async function compileAndLink(
  packageType, packageName, packageDirectory, metadata) {
  const filesToIgnore = {};
  forEachIfDefined(metadata.ignore, (fileToIgnore) => {
    filesToIgnore[fileToIgnore] = true;
  });

  // Make sure the path exists where we're going to put our outputs.
  const buildDirectory = getPackageBuildDirectory(packageType, packageName);
  if (!fs.existsSync(buildDirectory))
    fs.mkdirSync(buildDirectory, { recursive: true });

  let anythingChanged = false;
  let params = ' ' +
    constructIncludeAndDefineParams(
      packageDirectory, metadata, 'source', true, false)
      .join(' ');

  let objectFiles = [];
  let testObjectFiles = [];
  let errors = false;

  await foreachSourceFile(
    packageDirectory, buildDirectory, async function (file, buildPath) {
      if (filesToIgnore[file]) {
        return;
      }

      const isTest = file.endsWith('_test.cc') || file.endsWith('_test.c');

      if (isTest && !buildSettings.test) {
        // Skip this file because we're not running tests.
        return;
      }
      const objectFile = buildPath +
            /*sourceFileToObjectFile(packageDirectory, file) + */ '.obj';
      const buildCommand = getBuildCommand(file, packageType, params, objectFile);
      if (buildCommand == '') {
        // Skip this file.
        return;
      }

      let shouldCompileFile = false;
      if (isTest) {
        testObjectFiles.push({
          source: getDisplayFilename(file),
          objectFile: objectFile
        });
      } else {
        objectFiles.push(escapePath(objectFile));
      }

      if (!fs.existsSync(objectFile)) {
        // Compile if the output file doesn't exist.
        shouldCompileFile = true;
      } else {
        const objectFileTimestamp = fs.lstatSync(objectFile).mtimeMs;

        const deps = getDependenciesOfFile(file);
        if (deps == null) {
          // Compile because we don't know the dependencies.
          shouldCompileFile = true;
        } else {
          if (metadata.metadata_updated >= objectFileTimestamp) {
            shouldCompileFile = true;
          } else {
            for (let i = 0; i < deps.length; i++) {
              if (getFileLastModifiedTimestamp(deps[i]) >=
                objectFileTimestamp) {
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
        deferCommand(
          STAGE.COMPILE, buildCommand, 'Compiling ' + getDisplayFilename(file),
          file, metadata.output_warnings);
      }
    });

  return link(
    packageType, packageName, packageDirectory, objectFiles, testObjectFiles, anythingChanged,
    metadata);
}

async function link(
  packageType, packageName, packageDirectory, objectFiles, testObjectFiles, anythingChanged,
  metadata) {
  if (objectFiles.length == 0) {
    // Nothing to link. This might be a header only library, but an error for
    // non-libraries.
    return packageType == PackageType.LIBRARY;
  } else if (packageType == PackageType.LIBRARY || buildSettings.test) {
    // Link library.
    if (packageType == PackageType.LIBRARY) {
      librariesWithObjectFiles[packageName] = true;
    }
    const binaryPath = libraryBinaryPath(packageType, packageName);
    if (anythingChanged || !fs.existsSync(binaryPath)) {
      // Something changed or the file doesn't exist.
      if (fs.existsSync(binaryPath)) {
        fs.unlinkSync(binaryPath);
      }

      const command = getLinkerCommand(
        PackageType.LIBRARY, escapePath(binaryPath), objectFiles.join(' '));
      deferCommand(
        STAGE.LINK_LIBRARY, command, 'Linking ' + getNonCapitalizedNameOfPackage(packageType, packageName),
        metadata.output_warnings);
      if (packageType == PackageType.LIBRARY) {
        librariesGoingToBeBuilt[packageName] = true;
      }
    }

    if (buildSettings.test && testObjectFiles.length > 0) {
      deferTestsForPackage(packageType, packageName, testObjectFiles);
    }
    return true;
  } else if (packageType == PackageType.APPLICATION) {
    if (buildSettings.test) return true;
    // Link application.
    const binaryPath = getPackageBuildDirectory(packageType, packageName) +
      packageName + '.app';
    const librariesToLink = [];

    forEachIfDefined(metadata.libraries, library => {
      if (librariesWithObjectFiles[library]) {
        // There can be header only libraries that didn't build anything.
        librariesToLink.push(libraryBinaryPath(PackageType.LIBRARY, library));
      }
      const libraryMetadata = getStandaloneLibraryMetadata(library);
      if (libraryMetadata.has_assets) {
        librariesWithAssets[library] = true;
      }
    });

    if (metadata.has_assets) {
      applicationsWithAssets[packageName] = true;
    }

    if (!anythingChanged && fs.existsSync(binaryPath)) {
      // We already exist. Check if any libraries are newer that us (even if
      // they didn't recompile.)
      const ourTimestamp = getFileLastModifiedTimestamp(binaryPath);

      for (let i = 0; i < librariesToLink.length; i++) {
        const libraryToLink = librariesToLink[i];
        if (librariesGoingToBeBuilt[libraryToLink] ||
          getFileLastModifiedTimestamp(libraryToLink) > ourTimestamp) {
          anythingChanged = true;
        }
      }
      if (!anythingChanged) {
        // No libraries are newer than us, no need to relink.
        return true;
      }
    }
    if (fs.existsSync(binaryPath)) {
      fs.unlinkSync(binaryPath);
    }
    let linkerInput = objectFiles.join(' ') /* pre-escaped */;
    librariesToLink.forEach(libraryToLink => {
      linkerInput += ' ' + escapePath(libraryToLink);
    });

    let command = getLinkerCommand(
      PackageType.APPLICATION, escapePath(binaryPath), linkerInput);
    deferCommand(
      STAGE.LINK_APPLICATION, command, 'Linking application ' + packageName,
      metadata.output_warnings);
    forgetFileLastModifiedTimestamp(binaryPath);

    compiled = true;

    return true;
  } else if (packageType == PackageType.KERNEL) {
    // Link kernel.
    const binaryPath =
      getPackageBuildDirectory(packageType, packageName) + 'kernel.app';
    if (!anythingChanged && fs.existsSync(binaryPath)) {
      return true;
    }
    if (fs.existsSync(binaryPath)) {
      fs.unlinkSync(binaryPath);
    }

    let command = getLinkerCommand(
      PackageType.KERNEL, escapePath(binaryPath),
      objectFiles.join(' ') /* pre-escaped */);
    deferCommand(
      STAGE.LINK_APPLICATION, command, 'Linking kernel',
      metadata.output_warnings);
    return true;
  } else {
    console.log('Unknown project type.');
    return false;
  }
}

// Builds a package.
async function forceBuild(packageType, packageName) {
  const isLocal = buildSettings.os != 'Perception';
  const metadata = getMetadata(packageName, packageType, isLocal);

  if (isLocal && (metadata.skip_local || (packageType == PackageType.KERNEL && !buildSettings.test))) {
    return true;
  }

  let error = false;

  // If there are any third party libraries, make sure they're loaded.
  forEachIfDefined(metadata.third_party_libraries, third_party_library => {
    error |=
      !makeSureThirdPartyIsLoaded(third_party_library, PackageType.LIBRARY);
  });

  // If we are third party, make sure we're loaded.
  if (metadata.third_party) {
    error |= !makeSureThirdPartyIsLoaded(packageName, packageType);
  }
  if (error) return false;

  // If anything we dependend on have Permebufs, transpile them.
  forEachIfDefined(
    metadata.libraries_with_permebufs, library_with_permebufs => {
      error |= !transpilePermebufToCppForPackage(
        library_with_permebufs, PackageType.LIBRARY);
    });

  // If we have Permebufs, transpile them.
  if (metadata.has_permebufs) {
    error |= !transpilePermebufToCppForPackage(packageName, packageType);
  }
  if (error) return false;

  // We have dependencies, build them before us.
  if (packageType == PackageType.APPLICATION && metadata.libraries) {
    for (let i = 0; i < metadata.libraries.length; i++) {
      const dependency = metadata.libraries[i];
      const success = await build(PackageType.LIBRARY, dependency);
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
    return compileAndLink(packageType, packageName, packageDirectory, metadata);
  } else {
    return true;
  }
};

module.exports = {
  build: build,
  buildAndMaybeRunTests: buildAndMaybeRunTests
};
