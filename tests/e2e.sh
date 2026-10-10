#!/usr/bin/env bash
#
# e2e.sh - Testes de ponta a ponta do ./meu_cliente (os 10 casos do PLANO.md).
#
# Precisa de internet e de acesso a servidores DNS externos (UDP 53).
# Os casos de sucesso são comparados com a resposta do 'dig', quando ele existe.
# Uso: ./tests/e2e.sh        (rode a partir da raiz do projeto, depois do make)

BIN=./meu_cliente
USO='Uso: ./meu_cliente <dominio> <ip_servidor_dns>'
UNB='unb.br <> unb-br.mail.protection.outlook.com'   # saída do exemplo do enunciado
passou=0
falhou=0

ok()    { passou=$((passou + 1)); echo "  ok    $1"; }
falha() { falhou=$((falhou + 1)); echo "  FALHA $1"; echo "        esperado: [$2]"; echo "        obtido:   [$3]"; }

# Roda o cliente e guarda stdout, stderr, código de saída e duração (s)
roda() {
    local ini fim
    ini=$(date +%s)
    "$BIN" "$@" >/tmp/e2e_out.$$ 2>/tmp/e2e_err.$$
    rc=$?
    fim=$(date +%s)
    out=$(cat /tmp/e2e_out.$$)
    err=$(cat /tmp/e2e_err.$$)
    dur=$((fim - ini))
}

# Para casos que dependem de resposta do servidor: se o programa saiu com
# código 3 (nenhuma resposta nas 3 tentativas), repete até 2 vezes.
# A rede pode perder pacotes UDP em rajadas; isso afeta também o dig.
roda_rede() {
    local n
    repeticoes=0
    roda "$@"
    for n in 1 2; do
        [ "$rc" != 3 ] && break
        repeticoes=$((repeticoes + 1))
        echo "  aviso: sem resposta do servidor (perda de pacotes?), repetindo $*"
        roda "$@"
    done
}

# Confere stdout, stderr e código de saída
confere() { # nome, stdout esperado, stderr esperado, código esperado
    if [ "$out" == "$2" ] && [ "$err" == "$3" ] && [ "$rc" == "$4" ]; then
        ok "$1"
    else
        falha "$1" "stdout=$2 | stderr=$3 | rc=$4" "stdout=$out | stderr=$err | rc=$rc"
    fi
}

# Monta a saída esperada a partir do dig: "dominio <> servidor", por preferência
esperado_dig() { # dominio, servidor
    dig +short +time=2 +tries=3 "$1" MX @"$2" | grep -E '^[0-9]+ ' | sort -n -k1,1 |
        awk -v d="$3" '{ sub(/\.$/, "", $2); print d " <> " $2 }'
}

if [ ! -x "$BIN" ]; then
    echo "Compile antes: make"
    exit 1
fi

echo "Casos do plano de testes:"

# 1 - Resolução bem-sucedida (exemplo do enunciado)
roda_rede unb.br 8.8.8.8
confere "1  unb.br 8.8.8.8" "$UNB" "" 0

# 2 - Vários MX, ordenados por preferência (testa compressão de nomes)
roda_rede gmail.com 8.8.8.8
if command -v dig >/dev/null; then
    confere "2  gmail.com 8.8.8.8 (comparado com dig)" "$(esperado_dig gmail.com 8.8.8.8 gmail.com)" "" 0
else
    [ "$rc" == 0 ] && [ "$(echo "$out" | wc -l)" -gt 1 ] && ok "2  gmail.com 8.8.8.8 (sem dig)" ||
        falha "2  gmail.com 8.8.8.8" "varias linhas, rc=0" "$out rc=$rc"
fi

# 3 - Domínio inexistente (exemplo do enunciado)
roda_rede imagdaskdasdasj.br 1.1.1.1
confere "3  imagdaskdasdasj.br 1.1.1.1" "Dominio imagdaskdasdasj.br nao encontrado" "" 2

# 4 - Domínio sem MX: fga.unb.br é um CNAME para fcte.unb.br (exemplo do enunciado)
roda_rede fga.unb.br 8.8.8.8
confere "4  fga.unb.br 8.8.8.8" "Dominio fga.unb.br nao possui entrada MX" "" 2

# 5 - Servidor que não responde: 3 tentativas de 2 s (exemplo do enunciado)
roda unb.br 1.2.3.4
confere "5  unb.br 1.2.3.4" "Nao foi possível coletar entrada MX para unb.br" "" 3
if [ "$dur" -ge 5 ] && [ "$dur" -le 8 ]; then
    ok "5b unb.br 1.2.3.4 levou ${dur}s (3 x 2 s)"
else
    falha "5b tempo do caso 5" "cerca de 6 s" "${dur}s"
fi

# 6 - Outro servidor DNS, mesmo resultado do caso 1
roda_rede unb.br 1.1.1.1
confere "6  unb.br 1.1.1.1" "$UNB" "" 0

# 7 - Falta o servidor DNS
roda unb.br
confere "7  sem o 2o argumento" "" "$USO" 1

# 8 - IP inválido
roda unb.br 999.1.1.1
confere "8  IP invalido 999.1.1.1" "" "$USO" 1

# 9 - Label com 64 caracteres
roda "$(printf 'a%.0s' $(seq 1 64)).com" 8.8.8.8
confere "9  label com 64 caracteres" "" "$USO" 1

# 10 - Ponto final no nome: mesma saída do caso 1
roda_rede unb.br. 8.8.8.8
confere "10 unb.br. 8.8.8.8" "$UNB" "" 0

echo "Casos extras:"

roda
confere "E1 sem argumentos" "" "$USO" 1

roda unb.br 8.8.8.8 sobrando
confere "E2 argumento a mais" "" "$USO" 1

roda unb..br 8.8.8.8
confere "E3 label vazio (unb..br)" "" "$USO" 1

# Null MX (RFC 7505): example.com publica "MX 0 .", ou seja, não recebe e-mail
roda_rede example.com 1.1.1.1
confere "E4 null MX (example.com)" "Dominio example.com nao possui entrada MX" "" 2

rm -f /tmp/e2e_out.$$ /tmp/e2e_err.$$
echo "e2e: $passou passaram, $falhou falharam"
[ "$falhou" -eq 0 ]
