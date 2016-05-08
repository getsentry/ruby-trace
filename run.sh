#!/bin/sh
(cd ext/rbtrace/; ruby extconf.rb; CFLAGS=-g make) && ruby -Ilib:. -Ilib:ext -r rbtrace -r demo -e $1
