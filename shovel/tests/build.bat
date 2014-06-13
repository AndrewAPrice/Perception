cd ..
node Parser.js -v tests\tests.src tests\tests.asm
node Assembler.js -v tests\tests.asm tests\tests.bin
pause