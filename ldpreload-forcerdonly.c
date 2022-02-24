/*
 * Copyright (C) 2014 Andrew Ayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization.
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>

typedef int (*open_fun_ptr)(const char*, int, ...);

int open (const char* pathname, int flags, ...)
{
	open_fun_ptr	orig_open = (open_fun_ptr)dlsym(RTLD_NEXT, "open");

	if (flags & (O_WRONLY|O_CREAT)) {
		errno = EROFS;
		return -1;
	} else if (flags & O_RDWR) {
		flags &= ~O_RDWR;
		flags |= O_RDONLY;
	}
	return orig_open(pathname, flags);
}
int open64 (const char* pathname, int flags, ...)
{
	open_fun_ptr	orig_open64 = (open_fun_ptr)dlsym(RTLD_NEXT, "open64");

	if (flags & (O_WRONLY|O_CREAT)) {
		errno = EROFS;
		return -1;
	} else if (flags & O_RDWR) {
		flags &= ~O_RDWR;
		flags |= O_RDONLY;
	}
	return orig_open64(pathname, flags);
}
