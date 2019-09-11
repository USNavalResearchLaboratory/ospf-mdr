# A Makefile for building debian package files.

PKGNAME		= quagga-mr
PKGVERSION	= $(shell awk '/^$(PKGNAME)/ \
	{gsub("[()]", "", $$2); print $$2; exit 0;}' debian/changelog)

PKGARCH		= $(shell dpkg --print-architecture)

PKGFILES	=					\
	$(PKGNAME)_$(PKGVERSION)_$(PKGARCH).changes	\
	$(PKGNAME)_$(PKGVERSION)_$(PKGARCH).deb		\
	$(PKGNAME)-doc_$(PKGVERSION)_all.deb

.PHONY: build
build: $(PKGFILES)

.PHONY: clean
clean:
	fakeroot make -f debian/rules clean

.PHONY: distclean
distclean: clean
	rm -rf $(PKGFILES)

configure:
	./bootstrap.sh

$(PKGFILES): configure debian debian/changelog
	dpkg-buildpackage -b -us -uc
	mv $(patsubst %, ../%, $(PKGFILES)) .
