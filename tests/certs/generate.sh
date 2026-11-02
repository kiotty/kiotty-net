#!/bin/sh
set -e

cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

ca() {
    name=$1
    openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 36500 \
        -subj "/CN=kiotty test $name" \
        -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -keyout "$work/$name.key" -out "$name.pem" 2>/dev/null
}

leaf() {
    name=$1
    cn=$2
    san=$3
    usage=$4
    start=$5
    end=$6
    openssl req -newkey rsa:2048 -nodes -subj "/CN=$cn" \
        -keyout "$name.key" -out "$work/$name.csr" 2>/dev/null
    cat > "$work/$name.ext" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=$usage
subjectAltName=$san
EOF
    mkdir -p "$work/db"
    : > "$work/db/index.txt"
    cat > "$work/ca.cnf" <<EOF
[ca]
default_ca=test
[test]
dir=$work/db
database=$work/db/index.txt
new_certs_dir=$work/db
serial=$work/db/serial
default_md=sha256
policy=any
unique_subject=no
copy_extensions=none
[any]
commonName=supplied
EOF
    openssl rand -hex 16 > "$work/db/serial"
    openssl ca -batch -notext -config "$work/ca.cnf" \
        -cert ca.pem -keyfile "$work/ca.key" \
        -startdate "$start" -enddate "$end" \
        -extfile "$work/$name.ext" \
        -in "$work/$name.csr" -out "$name.pem" 2>/dev/null
}

ca ca
ca other_ca
rm -f "$work/other_ca.key"

leaf server localhost "DNS:localhost,IP:127.0.0.1" serverAuth 20260101000000Z 21260101000000Z
leaf client "kiotty test client" "DNS:client.kiotty.test" clientAuth 20260101000000Z 21260101000000Z
leaf server_wrong_host wrong.kiotty.test "DNS:wrong.kiotty.test" serverAuth 20260101000000Z 21260101000000Z
leaf server_expired localhost "DNS:localhost,IP:127.0.0.1" serverAuth 20000101000000Z 20010101000000Z

printf 'this is not a certificate\n' > garbage.pem
