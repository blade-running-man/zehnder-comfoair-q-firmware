// The three attributes that keep a phone out of a password.
//
// Run: node src/pages/settings/tests/verbatim.test.ts
//
// It lives with the settings tests rather than beside the component because the component is
// .tsx and node's type stripping does not do JSX -- see harness.ts. The constant is in a .ts
// file of its own for the same reason, which is also what makes it possible to pin the values
// at all: a set of attributes written inline in TextField.tsx could only be checked by reading
// it.
//
// The failure being prevented: a mobile keyboard capitalises the first letter of a Wi-Fi key,
// the device reports an authentication failure, and nothing anywhere -- not the log, not the
// router, not the form -- shows that the typed password and the sent password differ by one
// bit. It is the least diagnosable support ticket this page can produce.

import { VERBATIM_INPUT } from "../../../components/ui/TextField/verbatim.ts";
import { eq, report } from "./harness.ts";

eq(VERBATIM_INPUT.autocapitalize, "none",
   "'none' and not 'off': 'off' is the legacy spelling and Safari treats it as 'sentences'");
eq(VERBATIM_INPUT.autocorrect, "off",
   "autocorrect is not standardised and iOS honours it -- which is the platform that autocorrects");
eq(VERBATIM_INPUT.spellcheck, false,
   "the BOOLEAN false, not the string: preact assigns the DOM property, and a truthy string "
   + "'false' would switch spellcheck ON");
eq(Object.keys(VERBATIM_INPUT).sort(), ["autocapitalize", "autocorrect", "spellcheck"],
   "these three and nothing else -- an autocomplete rule belongs to the field that needs it, "
   + "not to every field that happens to be machine-read");

report("verbatim");
