
CC=gcc
CFLAGS=-std=gnu99 -c -Wall -pedantic -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED
LDFLAGS=-rdynamic
SOURCES=src/termomix.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=termomix
LIBS=-lgtk-3 -lgdk-3 -latk-1.0 -lgio-2.0 -lgdk_pixbuf-2.0 \
-lcairo-gobject -lpango-1.0 -lcairo -lgobject-2.0 -lglib-2.0 -lvte2_90 -lgtk-3 \
-lgdk-3 -latk-1.0 -lgdk_pixbuf-2.0 -lcairo-gobject -lpango-1.0 \
-lgio-2.0 -lgobject-2.0 -lglib-2.0 -lcairo -lX11 -lm -lvte2_90 -lX11 -lm
INCLUDES=-I/usr/include/gtk-3.0 \
-I/usr/include/pango-1.0 \
-I/usr/include/gio-unix-2.0 \
-I/usr/include/atk-1.0 -I/usr/include/cairo \
-I/usr/include/gdk-pixbuf-2.0 \
-I/usr/include/freetype2 \
-I/usr/include/glib-2.0 \
-I/usr/lib/x86_64-linux-gnu/glib-2.0/include \
-I/usr/include/pixman-1 \
-I/usr/include/libpng12 \
-I/usr/include/vte-2.90

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(LIBS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f src/*.o termomix

install:
	cp termomix /usr/local/bin
