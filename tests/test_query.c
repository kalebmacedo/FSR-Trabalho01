/*
 * test_query.c - Testes da montagem da consulta (src/dns_query.c).
 *
 * Imprime o pacote de unb.br em hexadecimal (pedido da etapa 2 do plano)
 * e confere cada byte contra o formato da RFC 1035, seção 4.1.
 */
#include <string.h>

#include "dns.h"
#include "test_util.h"

/* Imprime o pacote em hexadecimal, 16 bytes por linha */
static void hexdump(const uint8_t *buf, int len)
{
    int i;
    for (i = 0; i < len; i++)
        printf("%02X%s", buf[i], (i % 16 == 15 || i == len - 1) ? "\n" : " ");
}

/* Pacote esperado para unb.br com ID 0xABCD */
static const uint8_t UNB_BR[] = {
    0xAB, 0xCD,             /* ID */
    0x01, 0x00,             /* flags: consulta recursiva */
    0x00, 0x01,             /* QDCOUNT = 1 */
    0x00, 0x00,             /* ANCOUNT = 0 */
    0x00, 0x00,             /* NSCOUNT = 0 */
    0x00, 0x00,             /* ARCOUNT = 0 */
    0x03, 'u', 'n', 'b',    /* label "unb" */
    0x02, 'b', 'r',         /* label "br" */
    0x00,                   /* fim do nome */
    0x00, 0x0F,             /* QTYPE = MX (15) */
    0x00, 0x01              /* QCLASS = IN (1) */
};

static void test_unb_br_byte_a_byte(void)
{
    uint8_t buf[DNS_MAX_PACKET];
    int len = dns_build_query("unb.br", 0xABCD, buf, sizeof buf);

    printf("Consulta MX para unb.br (ID 0xABCD), %d bytes:\n", len);
    if (len > 0)
        hexdump(buf, len);

    CHECK(len == (int)sizeof UNB_BR, "unb.br deve gerar 24 bytes");
    CHECK(len > 0 && memcmp(buf, UNB_BR, sizeof UNB_BR) == 0,
          "bytes de unb.br diferentes do esperado");
}

static void test_ponto_final_aceito(void)
{
    uint8_t a[DNS_MAX_PACKET], b[DNS_MAX_PACKET];
    int la = dns_build_query("unb.br", 0x1234, a, sizeof a);
    int lb = dns_build_query("unb.br.", 0x1234, b, sizeof b);

    CHECK(la > 0 && la == lb && memcmp(a, b, (size_t)la) == 0,
          "unb.br. deve gerar o mesmo pacote que unb.br");
}

static void test_nomes_invalidos(void)
{
    uint8_t buf[DNS_MAX_PACKET];
    char label64[80];
    char label63[80];

    memset(label64, 'a', 64);
    strcpy(label64 + 64, ".br");
    memset(label63, 'a', 63);
    strcpy(label63 + 63, ".br");

    CHECK(dns_build_query("", 1, buf, sizeof buf) == -1, "nome vazio");
    CHECK(dns_build_query(".", 1, buf, sizeof buf) == -1, "nome so com ponto");
    CHECK(dns_build_query("unb..br", 1, buf, sizeof buf) == -1, "label vazio no meio");
    CHECK(dns_build_query(".unb.br", 1, buf, sizeof buf) == -1, "label vazio no inicio");
    CHECK(dns_build_query("unb.br..", 1, buf, sizeof buf) == -1, "dois pontos no fim");
    CHECK(dns_build_query(label64, 1, buf, sizeof buf) == -1, "label com 64 caracteres");
    CHECK(dns_build_query(label63, 1, buf, sizeof buf) > 0, "label com 63 caracteres e valido");
}

static void test_nome_longo_demais(void)
{
    uint8_t buf[DNS_MAX_PACKET];
    char nome[300];
    int i;

    /* 4 labels de 63 = 4*64 + 1 = 257 bytes codificados (> 255) */
    nome[0] = '\0';
    for (i = 0; i < 4; i++) {
        size_t n = strlen(nome);
        memset(nome + n, 'a', 63);
        nome[n + 63] = (i < 3) ? '.' : '\0';
        nome[n + 64] = '\0';
    }
    CHECK(dns_build_query(nome, 1, buf, sizeof buf) == -1,
          "nome com mais de 255 bytes codificados");

    /* 3 labels de 63 + 1 de 61 = 3*64 + 62 + 1 = 255 bytes: limite exato */
    nome[3 * 64 + 61] = '\0';
    CHECK(dns_build_query(nome, 1, buf, sizeof buf) > 0,
          "nome com exatamente 255 bytes codificados e valido");
}

static void test_buffer_pequeno(void)
{
    uint8_t buf[23]; /* unb.br precisa de 24 */
    CHECK(dns_build_query("unb.br", 1, buf, sizeof buf) == -1,
          "buffer menor que o pacote deve falhar");
}

static void test_id_aleatorio(void)
{
    uint16_t primeiro = dns_random_id();
    int diferentes = 0;
    int i;

    for (i = 0; i < 20; i++)
        if (dns_random_id() != primeiro)
            diferentes++;
    CHECK(diferentes > 0, "20 IDs seguidos nao podem ser todos iguais");
}

int main(void)
{
    test_unb_br_byte_a_byte();
    test_ponto_final_aceito();
    test_nomes_invalidos();
    test_nome_longo_demais();
    test_buffer_pequeno();
    test_id_aleatorio();
    return TEST_SUMMARY("test_query");
}
