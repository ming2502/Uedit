#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#define UEDIT_REGEX_FN RunRegex8
#define UEDIT_UNIT_BYTES 1
#include "search/regex_engine_impl.h"
