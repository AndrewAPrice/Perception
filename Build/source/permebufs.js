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
const {getPackageDirectory, getPackageBuildDirectory} =
    require('./package_directory');
const permebufParser = require('./permebuf_parser');
const {foreachPermebufSourceFile} = require('./source_files');
const permebufGenerator = require('./permebuf_generator');
const {buildPrefix, buildSettings} = require('./build_commands');
const {getMetadata} = require('./metadata');
const {PackageType, getPackageTypeDirectoryName} = require('./package_type');
const {generatedFilenameMap} = require('./generated_filename_map');
const {forgetFileLastModifiedTimestamp, getFileLastModifiedTimestamp} =
    require('./file_timestamps');

const transpiledLibrariesWithPermebufs = {};
const transpiledApplicationsWithPermebufs = {};

function forEachIfDefined(array, onEach) {
  if (array) array.forEach(onEach);
}

function compilePermebufToCpp(
    localPath, packageName, packageType, dependencies) {
  const symbolTable = {};
  const symbolsToGenerate = [];
  const cppIncludeFiles = {};
  const cppIncludeLiteFiles = {};
  const importedFilesMap = {}

  if (!permebufParser.parseFile(
          localPath, packageName, packageType, importedFilesMap, symbolTable,
          symbolsToGenerate, cppIncludeFiles, cppIncludeLiteFiles, true)) {
    return false;
  }

  if (!permebufGenerator.generateCppSources(
          localPath, packageName, packageType, symbolTable, symbolsToGenerate,
          cppIncludeFiles, cppIncludeLiteFiles)) {
    return false;
  }

  // Convert the imported files map into an array.
  Object.keys(importedFilesMap).forEach((importedFile) => {
    dependencies.push(importedFile);
  });

  return true;
}

async function transpilePermebufToCppForPackage(packageName, packageType) {
  if (packageType == PackageType.LIBRARY) {
    if (transpiledLibrariesWithPermebufs[packageName] == undefined) {
      transpiledLibrariesWithPermebufs[packageName] =
          await forceTranspilePermebufToCppForPackage(packageType, packageName);
    }
    return transpiledLibrariesWithPermebufs[packageName];
  } else if (packageType == PackageType.APPLICATION) {
    if (transpiledApplicationsWithPermebufs[packageName] == undefined) {
      transpiledApplicationsWithPermebufs[packageName] =
          await forceTranspilePermebufToCppForPackage(packageType, packageName);
    }
    return transpiledApplicationsWithPermebufs[packageName];
  }
  return false;
}

async function forceTranspilePermebufToCppForPackage(packageType, packageName) {
  const packageDirectory = getPackageDirectory(packageType, packageName);
  const packageBuildDirectory =
      getPackageBuildDirectory(packageType, packageName);
  let anythingChanged = false;

  // Make sure the path exists where we're going to put our outputs.
  if (!fs.existsSync(packageBuildDirectory))
    fs.mkdirSync(packageBuildDirectory, {recursive: true});

  const depsFile = packageBuildDirectory + 'permebuf_dependencies.json';
  let dependenciesPerFile =
      fs.existsSync(depsFile) ? JSON.parse(fs.readFileSync(depsFile)) : {};

  const isLocal = buildSettings.os != 'Perception';
  const metadata = getMetadata(packageName, packageType, isLocal);
  const filesToIgnore = {};
  forEachIfDefined(metadata.ignore, (fileToIgnore) => {
    filesToIgnore[fileToIgnore] = true;
  });

  let errors = false;
  // Construct generated files.
  await foreachPermebufSourceFile(
      packageDirectory, async function(fullPath, localPath) {
        if (filesToIgnore[fullPath]) {
          return;
        }

        if (!localPath.endsWith('.permebuf')) {
          // Not a permebuf file.
          return;
        }

        let shouldCompileFile = false;

        // We just need one of the generated files to tell if the source file is
        // newer or not.
        const outputFile = packageDirectory + 'generated/source/permebuf/' +
            getPackageTypeDirectoryName(packageType) + '/' + packageName + '/' +
            localPath + '.cc';

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
              if (getFileLastModifiedTimestamp(deps[i]) >=
                  outputFileTimestamp) {
                // Compile because one of the dependencies is newer.
                shouldCompileFile = true;
                break;
              }
            }
          }
        }

        if (shouldCompileFile) {
          deps = [];
          if (!compilePermebufToCpp(
                  localPath, packageName, packageType, deps)) {
            errors = true;
          }
          anythingChanged = true;
          dependenciesPerFile[fullPath] = deps;
        }
      });


  if (anythingChanged) {
    fs.writeFileSync(depsFile, JSON.stringify(dependenciesPerFile));
  }

  return !errors;
};

module.exports = {
  transpilePermebufToCppForPackage : transpilePermebufToCppForPackage
};
