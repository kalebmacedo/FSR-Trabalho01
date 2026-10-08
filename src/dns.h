/*
 * dns.h - Interface comum do cliente DNS (consultas MX).
 *
 * Este cabeçalho é o "contrato" entre os módulos do trabalho:
 *   dns_query.c  monta a consulta byte a byte
 *   net.c        envia por UDP e espera a resposta (2 s, até 3 tentativas)
 *   dns_parse.c  interpreta a resposta e extrai os registros MX
 *   main.c       lê os argumentos, chama os módulos e imprime o resultado
 *
 * Ninguém altera este arquivo sem avisar o grupo.
 * Referências: RFC 1035, seção 4 (formato da mensagem); Kurose, seção 2.5.
 */
#ifndef DNS_H
#define DNS_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint8_t, uint16_t */

/* Parâmetros do protocolo (RFC 1035) e do enunciado */
#define DNS_PORT        53     /* porta padrão do serviço DNS (UDP 53) */
#define DNS_TYPE_MX     15     /* TYPE/QTYPE de um registro MX */
#define DNS_CLASS_IN    1      /* classe IN (Internet) */
#define DNS_FLAGS_RD    0x0100 /* flags da consulta: só o bit RD (recursão desejada) */
#define DNS_HEADER_SIZE 12     /* o cabeçalho tem sempre 12 bytes */
#define DNS_MAX_PACKET  512    /* tamanho máximo de mensagem DNS via UDP sem EDNS */
#define DNS_RECV_BUF    4096   /* buffer de recepção, com folga além dos 512 bytes */
#define DNS_MAX_NAME    255    /* tamanho máximo de um nome codificado */
#define DNS_MAX_LABEL   63     /* tamanho máximo de cada label do nome */
#define DNS_TIMEOUT_S   2      /* espera por resposta em cada tentativa, em segundos */
#define DNS_MAX_TRIES   3      /* número máximo de tentativas */
#define DNS_MAX_MX      32     /* quantos registros MX guardamos, no máximo */

/* Um registro MX extraído da resposta */
typedef struct {
    uint16_t preference;                /* menor valor = servidor preferido */
    char     exchange[DNS_MAX_NAME + 1]; /* nome do servidor de e-mail, em texto */
} mx_record_t;

/* Resultado da interpretação da resposta */
typedef enum {
    DNS_OK,       /* encontrou um ou mais MX */
    DNS_NXDOMAIN, /* RCODE 3: o domínio não existe */
    DNS_NO_MX,    /* NOERROR, mas sem registro MX */
    DNS_FAIL      /* timeout, RCODE de erro ou pacote malformado */
} dns_status_t;

/* dns_query.c: retorna o tamanho do pacote ou -1 se o nome for inválido */
int dns_build_query(const char *domain, uint16_t id, uint8_t *buf, size_t bufsize);
uint16_t dns_random_id(void);

/* net.c: retorna o tamanho da resposta ou -1 após 3 tentativas */
int dns_send_and_receive(const char *server_ip, const uint8_t *query, int qlen,
                         uint16_t id, uint8_t *resp, size_t respsize);

/* net.c: igual à anterior, mas com a porta do servidor como parâmetro.
 * O programa usa sempre a porta 53; os testes usam um servidor falso local. */
int dns_send_and_receive_port(const char *server_ip, uint16_t port,
                              const uint8_t *query, int qlen, uint16_t id,
                              uint8_t *resp, size_t respsize);

/* dns_parse.c: preenche mx[] ordenado por preferência e *count */
dns_status_t dns_parse_mx(const uint8_t *resp, int len, uint16_t id,
                          mx_record_t *mx, int max_mx, int *count);

#endif /* DNS_H */
