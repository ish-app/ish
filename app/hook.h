//
//  hook.h
//  iSH
//
//  Created by Saagar Jha on 12/29/22.
//

#ifndef hook_h
#define hook_h

#include <stdbool.h>

void *find_symbol(void *base, char *symbol);
bool hook(void *old, void *new);

#endif /* hook_h */
