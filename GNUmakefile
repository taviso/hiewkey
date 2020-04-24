include ../GNUmakefile.common

LDLIBS      += user32

%.hem: %.dll
	cp $< $@

all: keyhelp.hem

keyhelp.dll: input.obj keyhelp.obj hiewgate.obj

clean::
	$(RM) *.hem

install: keyhelp.hem
	cp $^ ~/User/Applications/Hiew/hem/
