#!/bin/sh
(cd ext/rbtrace/; ruby extconf.rb; CFLAGS=-g make) && ruby -Ilib:ext -r rbtrace demo.rb
