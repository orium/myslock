bin=myslock
objs=myslock.o
cflags=-std=gnu99 -Wall -DHAVE_SHADOW_H
ldflags=-L/usr/lib -lc -lcrypt -L/usr/X11R6/lib -lX11 -lXext

all: $(objs)
	gcc $(cflags) $(ldflags) $(objs) -o $(bin)

myslock.o: myslock.c
	gcc -c $(cflags) -Wall $<

clean:
	rm -f *.o $(bin)

.PHONY: all clean install

install: $(bin)
	cp $(bin) /usr/bin/
	chmod 755 /usr/bin/$(bin)
	chmod u+s /usr/bin/$(bin)
