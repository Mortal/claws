#!/bin/sh
# Copyright 1999-2014 the Claws Mail team.
# This file is part of Claws Mail package, and distributed under the
# terms of the General Public License version 3 (or later).
# See COPYING file for license details.


aclocal -I m4 \
  && libtoolize --force --copy \
  && autoheader \
  && automake --add-missing --foreign --copy \
  && autoconf 
if test -z "$NOCONFIGURE"; then
exec ./configure --enable-maintainer-mode $@
fi   
