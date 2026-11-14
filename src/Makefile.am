AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" -DSYSCONFDIR=\"$(sysconfdir)\" \
	-I$(top_builddir)/lib -I$(top_srcdir)/lib \
	-I$(top_srcdir)/third_party/libvterm/include -I$(top_srcdir)/third_party/tomlc17

CLEANFILES = revision.h nbl-default-config.inc

nbl_config.o: nbl-default-config.inc

nbl-default-config.inc: $(top_srcdir)/config.toml.example
	LC_ALL=C od -An -v -tu1 "$<" | awk '{for (i = 1; i <= NF; i++) printf "%s,", $$i; print ""}' > $@

if BUILDING_FROM_GIT
SOMETHING = "REVISION \"`git describe --tags 2>/dev/null`\""
else
SOMETHING = "PLAINBUILD \"from tarball\""
endif

nano.o: revision.h
winio.o: revision.h

# Always run the commands in this rule.  But update the target
# only when the revision actually changed.
revision.h: FORCE
	@[ -f $@ ] || touch $@
	@! git describe >/dev/null 2>&1 || \
			echo "#define $(SOMETHING)" | cmp -s $@ - || \
			echo "#define $(SOMETHING)" > $@
FORCE:

bin_PROGRAMS = 	nbl
nbl_SOURCES = \
		nbl.h \
		nbl_config.c \
		nbl_pairs.c \
		nbl_run.c \
		nbl_tools.c \
		nbl_terminal.c \
		nbl_mouse.c \
		nbl_completion.c \
		nbl_diagnostics.c \
		nbl_mentor.c \
		nbl_theme.c \
		nbl_explorer.c \
		nbl_fileops.c \
		nbl_tree.h \
		nbl_tree.c \
		definitions.h \
		prototypes.h \
		browser.c \
		chars.c \
		color.c \
		cut.c \
		files.c \
		global.c \
		help.c \
		history.c \
		move.c \
		nano.c \
		prompt.c \
		rcfile.c \
		search.c \
		text.c \
		utils.c \
		winio.c

noinst_LIBRARIES = libnbl-vterm.a libnbl-toml.a
libnbl_toml_a_CPPFLAGS = -I$(top_srcdir)/third_party/tomlc17
libnbl_toml_a_SOURCES = ../third_party/tomlc17/tomlc17.c
libnbl_vterm_a_CPPFLAGS = -I$(top_srcdir)/third_party/libvterm/include
libnbl_vterm_a_SOURCES = \
		../third_party/libvterm/src/encoding.c \
		../third_party/libvterm/src/keyboard.c \
		../third_party/libvterm/src/mouse.c \
		../third_party/libvterm/src/parser.c \
		../third_party/libvterm/src/pen.c \
		../third_party/libvterm/src/screen.c \
		../third_party/libvterm/src/state.c \
		../third_party/libvterm/src/unicode.c \
		../third_party/libvterm/src/vterm.c

nbl_LDADD = libnbl-vterm.a libnbl-toml.a $(top_builddir)/lib/libgnu.a @LIBINTL@ \
		$(GETRANDOM_LIB) $(CLOCK_TIME_LIB) $(LIBTHREAD)
