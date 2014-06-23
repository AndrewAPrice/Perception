cd ..
node Parser.js -v tests\benchmarks.src tests\benchmarks.asm
node Assembler.js -v tests\benchmarks.asm tests\benchmarks.bin

node Parser.js -v tests\ssa.src tests\ssa.asm
node Assembler.js -v tests\ssa.asm tests\ssa.bin

pause