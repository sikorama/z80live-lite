# Makefile rasm-lite — build simple sans dépendance (cmake optionnel, cf. CMakeLists.txt)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

.PHONY: all test clean

CORE = z80.cpp expr.cpp parser.cpp pp.cpp asm.cpp sna.cpp

all: z80_test pp_test parser_test asm_test sna_test ppdump rasmlite

z80_test: z80.cpp z80_test.cpp z80.h
	$(CXX) $(CXXFLAGS) z80.cpp z80_test.cpp -o $@

pp_test: pp.cpp expr.cpp pp_test.cpp pp.h expr.h
	$(CXX) $(CXXFLAGS) pp.cpp expr.cpp pp_test.cpp -o $@

parser_test: parser.cpp z80.cpp parser_test.cpp parser.h z80.h
	$(CXX) $(CXXFLAGS) parser.cpp z80.cpp parser_test.cpp -o $@

asm_test: asm.cpp parser.cpp z80.cpp expr.cpp asm_test.cpp asm.h
	$(CXX) $(CXXFLAGS) asm.cpp parser.cpp z80.cpp expr.cpp asm_test.cpp -o $@

sna_test: sna.cpp sna_test.cpp sna.h
	$(CXX) $(CXXFLAGS) sna.cpp sna_test.cpp -o $@

ppdump: pp.cpp expr.cpp pp_main.cpp pp.h expr.h
	$(CXX) $(CXXFLAGS) pp.cpp expr.cpp pp_main.cpp -o $@

rasmlite: $(CORE) asm_main.cpp asm.h pp.h
	$(CXX) $(CXXFLAGS) $(CORE) asm_main.cpp -o $@

test: z80_test pp_test parser_test asm_test sna_test
	./z80_test
	./pp_test
	./parser_test
	./asm_test
	./sna_test

clean:
	rm -f z80_test pp_test parser_test asm_test sna_test ppdump rasmlite
	rm -rf build
