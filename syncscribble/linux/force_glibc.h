/*
This is taken from force_link_glibc_2.19.h (Ubuntu 14.04 era) from https://github.com/wheybags/glibc_version_header - technically libstdc++ must be recomplied with this header too, but it seems that for Write, only pow() and exp() use a recent glibc version, so it should be safe.  To be extra safe, we will only override pow() and exp().  But we have to take care if we update libstdc++ (currently using g++ 8.2)!

Use `strings <executable> | grep "GLIBC_"` to see what symbol versions are being requested.

The correct way to build a portable application for Linux is to just build on the oldest version of Linux you want to support.  In theory, cross-compiling with, e.g., github.com/theopolis/build-anywhere is possible, but requires all dependencies be rebuilt (including, e.g, X11) ... so unless a recent compiler is essential, it's easier to just build
on an old version of Linux.

Update Oct 2022: glibc 2.34 introduces __libc_start_main@GLIBC_2.34 which cannot be prevented with symver, at least not without
 something like -nostdlib -nostartfiles, or editing the ELF ( https://www.lightofdawn.org/wiki/wiki.cgi/NewAppsOnOldGlibc ), or
 perhaps -Wl,--wrap ...  In any case, some libstdc++ (GLIBCXX) fn versions have also been updated, so we have to give up on the
 symver hack and just build with an older toolchain.  Fortunately, debootstrap makes it easy to setup an older version of Debian
 in a chroot.
*/

#if !defined(SET_GLIBC_LINK_VERSIONS_HEADER) && !defined(__ASSEMBLER__)
#define SET_GLIBC_LINK_VERSIONS_HEADER
__asm__(".symver exp,exp@GLIBC_2.2.5");
__asm__(".symver pow,pow@GLIBC_2.2.5");
__asm__(".symver log,log@GLIBC_2.2.5");
__asm__(".symver expf,expf@GLIBC_2.2.5");
__asm__(".symver powf,powf@GLIBC_2.2.5");
__asm__(".symver logf,logf@GLIBC_2.2.5");
#endif
