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
        int k;

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
        /* Copia o label; bytes não imprimíveis (controle, espaço, não ASCII)
         * viram '?', para o servidor não mandar sequências de controle ao terminal */
        for (k = 0; k < b; k++) {
            uint8_t c = resp[pos + 1 + k];
            out[n++] = (c > 0x20 && c < 0x7F) ? (char)c : '?';
        }
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

/*
 * Lê um registro da seção de resposta a partir de 'pos':
 * NAME, TYPE (2), CLASS (2), TTL (4), RDLENGTH (2) e RDATA.
 * Se for um MX da classe IN, guarda-o em mx[*count].
 * Retorna a posição do próximo registro ou -1 se o registro
 * estiver incompleto ou malformado.
 */
static int le_registro(const uint8_t *resp, int len, int pos,
                       mx_record_t *mx, int max_mx, int *count)
{
    uint16_t tipo = 0, classe = 0, rdlength = 0;
    char nome[DNS_MAX_NAME + 1];
    int rdata;

    pos = le_nome(resp, len, pos, nome, sizeof nome); /* dono do registro */
    if (pos < 0)
        return -1;
    if (le_u16(resp, len, pos, &tipo) < 0 ||
        le_u16(resp, len, pos + 2, &classe) < 0 ||
        le_u16(resp, len, pos + 8, &rdlength) < 0) /* pula o TTL (4 bytes) */
        return -1;
    rdata = pos + 10;
    if (rdata + rdlength > len)
        return -1;

    /* Só interessam registros MX da classe IN; CNAME e outros são pulados */
    if (tipo == DNS_TYPE_MX && classe == DNS_CLASS_IN) {
        uint16_t preference = 0;
        int fim;

        /* RDATA do MX: PREFERENCE (2 bytes) + EXCHANGE (nome) */
        if (rdlength < 3)
            return -1;
        le_u16(resp, len, rdata, &preference);
        fim = le_nome(resp, len, rdata + 2, nome, sizeof nome);
        if (fim < 0 || fim > rdata + rdlength)
            return -1; /* nome passa do fim do RDATA */

        /* "MX 0 ." (null MX, RFC 7505) significa "não recebe e-mail": ignoramos */
        if (nome[0] != '\0' && *count < max_mx) {
            mx[*count].preference = preference;
            strcpy(mx[*count].exchange, nome);
            (*count)++;
        }
    }
    return rdata + rdlength; /* próximo registro */
}

dns_status_t dns_parse_mx(const uint8_t *resp, int len, uint16_t id,
                          mx_record_t *mx, int max_mx, int *count)
{
    uint16_t rid = 0, flags = 0, qdcount = 0, ancount = 0;
    char nome[DNS_MAX_NAME + 1];
    int pos, i, truncada;

    if (count == NULL)
        return DNS_FAIL;
    *count = 0;
    if (resp == NULL || mx == NULL || max_mx <= 0 || len < DNS_HEADER_SIZE)
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
    if (qdcount != 1)
        return DNS_FAIL; /* a resposta deve repetir a única pergunta enviada */
    if ((flags & 0x000F) == DNS_RCODE_NXDOMAIN)
        return DNS_NXDOMAIN;
    if ((flags & 0x000F) != 0)
        return DNS_FAIL; /* SERVFAIL, REFUSED etc. */
    truncada = (flags & 0x0200) != 0; /* bit TC: resposta truncada */

    /* ---- Seção de pergunta: só pulamos (nome + QTYPE + QCLASS) ---- */
    pos = DNS_HEADER_SIZE;
    for (i = 0; i < qdcount; i++) {
        uint16_t qtype = 0, qclass = 0;

        pos = le_nome(resp, len, pos, nome, sizeof nome);
        if (pos < 0 || le_u16(resp, len, pos, &qtype) < 0 ||
            le_u16(resp, len, pos + 2, &qclass) < 0)
            return DNS_FAIL;
        if (qtype != DNS_TYPE_MX || qclass != DNS_CLASS_IN)
            return DNS_FAIL;
        pos += 4;
    }

    /* ---- Seção de resposta: um registro por vez ---- */
    for (i = 0; i < ancount; i++) {
        pos = le_registro(resp, len, pos, mx, max_mx, count);
        if (pos < 0)
            break; /* registro incompleto ou malformado */
    }

    /* Sem o bit TC, um registro incompleto indica pacote malformado.
     * Com TC = 1, o servidor avisou que cortou a resposta (não coube
     * em 512 bytes): usamos os MX completos que chegaram antes do corte. */
    if (i < ancount && !truncada)
        return DNS_FAIL;

    ordena_mx(mx, *count);
    if (*count > 0)
        return DNS_OK;
    /* Resposta cortada sem nenhum MX completo não prova que o domínio
     * não tem MX: tratamos como falha na coleta */
    return truncada ? DNS_FAIL : DNS_NO_MX;
}
