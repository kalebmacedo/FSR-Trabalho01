/*
 * test_util.h - Mini framework de testes (sem bibliotecas externas).
 *
 * CHECK(cond, msg) conta o teste e, se a condição falhar, imprime
 * arquivo, linha e mensagem. TEST_SUMMARY() imprime o placar e
 * devolve 0 (tudo passou) ou 1 (houve falha) para o main do teste.
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        tests_run++;                                                      \
        if (!(cond)) {                                                    \
            tests_failed++;                                               \
            fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, msg); \
        }                                                                 \
    } while (0)

#define TEST_SUMMARY(nome)                                                \
    (printf("%s: %d/%d verificacoes passaram\n", nome,                    \
            tests_run - tests_failed, tests_run),                         \
     tests_failed == 0 ? 0 : 1)

#endif /* TEST_UTIL_H */
