#!/bin/bash

set -x

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

WORKING_DIR=$PWD
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
ETELF_APP="$SCRIPT_DIR/../../app/etelf"

OUTPUT_FILE=${OUTPUT_FILE:-"lifter_eval_results.txt"}
SKIP_EXTRACT=${SKIP_EXTRACT:-false}

# Verify environment and inputs
if [ "$1" == "" ]; then
    echo -e "${RED}lifter-eval directory not defined. Expected use:\n\t./lifter-eval.sh path/to/lifter-eval\nMake sure you have cloned the lifter-eval repo and pointed this script to the lifter-eval directory.${NC}"
    exit 1
elif [ ! -f "$1/results.tar.xz" ]; then
    echo -e "${RED}Results tarball not found. Expected use:\n\t./lifter-eval.sh path/to/lifter-eval\nMake sure you have cloned the lifter-eval repo and pointed this script to the lifter-eval directory.${NC}"
    exit 1
elif [ -f $OUTPUT_FILE ]; then
    echo -e "${RED}Output file $OUTPUT_FILE already exists, change the OUTPUT_FILE variable or delete/rename the existing output file.${NC}"
    exit 1
elif [ ! $(git lfs version) ]; then
    echo -e "${RED}Warning: Git LFS is required to properly clone lifter-eval. Verify that Git LFS is installed and the results tarball is fully downloaded.${NC}"
fi

LIFTER_EVAL_DIR="$1"
LIFTER_SUBJECT_DIR="$LIFTER_EVAL_DIR/subjects"
BUILT_BINS_ARCHIVE="$LIFTER_EVAL_DIR/results.tar.xz"
BUILT_BINS_DIR="$LIFTER_EVAL_DIR/results"

COMPILE_DIR="$WORKING_DIR/lifter-eval-comp"
REBUILT_BINS_DIR="$WORKING_DIR/egalito-lifted"

mkdir -p $COMPILE_DIR
mkdir -p $REBUILT_BINS_DIR

# Extract pre-compiled binaries
if [ ! $SKIP_EXTRACT ]; then
    rm -r $BUILT_BINS_DIR/*
    tar -xf $BUILT_BINS_ARCHIVE -C $LIFTER_EVAL_DIR
fi

# Lift and rebuild all subject ELFs and required tests
failed_tests=()
last_compiled=""
test_command="smoke-test"
for elf in ${BUILT_BINS_DIR}/*.elf;do
    input_base="$(basename $elf)"
    output_elf="$REBUILT_BINS_DIR/$input_base"
    $ETELF_APP -m $elf $output_elf

    subject_name="${input_base%%.*}.sh"
    subject_path="$LIFTER_SUBJECT_DIR/$subject_name"

    # Make sure we compile the relevant subject at least once
    if [ "$subject_name" != "$last_compiled" ]; then
        last_compiled=$subject_name
        CC=gcc CFLAGS=-g $subject_path compile $COMPILE_DIR
        # If evaluation is implemented, prefer it over smoke testing
        $subject_path evaluate $elf
        if [ $? == 0 ]; then
            test_command="evaluate"
        else
            test_command="smoke-test"
        fi
    fi

    # Test the rebuilt elf
    $subject_path $test_command $output_elf
    if [ $? != 0 ]; then
        failed_tests+=($elf)
        echo -e "$elf\t-\tFAIL" >> $OUTPUT_FILE
    else
        echo -e "$elf\t-\tPASS" >> $OUTPUT_FILE
    fi
done

# Cleanup
rm -r $COMPILE_DIR
rm -r $REBUILT_BINS_DIR

if [ ${#failed_tests[@]} == 0 ]; then
    echo "All tests passed"
    exit 0
fi

echo -e "${RED}Failed tests:${NC}"
for ((i=0; i<${#failed_tests[@]}; i++))
do
    echo "${failed_tests[$i]}"
done
exit 1
