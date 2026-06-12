#!/bin/bash

set -e

PASS_LIB="./lib/LLVMOurPass.so"
TEST_DIR="./tests"
OUT_DIR="./test_outputs"

mkdir -p "$OUT_DIR"

if [ ! -f "$PASS_LIB" ]; then
    echo "Greska: nije pronadjena biblioteka $PASS_LIB"
    echo "Prvo build-uj projekat i proveri da li postoji lib/LLVMOurPass.so"
    exit 1
fi

if [ ! -d "$TEST_DIR" ]; then
    echo "Greska: ne postoji folder $TEST_DIR"
    echo "Napravi folder tests i u njega stavi .c test fajlove"
    exit 1
fi

for C_FILE in "$TEST_DIR"/*.c; do
    if [ ! -f "$C_FILE" ]; then
        echo "Nema .c fajlova u folderu $TEST_DIR"
        exit 1
    fi

    BASENAME=$(basename "$C_FILE" .c)

    RAW_LL="$OUT_DIR/${BASENAME}.ll"
    SSA_LL="$OUT_DIR/${BASENAME}_ssa.ll"
    FULL_LL="$OUT_DIR/${BASENAME}_full.ll"

    echo "========================================"
    echo "Test: $C_FILE"
    echo "========================================"

    echo "[1] C -> LLVM IR"
    ./bin/clang -emit-llvm -S \
        -fno-discard-value-names \
        -Xclang -disable-O0-optnone \
        -o "$RAW_LL" "$C_FILE"

    echo "[2] mem2reg"
    ./bin/opt -mem2reg -S -enable-new-pm=0 \
        -o "$SSA_LL" "$RAW_LL"

    echo "[3] OurSCCP"
    ./bin/opt -load "$PASS_LIB" -enable-new-pm=0 \
        -oursccp  -verify \
        -S "$SSA_LL" -o "$FULL_LL"

    echo "[4] verify final IR"
    ./bin/opt -enable-new-pm=0 -verify -S "$FULL_LL" -o /dev/null

    echo "Gotovo:"
    echo "  $RAW_LL"
    echo "  $SSA_LL"
    echo "  $FULL_LL"
    echo
done

echo "Svi testovi su prosli."
