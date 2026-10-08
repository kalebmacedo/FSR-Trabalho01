/*
 * net.c - Envio da consulta e recepção da resposta via socket UDP.
 *
 * Regras do enunciado:
 *   - transporte UDP, servidor na porta 53;
 *   - esperar 2 s pela resposta; se não chegar, nova tentativa;
 *   - no máximo 3 tentativas; depois, erro.
 * Só usamos a biblioteca de sockets (socket, sendto, select, recvfrom).
 */
#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

#include "dns.h"

/* Milissegundos desde um instante fixo (relógio monotônico,
 * que não volta para trás se alguém mudar a hora do sistema) */
static long agora_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/*
 * Confere se o datagrama recebido é a resposta da NOSSA consulta:
 *   - veio do IP e da porta do servidor consultado;
 *   - tem pelo menos o cabeçalho (12 bytes);
 *   - o ID é o mesmo que enviamos;
 *   - o bit QR vale 1 (é resposta, não consulta).
 * Qualquer outro pacote é ignorado (proteção contra respostas forjadas).
 */
static int resposta_valida(const uint8_t *resp, ssize_t n, uint16_t id,
                           const struct sockaddr_in *de, const struct sockaddr_in *servidor)
{
    if (de->sin_family != AF_INET ||
        de->sin_addr.s_addr != servidor->sin_addr.s_addr ||
        de->sin_port != servidor->sin_port)
        return 0;
    if (n < DNS_HEADER_SIZE)
        return 0;
    if (((resp[0] << 8) | resp[1]) != id)
        return 0;
    if ((resp[2] & 0x80) == 0)
        return 0;
    return 1;
}

/*
 * Espera, até o prazo 'limite_ms', por uma resposta válida.
 * Pacotes inválidos são descartados e a espera continua no tempo que resta.
 * Retorna o tamanho da resposta ou -1 se o prazo acabar.
 */
static int espera_resposta(int sock, long limite_ms, uint16_t id,
                           const struct sockaddr_in *servidor,
                           uint8_t *resp, size_t respsize)
{
    for (;;) {
        long resta = limite_ms - agora_ms();
        fd_set leitura;
        struct timeval tv;
        struct sockaddr_in de;
        socklen_t delen = sizeof de;
        ssize_t n;
        int pronto;

        if (resta <= 0)
            return -1; /* acabou o tempo desta tentativa */

        /* select() bloqueia até chegar algo no socket ou o tempo acabar */
        FD_ZERO(&leitura);
        FD_SET(sock, &leitura);
        tv.tv_sec = resta / 1000;
        tv.tv_usec = (resta % 1000) * 1000;
        pronto = select(sock + 1, &leitura, NULL, NULL, &tv);
        if (pronto < 0 && errno == EINTR)
            continue; /* interrompido por sinal: tenta de novo */
        if (pronto <= 0)
            return -1; /* timeout ou erro */

        memset(&de, 0, sizeof de);
        n = recvfrom(sock, resp, respsize, 0, (struct sockaddr *)&de, &delen);
        if (n < 0)
            continue; /* erro pontual de recepção: segue esperando */
        if (resposta_valida(resp, n, id, &de, servidor))
            return (int)n;
        /* pacote que não é a nossa resposta: descarta e continua */
    }
}

int dns_send_and_receive_port(const char *server_ip, uint16_t port,
                              const uint8_t *query, int qlen, uint16_t id,
                              uint8_t *resp, size_t respsize)
{
    struct sockaddr_in servidor;
    int sock;
    int tentativa;
    int n = -1;

    /* Endereço do servidor: IPv4 em texto -> binário (sem resolver nomes) */
    memset(&servidor, 0, sizeof servidor);
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &servidor.sin_addr) != 1)
        return -1;

    /* AF_INET = IPv4; SOCK_DGRAM = UDP */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;

    for (tentativa = 1; tentativa <= DNS_MAX_TRIES; tentativa++) {
        ssize_t enviados = sendto(sock, query, (size_t)qlen, 0,
                                  (const struct sockaddr *)&servidor, sizeof servidor);
        if (enviados != qlen)
            continue; /* falha no envio conta como tentativa perdida */

        n = espera_resposta(sock, agora_ms() + DNS_TIMEOUT_S * 1000L, id,
                            &servidor, resp, respsize);
        if (n > 0)
            break; /* chegou a resposta: não precisa de nova tentativa */
    }

    close(sock);
    return n;
}

int dns_send_and_receive(const char *server_ip, const uint8_t *query, int qlen,
                         uint16_t id, uint8_t *resp, size_t respsize)
{
    return dns_send_and_receive_port(server_ip, DNS_PORT, query, qlen, id, resp, respsize);
}
