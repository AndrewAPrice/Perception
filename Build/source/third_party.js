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
const path = require('path');
const child_process = require('child_process');
const { getPackageDirectory } = require('./package_directory');
const { PackageType } = require('./package_type');
const { getStandaloneApplicationMetadata, getStandaloneLibraryMetadata } =
  require('./metadata');
const { getFileLastModifiedTimestamp } = require('./file_timestamps');
const { createDirectoryIfItDoesntExist, removeDirectoryIfEmpty } = require('./utils');

const loadedThirdPartyApplications = {};
const loadedThirdPartyLibraries = {};

const repositoriesDirectory = "temp/repositories/";

const repositoriesMapPath = repositoriesDirectory + "repositories.json";
let repositoriesToIds = null;
let nextRepositoryId = 0;
let repositoryMapNeedsFlushing = false;


function makeSureRepositoriesMapIsLoaded() {
  if (repositoriesToIds != null)
    return true;  // Already loaded.

  if (fs.existsSync(repositoriesMapPath)) {
    // Load the repositories map.
    try {
      const repoMetadata = JSON.parse(fs.readFileSync(repositoriesMapPath));
      repositoriesToIds = repoMetadata.repositoriesToIds;
      nextRepositoryId = repoMetadata.nextRepositoryId;
      return true;
    } catch (exp) {
      console.log('Error reading ' + repositoriesMapPath + ': ' + exp);
      return false;
    }
  }

  // Since there's no repository map, make sure the directory exists.
  if (!fs.existsSync(repositoriesDirectory))
    fs.mkdirSync(repositoriesDirectory, { recursive: true });

  // Create an empty repository map.
  repositoriesToIds = {};
  nextRepositoryId = true;
  return true;
}

function maybeFlushRepositoriesMap() {
  if (!repositoryMapNeedsFlushing)
    return;  // Nothing to do.


  fs.writeFileSync(
    repositoriesMapPath,
    JSON.stringify({
      "repositoriesToIds": repositoriesToIds,
      "nextRepositoryId": nextRepositoryId
    }));

  repositoryMapNeedsFlushing = false;
}

function getRepositoryDirectory(key) {
  let repoNumber = repositoriesToIds[key];
  let newRepository = false;
  if (repoNumber == undefined) {
    repoNumber = nextRepositoryId++;
    repositoriesToIds[key] = repoNumber;
    repositoryMapNeedsFlushing = true;
    newRepository = true;
  }

  const directory = repositoriesDirectory + repoNumber;

  if (newRepository && fs.existsSync(directory)) {
    fs.rmSync(directory, { recursive: true });
  }

  return directory;
}

function loadRepository(repositoryMetadata, placeholderInfo) {
  const repositoryKey = repositoryMetadata.type + '#' + repositoryMetadata.url;
  let repositoryDirectory = null;

  switch (repositoryMetadata.type) {
    case 'git': {
      repositoryDirectory = getRepositoryDirectory(repositoryKey);

      let verbFirstUpper = null;
      let verb = null;
      let command = null;
      const execParams = { stdio: 'inherit' };
      if (fs.existsSync(repositoryDirectory)) {
        verbFirstUpper = 'Updating ';
        verb = 'updating ';
        command = 'git pull ' + repositoryMetadata.url;
        execParams.cwd = repositoryDirectory;
      } else {
        verbFirstUpper = 'Cloning ';
        verb = 'cloning ';
        command = 'git clone ' + repositoryMetadata.url + ' ' + repositoryDirectory;
      }

      console.log(verbFirstUpper + repositoryMetadata.url);
      try {
        child_process.execSync(command, execParams);
      } catch (exp) {
        console.log('Error ' + verb + repositoryMetadata.url + ': ' + exp);
        return false;
      }
      break;
    }
    case 'zip': {
      // Download zip.
      const baseDir = getRepositoryDirectory(repositoryKey);

      if (!fs.existsSync(baseDir))
        fs.mkdirSync(baseDir, { recursive: true });

      const zipPath = baseDir + '/download.zip';

      if (!fs.existsSync(zipPath)) {
        console.log('Downloading ' + repositoryMetadata.url);

        const command = 'curl -L ' + repositoryMetadata.url + ' --output ' + zipPath;
        try {
          child_process.execSync(command, { stdio: 'inherit' });
        } catch (exp) {
          console.log('Error downloading ' + repositoryMetadata.url + ': ' + exp);
          return false;
        }
      }

      // Extract zip.
      repositoryDirectory = baseDir + '/extracted';
      if (!fs.existsSync(repositoryDirectory)) {
        const command = 'unzip ' + zipPath + ' -d ' + repositoryDirectory;
        try {
          child_process.execSync(command, { stdio: 'inherit' });
        } catch (exp) {
          console.log('Error extracting ' + repositoryMetadata.url + exp);
          return false;
        }
      }
      break;
    }
    default:
      console.log('Unknown repository type: ' + JSON.stringify(repositoryMetadata));
      return false;
  }


  placeholderInfo.placeholders['${' + repositoryMetadata.placeholder + '}'] = repositoryDirectory;
  placeholderInfo.regex = null;
  return true;
}

function loadRepositories(repositoriesMetadata, placeholderInfo) {
  if (!repositoriesMetadata) return true;

  if (!makeSureRepositoriesMapIsLoaded()) return false;

  let success = true;
  repositoriesMetadata.forEach(repositoryMetadata => {
    success &= loadRepository(repositoryMetadata, placeholderInfo);
  });

  maybeFlushRepositoriesMap();

  return success;
}

function splitArray(str, arraysToSplit, arraysToSplitIndex, placeholderInfo) {
  const results = [];
  const arrayToSplit = arraysToSplit[arraysToSplitIndex];
  arraysToSplitIndex++;
  placeholderInfo.placeholders[arrayToSplit].forEach((arrayValue) => {
    const permutation = str.replaceAll(arrayToSplit, arrayValue);

    if (arrayToSplit.length = arraysToSplitIndex) {
      results.push(permutation);
    } else {
      splitArray(permuation, arraysToSplit, arraysToSplitIndex, placeholderInfo).forEach(results.push);
    }
  });
  return results;
}
const REGEXP_SPECIAL_CHAR =
  /[\!\#\$\%\^\&\*\)\(\+\=\.\<\>\{\}\[\]\:\;\'\"\|\~\`\_\-]/g;

function substitutePlaceholders(str, placeholderInfo, allowArrays) {
  if (!placeholderInfo.regex) {
    const placeholderKeys = [];
    Object.keys(placeholderInfo.placeholders).forEach(key => {
      placeholderKeys.push(key.replace(REGEXP_SPECIAL_CHAR, '\\$&'));
    });
    const regex = placeholderKeys.join("|");
    placeholderInfo.regex = new RegExp(regex, "gi");
  }

  const arraysToSplit = [];

  str = str.replaceAll(placeholderInfo.regex, (matched) => {
    const val = placeholderInfo.placeholders[matched];
    if (Array.isArray(val)) {
      if (allowArrays) {
        arraysToSplit.push(matched);
      } else {
        console.log(matched + ' is an array where an array isn\'t supported, in: ' + str);
      }
      return matched;
    } else {
      return val;
    }
  });

  if (arraysToSplit.length > 0) {
    return splitArray(str, arraysToSplit, 0, placeholderInfo);
  } else {
    return str;
  }
}

function evaluatePath(path, placeholderInfo, allowArrays) {
  if (!path.startsWith('$')) {
    path = placeholderInfo.placeholders['${@}'] + '/' + path;
  }

  return substitutePlaceholders(path, placeholderInfo, allowArrays);
}


function convertExtensionArrayToMap(operationMetadata) {
  if (!operationMetadata.extensions ||
    operationMetadata.extensions.length == 0) {
    return null;
  }

  const extensionsMap = {};
  operationMetadata.extensions.forEach(extension => {
    extensionsMap[extension] = true;
  });
  return extensionsMap;
}

function createExcludeMap(operationMetadata, placeholderInfo) {
  const excludeMap = {};
  if (!operationMetadata.exclude) return excludeMap;

  operationMetadata.exclude.forEach(excludePath => {
    const rawPaths = evaluatePath(excludePath, placeholderInfo, /*allowArrays=*/true);
    if (Array.isArray(rawPaths)) {
      rawPaths.forEach(rawPath => {
        excludeMap[rawPath] = true;
      });
    } else {
      excludeMap[rawPaths] = true;
    }
  });

  return excludeMap;
}

function createReplaceArray(replace, placeholderInfo) {
  if (!replace || !replace.replacements) return null;

  const replacements = [];
  replace.replacements.forEach((replacement) => {
    if (replacement.length != 2) {
      console.log("Ignoring invalid replacement for " + replace.file + ": " + replacement);
      return;
    }
    const needle = substitutePlaceholders(replacement[0], placeholderInfo, /*allowArrays=*/true);
    const replaceWith = substitutePlaceholders(replacement[1], placeholderInfo);
    if (Array.isArray(needle)) {
      needle.forEach(singleNeedle => {
        replacements.push([singleNeedle, replaceWith]);
      });
    } else {
      replacements.push([needle, replaceWith]);
    }
  })
  return replacements;
}

function createReplaceMap(operationMetadata, placeholderInfo) {
  const replaceMap = {};
  if (!operationMetadata.replace) return replaceMap;

  operationMetadata.replace.forEach((replace) => {
    const rawPaths = evaluatePath(replace.file, placeholderInfo, /*allowArrays=*/true);
    const replaceArray = createReplaceArray(replace, placeholderInfo);
    if (Array.isArray(rawPaths)) {
      rawPaths.forEach(rawPath => {
        replaceMap[rawPaths] =
          replaceArray;
      });
    }
    else {
      replaceMap[rawPaths] =
        replaceArray;
    }
  });
  return replaceMap;
}

function copyFile(from, to, replaceMap, thirdPartyFiles) {
  createDirectoryIfItDoesntExist(path.dirname(to));
  if (replaceMap[to]) {
    let contents = null;
    try {
      contents = fs.readFileSync(from, 'utf8');
    } catch (exp) {
      console.log('Error reading ' + from);
      return false;
    }
    replaceMap[to].forEach((replacement) => {
      contents = contents.replaceAll(replacement[0], replacement[1]);
    });

    try {
      fs.writeFileSync(to, contents, 'utf8');
    } catch (exp) {
      console.log('Error writing ' + to);
      return false;
    }

  } else {
    try {
      fs.copyFileSync(from, to);
    } catch (exp) {
      console.log('Error copying ' + from + ' to ' + to);
      return false;
    }
  }

  console.log("Copying " + to);

  thirdPartyFiles[to] = true;
  return true;
}

function copyFilesInDirectory(from, to, thirdPartyFiles, extensions, recursive, excludeMap, replaceMap) {
  let success = true;
  fs.readdirSync(from).forEach((fileName) => {
    const toPath = to + '/' + fileName;
    if (excludeMap[toPath]) return;
    const fromPath = from + '/' + fileName;
    if (extensions && !extensions[path.extname(fileName)])
      return;

    const fileStats = fs.lstatSync(fromPath);
    if (fileStats.isDirectory()) {
      if (recursive) {
        success &= copyFilesInDirectory(fromPath, toPath, thirdPartyFiles, extensions, recursive, excludeMap, replaceMap);
      }

    } else {
      success &= copyFile(fromPath, toPath, replaceMap, thirdPartyFiles);
    }
  });
  return success;
}

function executeCopy(operationMetadata, placeholderInfo, thirdPartyFiles) {
  const from = evaluatePath(operationMetadata.source, placeholderInfo);
  const to = evaluatePath(operationMetadata.destination, placeholderInfo);
  const replaceMap = createReplaceMap(operationMetadata, placeholderInfo);

  if (!fs.existsSync(from)) {
    console.log(operationMetadata.source + ' doesn\'t exist, full path: ' + from);
    return false;
  }

  const fileStats = fs.lstatSync(from);
  if (fileStats.isDirectory()) {
    const extensions = convertExtensionArrayToMap(operationMetadata);
    const recursive = operationMetadata.recursive;
    const excludeMap = createExcludeMap(operationMetadata, placeholderInfo);
    return copyFilesInDirectory(from, to, thirdPartyFiles, extensions, recursive, excludeMap, replaceMap);
  } else {
    // Copying a single file.
    return copyFile(from, to, replaceMap, thirdPartyFiles);
  }
}

function executeCreateDirectory(operationMetadata, placeholderInfo, thirdPartyFiles) {
  let paths = evaluatePath(operationMetadata.path, placeholderInfo, /*allowArrays=*/true);
  if (!Array.isArray(paths)) paths = [paths];

  paths.forEach(path => {
    if (!fs.existsSync(path))
      fs.mkdirSync(path, { recursive: true });
  });
  return true;
}

function evaluate(expression) {
  if (Array.isArray(expression)) {
    const results = [];
    expression.forEach(singleExpression => {
      results.push(eval(singleExpression));
    });
    return results;
  } else {
    return eval(expression);
  }
}

function executeEvaluate(operationMetadata, placeholderInfo, thirdPartyFiles) {
  const valueKeys = Object.keys(operationMetadata.values).forEach(key => {
    placeholderInfo.placeholders["${" + key + "}"] =
      evaluate(substitutePlaceholders(operationMetadata.values[key], placeholderInfo, /*splitArrays=*/true));
  });
  placeholderInfo.regex = null;

  return true;
}

function executeExecute(operationMetadata, placeholderInfo, thirdPartyFiles) {
  // Run the command if the newest input is newer than the oldest output, or if we're missing
  // an output.
  let newestInput = Number.NEGATIVE_INFINITY;
  let oldestOutput = Number.POSITIVE_INFINITY;
  let missingOutput = false;

  for (let inputIndex = 0; operationMetadata.inputs && inputIndex < operationMetadata.inputs.length; inputIndex++) {
    const inputPath = operationMetadata.inputs[inputIndex];
    let rawPaths = evaluatePath(inputPath, placeholderInfo, /*allowArrays=*/true);
    if (!Array.isArray(rawPaths)) rawPaths = [rawPaths];
    for (let rawPathIndex = 0; rawPathIndex < rawPaths.length; rawPaths++) {
      const rawPath = rawPaths[rawPathIndex];
      if (!fs.existsSync(rawPath)) {
        console.log(inputPath + ' doesn\'t exist. Full path ' + rawPath);
        return false;
      }
      newestInput = Math.max(getFileLastModifiedTimestamp(rawPath), newestInput);
    }
  }

  const rawOutputPaths = [];

  for (let outputIndex = 0; operationMetadata.outputs && outputIndex < operationMetadata.outputs.length; outputIndex++) {
    const outputPath = operationMetadata.outputs[outputIndex];
    let rawPaths = evaluatePath(outputPath, placeholderInfo, /*allowArrays=*/true);
    if (!Array.isArray(rawPaths)) rawPaths = [rawPaths];
    for (let rawPathIndex = 0; rawPathIndex < rawPaths.length; rawPaths++) {
      const rawPath = rawPaths[rawPathIndex];
      rawOutputPaths.push(rawPath);
      thirdPartyFiles[rawPath] = true;
      if (fs.existsSync(rawPath)) {
        oldestOutput = Math.min(getFileLastModifiedTimestamp(rawPath), oldestOutput);
      } else {
        missingOutput = true;
        createDirectoryIfItDoesntExist(path.dirname(rawPath));
      }
    }
  }

  if (!missingOutput && newestInput < oldestOutput && !operationMetadata.alwaysRun) {
    return true;  // Nothing to do.
  }

  // Remove any old outputs.
  rawOutputPaths.forEach(path => {
    console.log("Creating " + path);
    if (fs.existsSync(path))
      fs.unlinkSync(path);
  });

  const command = substitutePlaceholders(operationMetadata.command, placeholderInfo);
  const execParams = { stdio: 'inherit' };

  if (operationMetadata.directory) {
    execParams.cwd = substitutePlaceholders(operationMetadata.directory, placeholderInfo);
  }

  try {
    child_process.execSync(command, execParams);
  } catch (exp) {
    console.log('Error executing: ' + command);
    console.log('Returned error: ' + exp);
    return false;
  }

  return true;
}

function executeJoinArray(operationMetadata, placeholderInfo, thirdPartyFiles) {
  const replacements = createReplaceArray(operationMetadata, placeholderInfo);
  let values = substitutePlaceholders(operationMetadata.value, placeholderInfo, /*allowArrays=*/true);
  if (!Array.isArray(values)) values = [values];
  const finalValues = [];

  if (replacements) {
    values.forEach(value => {
      replacements.forEach((replacement) => {
        value = value.replaceAll(replacement[0], replacement[1]);
      });
      finalValues.push(value);
    });
  } else {
    finalValues = values;
  }

  const joinValue = substitutePlaceholders(operationMetadata.joint, placeholderInfo);


  placeholderInfo.placeholders['${' + operationMetadata.placeholder + '}'] = finalValues.join(joinValue);
  placeholderInfo.regex = null;

  return true;
}

function executeReadFilesInDirectory(operationMetadata, placeholderInfo, thirdPartyFiles) {
  let paths = substitutePlaceholders(operationMetadata.path, placeholderInfo, /*allowArrays=*/true);
  if (!Array.isArray(paths)) {
    paths = [paths];
  }

  const filesFound = [];
  const extensions = convertExtensionArrayToMap(operationMetadata);
  const replacements = createReplaceArray(operationMetadata, placeholderInfo);

  for (let pathIndex = 0; pathIndex < paths.length; pathIndex++) {
    const dir = paths[pathIndex];
    if (!fs.existsSync(dir)) {
      console.log('Directory does not exist: ' + operationMetadata.path + ' full path: ' + dir);
      return false;
    }

    const fileStats = fs.lstatSync(dir);
    if (!fileStats.isDirectory()) {
      console.log('Not a directory: ' + operationMetadata.path);
      return false;
    }

    const fullPath = operationMetadata.fullPath ? true : false;

    const filesInDirectory = fs.readdirSync(dir);
    for (let i = 0; i < filesInDirectory.length; i++) {
      const entryName = filesInDirectory[i];
      if (extensions && !extensions[path.extname(entryName)])
        continue;
      let outputPath = (fullPath ? dir + '/' + entryName : entryName);
      if (replacements) {
        replacements.forEach((replacement) => {
          outputPath = outputPath.replaceAll(replacement[0], replacement[1]);
        })
      }
      filesFound.push(outputPath);
    }
  }

  placeholderInfo.placeholders['${' + operationMetadata.placeholder + '}'] = filesFound;
  placeholderInfo.regex = null;
  return true;
}

function executeReadRegExFromFile(operationMetadata, placeholderInfo, thirdPartyFiles) {
  const path = evaluatePath(operationMetadata.file, placeholderInfo);
  if (!fs.existsSync(path)) {
    console.log(operationMetadata.file + ' does not exist. Full path: ' + path);
    return false;
  }

  let fileContents = '';
  try {
    fileContents = fs.readFileSync(path);
  } catch (exp) {
    console.log('Error reading ' + operationMetadata.file + ': ' + exp);
    return false;
  }

  const valueKeys = Object.keys(operationMetadata.values);

  for (let valueKeysIndex = 0; valueKeysIndex < valueKeys.length; valueKeysIndex++) {
    const keys = valueKeys[valueKeysIndex].split(',');
    const regExp = new RegExp(operationMetadata.values[valueKeys[valueKeysIndex]]);
    const values = regExp.exec(fileContents);

    for (let index = 0; index < keys.length &&
      index < values.length; index++) {
      if (keys[index] == '') continue;  // Don't care about this value.
      placeholderInfo.placeholders['${' + keys[index] + '}'] = values[index];
    }
  }

  placeholderInfo.regex = null;

  return true;
}

const operationLookupTable = {
  'copy': executeCopy,
  'createDirectory': executeCreateDirectory,
  'evaluate': executeEvaluate,
  'execute': executeExecute,
  'joinArray': executeJoinArray,
  'readFilesInDirectory': executeReadFilesInDirectory,
  'readRegExFromFile': executeReadRegExFromFile
};

function executeOperation(operationMetadata, placeholderInfo, thirdPartyFiles) {
  const method = operationLookupTable[operationMetadata.operation];
  if (!method) {
    console.log('Unknown operation: ' + operationMetadata);
    return false;
  }

  return method(operationMetadata, placeholderInfo, thirdPartyFiles);
}

function flushThirdPartyFiles(thirdPartyFiles, thirdPartyFilesPath, hadErrors) {
  let thirdPartyFilesFromLastRun = {}
  if (fs.existsSync(thirdPartyFilesPath)) {
    try {
      thirdPartyFilesFromLastRun = JSON.parse(fs.readFileSync(thirdPartyFilesPath, 'utf8'));
    } catch (exp) {
      console.log('Error reading ' + thirdPartyFilesPath + ': ' + exp);
      return false;
    }
  }

  if (hadErrors) {
    // Remove everything generated from this run and last run.

    Object.keys(thirdPartyFilesFromLastRun).forEach(filePath => {
      if (fs.existsSync(filePath)) {
        fs.unlinkSync(filePath);
        removeDirectoryIfEmpty(path.dirname(filePath));
      }
    });
    Object.keys(thirdPartyFiles).forEach(filePath => {
      if (fs.existsSync(filePath)) {
        fs.unlinkSync(filePath);
        removeDirectoryIfEmpty(path.dirname(filePath));
      }
    });

    // Remove the third party file so this package has to be prepared again on the next run.
    if (fs.existsSync(thirdPartyFilesPath))
      fs.unlinkSync(thirdPartyFilesPath);
  } else {
    // Remove files that were created during the last run but are no longer needed.

    // Remove files created this run from the set of files created during the last run. The remaining files are
    // ones that are no longer needed.
    Object.keys(thirdPartyFiles).forEach(filePath => {
      if (thirdPartyFilesFromLastRun[filePath])
        delete thirdPartyFilesFromLastRun[filePath];
    });

    // Delete the no longer needed files.
    Object.keys(thirdPartyFilesFromLastRun).forEach(filePath => {
      if (fs.existsSync(filePath)) {
        console.log('Removing ' + filePath);
        fs.unlinkSync(filePath);
        removeDirectoryIfEmpty(path.dirname(filePath));
      }
    });

    // Save the files that were generated this run.
    try {
      fs.writeFileSync(thirdPartyFilesPath, JSON.stringify(thirdPartyFiles, 'utf8'));
    } catch (exp) {
      console.log('Error writing ' + thirdPartyFilesPath + ': ' + exp);
      return false;
    }
  }
  return true;
}

function prepareThirdPartyPackage(metadata, packageDirectory, thirdPartyFilesPath) {
  const placeholderInfo = {
    regex: null,
    placeholders: { "${@}": packageDirectory.substr(0, packageDirectory.length - 1) }
  };
  if (!loadRepositories(metadata.repositories, placeholderInfo))
    return false;

  const thirdPartyFiles = {};

  // Run each operation.
  for (let operationIndex = 0; operationIndex < metadata.operations.length; operationIndex++) {
    let success = false;
    // try {
    success = executeOperation(metadata.operations[operationIndex], placeholderInfo, thirdPartyFiles);
    /*  } catch (err) {
        console.log('Error executing operation:');
        console.log(JSON.stringify(metadata.operations[operationIndex]));
        console.log('Error: ' + err);
        success = false;
      }*/

    if (!success) {
      flushThirdPartyFiles(thirdPartyFiles, thirdPartyFilesPath, /*hadErrors=*/true);
      return false;
    }
  }

  return flushThirdPartyFiles(thirdPartyFiles, thirdPartyFilesPath, /*hadErrors=*/false);
}

function loadThirdParty(packageName, packageType, metadata) {
  if (!metadata.third_party) {
    // Not a third party package. This should never trigger.
    console.log(
      'Warning: ' + packageName +
      ' is not a third party package. Not sure why third_party.js#loadThirdParty is being called.');
    return true;
  }

  const packageDirectory = getPackageDirectory(packageType, packageName);
  const metadataPath = packageDirectory + 'third_party.json';

  if (!fs.existsSync(metadataPath)) {
    throw Error(
      'No third_party.json file exists for ' +
      (packageType == PackageType.APPLICATION ? 'application' : 'library') +
      ' "' + name + '". Expected the file to be at: ' + metadataPath);
  }
  const thirdPartyFilesPath = packageDirectory + 'third_party_files.json';
  if (!fs.existsSync(thirdPartyFilesPath) ||
    getFileLastModifiedTimestamp(metadataPath) > getFileLastModifiedTimestamp(thirdPartyFilesPath)) {
    let metadata = {};
    try {
      metadata = JSON.parse(fs.readFileSync(metadataPath));
    } catch (exp) {
      console.log('Error reading ' + metadataPath + ': ' + exp);
      return false;
    }

    console.log('Preparing ' + packageName);
    return prepareThirdPartyPackage(metadata, packageDirectory, thirdPartyFilesPath);
  }
  return true;
}

function makeSureThirdPartyIsLoaded(packageName, packageType) {
  if (packageType == PackageType.LIBRARY) {
    if (loadedThirdPartyLibraries[packageName] == undefined) {
      loadedThirdPartyLibraries[packageName] = loadThirdParty(
        packageName, packageType, getStandaloneLibraryMetadata(packageName));
    }
    return loadedThirdPartyLibraries[packageName];
  } else if (packageType == PackageType.APPLICATION) {
    if (loadedThirdPartyApplications[packageName] == undefined) {
      loadedThirdPartyApplications[packageName] = loadThirdParty(
        packageName, packageType,
        getStandaloneApplicationMetadata(packageName));
    }
    return loadedThirdPartyApplications[packageName];
  }
  return false;
}

module.exports = {
  makeSureThirdPartyIsLoaded: makeSureThirdPartyIsLoaded
};
