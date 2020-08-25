# A Makefile for building debian package files.
#
# usage:
#     make -f quagga.deb.mk
# to exclude the doc package:
#     DEB_BUILD_OPTIONS=nodoc make -f quagga.deb.mk

MAINTAINER	= Tom Goff
MAINTAINER_EMAIL= thomas.goff@ll.mit.edu

BASEVERSION	:= $(shell sed -n -r -e \
	'/^AC_INIT/{s/.*,[[:space:]]*(.*)[[:space:]]*,.*/\1/p;q;}' \
	configure.ac)

ifneq ($(strip $(shell git rev-parse --git-dir 2> /dev/null)),)
COMMITDATE	:= .$(shell date -u -d \
	@$(shell git log -1 --format='%ct') +%Y%m%d.%H%M%S)
COMMITHASH	:= $(shell git log -1 --format='%h')
DIRTY		:= $(shell git diff --quiet HEAD || echo .dirty)
COMMIT		:= .g$(COMMITHASH)$(DIRTY)
else
ifneq ($(strip $(shell svn info --show-item kind 2> /dev/null)),)
COMMITDATE	:= .$(shell date -u -d \
	$(shell svn log -rHEAD --xml 2> /dev/null | \
	        sed -n -r -e '/<date>/{s|<date>(.*)</date>|\1|;p;q}') \
	+%Y%m%d.%H%M%S)
COMMITREV	:= $(shell svn log -rHEAD --xml 2> /dev/null | \
	sed -n -r -e '/revision=/{s/.*revision="([0-9]+)".*/\1/;p;q}')

DIRTY		:= $(shell (svn status -q | grep -q .) && echo .dirty)
COMMIT		:= .s$(COMMITREV)$(DIRTY)
endif
endif

VERSION		= $(BASEVERSION)$(COMMITDATE)$(COMMIT)

PKGNAME		= quagga-mr
PKGVERSION	= $(VERSION)

PKGARCH		= $(shell dpkg --print-architecture)

PKGFILES	=					\
	$(PKGNAME)_$(PKGVERSION)_$(PKGARCH).changes	\
	$(PKGNAME)_$(PKGVERSION)_$(PKGARCH).deb		\
	$(PKGNAME)-dbg_$(PKGVERSION)_$(PKGARCH).deb

ifeq (,$(filter nodoc,$(DEB_BUILD_OPTIONS)))
BUILD		= -b
PKGFILES	+= $(PKGNAME)-doc_$(PKGVERSION)_all.deb
else
BUILD		= -B
endif

.PHONY: all
all: clean build

.PHONY: clean
clean:
	test ! -e Makefile || make maintainer-clean
	test ! -e debian/control || fakeroot make -f debian/rules clean
	rm -f debian/changelog debian/control

.PHONY: distclean
distclean: clean
	rm -rf $(PKGFILES)

.PHONY: build
build: $(PKGFILES)

configure:
	./bootstrap.sh

%:: %.in
	sed							\
	    -e 's|[@]VERSION[@]|$(VERSION)|g'			\
	    -e 's|[@]MAINTAINER[@]|$(MAINTAINER)|g'		\
	    -e 's|[@]MAINTAINER_EMAIL[@]|$(MAINTAINER_EMAIL)|g'	\
	    -e "s|[@]RFC2822_DATE[@]|$$(date -R)|g"		\
	    $< > $@

$(PKGFILES): configure debian debian/changelog debian/control
	dpkg-buildpackage $(BUILD) -us -uc
	mv $(patsubst %, ../%, $(PKGFILES)) .
