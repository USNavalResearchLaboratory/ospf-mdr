## Process this file with automake to produce Makefile.in.

SUBDIRS = lib @ZEBRA@ @BGPD@ @RIPD@ @RIPNGD@ @OSPFD@ @OSPF6D@ @BABELD@ \
         @ISISD@ @XPIMD@ @WATCHQUAGGA@ @VTYSH@ @OSPFCLIENT@ @DOC@ m4 @pkgsrcdir@ \
         redhat @SOLARIS@

DIST_SUBDIRS = lib zebra bgpd ripd ripngd ospfd ospf6d babeld \
	  isisd xpimd watchquagga vtysh ospfclient doc m4 pkgsrc redhat tests \
	  solaris debian

EXTRA_DIST = aclocal.m4 SERVICES TODO REPORTING-BUGS INSTALL.quagga.txt \
	update-autotools \
	vtysh/Makefile.in vtysh/Makefile.am \
	tools/mrlg.cgi tools/rrcheck.pl tools/rrlookup.pl tools/zc.pl \
	tools/zebra.el tools/multiple-bgpd.sh \
	RELEASE_NOTES.MobileRouting quagga.deb.mk quagga.rpm.mk

if POST_INSTALL_LDCONFIG
install-exec-local:
	if [ -w /etc/ld.so.cache ]; then ldconfig; fi
endif

if HAVE_LATEX

HACKING.pdf: HACKING.tex
	$(LATEXMK) -pdf $<

clean-local:
	-$(LATEXMK) -C HACKING.tex

endif

ACLOCAL_AMFLAGS = -I m4
