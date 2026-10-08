# FRC Trabalho 01: Cliente DNS (MX) em C

Oct 5, 2026 · @kaleb de souza macedo

> Transcrição em Markdown do plano original (`docs/FRC Trabalho 01 Cliente DNS (MX) em C.pdf`).
> A seção final, "Revisão do Claude", não faz parte do plano original: ela lista os ajustes feitos sobre o plano.

## Contexto e premissas

Vamos implementar em **C** um cliente DNS que consulta registros **MX** via UDP, entregue até **13/10/2026** por um grupo de 4 pessoas.

- **Disciplina:** Fundamentos de Redes de Computadores, Prof. Tiago Alves, UnB Gama.
- **Grupo:** 4 pessoas. O professor avisou em sala que o enunciado errou ao falar em duplas.
- **Linguagem:** C, porque vale 100% da nota (C++ e Go valem 95%; linguagens interpretadas e Java, 90%).
- **Nota:** 80% código-fonte, 20% documentação.
- **Risco que anula tudo:** código com erro de compilação, ou código parecido com o de outro grupo (plágio).
- **Referências:** RFC 1034 e RFC 1035 (seção 4, formato da mensagem), Kurose seção 2.5 (DNS) e *Advanced Linux Programming* seção 5.5 (sockets).

## Requisitos do enunciado

Todos os itens abaixo vêm literalmente do enunciado e são obrigatórios.

| Item | Requisito |
|---|---|
| Uso | `./meu_cliente <dominio> <ip_servidor_dns>` |
| Transporte | UDP, porta 53, apenas biblioteca de socket UDP |
| Proibido | Bibliotecas de comunicação/DNS (`res_query`, `getaddrinfo` para resolver, libs de terceiros) |
| Montagem da consulta | Obrigatoriamente byte a byte, pelo programa |
| Parsing da resposta | Pode usar funções auxiliares para manipular campos |
| Transaction ID | Número aleatório de 16 bits |
| Flags | `0x0100` (consulta recursiva) |
| QDCOUNT | `0x0001` |
| ANCOUNT, NSCOUNT, ARCOUNT | `0x0000` |
| Pergunta | Nome do argumento 1, Type MX (15), Class IN (1) |
| Timeout | Aguardar 2 s pela resposta; se não chegar, nova tentativa |
| Tentativas | Até 3; depois, mensagem de erro |
| Saída de sucesso | `nome_dominio <> nome_servidor_email` em stdout |
| Entrega | ZIP no Sigaa, só fontes e documentação, sem executáveis |
| Documentação | Markdown ou PDF: SO usado, ambiente, como compilar, como executar, telas, limitações |

Exemplos de interação do enunciado:

```
$ ./meu_cliente unb.br 8.8.8.8
unb.br <> unb-br.mail.protection.outlook.com

$ ./meu_cliente imagdaskdasdasj.br 1.1.1.1
Dominio imagdaskdasdasj.br nao encontrado

$ ./meu_cliente fga.unb.br 8.8.8.8
Dominio fga.unb.br nao possui entrada MX

$ ./meu_cliente unb.br 1.2.3.4
Nao foi possível coletar entrada MX para unb.br
```

## Decisões de interpretação

O enunciado tem lacunas e o professor não as esclareceu, então adotamos as decisões abaixo e as registramos no README.

| Ponto ambíguo | Decisão |
|---|---|
| "Receberá entradas do teclado" | Entrada por argumentos de linha de comando, como nos exemplos |
| Frase "Caso o nome não exista," incompleta | RCODE 3 (NXDOMAIN) imprime `Dominio X nao encontrado` |
| Vários registros MX | Imprimir todos, um por linha, ordenados por preferência crescente |
| Texto das mensagens | Copiar caractere por caractere do enunciado, incluindo o acento em "possível" |
| SERVFAIL, REFUSED e outros RCODEs | `Nao foi possível coletar entrada MX para X`, sem nova tentativa |
| ID entre tentativas | Mesmo ID nas 3 tentativas; respostas com outro ID são descartadas |
| Resposta truncada (bit TC) | Usar o que veio; sem fallback para TCP (limitação documentada) |
| Nome do ZIP com 4 pessoas | Estender o padrão: `nome_sobrenome_matricula` x4 + `_trab01.zip` |
| Executável | Makefile gera o binário `meu_cliente`, igual aos exemplos |
| Códigos de saída | 0 sucesso; 1 erro de uso; 2 não encontrado/sem MX; 3 falha de rede ou servidor |

O parágrafo de introdução fala em "Sistemas Operacionais", provavelmente reaproveitado de outra disciplina, e não altera o trabalho.

## Funcionamento técnico

O programa monta um pacote de 12 bytes de cabeçalho mais a pergunta, envia por UDP, espera até 2 s, repete até 3 vezes e interpreta a resposta.

### 1. Montagem da consulta

Cabeçalho (12 bytes, todos os campos em network byte order com `htons`):

| Campo | Bytes | Valor |
|---|---|---|
| ID | 2 | aleatório (`/dev/urandom`) |
| Flags | 2 | `0x0100` |
| QDCOUNT | 2 | `0x0001` |
| ANCOUNT | 2 | `0x0000` |
| NSCOUNT | 2 | `0x0000` |
| ARCOUNT | 2 | `0x0000` |

Pergunta: o nome em labels (tamanho + caracteres, terminando em `00`), depois QTYPE e QCLASS. Exemplo para `unb.br`:

```
03 75 6E 62 02 62 72 00 | 00 0F | 00 01
 3  u  n  b  2  b  r fim    MX      IN
```

Validações: cada label com 1 a 63 bytes, nome codificado com no máximo 255 bytes, sem labels vazios (ex.: `unb..br`). Um ponto final opcional (`unb.br.`) é aceito.

### 2. Envio e tentativas

1. `inet_pton` valida o IP do servidor.
2. `socket(AF_INET, SOCK_DGRAM, 0)`.
3. Para cada tentativa (1 a 3): `sendto` para porta 53, depois `select()` com 2 s.
4. Se chegar pacote: `recvfrom` (buffer de 512 bytes ou mais) e checagem de ID e QR. ID diferente é descartado e a espera continua no tempo que resta.
5. Após 3 tentativas sem resposta válida: mensagem de falha.

### 3. Interpretação da resposta

1. Ler o cabeçalho: ID igual ao enviado, bit QR = 1, RCODE (4 bits menos significativos das flags).
2. RCODE 3: domínio não encontrado. RCODE diferente de 0 e 3: falha.
3. Pular a seção de pergunta (nome + 4 bytes).
4. Percorrer ANCOUNT registros: nome, TYPE (2), CLASS (2), TTL (4), RDLENGTH (2), RDATA.
5. Só registros com TYPE = 15 interessam; CNAME e outros são pulados usando RDLENGTH.
6. RDATA do MX: PREFERENCE (2 bytes) + EXCHANGE (nome, possivelmente comprimido).
7. NOERROR sem nenhum MX: domínio sem entrada MX.

### 4. Compressão de nomes (ponto mais delicado)

Um byte com os dois bits altos ligados (`(b & 0xC0) == 0xC0`) é um ponteiro: os 14 bits seguintes são o offset, a partir do início do pacote, de onde o nome continua.

- O leitor de nomes deve seguir ponteiros, mas devolver ao chamador a posição logo após o primeiro ponteiro.
- Proteção contra loop: limitar o número de saltos (ex.: 64) e o tamanho do nome.
- Toda leitura checa os limites do buffer recebido; um pacote malformado vira falha, nunca leitura fora da memória.

## Casos e mensagens de saída

Cada situação produz exatamente uma das mensagens abaixo, copiadas do enunciado.

| Situação | Saída | Código de saída |
|---|---|---|
| Um ou mais MX encontrados | `unb.br <> unb-br.mail.protection.outlook.com` (uma linha por MX) | 0 |
| RCODE 3 (NXDOMAIN) | `Dominio X nao encontrado` | 2 |
| NOERROR sem registro MX | `Dominio X nao possui entrada MX` | 2 |
| 3 timeouts, RCODE de erro ou resposta malformada | `Nao foi possível coletar entrada MX para X` | 3 |
| Argumentos faltando, IP inválido ou nome inválido | `Uso: ./meu_cliente <dominio> <ip_servidor_dns>` (em stderr) | 1 |

As mensagens de resultado vão para stdout, como pede o enunciado; só a mensagem de uso vai para stderr.

## Ambiente de desenvolvimento

Desenvolver no macOS é possível, mas todo teste final deve rodar em Linux com GCC, onde o professor provavelmente compila.

- No Mac, `gcc` é um apelido do clang, que aceita coisas que o GCC rejeita e vice-versa.
- Opção mais rápida para Linux: Docker Desktop ou OrbStack.

```
docker run -it --rm -v "$PWD":/src -w /src gcc:latest bash
make clean && make
./meu_cliente unb.br 8.8.8.8
```

- Alternativas: VM Ubuntu com Multipass ou UTM, laboratório da FGA, ou o computador de alguém do grupo com Linux.
- Flags de compilação: `-Wall -Wextra -std=c99 -pedantic`. Meta: zero warnings.
- Usar `#define _POSIX_C_SOURCE 200112L` no topo dos `.c` que usam sockets, para compilar com `-std=c99` no Linux.
- Proibido para portabilidade: `arc4random` (não existe em glibc antigo). O ID aleatório vem de `/dev/urandom`, com `srand(time ^ getpid)` como reserva.
- Ferramentas de apoio: `dig unb.br MX @8.8.8.8` para comparar resultados e Wireshark com filtro `dns` para ver o pacote.

## Estrutura do projeto

O código fica dividido em 4 módulos com interfaces definidas em `dns.h`, para que cada pessoa trabalhe em paralelo.

```
trab01/
├── Makefile        # gera ./meu_cliente; alvos all e clean
├── README.md       # documentação (20% da nota)
└── src/
    ├── dns.h       # constantes, structs e protótipos
    ├── main.c      # argumentos, fluxo principal, mensagens
    ├── dns_query.c # montagem da consulta
    ├── dns_parse.c # parsing da resposta e descompressão
    └── net.c       # socket UDP, timeout e 3 tentativas
```

Interfaces sugeridas para `dns.h` (podem ser ajustadas, mas combinadas antes de codar):

```c
#define DNS_PORT        53
#define DNS_TYPE_MX     15
#define DNS_CLASS_IN    1
#define DNS_MAX_PACKET  512
#define DNS_MAX_NAME    255
#define DNS_TIMEOUT_S   2
#define DNS_MAX_TRIES   3
#define DNS_MAX_MX      32

typedef struct {
    uint16_t preference;
    char     exchange[DNS_MAX_NAME + 1];
} mx_record_t;

typedef enum {
    DNS_OK,         /* encontrou MX */
    DNS_NXDOMAIN,   /* RCODE 3 */
    DNS_NO_MX,      /* NOERROR sem MX */
    DNS_FAIL        /* timeout, RCODE de erro ou pacote malformado */
} dns_status_t;

/* dns_query.c: retorna tamanho do pacote ou -1 se o nome for inválido */
int dns_build_query(const char *domain, uint16_t id, uint8_t *buf, size_t bufsize);
uint16_t dns_random_id(void);

/* net.c: retorna tamanho da resposta ou -1 após 3 tentativas */
int dns_send_and_receive(const char *server_ip, const uint8_t *query, int qlen,
                         uint16_t id, uint8_t *resp, size_t respsize);

/* dns_parse.c: preenche mx[] ordenado por preferência e *count */
dns_status_t dns_parse_mx(const uint8_t *resp, int len, uint16_t id,
                          mx_record_t *mx, int max_mx, int *count);
```

## Divisão de tarefas

Cada pessoa é dona de um módulo, e todas revisam o código de pelo menos uma outra antes da entrega.

| Pessoa | Módulo | Entregas |
|---|---|---|
| A | `dns_query.c` | Codificação em labels, validações de nome, cabeçalho, ID aleatório |
| B | `dns_parse.c` | Validação da resposta, RCODE, pular pergunta, descompressão de nomes, extração e ordenação dos MX |
| C | `net.c`, `main.c`, `Makefile` | Socket, `select` com 2 s, 3 tentativas, validação de argumentos, integração |
| D | `README.md`, testes | Plano de testes, comparação com `dig`, capturas do Wireshark, empacotamento do ZIP |

Regras do repositório:

- GitHub **privado**, para evitar cópia por outros grupos.
- Uma branch por módulo, merge na `main` só depois de compilar sem warnings.
- Ninguém altera `dns.h` sem avisar o grupo.

## Cronograma

A meta é ter o código pronto em 09/10 e usar os dias seguintes só para testes, documentação e entrega.

| Data | Atividade |
|---|---|
| Seg 05 e Ter 06/10 | Estudo (Kurose 2.5, RFC 1035 seção 4, ALP 5.5). Ambiente Linux montado. Repositório criado. `dns.h` fechado |
| Qua 07 e Qui 08/10 | Implementação dos módulos em paralelo |
| Sex 09/10 | Integração e primeiros testes de ponta a ponta |
| Sáb 10 e Dom 11/10 | Bateria de testes em Linux, casos de borda, conferência no Wireshark |
| Seg 12/10 | README completo, revisão cruzada, teste do ZIP compilando do zero |
| Ter 13/10 | Entrega no Sigaa cedo. Confirmar o horário limite |

## Plano de testes

Cada caso é comparado com `dig <dominio> MX @<servidor>` e executado em Linux.

| # | Comando | Resultado esperado |
|---|---|---|
| 1 | `./meu_cliente unb.br 8.8.8.8` | Uma linha `unb.br <> ...` igual ao `dig` |
| 2 | `./meu_cliente gmail.com 8.8.8.8` | Várias linhas, ordenadas por preferência (testa compressão) |
| 3 | `./meu_cliente imagdaskdasdasj.br 1.1.1.1` | `Dominio imagdaskdasdasj.br nao encontrado` |
| 4 | `./meu_cliente fga.unb.br 8.8.8.8` | `Dominio fga.unb.br nao possui entrada MX` (confirmar com `dig` antes) |
| 5 | `./meu_cliente unb.br 1.2.3.4` | `Nao foi possível coletar entrada MX para unb.br`, após cerca de 6 s |
| 6 | `./meu_cliente unb.br 1.1.1.1` | Mesmo resultado do caso 1 |
| 7 | `./meu_cliente unb.br` | Mensagem de uso, código 1 |
| 8 | `./meu_cliente unb.br 999.1.1.1` | Mensagem de uso, código 1 |
| 9 | Label com 64 caracteres | Mensagem de uso, código 1 |
| 10 | `./meu_cliente unb.br. 8.8.8.8` | Mesmo resultado do caso 1 |

Verificações extras:

- Medir o caso 5 com `time` para confirmar os 3 x 2 s.
- No Wireshark, conferir ID, flags `0x0100`, contadores e Type MX / Class IN da consulta.
- A rede da UnB pode bloquear DNS externo (o próprio enunciado avisa); testar também em casa.
- Opcional: rodar com `valgrind` ou `-fsanitize=address,undefined` para pegar leituras fora do buffer.

## Documentação (README.md)

O README vale 20% da nota e precisa cobrir os 6 itens pedidos, mais as decisões de interpretação.

1. **Integrantes:** nome e matrícula dos 4.
2. **Sistema operacional:** onde foi desenvolvido (macOS) e onde foi testado (distribuição Linux + versão do GCC).
3. **Ambiente de desenvolvimento:** VS Code, GCC, Make, Docker ou VM, Git.
4. **Como compilar:** `make` (e `make clean`).
5. **Como executar:** `./meu_cliente <dominio> <ip_servidor_dns>`.
6. **Telas:** saída real de cada caso do plano de testes; opcionalmente prints do Wireshark.
7. **Limitações conhecidas:** só IPv4; só consultas MX; sem fallback para TCP em respostas truncadas; respostas limitadas a 512 bytes (sem EDNS); mensagens sem acentuação, exceto onde o enunciado usa.
8. **Decisões de interpretação:** a tabela desta seção do plano, resumida.

## Checklist de entrega

- [ ] Compila sem erros e sem warnings em Linux, a partir de um clone limpo
- [ ] Nenhuma biblioteca de DNS ou de comunicação além de sockets
- [ ] Consulta montada byte a byte, conferida no Wireshark
- [ ] Todos os 10 casos do plano de testes passando
- [ ] Mensagens idênticas às do enunciado, caractere por caractere
- [ ] README com os 8 itens
- [ ] ZIP só com `Makefile`, `src/` e `README.md` (sem binário, sem `.o`, sem `.git`)
- [ ] Nome do ZIP: `nome_sobrenome_matricula` dos 4 + `_trab01.zip`
- [ ] Entregue no Sigaa antes do horário limite de 13/10

## Instruções para o Claude Code

Exporte este documento em Markdown, salve como `PLANO.md` na raiz do repositório e cole o prompt abaixo no Claude Code.

```
Leia PLANO.md inteiro antes de qualquer coisa. Ele é a especificação do trabalho.

Regras inegociáveis:
- Linguagem C (C99), compilável com: gcc -Wall -Wextra -std=c99 -pedantic, zero warnings.
- Apenas sockets UDP (sys/socket.h, netinet/in.h, arpa/inet.h, sys/select.h).
  Proibido: res_query, getaddrinfo/gethostbyname para resolver, qualquer lib de DNS.
- O pacote de consulta deve ser montado byte a byte.
- Mensagens de saída idênticas às da seção "Casos e mensagens de saída".
- Código portável macOS/Linux: nada de arc4random; ID aleatório via /dev/urandom.
- Toda leitura do pacote de resposta deve checar limites do buffer.

Ordem de trabalho:
1. Criar a estrutura de pastas, o Makefile (binário meu_cliente) e o src/dns.h
   conforme a seção "Estrutura do projeto". Parar e me mostrar.
2. Implementar dns_query.c e um teste que imprime o pacote em hexadecimal
   para unb.br. Parar e me mostrar.
3. Implementar net.c (select com 2 s, 3 tentativas, descartar ID diferente).
4. Implementar dns_parse.c com descompressão de nomes e proteção contra loop.
5. Implementar main.c e rodar os casos do "Plano de testes".

Explique cada trecho com comentários curtos em português, pois vamos
apresentar e defender o código.
```

Dicas de uso:

- Peça uma etapa por vez e leia o código antes de seguir; o grupo precisa entender tudo para defender o trabalho.
- Antes de cada commit, rode `make clean && make` dentro do container Linux.
- Cada membro do grupo deve conseguir explicar a descompressão de nomes e o laço de tentativas, que são as partes mais prováveis de perguntas do professor.

---

## Revisão do Claude (05/10/2026): ajustes adotados na implementação (08/10/2026)

O plano cobre todos os requisitos obrigatórios do enunciado. Os pontos abaixo corrigem ou completam o plano; todos foram implementados, testados e documentados no README.

1. **Ponteiro de compressão (correção técnica da seção 4).** O offset não são "os 14 bits seguintes" ao byte: são os 6 bits baixos desse byte mais os 8 bits do byte seguinte, ou seja, `offset = ((b & 0x3F) << 8) | b2`. Labels cujo primeiro byte começa com `01` ou `10` (`0x40`, `0x80`) são reservados e devem ser tratados como pacote malformado.
2. **Caso 4 (`fga.unb.br`) confirmado com `dig` em 05/10/2026.** A resposta é NOERROR com um único registro **CNAME** (`fga.unb.br → fcte.unb.br`) e nenhum MX. Um parser que trate a primeira resposta como MX imprime lixo; por isso o item 5 da seção 3 (pular tudo que não for TYPE 15) é obrigatório.
3. **Origem da resposta.** Além de ID e QR, descartar datagramas cujo remetente (`recvfrom`) não seja `ip_servidor:53`. Isso liga o código ao tema de envenenamento de cache visto nos slides do módulo 2.
4. **Caso 10 (`unb.br.`).** Definir qual nome é impresso. Proposta: remover o ponto final, para a saída ser idêntica à do caso 1.
5. **Nada além do resultado em stdout.** Mensagens de depuração ou `perror` vão só para stderr, para não "sujar" a saída que o professor compara.
6. **Falha no `sendto`.** Conta como tentativa perdida e segue para a próxima.
7. **Null MX (RFC 7505, `MX 0 .`).** Proposta: tratar como `Dominio X nao possui entrada MX` e documentar.
8. **Repositório.** Não versionar `docs/`: os livros e slides têm direitos autorais e o Kurose tem 36 MB.
9. **ZIP com testes.** O alvo `make entrega ZIP=...` empacota `Makefile`, `README.md`, `src/` e também `tests/`, para que `make test` funcione na máquina do professor.
10. **Perda de pacotes até o 8.8.8.8.** Em testes repetidos, o 8.8.8.8 deixou de responder rajadas de consultas (o `dig` foi afetado igual). O código está correto; o `tests/e2e.sh` repete um caso até 2 vezes quando não há resposta, e o README registra isso nas limitações.
