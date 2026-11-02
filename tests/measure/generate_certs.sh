#!/bin/sh
set -e

out=$1
mkdir -p "$out"
cd "$out"

key() {
    kind=$1
    file=$2
    case $kind in
        rsa2048) openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$file" 2>/dev/null ;;
        rsa4096) openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out "$file" 2>/dev/null ;;
        ec256) openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$file" 2>/dev/null ;;
    esac
}

leaf() {
    kind=$1
    name=$2
    cn=$3
    san=$4
    usage=$5
    key "$kind" "${name}_$kind.key"
    openssl req -new -key "${name}_$kind.key" -subj "/CN=$cn" -out "${name}_$kind.csr" 2>/dev/null
    printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=%s\nsubjectAltName=%s\n' "$usage" "$san" > "${name}_$kind.ext"
    openssl x509 -req -in "${name}_$kind.csr" -CA "ca_$kind.pem" -CAkey "ca_$kind.key" -CAcreateserial \
        -days 36500 -sha256 -extfile "${name}_$kind.ext" -out "${name}_$kind.pem" 2>/dev/null
    rm -f "${name}_$kind.csr" "${name}_$kind.ext"
}

for kind in rsa2048 rsa4096 ec256; do
    key "$kind" "ca_$kind.key"
    openssl req -x509 -new -key "ca_$kind.key" -sha256 -days 36500 -subj "/CN=kiotty measure $kind" \
        -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -out "ca_$kind.pem" 2>/dev/null
    leaf "$kind" server localhost "DNS:localhost,IP:127.0.0.1" serverAuth
    leaf "$kind" client "kiotty measure client" "DNS:client.kiotty.test" clientAuth
done
rm -f ./*.srl
