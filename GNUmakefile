include GNUmakefile.common

LDLIBS      += user32

%.hem: %.dll
	$(CP) $< $@

all: keyhelp.hem

keyhelp.dll: input.obj keyhelp.obj hiewgate.obj hiewkey.res

clean::
	$(RM) *.hem

release::
	zip hiewkey.zip keyhelp.hem README.md
