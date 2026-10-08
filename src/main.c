/*
 * main.c - Cliente DNS para registros MX.
 *
 * Uso: ./meu_cliente <dominio> <ip_servidor_dns>
 *
 * Fluxo: valida os argumentos -> monta a consulta (dns_query.c)
 *        -> envia e espera a resposta (net.c) -> interpreta (dns_parse.c)
 *        -> imprime o resultado em stdout, no formato do enunciado.
 *
 * Códigos de saída: 0 sucesso; 1 erro de uso; 2 domínio não encontrado
 * ou sem MX; 3 falha de rede ou do servidor.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dns.h"

/* Mensagem de uso: única saída que vai para stderr */
static int uso(void)
{
    fprintf(stderr, "Uso: ./meu_cliente <dominio> <ip_servidor_dns>\n");
    return 1;
}

int main(int argc, char *argv[])
{
    uint8_t consulta[DNS_MAX_PACKET];
    uint8_t resposta[DNS_RECV_BUF];
    mx_record_t mx[DNS_MAX_MX];
    char dominio[DNS_MAX_NAME + 1];
    struct in_addr ip;
    uint16_t id;
    int qlen, rlen, count, i;
    size_t len;

    /* 1º argumento: nome a resolver; 2º: IP (IPv4) do servidor DNS */
    if (argc != 3)
        return uso();
    if (inet_pton(AF_INET, argv[2], &ip) != 1)
        return uso(); /* IP inválido, ex.: 999.1.1.1 */

    /* Monta a consulta MX; -1 indica nome de domínio inválido */
    id = dns_random_id();
    qlen = dns_build_query(argv[1], id, consulta, sizeof consulta);
    if (qlen < 0)
        return uso();

    /* Nome exibido: o digitado, sem o ponto final opcional ("unb.br." -> "unb.br").
     * Cabe em 'dominio' porque dns_build_query já validou o tamanho. */
    len = strlen(argv[1]);
    if (len > 0 && argv[1][len - 1] == '.')
        len--;
    memcpy(dominio, argv[1], len);
    dominio[len] = '\0';

    /* Envia por UDP (porta 53), 2 s de espera, até 3 tentativas */
    rlen = dns_send_and_receive(argv[2], consulta, qlen, id, resposta, sizeof resposta);
    if (rlen < 0) {
        printf("Nao foi possível coletar entrada MX para %s\n", dominio);
        return 3;
    }

    /* Interpreta a resposta e imprime o resultado */
    switch (dns_parse_mx(resposta, rlen, id, mx, DNS_MAX_MX, &count)) {
    case DNS_OK:
        for (i = 0; i < count; i++)
            printf("%s <> %s\n", dominio, mx[i].exchange);
        return 0;
    case DNS_NXDOMAIN:
        printf("Dominio %s nao encontrado\n", dominio);
        return 2;
    case DNS_NO_MX:
        printf("Dominio %s nao possui entrada MX\n", dominio);
        return 2;
    case DNS_FAIL:
    default:
        printf("Nao foi possível coletar entrada MX para %s\n", dominio);
        return 3;
    }
}
