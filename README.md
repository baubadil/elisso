# elisso

Yet another file manager for Gtk+ 3 (Linux(. This imitates the look for Nemo (or pre-fork Nautilus for that matter),
and should eventually have all the functionality of it. The reason for the rewrite is:

1) It should have proper multithreading and be as fast as possible, especially with folders with lots
   of items and symlinks, including thumbnails.

2) It should display a directory tree on the left, and do it correctly (that is, only display an expander
   if there is at least one subfolder). That should still be fast. No file manager since the OS/2 
   Workplace Shell has been able to do  that.

3) It should not crash.

It also serves as an exercise and testcase how to write a pretty application in modern C++11 using gtkmm 3, particularly the threading and concurrency with gtkmm and std::mutex and std::condition_variable.

Biggies that are still missing:

1) File operations. Only trashing and renaming files is implemented so far, copy/move is missing.

2) Drag and drop doesn't work either.

3) National language support. Strings are not yet marked for gettext.

4) The whole GNOME virtual file system thing. Internally Gio::File is used so that could be implemented.

## Building

Development happens on 64-bit Gentoo Linux, but other Linuxes should work. Other operating systems have not been
tested, but patches are welcome.

Requirements:

 1) The elisso repository requires the XWP library from phoxygen (https://github.com/baubadil/phoxygen) to build. 
    I haven't yet figured out how to do this with git submodules. Instead, the elisso tree has symlinks that point 
    into ../phoxygen/include and ../phoxygen/src. So clone both the phoxygen and the elisso repos under the same 
    parent directory (e.g. as src/phoxygen and src/elisso).

 2) Like phoxygen, elisso uses kBuild (the VirtualBox build system; http://trac.netlabs.org/kbuild). On Gentoo, 
    it's dev-util/kbuild and should be installed already if you have VirtualBox installed as it's required for 
    building it. On Debian it's "kbuild".

 3) Like phoxygen, elisso requires libpcre for fast regular expressions. pcre.h must be in INCLUDE somewhere and 
    libpcre must be somewhere where the linker can find it. On Gentoo it seems to be installed pretty much by 
    default, on Debian you need "libpcre3-dev".

Run "kmk" in the elisso source root directory to build. "kmk" is the make utility of kBuild. 
"kmk BUILD_TYPE=debug" will create a debug build instead of a release build.

There is no configuration presently, nor is there any install. After building, you will find the executable under out/linux.amd64/{release|debug}/stage/bin/elisso.

## Hacking the GtkIconView

Note that currently {include|src}/x-gtk contain a heavily edited copy of the GtkIconView control from GTK+ 3.22
(and gtkmm bindings as well). The code is functionally identical to the GTK IconView control but with edits
to try and fix the performance bottlenecks therein with folders that have more than a few hundred files. 
The replacement control has the same API but is called XGtkIconView to avoid conflicts. Depending on whether 
USE_XICONVIEW is defined at compile time in include/elisso/elisso.h, either the stock GtkIconView or the 
improved XGtkIconView control is used. 

Its still slightly buggy. Eventually I will port the changes that actually made a speed difference back to GTK+ 3.22 and supply a patch.

Enjoy!

