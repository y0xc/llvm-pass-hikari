#!/bin/bash

XCODE_SWIFTC=$(xcrun --find swiftc)
XCODE_SWIFT_FRONT=$(xcrun --find swift-frontend)
MAX_JOBS=1

USE_LL_IN_XCODE=1
USE_LL_IN_SWIFT=1

OPT=~/data/ollvm/build_llvm19/bin/opt
SLLVM_ROOT=~/Documents/Tool/sllvm_r1
SLLVM_IR=$SLLVM_ROOT/build19/sllvm.dylib
SLLVM_ASM=$SLLVM_ROOT/build_asm/sllvm_asm

if [ "$1" = "--version" ]; then
    exec $XCODE_SWIFTC "$@"
fi

$XCODE_SWIFTC "$@"

NEW_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -target) 
            TARGET="$2"
            NEW_ARGS+=("$1" "$2")
            shift 2
            ;;
        -parseable-output|-incremental|-enable-batch-mode)
            shift 1
            ;;
        -Xfrontend)
            NEW_ARGS+=("$2")
            shift 2
            ;;
        -emit-objc-header|-emit-const-values|-serialize-diagnostics)
            shift 1
            ;;
        -emit-module-path|-emit-objc-header-path|-emit-dependencies|-index-store-path|-const-gather-protocols-file|-working-directory)
            shift 2
            ;;
        -output-file-map)
            MAP_PATH="$2"
            shift 2
            ;;
        -sdk)
            SYSROOT="$2"
            NEW_ARGS+=("$1" "$2")
            shift 2
            ;;
        -j*)
            MAX_JOBS="${1:2}"
            shift 1
            ;;
        @*)
            shift 1
            ;;
        *)
            NEW_ARGS+=("$1")
            shift 1
            ;;
    esac
done

function log_run() {
    local cmd="$*"
    echo $cmd
    eval $cmd
    r=$?
    if [ $r -ne 0 ]; then
        exit -1
    fi
    return $r
}

echo swift:     $XCODE_SWIFTC $XCODE_SWIFT_FRONT
echo target:    $TARGET
echo sysroot:   $SYSROOT

if [ $USE_LL_IN_XCODE -eq 1 ]; then
    ARG_1="-emit-ir"
    EXT_1="ll"
else
    ARG_1="-emit-bc"
    EXT_1="bc"
fi

if [ $USE_LL_IN_SWIFT -eq 1 ]; then
    EXT_2="ll"
    ARG_2="-S -load-pass-plugin $SLLVM_IR -passes all"
else
    EXT_2="bc"
    ARG_2="-load-pass-plugin $SLLVM_IR -passes all"
fi


ALL_SWIFT_FILES=($(jq -r 'keys[] | select(endswith(".swift"))' "$MAP_PATH"))
for SRC_FILE in "${ALL_SWIFT_FILES[@]}"; do
    (
        OUTPUT=$(jq -r --arg key $SRC_FILE '.[$key].object' $MAP_PATH)
        echo "handle $SRC_FILE -> $OUTPUT"
        OTHER_FILES=("${ALL_SWIFT_FILES[@]/$SRC_FILE/}")
        log_run $XCODE_SWIFT_FRONT ${NEW_ARGS[@]} $ARG_1 -primary-file $SRC_FILE ${OTHER_FILES[@]} -o $OUTPUT.1.$EXT_1
        log_run $OPT $ARG_2 -o $OUTPUT.2.$EXT_2 $OUTPUT.1.$EXT_1
        log_run $XCODE_SWIFT_FRONT -target $TARGET -c $OUTPUT.2.$EXT_2 -o $OUTPUT
    ) &
    if [[ $(jobs -r | wc -l) -ge $MAX_JOBS ]]; then
        wait -n
    fi
done

wait

