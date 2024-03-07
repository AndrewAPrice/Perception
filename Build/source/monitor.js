// Copyright 2024 Google LLC
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
const { getPackageBinary } = require('./package_directory');
const { PackageType } = require('./package_type');
const { getTempDirectory } = require('./config');

// Monitors the Perception kernel running inside of an emulator.
const kControlCharacter = '\xff';
const kCoreDumpSpecialCommand = "CoreDump";

const kCoreDumpsSubDirectory = '/CoreDumps';
const kCoreDumpFilePrefix = 'Dump';
const kCoreDumpFileSuffix = '.dmp';

// Core dump steps.
const kCoreDumpStep_ReadProcessNameLength = 0;
const kCoreDumpStep_ReadProcessName = 1;
const kCoreDumpStep_ReadFileLength = 2;
const kCoreDumpStep_ReadFile = 3;

// Number of core dumps this run.
let coreDumpsThisRun = 0;

function getNextCoreDumpPath() {
    // Create the directory this program is in.
    const coreDumpDirectory = getTempDirectory() + kCoreDumpsSubDirectory
    if (!fs.existsSync(coreDumpDirectory))
        fs.mkdirSync(coreDumpDirectory, { recursive: true });
    coreDumpsThisRun++;
    const path = coreDumpDirectory + '/' + kCoreDumpFilePrefix + coreDumpsThisRun + kCoreDumpFileSuffix;
    if (fs.existsSync(path)) fs.unlinkSync(path);
    return path;
}

async function monitorProcess(emulatorProcess) {
    return new Promise((resolve, reject) => {
        let handlingSpecialOutput = false;
        let readingCommand = false;
        let buffer = '';
        let readingCoreDump = false;
        let step = 0;
        let remainingBytes = 0;
        let coreDumpProcess = '';
        let coreDumpPath = '';
        let coreDumpStream = null;

        const handleCommand = () => {
            switch (buffer) {
                case kCoreDumpSpecialCommand:
                    process.stdout.write("Reading core dump for ");
                    readingCoreDump = true;
                    step = kCoreDumpStep_ReadProcessNameLength;
                    buffer = '';
                    handlingSpecialOutput = true;
                    break;
                default:
                    console.error(`Unknown special command from Perception: ${buffer}`)
                    handlingSpecialOutput = false;
                    break;
            }
            readingCommand = false;
        };

        const startCoreDumpForProcess = () => {
            if (buffer.length > 0) {
                process.stdout.write(buffer);
                coreDumpProcess = getPackageBinary(PackageType.APPLICATION, 'kernel');
            } else {
                process.stdout.write('kernel');
                coreDumpProcess = getPackageBinary(PackageType.KERNEL, 'kernel');
            }

            coreDumpPath = getNextCoreDumpPath();
            coreDumpStream = fs.createWriteStream(coreDumpPath, {"encoding": "binary"});
            step = kCoreDumpStep_ReadFileLength;
            buffer = '';
        };

        const coreDumpSize = (size) => {
            process.stdout.write(" ( " + size + ' bytes)..');
            console.log();
        };

        const writeByteToCoreDump = (byte) => {
            coreDumpStream.write(byte);
        }

        const endCoreDumpForProcess = () => {
            coreDumpStream.end();
            console.log("Core dump for " + coreDumpProcess + " is at " + coreDumpPath);
            buffer = '';
            readingCoreDump = false;
            step = 0;
            handlingSpecialOutput = 0;
            coreDumpPath = '';
        };

        emulatorProcess.stdout.setEncoding('binary');
        emulatorProcess.stdout.on('data', (data) => {
            if (!handlingSpecialOutput && data.indexOf(kControlCharacter) == -1) {
                // Nothing special is going on, so dump everything as it is.
                process.stdout.write(data);
                return;
            }

            for (let i = 0; i < data.length; i++) {
                const char = data[i];
                if (!handlingSpecialOutput) {
                    if (char == kControlCharacter) {
                        readingCommand = true;
                        buffer = '';
                        handlingSpecialOutput = true;
                    } else {
                        process.stdout.write(char);
                    }
                } else if (readingCommand) {
                    if (char == kControlCharacter) {
                        readingCommand = false;
                        handleCommand();
                    } else {
                        buffer += char;
                    }
                } else if (readingCoreDump) {
                    switch (step) {
                        case kCoreDumpStep_ReadProcessNameLength:
                            if (char == kControlCharacter) {
                                remainingBytes = parseInt(buffer);
                                buffer = '';
                                if (remainingBytes == 0) {
                                    startCoreDumpForProcess();
                                } else {
                                    step = kCoreDumpStep_ReadProcessName;
                                }
                            } else {
                                buffer += char;
                            }
                            break;
                        case kCoreDumpStep_ReadProcessName:
                            buffer += char;
                            remainingBytes--;
                            if (remainingBytes == 0)
                                startCoreDumpForProcess();
                            break;
                        case kCoreDumpStep_ReadFileLength:
                            if (char == kControlCharacter) {
                                remainingBytes = parseInt(buffer);
                                buffer = '';
                                coreDumpSize(remainingBytes);
                                if (remainingBytes == 0) {
                                    endCoreDumpForProcess();
                                } else {
                                    step = kCoreDumpStep_ReadFile;
                                }
                            } else {
                                buffer += char;
                            }
                            break;
                        case kCoreDumpStep_ReadFile:
                            writeByteToCoreDump(char);
                            remainingBytes--;
                            if (remainingBytes == 0)
                                endCoreDumpForProcess();
                            break;
                        default:
                            handlingSpecialOutput = false;
                    }
                }
            }
        });
        emulatorProcess.stderr.on('data', (data) => {
            process.stderr.write(data);
        });

        emulatorProcess.on('error', (error) => {
            console.log(`Emulator returned with error: ${error}`);
        });
        emulatorProcess.on('close', resolve);
        emulatorProcess.on('exit', resolve);
    });
}

module.exports = {
    monitorProcess: monitorProcess
};
