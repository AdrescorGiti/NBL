/* Conservative explanations for local tool diagnostics. GPL-3.0-or-later. */
#include "prototypes.h"
#include "nbl.h"

#include <string.h>

const char *nbl_mentor_hint(const char *message)
{
	static const struct {
		const char *pattern, *hint;
	} hints[] = {
		{"looking for matching", N_("Check the matching closing quote or delimiter on this line.")},
		{"unterminated string", N_("The string has no closing quote where the parser expected one.")},
		{"missing terminating", N_("Check for a missing closing quote or an unintended newline.")},
		{"was never closed", N_("An opening delimiter has no matching closer. Check the surrounding lines.")},
		{"unexpected end of file", N_("The parser expected more input. Check for an unclosed quote, block, or delimiter.")},
		{"expected an indented block", N_("Indent the body after the preceding colon.")},
		{"unexpected indent", N_("Check this line's indentation against the surrounding block.")},
		{"undeclared", N_("No declaration was visible here. Check the spelling, declaration, or required header.")},
		{"not declared", N_("No declaration was visible here. Check the spelling, declaration, or required header.")},
		{"No such file", N_("Check that the named file or header exists and is on the search path.")},
		{"SC2086", N_("Double-quote the expansion to avoid word splitting and pathname expansion.")},
		{"SC2154", N_("The checker found a reference without a visible assignment. Check where the variable is set.")},
		{"unused variable", N_("The variable is declared but not used in this scope.")},
		{"Checker not found", N_("Install the relevant interpreter or compiler separately, or configure its executable path.")},
	};

	for (size_t i = 0; i < sizeof(hints) / sizeof(*hints); i++)
		if (strstr(message, hints[i].pattern))
			return _(hints[i].hint);
	return _("Start at the reported location and review the neighboring code and the tool message.");
}

const char *nbl_language_label(void)
{
	switch (nbl_language()) {
		case NBL_SH: return nbl_is_bash() ? "Bash" : "POSIX sh";
		case NBL_PYTHON: return "Python";
		case NBL_C: return "C";
		case NBL_CPP: return "C++";
		default: return _("Text");
	}
}
