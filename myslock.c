/* See LICENSE file for license details. */
// #define _XOPEN_SOURCE 500

#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

#define VERSION "1.1-orium"

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;

static void
die(const char *errstr, ...) {
	va_list ap;
    
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

#ifndef HAVE_BSD_AUTH
static const char *
getpw(void) { /* only run as root */
	const char *rval;
	struct passwd *pw;
    
	pw = getpwuid(getuid());
	if(!pw)
		die("slock: cannot retrieve password entry (make sure to suid or sgid slock)");
	endpwent();
	rval =  pw->pw_passwd;
    
#if HAVE_SHADOW_H
	if (strlen(rval) >= 1) { /* kludge, assumes pw placeholder entry has len >= 1 */
		struct spwd *sp;
		sp = getspnam(getenv("USER"));
		if(!sp)
			die("slock: cannot retrieve shadow entry (make sure to suid or sgid slock)\n");
		endspent();
		rval = sp->sp_pwdp;
	}
#endif
    
	/* drop privileges */
	if(setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0)
		die("slock: cannot drop privileges");
	return rval;
}
#endif

static void
display_msg(Display *dpy, Window win, const char *msg)
{
    static GC gc;
    static Bool initialized=False;
    
    if (!initialized)
    {
        gc=XCreateGC(dpy,win,(unsigned long)0,NULL);
        
        initialized=True;
    }
    
    XSetForeground(dpy,gc,0xb00000);
    
    XClearWindow(dpy,win);
    
    XDrawString(dpy,win,gc,0,12,msg,strlen(msg));
    
    XFlush(dpy);
}

static void
#ifdef HAVE_BSD_AUTH
readpw(Display *dpy)
#else
    readpw(Display *dpy, const char *pws)
#endif
{
	char buf[32], passwd[256];
	int num, screen;
	unsigned int len;
	KeySym ksym;
	XEvent ev;
    Bool pwmode;
    
	len = 0;
    
	running = True;
    pwmode = False;
    
	/* As "slock" stands for "Simple X display locker", the DPMS settings
	 * had been removed and you can set it with "xset" or some other
	 * utility. This way the user can easily set a customized DPMS
	 * timeout. */
	while(running && !XNextEvent(dpy, &ev)) {
		if(ev.type == KeyPress) {
			buf[0] = 0;
			num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
			if(IsKeypadKey(ksym)) {
				if(ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
               || IsMiscFunctionKey(ksym) || IsPFKey(ksym)
               || IsPrivateKeypadKey(ksym))
				continue;
			switch(ksym) {
			case XK_Return:
                if (!pwmode)
                    break;
                
				passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
				running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
				running = strcmp(crypt(passwd, pws), pws);
#endif
				if(running != False)
                {
                    
                    display_msg(dpy,(*locks)->win,"wrong password!");
                    sleep(1);
                }
                
				len = 0;
                pwmode=False;
                display_msg(dpy,(*locks)->win,"");
                
				break;
			case XK_Escape:
                pwmode=False;
                display_msg(dpy,(*locks)->win,"");
				len = 0;
				break;
			case XK_BackSpace:
				if(len)
					--len;
				break;
			default:
				if(num && !iscntrl((int) buf[0]) && (len + num < sizeof passwd)) {
					memcpy(passwd + len, buf, num);
					len += num;
                    passwd[len]='\0';
                    
                    if (!strcmp(passwd,"pw"))
                    {
                        pwmode=True;
                        len=0;
                        
                        display_msg(dpy,(*locks)->win,"pw-mode");
                    }
				}
				break;
			}
		}
		else for(screen = 0; screen < nscreens; screen++)
                 XRaiseWindow(dpy, locks[screen]->win);
	}
}

static void
unlockscreen(Display *dpy, Lock *lock) {
	if(dpy == NULL || lock == NULL)
		return;
    
	XUngrabPointer(dpy, CurrentTime);
    
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);
    
	free(lock);
}

static Lock *
lockscreen(Display *dpy, int screen) {
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned int len;
	Lock *lock;
	XColor black, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;
    
	if(dpy == NULL || screen < 0)
		return NULL;
    
	lock = malloc(sizeof(Lock));
	if(lock == NULL)
		return NULL;
    
	lock->screen = screen;
    
	lock->root = RootWindow(dpy, lock->screen);
    
	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = BlackPixel(dpy, lock->screen);
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
                              0, DefaultDepth(dpy, lock->screen), CopyFromParent,
                              DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), "black", &black, &dummy);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &black, &black, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);
	for(len = 1000; len; len--) {
		if(XGrabPointer(dpy, lock->root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                        GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if(running && (len > 0)) {
		for(len = 1000; len; len--) {
			if(XGrabKeyboard(dpy, lock->root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
               == GrabSuccess)
				break;
			usleep(1000);
		}
	}
    
	running &= (len > 0);
    
	if(!running) {
		unlockscreen(dpy, lock);
		lock = NULL;
	}
	else 
		XSelectInput(dpy, lock->root, SubstructureNotifyMask);
    
	return lock;
}

static void
usage(void) {
	fprintf(stderr, "usage: slock [-v]\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv) {
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int screen;
    
	if((argc == 2) && !strcmp("-v", argv[1]))
		die("slock-%s, © 2006-2013 Anselm R Garbe, "
            "Diogo Sousa (orium)\n", VERSION);
	else if(argc != 1)
		usage();
    
	if(!getpwuid(getuid()))
		die("slock: no passwd entry for you");
    
#ifndef HAVE_BSD_AUTH
	pws = getpw();
#endif
    
	if(!(dpy = XOpenDisplay(0)))
		die("slock: cannot open display");
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	locks = malloc(sizeof(Lock *) * nscreens);
	if(locks == NULL)
		die("slock: malloc: %s", strerror(errno));
    
    
	int nlocks = 0;
	for(screen = 0; screen < nscreens; screen++) {
		if ( (locks[screen] = lockscreen(dpy, screen)) != NULL)
			nlocks++;
	}
    
	XSync(dpy, False);
    
	/* Did we actually manage to lock something? */
	if (nlocks == 0) { // nothing to protect
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}
    
	/* Everything is now blank. Now wait for the correct password. */
#ifdef HAVE_BSD_AUTH
	readpw(dpy);
#else
	readpw(dpy, pws);
#endif
    
	/* Password ok, unlock everything and quit. */
	for(screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);
    
	free(locks);
	XCloseDisplay(dpy);
    
	return 0;
}
