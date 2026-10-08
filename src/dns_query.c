/*
 * dns_query.c - Montagem da consulta DNS (RFC 1035, seção 4.1).
 *
 * O enunciado exige que o payload seja montado programaticamente,
 * por isso cada byte é escrito à mão, sem biblioteca de DNS.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dns.h"

/* Escreve um valor de 16 bits em network byte order (big-endian):
 * primeiro o byte mais significativo, depois o menos significativo. */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/*
 * Converte "unb.br" para o formato de labels: 03 u n b 02 b r 00.
 * Cada label vira [tamanho][caracteres]; o nome termina com o byte 0.
 * Aceita um ponto final opcional ("unb.br.").
 * Retorna quantos bytes escreveu em out, ou -1 se o nome for inválido.
 */
static int encode_name(const char *domain, uint8_t *out, size_t outsize)
{
    size_t len = strlen(domain);
    size_t pos = 0;   /* próxima posição livre em out */
    size_t start = 0; /* início do label atual em domain */

    /* Remove o ponto final opcional; "." sozinho ou vazio é inválido */
    if (len > 0 && domain[len - 1] == '.')
        len--;
    if (len == 0)
        return -1;

    while (start <= len) {
        /* Procura o fim do label atual (próximo ponto ou fim do nome) */
        size_t end = start;
        size_t label_len;
        while (end < len && domain[end] != '.')
            end++;
        label_len = end - start;

        /* Label vazio ("unb..br") ou maior que 63 bytes é inválido */
        if (label_len == 0 || label_len > DNS_MAX_LABEL)
            return -1;
        /* Precisa caber: byte de tamanho + label + byte 0 final */
        if (pos + 1 + label_len + 1 > DNS_MAX_NAME || pos + 1 + label_len + 1 > outsize)
            return -1;

        out[pos++] = (uint8_t)label_len;
        memcpy(out + pos, domain + start, label_len);
        pos += label_len;

        start = end + 1; /* pula o ponto */
    }

    out[pos++] = 0; /* label raiz: fim do nome */
    return (int)pos;
}

/*
 * Monta a consulta MX completa em buf:
 *   cabeçalho (12 bytes) + nome em labels + QTYPE (2) + QCLASS (2).
 * Retorna o tamanho do pacote ou -1 se o nome for inválido
 * ou se o buffer não comportar o pacote.
 */
int dns_build_query(const char *domain, uint16_t id, uint8_t *buf, size_t bufsize)
{
    int name_len;

    if (domain == NULL || buf == NULL || bufsize < DNS_HEADER_SIZE)
        return -1;

    /* Cabeçalho: valores fixados pelo enunciado */
    put_u16(buf + 0, id);           /* ID aleatório da transação */
    put_u16(buf + 2, DNS_FLAGS_RD); /* 0x0100: consulta recursiva */
    put_u16(buf + 4, 1);            /* QDCOUNT: 1 pergunta */
    put_u16(buf + 6, 0);            /* ANCOUNT */
    put_u16(buf + 8, 0);            /* NSCOUNT */
    put_u16(buf + 10, 0);           /* ARCOUNT */

    /* Seção de pergunta: QNAME */
    name_len = encode_name(domain, buf + DNS_HEADER_SIZE, bufsize - DNS_HEADER_SIZE);
    if (name_len < 0)
        return -1;

    /* QTYPE e QCLASS logo depois do nome */
    if ((size_t)(DNS_HEADER_SIZE + name_len + 4) > bufsize)
        return -1;
    put_u16(buf + DNS_HEADER_SIZE + name_len, DNS_TYPE_MX);
    put_u16(buf + DNS_HEADER_SIZE + name_len + 2, DNS_CLASS_IN);

    return DNS_HEADER_SIZE + name_len + 4;
}

/*
 * Gera o ID de 16 bits da transação.
 * Usa /dev/urandom (fonte aleatória do sistema); se não conseguir abrir,
 * cai para rand() semeado com hora e PID do processo.
 */
uint16_t dns_random_id(void)
{
    static int seeded = 0;
    uint8_t bytes[2];
    FILE *f = fopen("/dev/urandom", "rb");

    if (f != NULL) {
        size_t n = fread(bytes, 1, sizeof bytes, f);
        fclose(f);
        if (n == sizeof bytes)
            return (uint16_t)((bytes[0] << 8) | bytes[1]);
    }

    if (!seeded) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        seeded = 1;
    }
    return (uint16_t)(((rand() & 0xFF) << 8) | (rand() & 0xFF));
}
