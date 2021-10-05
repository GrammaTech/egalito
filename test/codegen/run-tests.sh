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

failed_tests=""
for ((i=0; i<${#test_scripts[@]}; i++))
do
    _test=${test_scripts[$i]}
    echo $_test
    if $_test; then
	    ((TESTS_PASSED++))
    else
        failed_tests+=$_test
        failed_tests+='\n'
    fi
done


echo "${TESTS_PASSED} tests passed out of ${#test_scripts[@]}"
if [ ! -z "${failed_tests}" ]; then
    echo 'Tests failed:'
    echo -e $failed_tests
    exit 1
else
    exit 0
fi
