/*
 * dns_parse.c - Interpretação da resposta DNS (RFC 1035, seção 4.1).
 *
 * Lê o cabeçalho, pula a seção de pergunta, percorre a seção de resposta
 * e extrai os registros MX (TYPE 15), descomprimindo nomes (seção 4.1.4).
 * Toda leitura confere os limites do pacote: um pacote malformado vira
 * DNS_FAIL, nunca uma leitura fora da memória.
 */
#include <string.h>

#include "dns.h"

#define DNS_RCODE_NXDOMAIN 3  /* RCODE 3: o nome não existe */
#define MAX_SALTOS         64 /* limite de ponteiros seguidos (contra loops) */

/* Lê 16 bits em network byte order na posição pos; -1 se passar do fim */
static int le_u16(const uint8_t *resp, int len, int pos, uint16_t *out)
{
    if (pos < 0 || pos + 2 > len)
        return -1;
    *out = (uint16_t)((resp[pos] << 8) | resp[pos + 1]);
    return 0;
}

/*
 * Lê um nome a partir de 'pos' e o escreve em texto em 'out'
 * (ex.: "mx.exemplo.com"; o nome raiz vira "").
 *
 * Cada byte de tamanho indica um label, exceto quando os dois bits altos
 * valem 11: aí é um PONTEIRO de 2 bytes, cujo deslocamento (14 bits) é
 * ((b1 & 0x3F) << 8) | b2, contado a partir do início do pacote.
 *
 * Retorna a posição logo depois do nome NO LUGAR ORIGINAL (ou seja, logo
 * após o primeiro ponteiro, se houver), ou -1 se o nome for inválido.
 */
static int le_nome(const uint8_t *resp, int len, int pos, char *out, size_t outsize)
{
    int depois = -1; /* onde o chamador continua a leitura */
    int saltos = 0;  /* ponteiros seguidos até agora */
    int wire = 0;    /* tamanho do nome no formato de labels */
    size_t n = 0;    /* caracteres já escritos em out */

    for (;;) {
        uint8_t b;

        if (pos < 0 || pos >= len)
            return -1;
        b = resp[pos];

        if ((b & 0xC0) == 0xC0) {
            /* Ponteiro de compressão: o nome continua em outro ponto */
            int offset;
            if (pos + 1 >= len)
                return -1;
            offset = ((b & 0x3F) << 8) | resp[pos + 1];
            if (depois < 0)
                depois = pos + 2; /* só o primeiro ponteiro define o retorno */
            if (++saltos > MAX_SALTOS)
                return -1; /* ponteiros em loop */
            pos = offset;
            continue;
        }
        if ((b & 0xC0) != 0)
            return -1; /* bits 01 ou 10: tipos de label reservados */

        if (b == 0) {
            /* Label raiz: fim do nome */
            if (depois < 0)
                depois = pos + 1;
            break;
        }

        /* Label comum de b bytes (1 a 63) */
        if (pos + 1 + b > len)
            return -1;
        wire += 1 + b;
        if (wire + 1 > DNS_MAX_NAME)
            return -1; /* nome maior que 255 bytes */
        if (n > 0) {
            if (n + 1 >= outsize)
                return -1;
            out[n++] = '.';
        }
        if (n + b >= outsize)
            return -1;
        memcpy(out + n, resp + pos + 1, b);
        n += b;
        pos += 1 + b;
    }

    out[n] = '\0';
    return depois;
}

/* Ordena os MX por preferência crescente (insertion sort, estável) */
static void ordena_mx(mx_record_t *mx, int count)
{
    int i, j;
    for (i = 1; i < count; i++) {
        mx_record_t atual = mx[i];
        for (j = i - 1; j >= 0 && mx[j].preference > atual.preference; j--)
            mx[j + 1] = mx[j];
        mx[j + 1] = atual;
    }
}

dns_status_t dns_parse_mx(const uint8_t *resp, int len, uint16_t id,
                          mx_record_t *mx, int max_mx, int *count)
{
    uint16_t rid = 0, flags = 0, qdcount = 0, ancount = 0;
    char nome[DNS_MAX_NAME + 1];
    int pos, i;

    *count = 0;
    if (resp == NULL || len < DNS_HEADER_SIZE)
        return DNS_FAIL;

    /* ---- Cabeçalho (12 bytes) ---- */
    le_u16(resp, len, 0, &rid);
    le_u16(resp, len, 2, &flags);
    le_u16(resp, len, 4, &qdcount);
    le_u16(resp, len, 6, &ancount);

    if (rid != id)
        return DNS_FAIL; /* resposta de outra consulta */
    if ((flags & 0x8000) == 0)
        return DNS_FAIL; /* QR = 0: não é resposta */
    if (((flags >> 11) & 0x0F) != 0)
        return DNS_FAIL; /* OPCODE diferente de QUERY padrão */
    if ((flags & 0x000F) == DNS_RCODE_NXDOMAIN)
        return DNS_NXDOMAIN;
    if ((flags & 0x000F) != 0)
        return DNS_FAIL; /* SERVFAIL, REFUSED etc. */

    /* ---- Seção de pergunta: só pulamos (nome + QTYPE + QCLASS) ---- */
    pos = DNS_HEADER_SIZE;
    for (i = 0; i < qdcount; i++) {
        pos = le_nome(resp, len, pos, nome, sizeof nome);
        if (pos < 0 || pos + 4 > len)
            return DNS_FAIL;
        pos += 4;
    }

    /* ---- Seção de resposta: NAME, TYPE, CLASS, TTL, RDLENGTH, RDATA ---- */
    for (i = 0; i < ancount; i++) {
        uint16_t tipo = 0, classe = 0, rdlength = 0;
        int rdata;

        pos = le_nome(resp, len, pos, nome, sizeof nome); /* dono do registro */
        if (pos < 0)
            return DNS_FAIL;
        if (le_u16(resp, len, pos, &tipo) < 0 ||
            le_u16(resp, len, pos + 2, &classe) < 0 ||
            le_u16(resp, len, pos + 8, &rdlength) < 0) /* pula o TTL (4 bytes) */
            return DNS_FAIL;
        rdata = pos + 10;
        if (rdata + rdlength > len)
            return DNS_FAIL;

        /* Só interessam registros MX da classe IN; CNAME e outros são pulados */
        if (tipo == DNS_TYPE_MX && classe == DNS_CLASS_IN) {
            uint16_t preference = 0;
            int fim;

            /* RDATA do MX: PREFERENCE (2 bytes) + EXCHANGE (nome) */
            if (rdlength < 3)
                return DNS_FAIL;
            le_u16(resp, len, rdata, &preference);
            fim = le_nome(resp, len, rdata + 2, nome, sizeof nome);
            if (fim < 0 || fim > rdata + rdlength)
                return DNS_FAIL; /* nome passa do fim do RDATA */

            /* "MX 0 ." (null MX, RFC 7505) significa "não recebe e-mail": ignoramos */
            if (nome[0] != '\0' && *count < max_mx) {
                mx[*count].preference = preference;
                strcpy(mx[*count].exchange, nome);
                (*count)++;
            }
        }
        pos = rdata + rdlength; /* próximo registro */
    }

    ordena_mx(mx, *count);
    return (*count > 0) ? DNS_OK : DNS_NO_MX;
}
