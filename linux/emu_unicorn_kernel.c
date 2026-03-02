#include <linux/moduleparam.h>
#include "emu_unicorn.h"

#define KBUILD_MODNAME "unicorn"

bool unicorn_trace;
module_param_named(trace, unicorn_trace, bool, 0);
