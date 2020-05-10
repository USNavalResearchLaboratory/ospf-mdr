RPMBUILD = .rpmbuild

VERSION = $(shell awk -F,					\
	'/^AC_INIT/ {sub("^[ \t]*", "", $$2); print $$2; exit}'	\
	configure.ac 2> /dev/null)

.PHONY: all
all: clean build

.PHONY: clean
clean:
	rm -rf $(RPMBUILD)

.PHONY: build
build: quagga-$(VERSION).tar.gz redhat/quagga.spec
	for d in SOURCES SPECS; do mkdir -p $(RPMBUILD)/$$d; done
	cp -afv quagga-$(VERSION).tar.gz $(RPMBUILD)/SOURCES
	cp -afv redhat/quagga-tmpfs.conf $(RPMBUILD)/SOURCES
	cp -afv redhat/quagga.spec $(RPMBUILD)/SPECS
	rpmbuild -bb --clean $(RPMBUILD)/SPECS/quagga.spec \
	    --define '_topdir $(abspath $(RPMBUILD))'
	@printf '\nRPM packages saved in $(RPMBUILD)/RPMS\n\n'

quagga-$(VERSION).tar.gz: Makefile
	$(MAKE) dist

Makefile redhat/quagga.spec: configure
	./configure --with-pkg-git-version

configure: bootstrap.sh
	./bootstrap.sh
