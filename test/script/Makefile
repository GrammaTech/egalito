P_ARCH := $(shell uname -m)

define x86_only
$(if $(filter x86_64,$(P_ARCH)),$1)
endef

define aarch64_only
$(if $(filter aarch64,$(P_ARCH)),$1)
endef

define arch_dep
$(if $(filter x86_64,$(P_ARCH)), $1 x86_64-debian, $1 aarch64-openSuSE)
endef

.PHONY: test
test:
	./run-tests.sh

jt:
	$(call arch_dep,./jumptable-libc.sh)
