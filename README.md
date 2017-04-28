# elisso

Yet another file manager for Gtk+ 3. This imitates the look for Nemo (or pre-fork Nautilus for that matter),
and should eventually have all the functionality of it. The reason for the rewrite is:

1) It should have proper multithreading and be as fast as possible, especially with folders with lots
   of items and symlinks.

2) It should display a directory tree on the left, and do it correctly (that is, only display an expander
   if there is at least one subfolder).

3) It should be written in modern C++11 using gtkmm 3, particularly as an exercise how to do threading
   and concurrency with gtkmm and std::mutex and std::condition_variable.

4) It should not crash.

Biggies that are still missing:

1) File operations. Only trashing files is implemented so far, copy/move is missing.

2) Drag and drop doesn't either.

3) National language support. Strings are not yet marked for gettext.


