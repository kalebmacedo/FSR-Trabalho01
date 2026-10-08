# Makefile do cliente DNS (MX)
#   make        compila e gera ./meu_cliente
#   make test   compila e roda os testes unitários (sem rede)
#   make clean  apaga o binário, os objetos e os testes compilados

CC     = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -O2

TARGET = meu_cliente
SRCS   = src/main.c src/dns_query.c src/dns_parse.c src/net.c
OBJS   = $(SRCS:.c=.o)

TESTS  = tests/test_query tests/test_parse tests/test_net

all: $(TARGET)

# Liga os objetos e gera o executável
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Cada .c vira um .o; todos dependem de dns.h
src/%.o: src/%.c src/dns.h
	$(CC) $(CFLAGS) -c $< -o $@

# Testes unitários: cada um é ligado só com o módulo que testa
tests/test_query: tests/test_query.c tests/test_util.h src/dns_query.o
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_query.c src/dns_query.o

tests/test_parse: tests/test_parse.c tests/test_util.h src/dns_parse.o
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_parse.c src/dns_parse.o

tests/test_net: tests/test_net.c tests/test_util.h src/net.o src/dns_query.o
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_net.c src/net.o src/dns_query.o

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

# Testes de ponta a ponta com servidores DNS reais (precisa de internet)
e2e: $(TARGET)
	@bash tests/e2e.sh

clean:
	rm -f $(TARGET) $(OBJS) $(TESTS)

# Pacote para o Sigaa, só com fontes e documentação (sem executáveis):
#   make entrega ZIP=nome_sobrenome_matricula_..._trab01.zip
entrega: clean
	@test -n "$(ZIP)" || { echo "Uso: make entrega ZIP=nome_sobrenome_matricula_(x4)_trab01.zip"; exit 1; }
	rm -f $(ZIP)
	zip -r $(ZIP) Makefile README.md src tests

.PHONY: all test e2e clean entrega
