# Trabalho 01: Cliente DNS (MX) em C

Fundamentos de Redes de Computadores, Prof. Tiago Alves, UnB Gama.

Cliente DNS que consulta registros **MX** (servidores de e-mail) de um domínio, enviando a consulta por **UDP** para a porta 53 de um servidor DNS informado pelo usuário. A consulta é montada byte a byte pelo próprio programa (RFC 1035, seção 4), sem bibliotecas de DNS ou de comunicação além da interface de sockets.

## Conformidade com o enunciado

| Requisito | Como foi atendido |
| --- | --- |
| Dois argumentos de linha de comando | `./meu_cliente <dominio> <ip_servidor_dns>` |
| Transporte UDP na porta 53 | Socket `AF_INET`/`SOCK_DGRAM` e envio com `sendto()` para a porta 53 |
| Consulta montada programaticamente | Cabeçalho e pergunta DNS são escritos byte a byte em `src/dns_query.c`, sem biblioteca de DNS |
| Transaction ID aleatório de 16 bits | Gerado por `dns_random_id()` a partir de `/dev/urandom`, com uma alternativa baseada no relógio e no PID |
| Flags `0x0100` | Somente o bit RD (recursion desired) é ativado |
| Uma pergunta e nenhuma resposta no pedido | QDCOUNT = 1; ANCOUNT, NSCOUNT e ARCOUNT = 0 |
| Query MX/IN | QTYPE = 15 (MX) e QCLASS = 1 (IN) para o domínio recebido |
| Espera de 2 segundos e até 3 tentativas | `select()` controla o prazo de cada tentativa em `src/net.c` |
| Formato da saída | Cada resultado é impresso como `nome_dominio <> nome_servidor_email` em stdout |
| Interpretação da resposta | O programa trata RCODE, percorre os RRs, descomprime nomes e extrai preferência e servidor MX em `src/dns_parse.c` |
| Somente sockets como recurso de comunicação | Não são usadas bibliotecas de DNS, resolvedores do sistema ou bibliotecas de comunicação de terceiros |

As seções **Sistema operacional**, **Ambiente de desenvolvimento**, **Como construir**, **Como executar**, **Telas (instruções de uso)** e **Limitações conhecidas** contêm os seis itens de documentação mínima exigidos no enunciado.

## Integrantes

| Nome | Matrícula |
| --- | --- |
| Kaleb de Souza Macedo | 231026975 |
| Lucas Monteiro Freitas | 231035446 |
| João Maurício Pilla Nascimento | 231011533 |

> **Antes da entrega:** substituir todos os campos `_preencher_` pelos nomes completos e matrículas. O enunciado escrito menciona duplas; esta tabela tem quatro linhas porque o professor informou em sala que o trabalho pode ser feito por quatro integrantes.

## Sistema operacional

- **Desenvolvimento:** macOS 26.5.1 (Apple Silicon).
- **Testes em Linux:** Ubuntu 24.04.4 LTS (WSL2), GCC 13.3.0.

O código usa apenas C99 e a API POSIX de sockets, então compila em Linux e em macOS.

## Ambiente de desenvolvimento

- Editor: Visual Studio Code.
- Compilador: GCC (no macOS, o comando `gcc` é o Apple clang 15.0.0); no Linux, GCC.
- Build: GNU Make.
- Controle de versão: Git (repositório privado no GitHub).
- Ferramentas de apoio: `dig`, para comparar resultados, e Wireshark, com filtro `dns`, para inspecionar os pacotes.

### Pré-requisitos

Para compilar e executar a aplicação, são necessários um ambiente POSIX, um compilador C compatível com C99 e o GNU Make. Em Ubuntu/Debian, o pacote `build-essential` fornece o compilador e o Make:

```bash
sudo apt install build-essential
```

Os testes unitários não precisam de acesso à internet. Os testes de ponta a ponta também exigem o comando `dig` (pacote `dnsutils`) e uma rede que permita tráfego UDP para a porta 53. Para gerar o arquivo de entrega, é necessário o comando `zip`.

## Como construir

Na raiz do projeto:

```bash
make
```

O comando gera o executável `meu_cliente`. Flags de compilação: `-Wall -Wextra -std=c99 -pedantic -O2` (compila sem warnings).

Para apagar o executável e os arquivos objeto:

```bash
make clean
```

## Como executar

```bash
./meu_cliente <dominio> <ip_servidor_dns>
```

- `<dominio>`: nome cujos registros MX se quer consultar (ex.: `unb.br`).
- `<ip_servidor_dns>`: endereço IPv4 do servidor DNS a consultar (ex.: `8.8.8.8`). O servidor é contatado na porta padrão do DNS, UDP 53.

O resultado aparece na saída padrão (stdout). Códigos de saída:

| Código | Situação |
| --- | --- |
| 0 | Um ou mais registros MX encontrados |
| 1 | Erro de uso (argumentos faltando, IP inválido ou nome inválido) |
| 2 | Domínio não encontrado ou sem entrada MX |
| 3 | Servidor não respondeu após 3 tentativas, ou respondeu com erro |

Observação: a rede da UnB pode bloquear consultas a servidores DNS externos, como avisa o enunciado. Nesse caso, use uma rede que permita UDP 53 para fora.

## Telas (instruções de uso)

Exemplos de uso e saídas esperadas (os servidores MX publicados por cada domínio podem mudar ao longo do tempo):

```console
# Resolução bem-sucedida
$ ./meu_cliente unb.br 8.8.8.8
unb.br <> unb-br.mail.protection.outlook.com

# Domínio com vários servidores de e-mail: uma linha por MX, por ordem de preferência
$ ./meu_cliente gmail.com 8.8.8.8
gmail.com <> gmail-smtp-in.l.google.com
gmail.com <> alt1.gmail-smtp-in.l.google.com
gmail.com <> alt2.gmail-smtp-in.l.google.com
gmail.com <> alt3.gmail-smtp-in.l.google.com
gmail.com <> alt4.gmail-smtp-in.l.google.com

# Resolução com falha: nome de domínio não existe
$ ./meu_cliente imagdaskdasdasj.br 1.1.1.1
Dominio imagdaskdasdasj.br nao encontrado

# Resolução com falha: domínio não possui entrada MX
# (fga.unb.br é um CNAME para fcte.unb.br, que não tem MX)
$ ./meu_cliente fga.unb.br 8.8.8.8
Dominio fga.unb.br nao possui entrada MX

# Resolução com falha: servidor não existe/não atendeu (3 tentativas x 2 s = ~6 s)
$ time ./meu_cliente unb.br 1.2.3.4
Nao foi possível coletar entrada MX para unb.br
./meu_cliente unb.br 1.2.3.4  0.00s user 0.00s system 0% cpu 6.015 total

# Ponto final opcional no nome
$ ./meu_cliente unb.br. 8.8.8.8
unb.br <> unb-br.mail.protection.outlook.com

# Erros de uso (mensagem em stderr)
$ ./meu_cliente unb.br
Uso: ./meu_cliente <dominio> <ip_servidor_dns>
$ ./meu_cliente unb.br 999.1.1.1
Uso: ./meu_cliente <dominio> <ip_servidor_dns>
```

## Testes

```bash
make test   # testes unitários, sem internet
make e2e    # testes de ponta a ponta com servidoresDDNS reais (precisa de internet)
```

- `tests/test_query.c`: confere byte a byte o pacote de consulta (imprime o de `unb.br` em hexadecimal) e as validações de nome.
- `tests/test_parse.c`: respostas montadas à mão, cobrindo compressão de nomes, CNAME sem MX, NXDOMAIN, SERVFAIL, pacotes cortados em todas as posições, ponteiro em loop, ponteiro para fora do pacote, respostas truncadas (bit TC) e bytes não imprimíveis nos nomes.
- `tests/test_net.c`: sobe um servidor DNS falso local e confere as 3 tentativas de 2 s e o descarte de respostas com ID errado, QR=0 ou vindas de outra porta.
- `tests/e2e.sh`: os 10 casos do plano de testes, comparando a saída com o `dig`, e casos extras, como o "Null MX" de `example.com`.

Pacote da consulta MX para `unb.br` (com ID `0xABCD`), gerado por `make test`:

```text
AB CD 01 00 00 01 00 00 00 00 00 00 03 75 6E 62
02 62 72 00 00 0F 00 01
```

| Bytes | Campo | Valor |
| --- | --- | --- |
| `AB CD` | ID | aleatório (lido de `/dev/urandom`) |
| `01 00` | Flags | `0x0100`: consulta recursiva |
| `00 01` | QDCOUNT | 1 pergunta |
| `00 00` x3 | ANCOUNT, NSCOUNT, ARCOUNT | 0 |
| `03 75 6E 62 02 62 72 00` | QNAME | `3 u n b 2 b r 0` |
| `00 0F` | QTYPE | MX (15) |
| `00 01` | QCLASS | IN (1) |

## Organização do código

```text
Makefile
README.md
src/
  dns.h        constantes, tipos e protótipos compartilhados
  main.c       argumentos, fluxo principal e mensagens de saída
  dns_query.c  montagem da consulta byte a byte e ID aleatório
  net.c        socket UDP, espera de 2 s com select() e 3 tentativas
  dns_parse.c  interpretação da resposta e descompressão de nomes
tests/         testes unitários e de ponta a ponta
```

Funcionamento resumido:

1. `main.c` valida o IP com `inet_pton` (só converte texto em endereço, não resolve nomes) e chama `dns_build_query`.
2. `net.c` cria um socket `AF_INET`/`SOCK_DGRAM`, envia a consulta com `sendto` e espera com `select()` até 2 s. Se não chegar resposta válida, envia de novo, até 3 vezes. A resposta só é aceita se vier do IP e da porta 53 consultados, com o mesmo ID e com o bit QR = 1; qualquer outro pacote é descartado e a espera continua no tempo restante.
3. `dns_parse.c` lê o RCODE (3 = domínio inexistente), pula a seção de pergunta e percorre a seção de resposta. Só registros TYPE 15 (MX), classe IN, são extraídos; CNAME e outros são pulados pelo RDLENGTH. Os nomes podem vir comprimidos (RFC 1035, 4.1.4): um byte com os dois bits altos em `11` inicia um ponteiro de 14 bits, `((b1 & 0x3F) << 8) | b2`, para outra posição do pacote. O leitor segue no máximo 64 ponteiros, para não entrar em loop, e confere os limites do pacote em toda leitura.
4. Os MX são ordenados por preferência crescente e impressos no formato `nome_dominio <> nome_servidor_email`.

## Decisões de interpretação

O enunciado deixa alguns pontos em aberto. Adotamos:

| Ponto | Decisão |
| --- | --- |
| "Receberá entradas do teclado" | Entrada pelos argumentos da linha de comando, como nos exemplos do enunciado |
| Nome inexistente (frase incompleta no enunciado) | RCODE 3 (NXDOMAIN) imprime `Dominio X nao encontrado` |
| Vários registros MX | Todos são impressos, um por linha, em ordem crescente de preferência |
| Texto das mensagens | Idêntico ao dos exemplos do enunciado, inclusive o acento em "possível" |
| SERVFAIL, REFUSED e outros RCODEs de erro | `Nao foi possível coletar entrada MX para X`, sem nova tentativa |
| ID nas tentativas | O mesmo ID nas 3 tentativas (é a mesma transação) |
| Ponto final no nome (`unb.br.`) | Aceito; a saída mostra o nome sem o ponto, igual a `unb.br` |
| "Null MX" (`MX 0 .`, RFC 7505: o domínio não recebe e-mail) | Tratado como `Dominio X nao possui entrada MX` |
| Resposta truncada (bit TC) | Usa os MX completos que chegaram; se nenhum MX chegou inteiro, `Nao foi possível coletar entrada MX para X` |
| Bytes não imprimíveis nos nomes recebidos | Exibidos como `?`, para o servidor não enviar sequências de controle ao terminal |
| Argumentos faltando, IP ou nome inválido | Mensagem de uso em stderr, código de saída 1 |

## Limitações conhecidas

- Só IPv4: o servidor DNS precisa ser informado por um endereço IPv4.
- Só consultas do tipo MX, classe IN.
- Sem fallback para TCP: se a resposta vier truncada (bit TC), usamos os registros MX completos que chegaram antes do corte. Se nenhum chegou inteiro, o programa informa `Nao foi possível coletar entrada MX para X`, pois não dá para afirmar que o domínio não tem MX.
- Sem EDNS: a consulta segue o formato do enunciado, então o servidor limita a resposta a 512 bytes.
- Guarda no máximo 32 registros MX por resposta.
- O nome de domínio é enviado como digitado; não há suporte a nomes internacionalizados (acentos/IDN) nem a sequências de escape.
- Mensagens sem acentuação, exceto onde o enunciado usa ("possível").
- Em redes com perda de pacotes UDP, as 3 tentativas podem não bastar. Em testes repetidos, o 8.8.8.8 deixou de responder algumas consultas seguidas (o `dig` foi afetado da mesma forma); o programa então informa a falha após ~6 s, como pede o enunciado.

## Preparação da entrega

O enunciado exige um arquivo ZIP com os códigos-fonte e a documentação, sem executáveis. Antes de gerar o pacote, preencha a tabela de integrantes e execute:

```bash
make test
make entrega ZIP=nome_sobrenome_matricula_nome_sobrenome_matricula_trab01.zip
```

Se o grupo tiver quatro integrantes conforme a orientação dada em sala, acrescente os quatro conjuntos `nome_sobrenome_matricula` antes de `_trab01.zip`. O alvo `entrega` executa `make clean` e inclui apenas `Makefile`, `README.md`, `src/` e `tests/`. Confira o conteúdo antes de enviar:

```bash
unzip -l nome_sobrenome_matricula_nome_sobrenome_matricula_trab01.zip
```

Checklist final:

- [ ] nomes e matrículas preenchidos no README;
- [ ] `make clean && make` concluído sem erros ou avisos em Linux;
- [ ] `make test` concluído com sucesso;
- [ ] exemplos principais executados em uma rede que permita UDP 53;
- [ ] ZIP nomeado no padrão solicitado;
- [ ] ZIP contém fontes e documentação e não contém `meu_cliente`, arquivos `.o` ou executáveis de teste.

## Referências

- [RFC 1034 — Domain Names: Concepts and Facilities](https://www.rfc-editor.org/rfc/rfc1034).
- [RFC 1035 — Domain Names: Implementation and Specification](https://www.rfc-editor.org/rfc/rfc1035), seção 4 (formato da mensagem) e seção 4.1.4 (compressão de nomes).
- KUROSE, J.; ROSS, K. _Redes de Computadores e a Internet: uma abordagem top-down_. 6. ed., seção 2.5.
- MITCHELL, M.; OLDHAM, J.; SAMUEL, A. _Advanced Linux Programming_. New Riders, 2001, seção 5.5 (sockets).
- [RFC 7505 — A "Null MX" No Service Resource Record for Domains That Accept No Mail](https://www.rfc-editor.org/rfc/rfc7505).
