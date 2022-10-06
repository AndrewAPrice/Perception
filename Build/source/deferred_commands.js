
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

const util = require('node:util');
const exec = util.promisify(require('node:child_process').exec);
const readFile = util.promisify(require('node:fs').readFile);
const {buildSettings} = require('./build_commands');
const {setDependenciesOfFile, flushDependenciesForFile} =
    require('./dependencies_per_file');
const {rootDirectory} = require('./root_directory');
const {escapePath} = require('./utils');
const {stdout} = require('node:process');

const STAGE = {
  COMPILE: 0,
  LINK_LIBRARY: 1,
  LINK_APPLICATION: 2
};

const ERASE_LINE = '\033[2K\r';

const deferredCommands = {};

function deferCommand(stage, command, title, sourceFile) {
  if (deferredCommands[stage] == undefined) {
    deferredCommands[stage] = [];
  }

  deferredCommands[stage].push(
      {command: command, title: title, sourceFile: sourceFile});
}

async function processAndUpdatePerFileDeps(sourceFile, dependenciesFile) {
  if (dependenciesFile) {
    let depsStr = (await readFile(dependenciesFile)).toString('utf8');
    let separator = depsStr.indexOf(':');
    if (separator == -1) {
      throw 'Error parsing dependencies: ' + dependenciesFile;
    }
    let newDeps = [];
    depsStr.substring(separator + 1).split('\\\n').forEach(s1 => {
      // Pipe is an invalid path character, so we'll temporarily replace
      // '\ ' (a space in the path) into '|' before splitting the files
      // that are seperated by spaces.
      s1.replace(/\\ /g, '|').split(' ').forEach(s2 => {
        s2 = s2.replace(/\|/g, ' ').trim();
        if (s2.length > 1) {
          newDeps.push(s2);
        }
      });
    });
    setDependenciesOfFile(sourceFile, newDeps);
  } else {
    // The file doesn't have any dependencies other than itself.
    setDependenciesOfFile(sourceFile, [sourceFile]);
  }
}

async function runCommand(
    deferredCommand, actionBeingPerformed, outputDepsFile,
    escapedOutputDepsFile, errors, silent) {
  let command = deferredCommand.command;
  let updatePerFileDeps = false;
  let useDepsFile = false;

  if (command.indexOf('${deps}') > 0) {
    command = command.split('${deps}').join(escapedOutputDepsFile);
    useDepsFile = true;
  }

  if (deferredCommand.sourceFile) {
    updatePerFileDeps = true;
  }

  try {
    await exec(command);
  } catch (exp) {
    if (!silent) {
      errors.push({
        command: deferredCommand.title,
        error: exp.stderr});
      process.stdout.write(' ');
    }
  }

  if (updatePerFileDeps) {
    await processAndUpdatePerFileDeps(
        deferredCommand.sourceFile, useDepsFile ? outputDepsFile : null);
  }
}

async function runCommandsUntilDone(
    commands, actionBeingPerformed, id, errors, totalSize, silent) {
  const outputDepsFile = rootDirectory + 'Build/temp/deps' + id + '.d';
  const escapedOutputDepsFile = escapePath(outputDepsFile);

  while (commands.length > 0) {
    const deferredCommand = commands.pop();
    if (!silent) {
      process.stdout.write(
          ERASE_LINE + actionBeingPerformed + (totalSize - commands.length) +
          '/' + totalSize);
    }
    await runCommand(
        deferredCommand, actionBeingPerformed, outputDepsFile,
        escapedOutputDepsFile, errors, silent);
  }
}

async function runCommandsForStage(stage, silent) {
  const commands = deferredCommands[stage];
  if (!commands) return true;  // No commands for this stage.

  let actionBeingPerformed = '';
  switch (stage) {
    case STAGE.COMPILE:
      actionBeingPerformed = 'Compiling ';
      break;
    case STAGE.LINK_LIBRARY:
      actionBeingPerformed = 'Linking libraries ';
      break;
    case STAGE.LINK_APPLICATION:
      actionBeingPerformed = 'Linking applications ';
      break;
  }
  if (!silent) process.stdout.write(' ');

  const errors = [];
  const tasks = [];
  const totalCommands = commands.length;
  for (let i = 0; i < buildSettings.parallelTasks; i++) {
    tasks.push(runCommandsUntilDone(
        commands, actionBeingPerformed, i, errors, totalCommands, silent));
  }

  await Promise.all(tasks);

  if (!silent) console.log(' ');

  if (errors.length > 0) {
    if (!silent) {
      errors.forEach(error => {
        console.log('');
        console.log(error.command);
        console.log(error.error);
      });
    }
    return false;
  }

  return true;
}

async function runDeferredCommandsInternal(silent) {
  if (!(await runCommandsForStage(STAGE.COMPILE, silent))) {
    return false;
  }

  if (!(await runCommandsForStage(STAGE.LINK_LIBRARY, silent))) {
    return false;
  }

  if (!(await runCommandsForStage(STAGE.LINK_APPLICATION, silent))) {
    return false;
  }
  return true;
}

async function runDeferredCommands(silent) {
  const result = await runDeferredCommandsInternal(silent);
  flushDependenciesForFile();
  return result;
}

module.exports = {
  STAGE : STAGE,
  deferCommand : deferCommand,
  runDeferredCommands : runDeferredCommands
};