#ifndef __WINUTIL_H
#define __WINUTIL_H

#include <malloc.h>

#define strdupa(str) strcpy(_malloca(strlen(str) + 1), (str))

PVOID mempcpy(PVOID dest, const PVOID src, SIZE_T count);

#endif
