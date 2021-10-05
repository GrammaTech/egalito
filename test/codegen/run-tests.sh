#! /usr/bin/env bash

TESTS_PASSED=0
# NTESTS=0

arch_dep(){
    if [ $(uname -p) == "x86_64" ]; then
    echo "x86_64-debian"
    else
    echo "aarch64-openSuSE"
    fi
}

x86_only(){
    if [ $(uname -p) == "x86_64" ]; then
    echo ${1}
    fi
}

test_scripts=("./run-build.sh -m hello" 
	"./run-build.sh -m jumptable"
	"./run-system.sh -m /bin/ls"
	"./run-system.sh -m /bin/cat"
	"./run-system.sh -m /bin/gzip"
	"./run-system.sh -m /bin/grep"
	"./run-system.sh -m /usr/bin/env"
	"./run-system.sh -m /usr/bin/make"
	"./run-system.sh -m /usr/bin/dpkg"
	"./run-system.sh -m /usr/bin/find"
)

all_passed=0
for ((i=0; i<${#test_scripts[@]}; i++))
do
    _test=${test_scripts[$i]}
    echo $_test
    if $_test; then
	    ((TESTS_PASSED++))
    else
	    all_passed=1
    fi
done

echo "${TESTS_PASSED} tests passed out of ${#test_scripts[@]}"

exit $all_passed
