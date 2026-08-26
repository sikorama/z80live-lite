# Makefile fantams — build simple sans dépendance (cmake optionnel, cf. CMakeLists.txt)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

.PHONY: all test clean

CORE = z80.cpp expr.cpp keywords.cpp parser.cpp pp.cpp asm.cpp beautify.cpp sna.cpp sym.cpp

all: z80_test expr_test pp_test parser_test asm_test beautify_test sna_test ppdump fantams

z80_test: z80.cpp z80_test.cpp z80.h
	$(CXX) $(CXXFLAGS) z80.cpp z80_test.cpp -o $@

# expr lit les littéraux de chaîne par kw::readLiteral (ADR 0010) : la règle
# « où finit un littéral » vit une seule fois, dans keywords.
expr_test: expr.cpp keywords.cpp z80.cpp expr_test.cpp expr.h keywords.h
	$(CXX) $(CXXFLAGS) expr.cpp keywords.cpp z80.cpp expr_test.cpp -o $@

pp_test: pp.cpp expr.cpp z80.cpp keywords.cpp pp_test.cpp pp.h expr.h z80.h keywords.h
	$(CXX) $(CXXFLAGS) pp.cpp expr.cpp z80.cpp keywords.cpp pp_test.cpp -o $@

parser_test: parser.cpp z80.cpp keywords.cpp parser_test.cpp parser.h z80.h keywords.h
	$(CXX) $(CXXFLAGS) parser.cpp z80.cpp keywords.cpp parser_test.cpp -o $@

asm_test: asm.cpp sym.cpp parser.cpp z80.cpp expr.cpp keywords.cpp asm_test.cpp asm.h sym.h keywords.h
	$(CXX) $(CXXFLAGS) asm.cpp sym.cpp parser.cpp z80.cpp expr.cpp keywords.cpp asm_test.cpp -o $@

# Le beautify n'a besoin que du parseur et du vocabulaire réservé : ni adresse,
# ni octet, ni assemblage (ADR 0013).
# beautify.cpp lui-meme n'a besoin que de keywords + z80 ; l'assembleur n'est la
# que pour l'invariant d'octets, verifie par les tests.
beautify_test: beautify.cpp keywords.cpp z80.cpp asm.cpp parser.cpp expr.cpp pp.cpp beautify_test.cpp beautify.h keywords.h
	$(CXX) $(CXXFLAGS) beautify.cpp keywords.cpp z80.cpp asm.cpp parser.cpp expr.cpp pp.cpp beautify_test.cpp -o $@

sna_test: sna.cpp sna_test.cpp sna.h
	$(CXX) $(CXXFLAGS) sna.cpp sna_test.cpp -o $@

ppdump: pp.cpp expr.cpp z80.cpp keywords.cpp pp_main.cpp pp.h expr.h z80.h keywords.h
	$(CXX) $(CXXFLAGS) pp.cpp expr.cpp z80.cpp keywords.cpp pp_main.cpp -o $@

fantams: $(CORE) asm_main.cpp asm.h pp.h sym.h
	$(CXX) $(CXXFLAGS) $(CORE) asm_main.cpp -o $@

test: z80_test expr_test pp_test parser_test asm_test beautify_test sna_test
	./z80_test
	./expr_test
	./pp_test
	./parser_test
	./asm_test
	./beautify_test
	./sna_test

clean:
	rm -f z80_test expr_test pp_test parser_test asm_test beautify_test sna_test ppdump fantams
	rm -rf build
