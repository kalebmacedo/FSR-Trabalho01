/*
 * test_net.c - Testes do envio/recepção UDP (src/net.c), sem internet.
 *
 * Cada teste cria um "servidor DNS falso" em 127.0.0.1 numa porta livre,
 * num processo filho (fork). O servidor se comporta de um jeito específico
 * (responde, fica calado, manda ID errado...) e devolve, como código de
 * saída, quantas consultas recebeu. Assim conferimos o laço de tentativas.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

#include "dns.h"
#include "test_util.h"

enum modo {
    RESPONDE,           /* responde a 1ª consulta corretamente */
    ID_ERRADO_ANTES,    /* manda resposta com ID errado, depois a certa */
    QR_ZERO_ANTES,      /* manda pacote com QR=0, depois a resposta certa */
    OUTRA_PORTA_ANTES,  /* responde de outra porta, depois da porta certa */
    CALA_UMA_VEZ,       /* ignora a 1ª consulta, responde a 2ª */
    CALADO              /* nunca responde; só conta as consultas */
};

/* Espera um datagrama por até 'seg' segundos; devolve o tamanho ou -1 */
static int recebe(int s, uint8_t *buf, size_t n, struct sockaddr_in *de, int seg)
{
    fd_set rf;
    struct timeval tv;
    socklen_t dl = sizeof *de;

    FD_ZERO(&rf);
    FD_SET(s, &rf);
    tv.tv_sec = seg;
    tv.tv_usec = 0;
    if (select(s + 1, &rf, NULL, NULL, &tv) <= 0)
        return -1;
    return (int)recvfrom(s, buf, n, 0, (struct sockaddr *)de, &dl);
}

/* Envia 'n' bytes de 'pkt' ao cliente, trocando o bit QR e o ID se pedido */
static void envia(int s, const uint8_t *pkt, int n, const struct sockaddr_in *para,
                  int qr, uint16_t xor_id)
{
    uint8_t r[DNS_MAX_PACKET];
    memcpy(r, pkt, (size_t)n);
    r[0] ^= (uint8_t)(xor_id >> 8);
    r[1] ^= (uint8_t)(xor_id & 0xFF);
    if (qr)
        r[2] |= 0x80;
    else
        r[2] &= 0x7F;
    sendto(s, r, (size_t)n, 0, (const struct sockaddr *)para, sizeof *para);
}

/* Corpo do servidor falso (roda no processo filho) */
static int servidor(int s, enum modo m)
{
    uint8_t q[DNS_MAX_PACKET];
    struct sockaddr_in cli;
    int consultas = 0;
    int n;

    /* Encerra se ficar 3 s sem receber nada (o cliente já desistiu) */
    while ((n = recebe(s, q, sizeof q, &cli, 3)) > 0) {
        consultas++;
        switch (m) {
        case RESPONDE:
            envia(s, q, n, &cli, 1, 0);
            break;
        case ID_ERRADO_ANTES:
            envia(s, q, n, &cli, 1, 0xFFFF);
            envia(s, q, n, &cli, 1, 0);
            break;
        case QR_ZERO_ANTES:
            envia(s, q, n, &cli, 0, 0);
            envia(s, q, n, &cli, 1, 0);
            break;
        case OUTRA_PORTA_ANTES: {
            int outro = socket(AF_INET, SOCK_DGRAM, 0);
            envia(outro, q, n, &cli, 1, 0); /* mesmo conteúdo, porta de origem errada */
            close(outro);
            q[n] = 0xEE; /* a resposta legítima tem 1 byte a mais, para distinguir */
            envia(s, q, n + 1, &cli, 1, 0);
            break;
        }
        case CALA_UMA_VEZ:
            if (consultas >= 2)
                envia(s, q, n, &cli, 1, 0);
            break;
        case CALADO:
            break;
        }
        if (m != CALADO && !(m == CALA_UMA_VEZ && consultas < 2))
            break; /* já respondeu: encerra */
    }
    return consultas;
}

/*
 * Roda o cliente contra um servidor falso no modo 'm'.
 * Devolve o retorno de dns_send_and_receive_port e preenche
 * quantas consultas o servidor recebeu e quanto tempo levou.
 */
static int roda(enum modo m, int *consultas, double *segundos, uint8_t *resp, size_t rsize)
{
    uint8_t q[DNS_MAX_PACKET];
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    int s, qlen, r, status;
    uint16_t id = 0x4242;
    pid_t pid;
    struct timespec t0, t1;

    /* Servidor escuta em 127.0.0.1, numa porta escolhida pelo sistema */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    a.sin_port = 0;
    bind(s, (struct sockaddr *)&a, sizeof a);
    getsockname(s, (struct sockaddr *)&a, &al);

    pid = fork();
    if (pid == 0)
        _exit(servidor(s, m));
    close(s);

    qlen = dns_build_query("unb.br", id, q, sizeof q);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    r = dns_send_and_receive_port("127.0.0.1", ntohs(a.sin_port), q, qlen, id, resp, rsize);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    waitpid(pid, &status, 0);
    *consultas = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    *segundos = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    return r;
}

static void test_resposta_direta(void)
{
    uint8_t resp[DNS_MAX_PACKET];
    int c;
    double t;
    int r = roda(RESPONDE, &c, &t, resp, sizeof resp);

    CHECK(r == 24, "deve devolver a resposta de 24 bytes");
    CHECK(c == 1, "uma resposta imediata exige so 1 consulta");
    CHECK(r == 24 && resp[0] == 0x42 && resp[1] == 0x42 && (resp[2] & 0x80),
          "resposta devolvida deve ter o ID enviado e QR=1");
}

static void test_descarta_id_errado(void)
{
    uint8_t resp[DNS_MAX_PACKET];
    int c;
    double t;
    int r = roda(ID_ERRADO_ANTES, &c, &t, resp, sizeof resp);

    CHECK(r == 24 && resp[0] == 0x42 && resp[1] == 0x42,
          "resposta com ID errado deve ser descartada");
    CHECK(c == 1, "descartar ID errado nao deve gastar nova tentativa");
}

static void test_descarta_qr_zero(void)
{
    uint8_t resp[DNS_MAX_PACKET];
    int c;
    double t;
    int r = roda(QR_ZERO_ANTES, &c, &t, resp, sizeof resp);

    CHECK(r == 24 && (resp[2] & 0x80), "pacote com QR=0 nao e resposta");
}

static void test_descarta_outra_porta(void)
{
    uint8_t resp[DNS_MAX_PACKET];
    int c;
    double t;
    int r = roda(OUTRA_PORTA_ANTES, &c, &t, resp, sizeof resp);

    CHECK(r == 25, "resposta vinda de outra porta deve ser descartada");
}

static void test_nova_tentativa_apos_2s(void)
{
    uint8_t resp[DNS_MAX_PACKET];
    int c;
    double t;
    int r = roda(CALA_UMA_VEZ, &c, &t, resp, sizeof resp);

    CHECK(r == 24, "deve ter sucesso na 2a tentativa");
    CHECK(c == 2, "servidor deve receber exatamente 2 consultas");
    CHECK(t >= 1.9 && t < 3.0, "2a tentativa deve sair depois de ~2 s");
}

static void test_tres_tentativas_e_desiste(void)
{
    uint8_t resp[DNS_MAX_PACKET];
    int c;
    double t;
    int r = roda(CALADO, &c, &t, resp, sizeof resp);

    CHECK(r == -1, "sem resposta, deve devolver -1");
    CHECK(c == 3, "servidor deve receber exatamente 3 consultas");
    CHECK(t >= 5.9 && t < 7.0, "3 tentativas de 2 s devem levar ~6 s");
}

static void test_ip_invalido(void)
{
    uint8_t q[DNS_MAX_PACKET], resp[DNS_MAX_PACKET];
    int qlen = dns_build_query("unb.br", 1, q, sizeof q);

    CHECK(dns_send_and_receive("999.1.1.1", q, qlen, 1, resp, sizeof resp) == -1,
          "IP invalido deve devolver -1");
}

int main(void)
{
    test_ip_invalido();
    test_resposta_direta();
    test_descarta_id_errado();
    test_descarta_qr_zero();
    test_descarta_outra_porta();
    test_nova_tentativa_apos_2s();
    test_tres_tentativas_e_desiste();
    return TEST_SUMMARY("test_net");
}
