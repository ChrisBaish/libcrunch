/* We want to be able to treat pointers "polymorphically", i.e. 
 * via a void* lens,
 * without breaking the shadow space in the common case. */

#include <stdio.h>

void report_void(void *p)
{
	printf("Saw a void*: %p\n", p);
}

void frob_voids(void **pptr1, void **pptr2)
{
	if ((unsigned long) *pptr2 < (unsigned long) *pptr1)
	{
		printf("Frob means swap\n");
		/* swap them */
		void *tmp = *pptr2;
		*pptr2 = *pptr1;
		*pptr1 = tmp;
	}
}

/* We need to be able to cache bounds for these, but *not* have them
 * do fetches at startup as they are initialised. So make them static-storage
 * but initialize inside main(). */
int xs[2] = { 42, 69105 };
int *ptr[2];

int main(void)
{
	report_void(main);
	ptr[0] = &xs[1];
	ptr[1] = &xs[0];
	/* We want to get type info about "ptr" into the cache, so we can test 
	 * __store_pointer_nonlocal_via_voidptrptr's ability to grub around in it. */
	
	frob_voids((void**) &ptr[0], (void**) &ptr[1]);
	/* Now adjust one of them -- we should still get its bounds as if it's int*. */
	printf("After frobbing, *(ptr[0] + 1) is: %d\n", *(ptr[0] + 1));
	return 0;
}