/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#define LENGTH(X)       (sizeof X / sizeof X[0])
#if HAVE_SHADOW_H
#include <shadow.h>
#endif
#include <X11/Xmd.h>
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <Imlib2.h>

#include "arg.h"
#include "util.h"

char *argv0;
/* global count to prevent repeated error messages */
int count_error = 0;

enum {
	INIT,
	INPUT,
	INPUT_ALT,
	FAILED,
	NUMCOLS
};

#include "config.h"

struct lock {
	int screen;
	Window root, win;
	Pixmap pmap;
	Pixmap bgmap;
	unsigned long colors[NUMCOLS];
	unsigned int x, y;
	unsigned int xoff, yoff, mw, mh;
	Drawable drawable;
	GC gc;
	XRectangle rectangles[LENGTH(rectangles)];
};

struct xrandr {
	int active;
	int evbase;
	int errbase;
};

Imlib_Image image;

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
#include <fcntl.h>
#include <linux/oom.h>

static void
dontkillme(void)
{
	FILE *f;
	const char oomfile[] = "/proc/self/oom_score_adj";

	if (!(f = fopen(oomfile, "w"))) {
		if (errno == ENOENT)
			return;
		die("slock: fopen %s: %s\n", oomfile, strerror(errno));
	}
	fprintf(f, "%d", OOM_SCORE_ADJ_MIN);
	if (fclose(f)) {
		if (errno == EACCES)
			die("slock: unable to disable OOM killer. "
			    "Make sure to suid or sgid slock.\n");
		else
			die("slock: fclose %s: %s\n", oomfile, strerror(errno));
	}
}
#endif

static void
writemessage(Display *dpy, Window win, int screen)
{
	int len, line_len, width, height, s_width, s_height, i, j, k, tab_replace, tab_size;
	XGCValues gr_values;
	XFontStruct *fontinfo;
	XColor color, dummy;
	XineramaScreenInfo *xsi;
	GC gc;
	fontinfo = XLoadQueryFont(dpy, font_name);

	if (fontinfo == NULL) {
		if (count_error == 0) {
			fprintf(stderr, "slock: Unable to load font \"%s\"\n", font_name);
			fprintf(stderr, "slock: Try listing fonts with 'slock -f'\n");
			count_error++;
		}
		return;
	}

	tab_size = 8 * XTextWidth(fontinfo, " ", 1);

	XAllocNamedColor(dpy, DefaultColormap(dpy, screen),
		 text_color, &color, &dummy);

	gr_values.font = fontinfo->fid;
	gr_values.foreground = color.pixel;
	gc=XCreateGC(dpy,win,GCFont+GCForeground, &gr_values);

	/*  To prevent "Uninitialized" warnings. */
	xsi = NULL;

	/*
	 * Start formatting and drawing text
	 */

	len = strlen(message);

	/* Max max line length (cut at '\n') */
	line_len = 0;
	k = 0;
	for (i = j = 0; i < len; i++) {
		if (message[i] == '\n') {
			if (i - j > line_len)
				line_len = i - j;
			k++;
			i++;
			j = i;
		}
	}
	/* If there is only one line */
	if (line_len == 0)
		line_len = len;

	if (XineramaIsActive(dpy)) {
		xsi = XineramaQueryScreens(dpy, &i);
		s_width = xsi[0].width;
		s_height = xsi[0].height;
	} else {
		s_width = DisplayWidth(dpy, screen);
		s_height = DisplayHeight(dpy, screen);
	}

	height = s_height*3/7 - (k*20)/3;
	width  = (s_width - XTextWidth(fontinfo, message, line_len))/2;

	/* Look for '\n' and print the text between them. */
	for (i = j = k = 0; i <= len; i++) {
		/* i == len is the special case for the last line */
		if (i == len || message[i] == '\n') {
			tab_replace = 0;
			while (message[j] == '\t' && j < i) {
				tab_replace++;
				j++;
			}

			XDrawString(dpy, win, gc, width + tab_size*tab_replace, height + 20*k, message + j, i - j);
			while (i < len && message[i] == '\n') {
				i++;
				j = i;
				k++;
			}
		}
	}

	/* xsi should not be NULL anyway if Xinerama is active, but to be safe */
	if (XineramaIsActive(dpy) && xsi != NULL)
			XFree(xsi);
}

static const char *
gethash(void)
{
	const char *hash;
	struct passwd *pw;

	/* Check if the current user has a password entry */
	errno = 0;
	if (!(pw = getpwuid(getuid()))) {
		if (errno)
			die("slock: getpwuid: %s\n", strerror(errno));
		else
			die("slock: cannot retrieve password entry\n");
	}
	hash = pw->pw_passwd;

#if HAVE_SHADOW_H
	if (!strcmp(hash, "x")) {
		struct spwd *sp;
		if (!(sp = getspnam(pw->pw_name)))
			die("slock: getspnam: cannot retrieve shadow entry. "
			    "Make sure to suid or sgid slock.\n");
		hash = sp->sp_pwdp;
	}
#else
	if (!strcmp(hash, "*")) {
#ifdef __OpenBSD__
		if (!(pw = getpwuid_shadow(getuid())))
			die("slock: getpwnam_shadow: cannot retrieve shadow entry. "
			    "Make sure to suid or sgid slock.\n");
		hash = pw->pw_passwd;
#else
		die("slock: getpwuid: cannot retrieve shadow entry. "
		    "Make sure to suid or sgid slock.\n");
#endif /* __OpenBSD__ */
	}
#endif /* HAVE_SHADOW_H */

	return hash;
}

static void
resizerectangles(struct lock *lock)
{
	int i;

	for (i = 0; i < LENGTH(rectangles); i++){
		lock->rectangles[i].x = (rectangles[i].x * logosize)
                                + lock->xoff + ((lock->mw) / 2) - (logow / 2 * logosize);
		lock->rectangles[i].y = (rectangles[i].y * logosize)
                                + lock->yoff + ((lock->mh) / 2) - (logoh / 2 * logosize);
		lock->rectangles[i].width = rectangles[i].width * logosize;
		lock->rectangles[i].height = rectangles[i].height * logosize;
	}
}

static void
drawlogo(Display *dpy, struct lock *lock, int color)
{
	/*
	XSetForeground(dpy, lock->gc, lock->colors[BACKGROUND]);
	XFillRectangle(dpy, lock->drawable, lock->gc, 0, 0, lock->x, lock->y); */
	lock->drawable = lock->bgmap;
	XSetForeground(dpy, lock->gc, lock->colors[color]);
	XFillRectangles(dpy, lock->drawable, lock->gc, lock->rectangles, LENGTH(rectangles));
	XCopyArea(dpy, lock->drawable, lock->win, lock->gc, 0, 0, lock->x, lock->y, 0, 0);
	XSync(dpy, False);
}

static void
readpw(Display *dpy, struct xrandr *rr, struct lock **locks, int nscreens,
       const char *hash)
{
	XRRScreenChangeNotifyEvent *rre;
	char buf[32], passwd[256], *inputhash;
	int num, screen, running, failure, oldc;
	unsigned int len, color;
	KeySym ksym;
	XEvent ev;

	len = 0;
	running = 1;
	failure = 0;
	oldc = INIT;

	while (running && !XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress) {
			explicit_bzero(&buf, sizeof(buf));
			num = XLookupString(&ev.xkey, buf, sizeof(buf), &ksym, 0);
			if (IsKeypadKey(ksym)) {
				if (ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if (IsFunctionKey(ksym) ||
			    IsKeypadKey(ksym) ||
			    IsMiscFunctionKey(ksym) ||
			    IsPFKey(ksym) ||
			    IsPrivateKeypadKey(ksym))
				continue;
			switch (ksym) {
			case XK_Return:
				passwd[len] = '\0';
				errno = 0;
				if (!(inputhash = crypt(passwd, hash)))
					fprintf(stderr, "slock: crypt: %s\n", strerror(errno));
				else
					running = !!strcmp(inputhash, hash);
				if (running) {
					XBell(dpy, 100);
					failure = 1;
				}
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_Escape:
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_BackSpace:
				if (len)
					passwd[--len] = '\0';
				break;
			default:
				if (ksym == XK_u && ev.xkey.state & ControlMask) {
					explicit_bzero(&passwd, sizeof(passwd));
					failure = 0;
					len = 0;
					break;
				}
				if (controlkeyclear && iscntrl((int)buf[0]))
					continue;
				if (num && (len + num < sizeof(passwd))) {
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}
			color = len ? (len%2 ? INPUT : INPUT_ALT)
						: ((failure || failonclear) ? FAILED : INIT);
			if (running && oldc != color) {
				for (screen = 0; screen < nscreens; screen++) {
					drawlogo(dpy, locks[screen], color);
					writemessage(dpy, locks[screen]->win, screen);
				}
				oldc = color;
			}
		} else if (rr->active && ev.type == rr->evbase + RRScreenChangeNotify) {
			rre = (XRRScreenChangeNotifyEvent*)&ev;
			for (screen = 0; screen < nscreens; screen++) {
				if (locks[screen]->win == rre->window) {
					if (rre->rotation == RR_Rotate_90 ||
					    rre->rotation == RR_Rotate_270)
						XResizeWindow(dpy, locks[screen]->win,
						              rre->height, rre->width);
					else
						XResizeWindow(dpy, locks[screen]->win,
						              rre->width, rre->height);
					XClearWindow(dpy, locks[screen]->win);
					break;
				}
			}
		} else {
			for (screen = 0; screen < nscreens; screen++)
				XRaiseWindow(dpy, locks[screen]->win);
		}
	}
}

static struct lock *
lockscreen(Display *dpy, struct xrandr *rr, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int i, ptgrab, kbgrab;
	struct lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;
#ifdef XINERAMA
	XineramaScreenInfo *info;
	int n;
#endif

	if (dpy == NULL || screen < 0 || !(lock = malloc(sizeof(struct lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	if(image)
	{
		lock->bgmap = XCreatePixmap(dpy, lock->root, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen), DefaultDepth(dpy, lock->screen));
		imlib_context_set_image(image);
		imlib_context_set_display(dpy);
		imlib_context_set_visual(DefaultVisual(dpy, lock->screen));
		imlib_context_set_colormap(DefaultColormap(dpy, lock->screen));
		imlib_context_set_drawable(lock->bgmap);
		imlib_render_image_on_drawable(0, 0);
		imlib_free_image();
	}

	for (i = 0; i < NUMCOLS; i++) {
		XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen),
		                 colorname[i], &color, &dummy);
		lock->colors[i] = color.pixel;
	}

	lock->x = DisplayWidth(dpy, lock->screen);
	lock->y = DisplayHeight(dpy, lock->screen);
#ifdef XINERAMA
	if ((info = XineramaQueryScreens(dpy, &n))) {
		lock->xoff = info[0].x_org;
		lock->yoff = info[0].y_org;
		lock->mw = info[0].width;
		lock->mh = info[0].height;
	} else
#endif
	{
		lock->xoff = lock->yoff = 0;
		lock->mw = lock->x;
		lock->mh = lock->y;
	}
	lock->drawable = XCreatePixmap(dpy, lock->root,
								lock->x, lock->y, DefaultDepth(dpy, screen));
	lock->gc = XCreateGC(dpy, lock->root, 0, NULL);
	XSetLineAttributes(dpy, lock->gc, 1, LineSolid, CapButt, JoinMiter);


	/* init */
	wa.override_redirect = 1;
	lock->win = XCreateWindow(dpy, lock->root, 0, 0,
								lock->x, lock->y,
	                          0, DefaultDepth(dpy, lock->screen),
	                          CopyFromParent,
	                          DefaultVisual(dpy, lock->screen),
	                          CWOverrideRedirect | CWBackPixel, &wa);
	if(lock->bgmap)
		XSetWindowBackgroundPixmap(dpy, lock->win, lock->bgmap);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap,
	                                &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);

	resizerectangles(lock);

	/* Try to grab mouse pointer *and* keyboard for 600ms, else fail the lock */
	for (i = 0, ptgrab = kbgrab = -1; i < 6; i++) {
		if (ptgrab != GrabSuccess) {
			ptgrab = XGrabPointer(dpy, lock->root, False,
			                      ButtonPressMask | ButtonReleaseMask |
			                      PointerMotionMask, GrabModeAsync,
			                      GrabModeAsync, None, invisible, CurrentTime);
		}
		if (kbgrab != GrabSuccess) {
			kbgrab = XGrabKeyboard(dpy, lock->root, True,
			                       GrabModeAsync, GrabModeAsync, CurrentTime);
		}

		/* input is grabbed: we can lock the screen */
		if (ptgrab == GrabSuccess && kbgrab == GrabSuccess) {
			XMapRaised(dpy, lock->win);
			if (rr->active)
				XRRSelectInput(dpy, lock->win, RRScreenChangeNotifyMask);

			XSelectInput(dpy, lock->root, SubstructureNotifyMask);
			drawlogo(dpy, lock, INIT);
			return lock;
		}

		/* retry on AlreadyGrabbed but fail on other errors */
		if ((ptgrab != AlreadyGrabbed && ptgrab != GrabSuccess) ||
		    (kbgrab != AlreadyGrabbed && kbgrab != GrabSuccess))
			break;

		usleep(100000);
	}

	/* we couldn't grab all input: fail out */
	if (ptgrab != GrabSuccess)
		fprintf(stderr, "slock: unable to grab mouse pointer for screen %d\n",
		        screen);
	if (kbgrab != GrabSuccess)
		fprintf(stderr, "slock: unable to grab keyboard for screen %d\n",
		        screen);
	return NULL;
}

static void
usage(void)
{
	die("usage: slock [-v] [-f] [-m message] [cmd [arg ...]]\n");

}

int
main(int argc, char **argv) {
	struct xrandr rr;
	struct lock **locks;
	struct passwd *pwd;
	struct group *grp;
	uid_t duid;
	gid_t dgid;
	const char *hash;
	Display *dpy;
	int i, s, nlocks, nscreens;
	int count_fonts;
	char **font_names;
	CARD16 standby, suspend, off;
	BOOL dpms_state;

	ARGBEGIN {
	case 'v':
		puts("slock-"VERSION);
		return 0;
	case 'm':
		message = EARGF(usage());
		break;
	case 'f':
		if (!(dpy = XOpenDisplay(NULL)))
			die("slock: cannot open display\n");
		font_names = XListFonts(dpy, "*", 10000 /* list 10000 fonts*/, &count_fonts);
		for (i=0; i<count_fonts; i++) {
			fprintf(stderr, "%s\n", *(font_names+i));
		}
		return 0;
	default:
		usage();
	} ARGEND

	/* validate drop-user and -group */
	errno = 0;
	if (!(pwd = getpwnam(user)))
		die("slock: getpwnam %s: %s\n", user,
		    errno ? strerror(errno) : "user entry not found");
	duid = pwd->pw_uid;
	errno = 0;
	if (!(grp = getgrnam(group)))
		die("slock: getgrnam %s: %s\n", group,
		    errno ? strerror(errno) : "group entry not found");
	dgid = grp->gr_gid;

#ifdef __linux__
	dontkillme();
#endif

	hash = gethash();
	errno = 0;
	if (!crypt("", hash))
		die("slock: crypt: %s\n", strerror(errno));

	if (!(dpy = XOpenDisplay(NULL)))
		die("slock: cannot open display\n");

	/* drop privileges */
	if (setgroups(0, NULL) < 0)
		die("slock: setgroups: %s\n", strerror(errno));
	if (setgid(dgid) < 0)
		die("slock: setgid: %s\n", strerror(errno));
	if (setuid(duid) < 0)
		die("slock: setuid: %s\n", strerror(errno));

	/*Create screenshot Image*/
	Screen *scr = ScreenOfDisplay(dpy, DefaultScreen(dpy));
	image = imlib_create_image(scr->width,scr->height);
	imlib_context_set_image(image);
	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy,0));
	imlib_context_set_drawable(RootWindow(dpy,XScreenNumberOfScreen(scr)));
	imlib_copy_drawable_to_image(0,0,0,scr->width,scr->height,0,0,1);

#ifdef BLUR

	/*Blur function*/
	imlib_image_blur(blurRadius);
#endif // BLUR

#ifdef PIXELATION
	/*Pixelation*/
	int width = scr->width;
	int height = scr->height;

	for(int y = 0; y < height; y += pixelSize)
	{
		for(int x = 0; x < width; x += pixelSize)
		{
			int red = 0;
			int green = 0;
			int blue = 0;

			Imlib_Color pixel;
			Imlib_Color* pp;
			pp = &pixel;
			for(int j = 0; j < pixelSize && j < height; j++)
			{
				for(int i = 0; i < pixelSize && i < width; i++)
				{
					imlib_image_query_pixel(x+i,y+j,pp);
					red += pixel.red;
					green += pixel.green;
					blue += pixel.blue;
				}
			}
			red /= (pixelSize*pixelSize);
			green /= (pixelSize*pixelSize);
			blue /= (pixelSize*pixelSize);
			imlib_context_set_color(red,green,blue,pixel.alpha);
			imlib_image_fill_rectangle(x,y,pixelSize,pixelSize);
			red = 0;
			green = 0;
			blue = 0;
		}
	}


#endif

	/* check for Xrandr support */
	rr.active = XRRQueryExtension(dpy, &rr.evbase, &rr.errbase);

	/* get number of screens in display "dpy" and blank them */
	nscreens = ScreenCount(dpy);
	if (!(locks = calloc(nscreens, sizeof(struct lock *))))
		die("slock: out of memory\n");
	for (nlocks = 0, s = 0; s < nscreens; s++) {
		if ((locks[s] = lockscreen(dpy, &rr, s)) != NULL) {
			writemessage(dpy, locks[s]->win, s);
			nlocks++;
		} else {
			break;
		}
	}
	XSync(dpy, 0);

	/* did we manage to lock everything? */
	if (nlocks != nscreens)
		return 1;

	/* DPMS magic to disable the monitor */
	if (!DPMSCapable(dpy))
		die("slock: DPMSCapable failed\n");
	if (!DPMSInfo(dpy, &standby, &dpms_state))
		die("slock: DPMSInfo failed\n");
	if (!DPMSEnable(dpy) && !dpms_state)
		die("slock: DPMSEnable failed\n");
	if (!DPMSGetTimeouts(dpy, &standby, &suspend, &off))
		die("slock: DPMSGetTimeouts failed\n");
	if (!standby || !suspend || !off)
		die("slock: at least one DPMS variable is zero\n");
	if (!DPMSSetTimeouts(dpy, monitortime, monitortime, monitortime))
		die("slock: DPMSSetTimeouts failed\n");

	XSync(dpy, 0);


	/* run post-lock command */
	if (argc > 0) {
		switch (fork()) {
		case -1:
			die("slock: fork failed: %s\n", strerror(errno));
		case 0:
			if (close(ConnectionNumber(dpy)) < 0)
				die("slock: close: %s\n", strerror(errno));
			execvp(argv[0], argv);
			fprintf(stderr, "slock: execvp %s: %s\n", argv[0], strerror(errno));
			_exit(1);
		}
	}

	/* everything is now blank. Wait for the correct password */
	readpw(dpy, &rr, locks, nscreens, hash);

	for (nlocks = 0, s = 0; s < nscreens; s++) {
		XFreePixmap(dpy, locks[s]->drawable);
		XFreeGC(dpy, locks[s]->gc);
	}

	/* reset DPMS values to inital ones */
	DPMSSetTimeouts(dpy, standby, suspend, off);
	if (!dpms_state)
		DPMSDisable(dpy);
	XSync(dpy, 0);

	XCloseDisplay(dpy);

	return 0;
}
