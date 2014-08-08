CC=gcc
CFLAGS=-I. -DTEST -DLINUX -O3

LFLAGS=-lm -lstdc++

OBJECTS=array.o buffer.o city.o closure.o conversions.o functionpointer.o functionpointer_x86.o gc.o hooks.o instructions.o interpreter.o main.o \
	module.o object.o ssa.o ssa_conversions.o ssa_optimizer.o ssa_printer.o stack_helpers.o string.o stringtable.o turkey.o
	
HEADERS=hooks.h stack.h turkey.h

test: turkey

hooks.o: Linux/hooks.cpp hooks.h turkey.h
	$(CC) -o $@ -c $< $(CFLAGS)
	
main.o: Linux/main.cpp hooks.h turkey.h
	$(CC) -o $@ -c $< $(CFLAGS)

city.o: external/cityhash/city.cpp
	$(CC) -o $@ -c $< $(CFLAGS) -I./external/cityhash
	
array.o: array.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)
	
buffer.o: buffer.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)
	
closure.o: closure.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

conversions.o: conversions.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

functionpointer.o: functionpointer.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

functionpointer_x86.o: functionpointer_x86.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

gc.o: gc.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

instructions.o: instructions.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

interpreter.o: interpreter.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

module.o: module.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

object.o: object.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

ssa.o: ssa.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

ssa_conversions.o: ssa_conversions.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

ssa_optimizer.o: ssa_optimizer.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

ssa_printer.o: ssa_printer.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

stack_helpers.o: stack_helpers.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

string.o: string.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

stringtable.o: stringtable.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

turkey.o: turkey.cpp $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

turkey: $(OBJECTS)
	$(CC) $(LFLAGS) -o turkey *.o

clean:
	rm -f *.o turkey
	