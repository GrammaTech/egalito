#! /usr/bin/env bash

TESTS_PASSED=0
NTESTS=0

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

aarch64_only(){
    if [ $(uname -p) == "aarch64" ]; then
    echo ${1}
    fi
}

test_scripts="\
./hello.sh \
./argv.sh \
./islower.sh \
./jumptable-rtl.sh \
jumptable-libc.sh $(arch_dep) \
./environ.sh \
./codeform.sh \
./dwarf-diff.sh \
./codeform-dwarf-syms.sh \
./codeform-s.sh \
./verify-redzone.sh \
./codeform-debloat.sh \
./hello-process.sh \
./hello-thread.sh \
./nginx.sh \
./nginx-thread.sh \
$(x86_only ./coreutils.sh) \
./cout.sh \
./sandbox-stage3.sh"

all_passed=0
for _test in $test_scripts;
do
	((NTESTS++))
    echo $_test
    if $_test; then
	    ((TESTS_PASSED++))
    else
	    all_passed=1
    fi
done

echo "${TESTS_PASSED} tests passed out of ${NTESTS}\n"

exit $all_passed
