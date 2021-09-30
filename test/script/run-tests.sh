#! /usr/bin/env sh

TESTS_PASSED=0

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

test_scripts='./hello.sh \
	./argv.sh \
	./islower.sh \
	./jumptable-rtl.sh \
	"jumptable-libc.sh $(arch_dep)" \
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
    $(x86_only "./coreutils.sh") \
	./cout.sh \
	./sandbox-stage3.sh'

for _test in $test_scripts;
do
    echo $_test
    if [[ $_test ]]; then
    TESTS_PASSED += 1;
    fi
done

echo "${TESTS_PASSED} tests passed out of ${#test_scripts}\n"
