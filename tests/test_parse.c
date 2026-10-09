/*
 * test_parse.c - Testes da interpretação da resposta (src/dns_parse.c).
 *
 * As respostas são montadas à mão com pequenas funções auxiliares,
 * seguindo a RFC 1035, seção 4.1 (inclusive compressão de nomes, 4.1.4).
 */
#include <string.h>

#include "dns.h"
#include "test_util.h"

#define ID 0x1234

/* ---------- Montador de pacotes de teste ---------- */

typedef struct {
    uint8_t b[1024];
    int n;
} pkt_t;

static void u8(pkt_t *p, int v) { p->b[p->n++] = (uint8_t)v; }
static void u16(pkt_t *p, int v) { u8(p, (v >> 8) & 0xFF); u8(p, v & 0xFF); }

/* Nome sem compressão: "a.b" -> 01 a 01 b 00 */
static void nome(pkt_t *p, const char *s)
{
    while (*s) {
        const char *fim = strchr(s, '.');
        int len = fim ? (int)(fim - s) : (int)strlen(s);
        u8(p, len);
        memcpy(p->b + p->n, s, (size_t)len);
        p->n += len;
        s += len;
        if (*s == '.')
            s++;
    }
    u8(p, 0);
}

/* Só os labels, sem o 0 final (para terminar com um ponteiro) */
static void labels(pkt_t *p, const char *s)
{
    nome(p, s);
    p->n--; /* remove o 0 final */
}

/* Ponteiro de compressão para o deslocamento 'off' */
static void ptr(pkt_t *p, int off) { u16(p, 0xC000 | off); }

static void cabecalho(pkt_t *p, int id, int flags, int qd, int an)
{
    p->n = 0;
    u16(p, id);
    u16(p, flags);
    u16(p, qd);
    u16(p, an);
    u16(p, 0); /* NSCOUNT */
    u16(p, 0); /* ARCOUNT */
}

/* Seção de pergunta: o nome fica no deslocamento 12 */
static void pergunta(pkt_t *p, const char *s)
{
    nome(p, s);
    u16(p, DNS_TYPE_MX);
    u16(p, DNS_CLASS_IN);
}

/* Começa um registro de resposta cujo dono aponta para o nome da pergunta.
 * Devolve onde fica o RDLENGTH, para ser preenchido por fim_rr(). */
static int inicio_rr(pkt_t *p, int tipo)
{
    int pos;
    ptr(p, 12);
    u16(p, tipo);
    u16(p, DNS_CLASS_IN);
    u16(p, 0);
    u16(p, 3600); /* TTL */
    pos = p->n;
    u16(p, 0);    /* RDLENGTH provisório */
    return pos;
}

static void fim_rr(pkt_t *p, int pos_rdlen)
{
    int rdlen = p->n - (pos_rdlen + 2);
    p->b[pos_rdlen] = (uint8_t)(rdlen >> 8);
    p->b[pos_rdlen + 1] = (uint8_t)(rdlen & 0xFF);
}

/* Resposta típica de unb.br com 1 MX (dono comprimido com ponteiro) */
static void resposta_unb(pkt_t *p)
{
    int r;
    cabecalho(p, ID, 0x8180, 1, 1);
    pergunta(p, "unb.br");
    r = inicio_rr(p, DNS_TYPE_MX);
    u16(p, 0);
    nome(p, "unb-br.mail.protection.outlook.com");
    fim_rr(p, r);
}

/* ---------- Testes ---------- */

static void test_um_mx(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count = -1;
    dns_status_t st;

    resposta_unb(&p);
    st = dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count);

    CHECK(st == DNS_OK, "unb.br deve dar DNS_OK");
    CHECK(count == 1, "unb.br tem 1 MX");
    CHECK(count == 1 && strcmp(mx[0].exchange, "unb-br.mail.protection.outlook.com") == 0,
          "nome do servidor de e-mail de unb.br");
    CHECK(count == 1 && mx[0].preference == 0, "preferencia 0");
}

static void test_varios_mx_ordenados_e_comprimidos(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count = -1;
    int r, base;
    int prefs[] = {30, 5, 20, 40, 10};
    const char *esperado[] = {
        "gmail-smtp-in.l.google.com",
        "alt1.gmail-smtp-in.l.google.com",
        "alt2.gmail-smtp-in.l.google.com",
        "alt3.gmail-smtp-in.l.google.com",
        "alt4.gmail-smtp-in.l.google.com"
    };
    int i;

    cabecalho(&p, ID, 0x8180, 1, 5);
    pergunta(&p, "gmail.com");

    /* 1º MX (pref 30): alt3 + nome completo; guarda onde começa "gmail-smtp-in" */
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, prefs[0]);
    labels(&p, "alt3");
    base = p.n;
    labels(&p, "gmail-smtp-in.l.google");
    ptr(&p, 12 + 6); /* "com" da pergunta: 05 gmail | 03 com */
    fim_rr(&p, r);

    /* 2º MX (pref 5): só um ponteiro para "gmail-smtp-in..." */
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, prefs[1]);
    ptr(&p, base);
    fim_rr(&p, r);

    /* 3º, 4º e 5º: "altN" + ponteiro */
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, prefs[2]);
    labels(&p, "alt2");
    ptr(&p, base);
    fim_rr(&p, r);

    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, prefs[3]);
    labels(&p, "alt4");
    ptr(&p, base);
    fim_rr(&p, r);

    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, prefs[4]);
    labels(&p, "alt1");
    ptr(&p, base);
    fim_rr(&p, r);

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_OK, "gmail deve dar DNS_OK");
    CHECK(count == 5, "gmail tem 5 MX");
    for (i = 0; i < 5 && count == 5; i++)
        CHECK(strcmp(mx[i].exchange, esperado[i]) == 0,
              "MX fora de ordem de preferencia ou nome descomprimido errado");
    CHECK(count == 5 && mx[0].preference == 5 && mx[4].preference == 40,
          "preferencias ordenadas de forma crescente");
}

static void test_cname_sem_mx(void)
{
    /* fga.unb.br: NOERROR com só um CNAME para fcte.unb.br (visto com dig) */
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count = -1;
    int r;

    cabecalho(&p, ID, 0x8180, 1, 1);
    pergunta(&p, "fga.unb.br");
    r = inicio_rr(&p, 5); /* TYPE 5 = CNAME */
    labels(&p, "fcte");
    ptr(&p, 12 + 4); /* "unb.br" da pergunta: 03 fga | 03 unb ... */
    fim_rr(&p, r);

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_NO_MX,
          "CNAME sem MX deve dar DNS_NO_MX");
    CHECK(count == 0, "nenhum MX contado");
}

static void test_cname_seguido_de_mx(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count = -1;
    int r;

    cabecalho(&p, ID, 0x8180, 1, 2);
    pergunta(&p, "mail.exemplo.com");
    r = inicio_rr(&p, 5);
    nome(&p, "real.exemplo.com");
    fim_rr(&p, r);
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, 10);
    nome(&p, "mx.exemplo.com");
    fim_rr(&p, r);

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_OK,
          "CNAME seguido de MX deve dar DNS_OK");
    CHECK(count == 1 && strcmp(mx[0].exchange, "mx.exemplo.com") == 0,
          "o CNAME deve ser pulado e o MX extraido");
}

static void test_rcodes(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count;

    cabecalho(&p, ID, 0x8183, 1, 0); /* RCODE 3 */
    pergunta(&p, "imagdaskdasdasj.br");
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_NXDOMAIN,
          "RCODE 3 deve dar DNS_NXDOMAIN");

    cabecalho(&p, ID, 0x8182, 1, 0); /* RCODE 2 = SERVFAIL */
    pergunta(&p, "unb.br");
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "SERVFAIL deve dar DNS_FAIL");

    cabecalho(&p, ID, 0x8185, 1, 0); /* RCODE 5 = REFUSED */
    pergunta(&p, "unb.br");
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "REFUSED deve dar DNS_FAIL");

    cabecalho(&p, ID, 0x8180, 1, 0); /* NOERROR sem respostas */
    pergunta(&p, "unb.br");
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_NO_MX,
          "NOERROR sem respostas deve dar DNS_NO_MX");
}

static void test_cabecalho_invalido(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count;

    resposta_unb(&p);
    CHECK(dns_parse_mx(p.b, p.n, ID + 1, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "ID diferente deve dar DNS_FAIL");

    p.b[2] &= 0x7F; /* QR = 0: não é resposta */
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "QR=0 deve dar DNS_FAIL");

    CHECK(dns_parse_mx(p.b, 11, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "pacote menor que o cabecalho deve dar DNS_FAIL");
}

static void test_pergunta_invalida(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count;

    cabecalho(&p, ID, 0x8180, 0, 0);
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "resposta deve repetir exatamente uma pergunta");

    cabecalho(&p, ID, 0x8180, 1, 0);
    pergunta(&p, "unb.br");
    p.b[p.n - 3] = 1; /* QTYPE A em vez de MX */
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "QTYPE da resposta deve ser MX");

    cabecalho(&p, ID, 0x8180, 1, 0);
    pergunta(&p, "unb.br");
    p.b[p.n - 1] = 3; /* QCLASS CH em vez de IN */
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "QCLASS da resposta deve ser IN");
}

static void test_todo_corte_do_pacote_falha(void)
{
    /* Corta a resposta de unb.br em todos os tamanhos possíveis:
     * nenhum corte pode ler fora do buffer nem dar DNS_OK */
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count, len, falhas = 0;

    resposta_unb(&p);
    for (len = 0; len < p.n; len++)
        if (dns_parse_mx(p.b, len, ID, mx, DNS_MAX_MX, &count) != DNS_FAIL)
            falhas++;
    CHECK(falhas == 0, "pacote cortado deve sempre dar DNS_FAIL");
}

static void test_ponteiro_em_loop(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count, r, aqui;

    cabecalho(&p, ID, 0x8180, 1, 1);
    pergunta(&p, "unb.br");
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, 0);
    aqui = p.n;
    ptr(&p, aqui); /* ponteiro que aponta para si mesmo */
    fim_rr(&p, r);

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "ponteiro em loop deve dar DNS_FAIL");
}

static void test_ponteiro_para_fora(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count, r;

    cabecalho(&p, ID, 0x8180, 1, 1);
    pergunta(&p, "unb.br");
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, 0);
    ptr(&p, 0x3FFF); /* bem além do fim do pacote */
    fim_rr(&p, r);

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "ponteiro para fora do pacote deve dar DNS_FAIL");
}

static void test_offset_do_ponteiro_usa_14_bits(void)
{
    /* Ponteiro 0xC1 0x00 = deslocamento 0x100 (256): usa os 6 bits baixos
     * do 1º byte + o 2º byte. Colocamos o nome do servidor nesse ponto. */
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count, r;

    cabecalho(&p, ID, 0x8180, 1, 1);
    pergunta(&p, "unb.br");
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, 7);
    ptr(&p, 0x100);
    fim_rr(&p, r);
    while (p.n < 0x100)
        u8(&p, 0); /* enchimento até o deslocamento 256 */
    nome(&p, "longe.exemplo.com");

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_OK,
          "ponteiro com deslocamento >= 256 deve funcionar");
    CHECK(count == 1 && strcmp(mx[0].exchange, "longe.exemplo.com") == 0,
          "deslocamento = ((b1 & 0x3F) << 8) | b2");
}

static void test_label_reservado(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count, r;

    cabecalho(&p, ID, 0x8180, 1, 1);
    pergunta(&p, "unb.br");
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, 0);
    u8(&p, 0x41); /* bits 01: tipo de label reservado */
    u8(&p, 'x');
    u8(&p, 0);
    fim_rr(&p, r);

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "label com bits 01 ou 10 deve dar DNS_FAIL");
}

static void test_nome_alem_do_rdlength(void)
{
    /* RDLENGTH diz 3 bytes, mas o nome continua depois disso */
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count, r;

    resposta_unb(&p);
    r = 12 + 12 + 10; /* cabeçalho + pergunta (8+4) + dono/tipo/classe/TTL */
    p.b[r] = 0;
    p.b[r + 1] = 3;
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_FAIL,
          "nome do MX que passa do RDLENGTH deve dar DNS_FAIL");
}

static void test_null_mx(void)
{
    /* RFC 7505: "MX 0 ." indica que o domínio não recebe e-mail */
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count = -1, r;

    cabecalho(&p, ID, 0x8180, 1, 1);
    pergunta(&p, "exemplo.com");
    r = inicio_rr(&p, DNS_TYPE_MX);
    u16(&p, 0);
    u8(&p, 0); /* nome raiz "." */
    fim_rr(&p, r);

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_NO_MX,
          "null MX deve dar DNS_NO_MX");
}

static void test_respeita_max_mx(void)
{
    pkt_t p;
    mx_record_t mx[3];
    int count = -1, r, i;

    cabecalho(&p, ID, 0x8180, 1, 3);
    pergunta(&p, "exemplo.com");
    for (i = 0; i < 3; i++) {
        r = inicio_rr(&p, DNS_TYPE_MX);
        u16(&p, 30 - i * 10);
        nome(&p, "mx.exemplo.com");
        fim_rr(&p, r);
    }
    mx[2].preference = 0xBEEF; /* sentinela: não pode ser sobrescrita */

    CHECK(dns_parse_mx(p.b, p.n, ID, mx, 2, &count) == DNS_OK, "deve dar DNS_OK");
    CHECK(count == 2, "count nao passa de max_mx");
    CHECK(mx[2].preference == 0xBEEF, "nao escreve alem de max_mx");
}

static void test_truncado_usa_o_que_veio(void)
{
    pkt_t p;
    mx_record_t mx[DNS_MAX_MX];
    int count = -1;

    resposta_unb(&p);
    p.b[2] |= 0x02; /* bit TC (resposta truncada) */
    CHECK(dns_parse_mx(p.b, p.n, ID, mx, DNS_MAX_MX, &count) == DNS_OK,
          "com TC=1, os MX completos que vieram devem ser usados");
}

int main(void)
{
    test_um_mx();
    test_varios_mx_ordenados_e_comprimidos();
    test_cname_sem_mx();
    test_cname_seguido_de_mx();
    test_rcodes();
    test_cabecalho_invalido();
    test_pergunta_invalida();
    test_todo_corte_do_pacote_falha();
    test_ponteiro_em_loop();
    test_ponteiro_para_fora();
    test_offset_do_ponteiro_usa_14_bits();
    test_label_reservado();
    test_nome_alem_do_rdlength();
    test_null_mx();
    test_respeita_max_mx();
    test_truncado_usa_o_que_veio();
    return TEST_SUMMARY("test_parse");
}
