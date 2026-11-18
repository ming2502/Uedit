#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

#define UEDIT_REGEX_FN RunRegex16
#define UEDIT_UNIT_BYTES 2
#include "search/regex_engine_impl.h"
