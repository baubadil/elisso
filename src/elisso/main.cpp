/*
 * elisso -- PHP documentation tool. (C) 2015--2016 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#define DEF_STRING_IMPLEMENTATION

#include <gtkmm.h>

#include <iostream>


/***************************************************************************
 *
 *  Globals
 *
 **************************************************************************/



/***************************************************************************
 *
 *  Entry point
 *
 **************************************************************************/

int main(int argc, char *argv[])
{
  auto app =
    Gtk::Application::create(argc, argv,
      "org.gtkmm.examples.base");

  Gtk::Window window;
  window.set_default_size(200, 200);

  return app->run(window);
}

