CC=clang++

LLVM_MODULES=all

CPPFLAGS=`llvm-config --cppflags`
LDFLAGS=`llvm-config --ldflags`
LIBS=`llvm-config --libs all`

all:
	@mkdir -p build
	$(CC) main.cpp $(LDFLAGS) $(LIBS) -o ./build/llvm-test

run: all
	./build/llvm-test ./test.lang
	$(CC) -o ./build/main output.o
	@rm -rf output.o
	./build/main
