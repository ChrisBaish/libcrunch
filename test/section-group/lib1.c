#include <dlfcn.h>
#include "libcrunch.h"

void *l1(int arg)
{
	void *resolved = dlsym(__libcrunch_my_typeobj(), "__uniqtype__signed_int");
	return resolved;
}