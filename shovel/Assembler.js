var fs = require('fs');

// check arguments
var invalidParams = false;
var inputFile = null;
var outputFile = null;
var debug = false;
var verbose = false;

if(process.argv.length < 4)
	invalidParams = true;
else {
	for(var i = 2; i < process.argv.length - 2; i++) {
		switch(process.argv[i]) {
			case "-d": debug = true; break;
			case "-v": verbose = true; break;
			default: console.log("Unknown parameter " + process.argv[i] + "."); invalidParams = true; break;
		}
	}

	if(!invalidParams) {
		// last two parameters are input and output files
		inputFile = process.argv[process.argv.length - 2];
		outputFile = process.argv[process.argv.length - 1];
	}
}

if(invalidParams) {
	console.log("Usage is: node Assembler.js -d -v input output");
	console.log(" -d Include debugging information. This will grow the file!");
	console.log(" -v Be verbose about each step!");
	console.log(" input - Path to the input assembly source (must be 2nd to last parameter).");
	console.log(" output - Path to where to output the binary data (must be last parameter).");
	process.exit(-1);
}


// read the file in
var file = fs.readFileSync(inputFile, {encoding: 'UTF8'});

// split it up into lines
var lines = file.split('\n');

// list of functions
var functions = [];
var functionNames = {};

var errorMessage = function(line, message) {
	console.log("Error - line " + (line + 1) + ": " + message);
	process.exit(-1);
}

var globalStringTable = {}; // table of strings - key: string, value: {index, start, len}
var globalStrings = []; // array of strings
var globalCurrentStringTableLength = 0; // current string table length in bytes

var addString = function(str) {
	// look if this string is already in the string table
	var entry = globalStringTable[str];
	if(entry !== undefined)
		return entry.index;

	// add this string to the string table
	var len = str.length;
	var index = globalStrings.length;
	globalStringTable[str] = {
		index: index,
		start: globalCurrentStringTableLength,
		len: len
	};

	globalStrings.push(str);

	globalCurrentStringTableLength += len;

	return index;
};

var lineColumns; // an array of columns containing which column each line starts on, used for debugging

// find functions within file
var findFunctions = function() {
	var functionStart = -1;

	var column = 0;
	var name = "";

	if(debug) lineColumns = [];

	for(var i = 0; i < lines.length; i++) {
		if(debug) { // for debugging, save what column this line starts on
			lineColumns.push(column);
			column += lines[i].length + 1; // add 1 for the newline we split on
		}

		var line = lines[i].trim().toLowerCase();

		// start of a function - we could check against "function " to make sure it is a function and not a potential instruction that
		// starts with "function", that that would require replacing \t with spaces - on every line, so we may aswell do this check first
		if(line.indexOf("function") === 0) {
			// remove any comments that may be part of the name or otherwise
			var comment = line.indexOf('#');
			if(comment != -1)
				line = line.substring(0, comment);
			line = line.replace('\t', ' ').trim(); 

			// break up into components
			var args = line.split(" ");
			if(args[0] === "function") { // make sure it is a function and not some other instruction that starts with function
				if(args.length != 2)
					errorMessage(i, "Format of a function is 'function functionname'.");

				// already in a function? handle it
				if(functionStart != -1) {
					functions.push({start: functionStart, end: i, name: name});
				}

				functionStart = i;

				// add a reference to the new function name
				if(functionNames[args[1]] !== undefined)
					// already defined
					errorMessage(i, "Function name " + args[1] + " is already used as a function name on line " +
						(functions[functionNames[args[1]]].start + 1) + ".");

				functionNames[args[1]] = functions.length;

				name = args[1];
			}
		}
	}

	// handle last function
	if(functionStart != -1) {
		functions.push({start: functionStart, end: i, name: name});
	} else {
		console.log("Error: No functions found in the provided file.");
		process.exit(-1);
	}

	if(debug)// for debugging, save the last column
		lineColumns.push(column);
}

var instructions = {
	"add": {opcode: 0, special: false},
	"subtract": {opcode: 1, special: false},
	"divide": {opcode: 2, special: false},
	"multiply": {opcode: 3, special: false},
	"modulo": {opcode: 4, special: false},
	"invert": {opcode: 122, special: false},
	"increment": {opcode: 5, special: false},
	"decrement": {opcode: 6, special: false},
	"xor": {opcode: 7, special: false},
	"and": {opcode: 8, special: false},
	"or": {opcode: 9, special: false},
	"not": {opcode: 10, special: false},
	"shiftleft": {opcode: 11, special: false},
	"shiftright": {opcode: 12, special: false},
	"rotateleft": {opcode: 13, special: false},
	"rotateright": {opcode: 14, special: false},
	"isnull": {opcode: 15, special: false},
	"isnotnull": {opcode: 16, special: false},
	"equals": {opcode: 17, special: false},
	"notequals": {opcode: 18, special: false},
	"lessthan": {opcode: 19, special: false},
	"greaterthan": {opcode: 20, special: false},
	"lessthanorequals": {opcode: 21, special: false},
	"greaterthanorequals": {opcode: 22, special: false},
	"istrue": {opcode: 23, special: false},
	"isfalse": {opcode: 24, special: false},
	"pop": {opcode: 25, special: false},
	"popmany": {special: true}, // 26
	"grab": {special: true}, // 27,28,29 (8/16/32)
	"load": {special: true}, // 30,31,32 (8/16/32)
	"store": {special: true}, // 33,34,35 (8/16/32)
	"swap": {special: true}, // 36,37,38 (8/16/32)
	"loadclosure": {special: true}, // 39,40,41 (8/16/32)
	"storeclosure": {special: true}, // 42,43,44 (8/16/32)
	"newarray": {opcode: 45, special: false},
	"loadelement": {opcode: 46, special: false},
	"saveelement": {opcode: 47, special: false},
	"newobject": {opcode: 48, special: false},
	"deleteelement": {opcode: 49, special: false},
	"newbuffer": {opcode: 50, special: false},
	"loadbufferunsigned[8]": {opcode: 51, special: false},
	"loadbufferunsigned[16]": {opcode: 52, special: false},
	"loadbufferunsigned[32]": {opcode: 53, special: false},
	"loadbufferunsigned[16]": {opcode: 54, special: false},
	"storebufferunsigned[8]": {opcode: 55, special: false},
	"storebufferunsigned[16]": {opcode: 56, special: false},
	"storebufferunsigned[32]": {opcode: 57, special: false},
	"storebufferunsigned[16]": {opcode: 58, special: false},
	"loadbuffersigned[8]": {opcode: 59, special: false},
	"loadbuffersigned[16]": {opcode: 60, special: false},
	"loadbuffersigned[32]": {opcode: 61, special: false},
	"loadbuffersigned[16]": {opcode: 62, special: false},
	"storebuffersigned[8]": {opcode: 63, special: false},
	"storebuffersigned[16]": {opcode: 64, special: false},
	"storebuffersigned[32]": {opcode: 65, special: false},
	"storebuffersigned[16]": {opcode: 66, special: false},
	"loadbufferfloat[16]": {opcode: 67, special: false},
	"loadbufferfloat[32]": {opcode: 68, special: false},
	"loadbufferfloat[16]": {opcode: 69, special: false},
	"storebufferfloat[16]": {opcode: 70, special: false},
	"storebufferfloat[32]": {opcode: 71, special: false},
	"storebufferfloat[16]": {opcode: 72, special: false},
	"pushinteger": {special: true}, // 73,74,75,76 (8/16/32/64)
	"tointeger": {opcode: 77, special: false},
	"pushunsignedinteger": {special: true}, // 78,79,80,81 (8/16/32/64)
	"tounsignedinteger": {opcode: 82, special: false},
	"pushfloat": {special: true}, // 83
	"tofloat": {opcode: 84, special: false},
	"pushtrue": {opcode: 85, special: false},
	"pushfalse": {opcode: 86, special: false},
	"pushnull": {opcode: 87, special: false},
	"pushstring": {special: true}, // 88,89,90 (8/16/32)
	"tostring": {opcode: 121, special: false},
	"pushfunction": {special: true}, // 91
	"callfunction": {special: true}, // 92,93 (8/16)
	"callfunctionnoreturn": {special: true}, // 94,95 (8/16)
	"returnnull": {opcode: 96, special: false},
	"return": {opcode: 97, special: false},
	"gettype": {opcode: 98, special: false},
	"jump": {special: true}, // 99,100,101 (8/16/32)
	"jumpiftrue": {special: true}, // 102,103,104 (8/16/32)
	"jumpiffalse": {special: true}, // 105,106,107 (8/16/32)
	"jumpifnull": {special: true}, // 108,109,110 (8/16/32)
	"jumpifnotnull": {special: true}, // 111,112,113 (8/16/32)
	"require": {opcode: 114, special: false},
	"loadparameter": {special: true}, // 115,116,117 (8/16/32)
	"storeparameter": {special: true}, // 118,119,120
}; // total: 121 - increment this when adding new opcodes

// parses an operand to extract the string, returns null if somethings invalid
var parseString = function(l, instr, index) {
	var str = "";
	var instring = false;
	var foundstring = false;
	var stopparsing = false;

	while(index < instr.length && !stopparsing) {
		var c = instr[index];
		if(!instring) {
			if(c === '"') {
				instring = true;
				foundstring = true;
			} else if(c === '#') { // comment
				stopparsing = true;
			} else if(c !== ' ' && c !== '\t' && c !== '\n' && c !== '\r') {
				errorMessage(l, "Encounted a bad character instead of a string.");
			}
		} else {
			// we're in the string
			if(c == '\\') {
				//special character
				index++;
				if(index >= instr.length)
					errorMessage(l, "Escape character not followed by anything.");
				c = instr[index];

				switch(c) {
					case "\\": str += "\\"; break;
					case "t": str += "\t"; break;
					case "n": str += "\n"; break;
					case "0": str += "\0"; break;
					case "\"": str += "\""; break;
					default: errorMessage(l, "Unknown escape code \\" + c + ".");
				}
			} else if(c == '"') { // string terminator
				instring = false;
			} else {
				str += c;
			}
		}
		//errorMessage(0, "Bad string");
		index++;
	}

	if(instring) // string didn't terminate
		errorMessage(l, "String does not terminate.");

	if(!foundstring) // we didn't parse any string
		errorMessage(l, "No string was found.");

	return str;
};

// handle each function
var handleFunction = function(funcHeader) {
	// get the start and end lines of the function
	var start = funcHeader.start;
	var end = funcHeader.end;

	// set some properties that will be saved in the function header
	funcHeader.startAddr = totalCodeSize; // where in the code block this function starts
	// funcHeader.codeLength = 0; // the length of this code block - set below
	funcHeader.debugBlock = debugBlockSize; // debug block
	funcHeader.parameters = 0; // the number of input parameters
	funcHeader.localVariables = 0; // the number of local variables
	funcHeader.closureVariables = 0; // the number of closure variables

	var funcFilename = inputFile;
	var funcName = funcHeader.name;
	var localVarsNames, closureVarNames, paramNames;

	if(debug) { // debugging information
		funcHeader.functionColumnStart = lineColumns[funcHeader.start];
		funcHeader.functionColumnEnd = lineColumns[funcHeader.end];
		//funcHeader.filename = inputFile;
		localVarNames = {};
		closureVarNames = {};
		paramNames = {};
		funcHeader.lineCols = []; // one column for each line

		funcHeader.lineCols.push(lines[funcHeader.start]); // populate the header for the fact that we need something for
		funcHeader.lineCols.push(lines[funcHeader.start + 1]); //  the first line
	}

	var bytes = 0;
	var jumps = 0; // dynamic, figured out in second pass

	var labels = {};
	var jumpFixUps = []; // jumps to fix up

	var minCol = 0; // minimum column for line
	var maxCol = 0; // maximum column for line
	var autoColumns = true; // automatically populate the column for this line

	// pass 2, assemble most of it into byte code, record labels and count how many jumps there are between them
	for(var i = start + 1; i < end; i++) {
		var line = lines[i];

		if(debug) {
			// for debug assembles, we want to save the columns of each line
			if(autoColumns) {
				minCol = lineColumns[i];
				maxCol = lineColumns[i + 1]; // there's an extra line at the end of the file
			}

			funcHeader.lineCols.push(minCol);
			funcHeader.lineCols.push(maxCol);
		}

		// chop off comments
		var comment = line.indexOf('#');
		if(comment != -1)
			line = line.substring(0, comment);
		line = line.replace('\t', ' ').trim(); // replace tabs with space to make parsing easier

		// skip empty lines
		if(line.length == 0) {
			lines[i] = []; // make it blank (can be skipped if a comment was indented)
			continue;
		}

		// see what is on this line (label, metadata, comment, or instruction)
		if(line[0] == '.') { // label
			if(line.indexOf(' ') != -1)
				errorMessage(i, "Label does not take operands.");

			var label = line.substring(1);

			if(label.length == 0)
				errorMessage(i, "Missing the actual label.");

			// check if the label already exists
			if(labels[label] !== undefined) {
				errorMessage(i, "A label called " + label + " was already specified on line " + (labels[label].line + 1) + ".");
			}

			// add tha label
			labels[label] = {
				line: i,
				bytes: bytes,
				jumps: jumps
			};

			lines[i] = [];

		} else if(line[0] == '#') { // comment
			lines[i] = [];
		} else if(line[0] == '-') { // function metadata
			////// extract the property name and the operands
			var prop;
			var operands = line.indexOf(' ');
			if(operands != -1) { // has operands
				prop = line.substring(0, operands);
				operands = line.substring(operands + 1).split(' ');
			} else {
				operands = [];
				prop = line;
			}

			prop = prop.toLowerCase();

			switch(prop) {
				case '-name': // specifies the name of this function
					if(operands.length != 1)
						errorMessage(i, "-name takes 1 argument.");

					funcName.parameters = operands[0];
					break;
				case '-parameters': // specifies the number of parameters this function takes
					if(operands.length != 1)
						errorMessage(i, "-parameters takes 1 argument.");

					var op = parseInt(operands[0]);
					if(op < 0)
						errorMessage(i, "-parameters must take a positive number.");

					funcHeader.parameters = op;
					break;
				case '-locals': // specifies the number of local variables
					if(operands.length != 1)
						errorMessage(i, "-locals takes 1 argument.");

					var op = parseInt(operands[0]);
					if(op < 0)
						errorMessage(i, "-locals must take a positive number.");
					funcHeader.localVariables = op;
					break;
				case '-closures': // specifies the number of closure variables
					if(operands.length != 1)
						errorMessage(i, "-closures takes 1 argument.");

					var op = parseInt(operands[0]);
					if(op < 0)
						errorMessage(i, "-closures must take a positive number.");

					funcHeader.closureVariables = op;
					break;
				case '-parameter': // specifies the name of a parameter
					if(operands.length != 2)
						errorMessage(i, "-parameter takes 2 arguments.");

					var op = parseInt(operands[0]);
					if(op < 0)
						errorMessage(i, "-parameter must take a positive number.");
					if(debug)
						paramNames[op] = operands[1];
					break;
				case '-local': // specifies the name of a local variable
					if(operands.length != 2)
						errorMessage(i, "-local takes 2 arguments.");

					var op = parseInt(operands[0]);
					if(op < 0)
						errorMessage(i, "-local must take a positive number.");
					if(debug)
						localVarNames[op] = operands[1];
					break;
				case '-closure': // specifies the name of a closure variable
					if(operands.length != 2)
						errorMessage(i, "-closure takes 2 arguments.");

					var op = parseInt(operands[0]);
					if(op < 0)
						errorMessage(i, "-closure must take a positive number.");
					if(debug)
						closureVarNames[op] = operands[1];
					break;
				case '-filename': // specifies the filename where the source code in
					if(operands.length == 0)
						errorMessage(i, "-filename takes 1 argument.");

					if(debug)
						// get the entire operands, spaces and all
						funcFilename = line.substring(line.indexOf(' ') + 1).trim();
					break;
				break;
				case '-columns': // specfies the the columns in the source code this came from
					if(operands.length != 2)
						errorMessage(i, "-columns takes 2 arguments.");


					var op1 = parseInt(operands[0]);
					var op2 = parseInt(operands[1]);
					if(op1 < 0 || op2 < 0)
						errorMessage(i, "-columns must take a positive numbers.");

					if(op1 > op2) { // make sure op1 and op2 are in order
						var t = op2;
						opt2 = opt1;
						opt1 = t;
					}

					minCol = op1;
					maxCol = op2;
					autoColumns = false;
					break;
				case '-functioncolumns':
					if(operands.length != 2)
						errorMessage(i, "-functioncolumns takes 2 arguments.");


					var op1 = parseInt(operands[0]);
					var op2 = parseInt(operands[1]);
					if(op1 < 0 || op2 < 0)
						errorMessage(i, "-functioncolumns must take a positive numbers.");

					if(op1 > op2) { // make sure op1 and op2 are in order
						var t = op2;
						opt2 = opt1;
						opt1 = t;
					}

					funcHeader.functionColumnStart = op1;
					funcHeader.functionColumnEnd = op2;
					break;
				default:
					errorMessage(i, "Unknown property " + prop + ".");
			}

			lines[i] = [];
		} else { // instruction
			////// extract the instruction name and operands
			var inst;
			var operands = line.indexOf(' ');
			if(operands != -1) { // has operands
				inst = line.substring(0, operands);
				operands = line.substring(operands + 1).split(' ');
			} else {
				operands = [];
				inst = line;
			}

			inst = inst.toLowerCase();

			// look up the instruction
			var instruction = instructions[inst];
			if(instruction === undefined)
				errorMessage(i, "Unknown instruction " + inst + ".");

			// now replace the line with the bytes needed for the instruction
			if(instruction.special === false) {
				// not special, just basic opcode
				if(operands.length != 0) {
					errorMessage(i, inst + " does not take operands.");
					process.exit(-1);
				}
				lines[i] = [instruction.opcode];
				bytes++;
			} else {
				// special cases
				switch(inst) {
					case "popmany": // 26
						if(operands.length != 1)
							errorMessage(i, "popmany takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "popmany must take a positive number.");
						if(op >= 256)
							errorMessage(i, "popmany must take an argument less than 256.");

						lines[i] = [26, op];
						bytes += lines[i].length;
						break;
					case "grab": // 27,28,29 (8/16/32)
						if(operands.length != 1)
							errorMessage(i, "grab takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "grab must take a positive number.");
						if(op >= 4294967296)
							errorMessage(i, "grab argument must be less than 2^32.");
						if(op >= 65536) {
							lines[i] = [29, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [28, op & 255, (op >> 8) & 255];

						} else {
							lines[i] = [27, op];
						}
						bytes += lines[i].length;
						break;
					case "load": // 30,31,32 (8/16/32)
						if(operands.length != 1)
							errorMessage(i, "load takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "load must take a positive number.");
						if(op >= 4294967296)
							errorMessage(i, "load argument must be less than 2^32.");
						if(op >= 65536) {
							lines[i] = [32, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [31, op & 255, (op >> 8) & 255];

						} else {
							lines[i] = [30, op];
						}
						bytes += lines[i].length;
						break;
					case "store": // 33,34,35 (8/16/32)
						if(operands.length != 1)
							errorMessage(i, "store takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "store must take a positive number.");
						if(op >= 4294967296)
							errorMessage(i, "store argument must be less than 2^32.");
						if(op >= 65536) {
							lines[i] = [35, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [34, op & 255, (op >> 8) & 255];

						} else {
							lines[i] = [33, op];
						}
						bytes += lines[i].length;
						break;
					case "swap":  // 36,37,38 (8/16/32)
						if(operands.length != 2)
							errorMessage(i, "swap takes 2 arguments.");

						var op1 = parseInt(operands[0]);
						var op2 = parseInt(operands[1]);
						if(op1 < 0 || op2 < 0)
							errorMessage(i, "swap must take a positive numbers.");
						if(op1 >= 4294967296 || op2 >= 4294967296)
							errorMessage(i, "swap arguments must be less than 2^32.");
						if(op1 >= 65536 || op2 >= 65536) {
							lines[i] = [38, op1 & 255, (op1 >> 8) & 255, (op1 >> 16) & 255, (op1 >> 24) & 255,
								op2 & 255, (op2 >> 8) & 255, (op2 >> 16) & 255, (op2 >> 24) & 255];
						} else if(op1 >= 256 || op2 >= 256) {
							lines[i] = [37, op1 & 255, (op1 >> 8) & 255, op2 & 255, (op2 >> 8) & 255];

						} else {
							lines[i] = [36, op1, op2];
						}
						bytes += lines[i].length;
						break;
					case "loadclosure": // 39,40,41 (8/16/32)
						if(operands.length != 1)
							errorMessage(i, "loadclosure takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "loadclosure must take a positive number.");
						if(op >= 4294967296)
							errorMessage(i, "loadclosure argument must be less than 2^32.");
						if(op >= 65536) {
							lines[i] = [41, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [40, op & 255, (op >> 8) & 255];

						} else {
							lines[i] = [39, op];
						}
						bytes += lines[i].length;
						break;
					case "storeclosure": // 42,43,44 (8/16/32)
						if(operands.length != 1)
							errorMessage(i, "storeclosure takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "storeclosure must take a positive number.");
						if(op >= 4294967296)
							errorMessage(i, "storeclosure argument must be less than 2^32.");
						if(op >= 65536) {
							lines[i] = [44, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [43, op & 255, (op >> 8) & 255];

						} else {
							lines[i] = [42, op];
						}
						bytes += lines[i].length;
						break;
					case "pushinteger": // 73,74,75,76 (8/16/32/64)
						if(operands.length != 1)
							errorMessage(i, "pushinteger takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < -9223372036854775808 || op > 9223372036854775807)
							errorMessage(i, "pushuinteger must be between -2^64 and +2^63.");

						if(op < -2147483648 || op > 2147483647) {
							errorMessage(i, "64-bit pushinteger is not yet supported.");
							/*
							lines[i] = [76, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255,
								 (op >> 32) & 255, (op >> 40) & 255,  (op >> 48) & 255,  (op >> 56) & 255];
							*/
						} else if(op < -32768 || op > 32767) {
							if(op < 0)
								op +=  4294967295;
							lines[i] = [75, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op < -128 || op > 127) {
							if(op < 0)
								op += 65535;
							lines[i] = [74, op & 255, (op >> 8) & 255];
						} else {
							if(op < 0)
								op += 256;
							lines[i] = [73, op];
						}
						bytes += lines[i].length;
						break;
					case "pushunsignedinteger": // 78,79,80,81 (8/16/32/64)
						if(operands.length != 1)
							errorMessage(i, "pushunsignedinteger takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "pushunsignedinteger must take a positive number.");
						if(op > 18446744073709551615)
							errorMessage(i, "pushunsignedinteger argument must be less than 2^64.");
						if(op >= 4294967296) {
							errorMessage(i, "64-bit pushunsignedinteger is not yet supported.");
							/*
							lines[i] = [81, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255,
								 (op >> 32) & 255, (op >> 40) & 255,  (op >> 48) & 255,  (op >> 56) & 255];
							*/
						} else if(op >= 65536) {
							lines[i] = [80, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [79, op & 255, (op >> 8) & 255];
						} else {
							lines[i] = [78, op];
						}
						bytes += lines[i].length;
						break;
					case "pushfloat": // 83
						if(operands.length != 1)
							errorMessage(i, "pushfloat takes 1 argument.");

						var op = parseFloat(operands[0]);
						buffer.writeDoubleLE(op, 0);

						lines[i] = [83, buffer.readUInt8(0), buffer.readUInt8(1), buffer.readUInt8(2), buffer.readUInt8(3),
							buffer.readUInt8(4),buffer.readUInt8(5),buffer.readUInt8(6),buffer.readUInt8(7)];
						bytes += lines[i].length;
						break;
					case "pushstring": // 88,89,90 (8/16/32)
						var str = parseString(i, lines[i], line.toLowerCase().indexOf('pushstring') + 'pushstring'.length + 1);

						var index = addString(str);
						console.log("index " + index);

						var l;
						if(index >= 65536) {
							l = [90, index & 255, (index >> 8) & 255, (index >> 16) & 255, (index >> 24) & 255];
						} else if(index >= 256) {
							l = [89, index & 255, (index >> 8) & 255];
						} else {
							l = [88, index];
						}

						lines[i] = l;
						bytes += lines[i].length;
						break;
					case "pushfunction": // 91
						if(operands.length != 1)
							errorMessage(i, "pushfunction takes 1 argument.");

						var func = functionNames[operands[0]];
						if(func === undefined)
							errorMessage(i, "Function name " + func + " is undefined.");

						lines[i] = [91, func & 255, (func >> 8) & 255, (func >> 16) & 255, (func >> 24) & 255];
						bytes += lines[i].length;
						break;
					case "callfunction": // 92,93 (8/16)
						if(operands.length != 1)
							errorMessage(i, "callfunction takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "callfunction must take a positive number.");
						if(op >= 65536)
							errorMessage(i, "callfunction argument must be less than 65536.");

						if(op >= 256) {
							lines[i] = [93, op & 255, (op >> 8) & 255];
						} else {
							lines[i] = [92, op];
						}
						bytes += lines[i].length;
						break;
					case "callfunctionnoreturn": // 94,95 (8/16)
						if(operands.length != 1)
							errorMessage(i, "callfunctionnoreturn takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "callfunctionnoreturn must take a positive number.");
						if(op >= 65536)
							errorMessage(i, "callfunctionnoreturn argument must be less than 65536.");

						if(op >= 256) {
							lines[i] = [95, op & 255, (op >> 8) & 255];
						} else {
							lines[i] = [94, op];
						}
						bytes += lines[i].length;
						break;
					case "jump": // 99,100,101 (8/16/32)
					case "jumpiftrue": // 102,103,104 (8/16/32)
					case "jumpiffalse": // 105,106,107 (8/16/32)
					case "jumpifnull": // 108,109,110 (8/16/32)
					case "jumpifnotnull": // 111,112,113 (8/16/32)
						if(operands.length != 1)
							errorMessage(i, inst + " takes 1 argument.");

						lines[i] = [inst, operands[0]];
						jumpFixUps.push(i); // save this jump for us to fix up later
						
						jumps++;
						break;
					case "loadparameter": // 115,116,117 (8/16/32)
						if(operands.length != 1)
							errorMessage(i, "loadparameter takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "loadparameter must take a positive number.");
						if(op >= 4294967296)
							errorMessage(i, "loadparameter argument must be less than 2^32.");
						if(op >= 65536) {
							lines[i] = [117, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [116, op & 255, (op >> 8) & 255];

						} else {
							lines[i] = [115, op];
						}
						bytes += lines[i].length;
						break;
					case "storeparameter":// 118,119,120
						if(operands.length != 1)
							errorMessage(i, "storeparameter takes 1 argument.");

						var op = parseInt(operands[0]);
						if(op < 0)
							errorMessage(i, "storeparameter must take a positive number.");
						if(op >= 4294967296)
							errorMessage(i, "storeparameter argument must be less than 2^32.");
						if(op >= 65536) {
							lines[i] = [200, op & 255, (op >> 8) & 255, (op >> 16) & 255, (op >> 24) & 255];
						} else if(op >= 256) {
							lines[i] = [119, op & 255, (op >> 8) & 255];

						} else {
							lines[i] = [118, op];
						}
						bytes += lines[i].length;
						break;
					default:
						errorMessage(i, "Internal error - no case for special instruction " + inst + ".");
				}
			}
		}
	}


	// figure out how big we are, so we know if to use 8/16/32 bit jumps
	var jumpSize;
	if(bytes + jumps < 256)
		jumpSize = 1;
	else if(bytes + jumps * 2 < 65535)
		jumpSize = 2;
	else
		jumpSize = 4;

	var jumpInstructionSize = jumpSize + 1; // 1 byte for the instruction

	bytes += jumps * jumpInstructionSize; // calculate the total size of our function

	totalCodeSize += bytes;
	funcHeader.codeLength = bytes;

	// fix up the addresses of labels now that we know how big our jumps are
	for(var label in labels) {
		var l = labels[label];
		labels[label] = l.bytes + l.jumps * jumpInstructionSize;
	}

	// fix up our jump addresses now that we have our labels
	for(var i = 0; i < jumpFixUps.length; i++) {
		var j = jumpFixUps[i];

		var fixUp = lines[j];
		// fix the instruction
		switch(fixUp[0]) {
			case "jump": // 99,100,101 (8/16/32)
				if(jumpSize == 1)
					fixUp[0] = 99;
				else if(jumpSize == 2)
					fixUp[0] = 100;
				else
					fixUp[0] = 101;
				break;
			case "jumpiftrue": // 102,103,104 (8/16/32)
				if(jumpSize == 1)
					fixUp[0] = 102;
				else if(jumpSize == 2)
					fixUp[0] = 103;
				else
					fixUp[0] = 104;
				break;
			case "jumpiffalse": // 105,106,107 (8/16/32)
				if(jumpSize == 1)
					fixUp[0] = 105;
				else if(jumpSize == 2)
					fixUp[0] = 106;
				else
					fixUp[0] = 107;
				break;
			case "jumpifnull": // 108,109,110 (8/16/32)
				if(jumpSize == 1)
					fixUp[0] = 108;
				else if(jumpSize == 2)
					fixUp[0] = 109;
				else
					fixUp[0] = 110;
				break;
			case "jumpifnotnull": // 111,112,113 (8/16/32)
				if(jumpSize == 1)
					fixUp[0] = 111;
				else if(jumpSize == 2)
					fixUp[0] = 112;
				else
					fixUp[0] = 113;
				break;
			default:
				errorMessage(i, "Internal error - unknown jump.");
		}

		// fix the address
		var label = labels[fixUp[1]]; // label is now an address because it has been fixed up
		if(label === undefined)
			errorMessage(j, "Undefined label " + fixUp[1] + ".");

		if(jumpSize == 1)
			fixUp[1] = label;
		else if(jumpSize == 2) {
			fixUp[1] = label & 255;
			fixUp.push((label >> 8) & 255);
		}
		else if(jumpSize == 4) {
			fixUp[1] = label & 255;
			fixUp.push((label >> 8) & 255);
			fixUp.push((label >> 16) & 255);
			fixUp.push((label >> 24) & 255);
		}
	}

	// calculate the size of the debugging header

	if(debug) {
		// top is pointer to bytecols, name start/name length, filename start/filename length,
		// start col, end col, a pointer for each parameter, local, closure variable - start/length
		// string pointers are 6 bytes (32 bit pointer, 16 bit length)
		var debuggingBlockHeader = 4 + 6 + 6 + 4 + 4 + ((funcHeader.parameters + funcHeader.localVariables +
			funcHeader.closureVariables) * 6);

		var byteCols = bytes * 8; // two 32-bit column numbers for each line - yes it's huge but it's only in debugging builds

		// build us our string table, look up the names of each thing - add it if they exist, otherwise instert '?'
		var stringTable = "";
		
		funcHeader.nameStart = stringTable.length;
		funcHeader.nameLength = funcName.length;
		stringTable += funcName;

		funcHeader.filenameStart = stringTable.length;
		funcHeader.filenameLength = funcFilename.length;
		stringTable += funcFilename;


		var funcParams = [];
		var funcLocalVars = [];
		var funcClosureVars = [];

		for(var i = 0; i < funcHeader.parameters; i++) {
			var name = paramNames[i]; // find the name
			if(name === undefined)
				name = "?"; // unknown

			funcParams.push(stringTable.length);
			funcParams.push(name.length);
			stringTable += name;
		}

		funcHeader.paramNames = funcParams;

		for(var i = 0; i < funcHeader.localVariables; i++) {
			var name = localVarNames[i]; // find the name
			if(name === undefined)
				name = "?"; // unknown

			funcLocalVars.push(stringTable.length);
			funcLocalVars.push(name.length);
			stringTable += name;
		}

		funcHeader.localNames = funcLocalVars;

		for(var i = 0; i < funcHeader.closureVariables; i++) {
			var name = closureVarNames[i]; // find the name
			if(name === undefined)
				name = "?"; // unknown

			funcClosureVars.push(stringTable.length);
			funcClosureVars.push(name.length);
			stringTable += name;
		}

		funcHeader.closureNames = funcClosureVars;

		funcHeader.stringTable = stringTable;
		funcHeader.bytes = bytes;

		// offset this with the total size
		debugBlockSize += debuggingBlockHeader + byteCols + stringTable.length;
	}
};

var writeByte = function(b) {
	buffer.writeUInt8(b, 0);
	fs.writeSync(file, buffer, 0, 1, null);
};

var writeUInt16 = function(b) {
	buffer.writeUInt16LE(b, 0);
	fs.writeSync(file, buffer, 0, 2, null);
};

var writeUInt32 = function(b) {
	buffer.writeUInt32LE(b, 0);
	fs.writeSync(file, buffer, 0, 4, null);
};

var writeString = function(str) {
	for(var i = 0; i < str.length; i++) {
		buffer.write(str[i], 0);
		fs.writeSync(file, buffer, 0, 1, null);
	}
};

// write the file header out to disk
var writeFileHeader = function() {
	// header sizes for calculating the offsets into the file
	var iconStart = 50; // header size
	var iconSize = 0;
	var functionHeaderStart = iconStart + iconSize;
	var codeBlockStart = functionHeaderStart + 24 * functions.length;
	var stringTableStart = codeBlockStart + totalCodeSize;
	var debugBlockStart =  stringTableStart + 8 * globalStrings.length + globalCurrentStringTableLength;

	// write the magic key 12SHOVEL
	writeUInt32(0x48533231);
	writeUInt32(0x4C45564F);

	// write the version code
	writeUInt16(0);

	// icon
	writeUInt32(iconStart); // pointer
	writeUInt32(iconSize); // size

	// function header pointer
	writeUInt32(functionHeaderStart); // pointer
	writeUInt32(functions.length); // number of functions

	// code block
	writeUInt32(codeBlockStart); // pointer
	writeUInt32(totalCodeSize); // size

	// string table
	writeUInt32(stringTableStart); // string table start
	writeUInt32(globalStrings.length); // string table entries

	// debug block
	writeUInt32(debugBlockStart); // pointer
	writeUInt32(debugBlockSize); // size
};

// write the function header out to disk
var writeFunctionHeaders = function(funcHeader) {
	// function header is 24 bytes per function
	writeUInt32(funcHeader.startAddr); // start address into code block
	writeUInt32(funcHeader.codeLength); // length of the code block
	writeUInt32(funcHeader.debugBlock); // pointer to a debug block, if present
	writeUInt32(funcHeader.parameters); // number of input parameters
	writeUInt32(funcHeader.localVariables); // number of local variables
	writeUInt32(funcHeader.closureVariables); // number of closure variables
};

// write the function out to disk
var writeFunction = function(func) {
	var start = func.start;
	var end = func.end;

	// for each line in this function
	for(var i = start + 1; i < end; i++) {
		var line = lines[i];

		// for each byte in this line
		for(var j = 0; j < line.length; j++)
			writeByte(line[j]); // write it to the file
	}
};

// write out the string table
var writeStringTable = function() {
	// write out the indices/length of each string
	for(var i = 0; i < globalStrings.length; i++) {
		var strEntry = globalStringTable[globalStrings[i]];
		writeUInt32(strEntry.start);
		writeUInt32(strEntry.len);
	}

	// write out the strings
	for(var i = 0; i < globalStrings.length; i++)
		writeString(globalStrings[i]);
};

var writeDebugBlock = function(func) {
	// write the debug header
	var debuggingBlockHeader = 4 + 6 + 6 + 4 + 4 + ((func.parameters + func.localVariables + func.closureVariables) * 6);

 	// pointer to the string table at the end if the debug block
	var stringTableOffset = debuggingBlockHeader + func.bytes * 8;

	// top is pointer to bytecols, name start/name length, filename start/filename length,
	// start col, end col, a pointer for each parameter, local, closure variable - start/length
	// string pointers are 6 bytes (32 bit pointer, 16 bit length)

	writeUInt32(debuggingBlockHeader); // start of the code columns
	writeUInt32(func.nameStart);
	writeUInt16(func.nameLength);
	writeUInt32(func.filenameStart);
	writeUInt16(func.filenameLength);
	writeUInt32(func.functionColumnStart);
  	writeUInt32(func.functionColumnEnd);

  	// write parameter names
  	for(var i = 0; i < func.paramNames.length / 2; i++) {
  		writeUInt32(func.paramNames[i * 2]);
  		writeUInt16(func.paramNames[i * 2 + 1]);
  	}
  	// write local names
  	for(var i = 0; i < func.localNames.length / 2; i++) {
  		writeUInt32(func.localNames[i * 2]);
  		writeUInt16(func.localNames[i * 2 + 1]);
  	}
  	// write closure names
  	for(var i = 0; i < func.closureNames.length / 2; i++) {
  		writeUInt32(func.closureNames[i * 2]);
  		writeUInt16(func.closureNames[i * 2 + 1]);
  	}

  	// write byte columns
  	// for each line in this function
  	var l = 0; // the line (in func.lineCOls)
  	var start = func.start;
  	var end = func.end;
	for(var i = start + 1; i < end; i++, l+= 2) {
		var line = lines[i];

		var colStart = func.lineCols[l];
		var colEnd = func.lineCols[l + 1];

		// for each byte in this line
		for(var j = 0; j < line.length; j++) {
			// write the column out for this byte
			writeUInt32(colStart); // start column for this byte
			writeUInt32(colEnd); // end column for this byte
		}
	}

  	// write string table
  	writeString(func.stringTable);
};

var buffer = new Buffer(8); // 8-byte buffer for temp stuff

if(verbose)
	console.log("Pass 1 - Finding functions");

findFunctions();

if(verbose)
	console.log("Pass 2 - Assembling functions");

var debugBlockSize = 0; // as debug blocks grow, this goes up
var totalCodeSize = 0;
for(var i = 0; i < functions.length; i++)
	handleFunction(functions[i]);

if(verbose) {
	console.log(functions.length + " function(s), code takes up " + totalCodeSize + " byte(s).");
	console.log("Writing file header");
}

// open the file
var file = fs.openSync(outputFile, "w");

writeFileHeader();

if(verbose)
	console.log("Writing function headers");

for(var i = 0; i < functions.length; i++)
	writeFunctionHeaders(functions[i]);

if(verbose)
	console.log("Pass 3 - Writing out code");

// write out the code block
for(var i = 0; i < functions.length; i++)
	writeFunction(functions[i]);

if(verbose)
	console.log("Pass 4 - Writing out string table");
writeStringTable();

if(debug) {
	console.log("Pass 5 - Writing out debugging information");
	for(var i = 0; i < functions.length; i++)
		writeDebugBlock(functions[i]);
}

// close the file
fs.closeSync(file);
