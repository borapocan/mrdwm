/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
//#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <xcb/xcb.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>
#include <X11/Xlib-xcb.h>
#include <Imlib2.h>
#include <xcb/res.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define HIDDEN(C)               ((getstate(C->win) == IconicState))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

#define STATUSIMG_CACHE_MAX 64
#define STATUSIMG_PATH_MAX  4096

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
//enum { SchemeNorm, SchemeSel }; /* color schemes */
//enum { SchemeNorm, SchemeSel, SchemeHov, SchemeHid }; /* color schemes */
enum { SchemeNorm, SchemeSel, SchemeHov, SchemeHid, SchemeStatus, SchemeTagsSel, SchemeTagsNorm, SchemeInfoSel, SchemeInfoNorm }; /* color schemes */
enum { NetSupported, NetWMName, NetWMIcon, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetClientInfo, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkButton, ClkWinTitle,
       //ClkExBarLeftStatus, ClkExBarMiddle, ClkExBarRightStatus,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
typedef struct Preview Preview;
struct Preview {
	XImage *orig_image;
	XImage *scaled_image;
	Window win;
	unsigned int x, y;
	Preview *next;
};

struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, iscentered, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isterminal, noswallow;
	unsigned int icw, ich;
	Picture icon;
	pid_t pid;
	Client *next;
	Client *snext;
	Client *swallowing;
	Monitor *mon;
	Window win;
	Preview pre;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	int previewshow;
	Window tagwin;
	Pixmap *tagmap;
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;               /* bar geometry */
	int btw;              /* width of tasks portion of bar */
	int bt;               /* number of tasks */
//	int eby;	      /* extra bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	int gappx;            /* gaps between windows */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	//int extrabar;
	int hidsel;
	Client *clients;
	Client *sel;
	Client *hov;
	Client *stack;
	Monitor *next;
	Window barwin;
	//Window dockwin;
	//Window extrabarwin;
	const Layout *lt[2];
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int iscentered;
	int isfloating;
	int isterminal;
	int noswallow;
	int monitor;
} Rule;

typedef struct {
    char         path[STATUSIMG_PATH_MAX];
    Imlib_Image  img;
    int          orig_w;
    int          orig_h;
} StatusImgCache;

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void centeredmaster(Monitor *m);
static void centeredfloatingmaster(Monitor *m);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void deck(Monitor *m);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void dwindle(Monitor *mon);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void fibonacci(Monitor *mon, int s);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
//static void focusstack(const Arg *arg); // May not compile
static void focusstackvis(const Arg *arg);
static void focusstackhid(const Arg *arg);
static void focusstack(int inc, int vis);
static Atom getatomprop(Client *c, Atom prop);
static Picture geticonprop(Window w, unsigned int *icw, unsigned int *ich);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static pid_t getstatusbarpid(void);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void grid(Monitor *m);
static void hide(const Arg *arg);
static void hidewin(Client *c);
static int ispanel(Client *c);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void layoutmenu(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void runautostart(void);
static void scan(void);
static XImage *scaleimagetofit(XImage *orig_image, unsigned int tw, unsigned int th);
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setclienttagprop(Client *c);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setgaps(const Arg *arg);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void show(const Arg *arg);
static void showall(const Arg *arg);
static void showwin(Client *c);
static void showhide(Client *c);
static void sighup(int unused);
static void sigterm(int unused);
static void sigstatusbar(const Arg *arg);
static void spawn(const Arg *arg);
static void spiral(Monitor *mon);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tcl(Monitor *m);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
//static void toggleextrabar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void togglewin(const Arg *arg);
static void freeicon(Client *c);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updateicon(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);
static void previewallwin(const Arg *arg);
static void previewallwinwrap(const Arg *arg);
//static void setpreviewwindowsizepositions(unsigned int n, Monitor *m, unsigned int gappo, unsigned int gappi);
static XImage *getwindowximage(Client *c);
//static XImage *scaledownimage(XImage *orig_image, unsigned int cw, unsigned int ch);
static void showtagpreview(unsigned int i);
static void takepreview(void);
static void previewtag(const Arg *arg);
static pid_t getparentprocess(pid_t p);
static int isdescprocess(pid_t p, pid_t c);
static Client *swallowingclient(Window w);
static Client *termforwin(const Client *c);
static pid_t winpid(Window w);
static void flushimgcache(void);
static StatusImgCache * getimg(const char *path);
static int drawimg(const char *path, int x, int bh);
static int measureimg(const char *path, int bh);
static int collectpath(const char *text, int start, char *out);
/* variables */
static const char autostartblocksh[] = "autostart_blocking.sh";
static const char autostartsh[] = "autostart.sh";
//static const char dwmdir[] = "mrdwm";
//static const char localshare[] = "/usr/share/mrrobotos";
static const char broken[] = "Untitled";
static char stext[1024];
static int statusw;
static int statussig;
static pid_t statuspid = -1;
//static char estextl[256];
//static char estextr[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, dh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int restart = 0;
static int running = 1;
static int run_preview = 0;
static int run_recompile = 0;
static int preview_running = 0;

static volatile sig_atomic_t sigusr1_count = 0;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;
static xcb_connection_t *xcon;
static int vp;               /* vertical padding for bar */
static int sp;               /* side padding for bar */

static StatusImgCache _imgcache[STATUSIMG_CACHE_MAX];
static int            _imgcache_n = 0;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c)
{
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->iscentered = 0;
	c->isfloating = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isterminal = r->isterminal;
			c->noswallow = r->noswallow;
			c->iscentered = r->iscentered;
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

void
swallow(Client *p, Client *c)
{

	if (c->noswallow || c->isterminal)
		return;
	if (c->noswallow && !swallowfloating && c->isfloating)
		return;

	detach(c);
	detachstack(c);

	setclientstate(c, WithdrawnState);
	XUnmapWindow(dpy, p->win);

	p->swallowing = c;
	c->mon = p->mon;

	Window w = p->win;
	p->win = c->win;
	c->win = w;
	updatetitle(p);
	XMoveResizeWindow(dpy, p->win, p->x, p->y, p->w, p->h);
	arrange(p->mon);
	configure(p);
	updateclientlist();
}

void
unswallow(Client *c)
{
	c->win = c->swallowing->win;

	free(c->swallowing);
	c->swallowing = NULL;

	/* unfullscreen the client */
	setfullscreen(c, 0);
	updatetitle(c);
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
	setclientstate(c, NormalState);
	focus(NULL);
	arrange(c->mon);
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		//if (c->incw)
		//	*w -= *w % c->incw;
		//if (c->inch)
		//	*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
	unsigned int i, x, click;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;
	char *text, *s, ch;

	click = ClkRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		i = x = 0;
		//x += TEXTW(buttonbar);
		if (ev->x < x) {
			click = ClkButton;
		} else {
			do
				x += TEXTW(tags[i]);
			while (ev->x >= x && ++i < LENGTH(tags));
			if (i < LENGTH(tags)) {
				click = ClkTagBar;
				arg.ui = 1 << i;
				/* hide preview if we click the bar */
				if (selmon->previewshow) {
					selmon->previewshow = 0;
					XUnmapWindow(dpy, selmon->tagwin);
				}
			} else if (ev->x < x + TEXTW(selmon->ltsymbol)) {
				click = ClkLtSymbol;
			//else if (ev->x > selmon->ww - (int)TEXTW(stext))
			/* 2px right padding */
			//else if (ev->x > selmon->ww - TEXTW(stext) + lrpad - 2)
			} else if (ev->x > selmon->ww - statusw) {
				x = selmon->ww - statusw;
				click = ClkStatusText;
				statussig = 0;
				for (text = s = stext; *s && x <= ev->x; s++) {
					if ((unsigned char)(*s) < ' ') {
						ch = *s;
						*s = '\0';
						x += TEXTW(text) - lrpad;
						*s = ch;
						text = s + 1;
						if (x >= ev->x)
							break;
						statussig = ch;
					} else if (*s == '^' && *(s+1) == 'i') {
						/* measure image width */
						char imgpath[STATUSIMG_PATH_MAX];
						int end = collectpath(s + 2, 0, imgpath);
						x += measureimg(imgpath, bh) + 2;
						s += end + 1; /* skip past closing ^ */
						text = s + 1;
					} else if (*s == '^' && *(s+1) != '^') {
						/* skip other ^ commands */
						while (*s && *s != '^') s++;
					}
				}
			//else
			//	click = ClkWinTitle;
			} else {
				x += TEXTW(selmon->ltsymbol);
				if (m->bt > 0) {
					int remainder = m->btw % m->bt;
					int tabw = (1.0 / (double)m->bt) * m->btw + 1;
					Client *found = NULL;
					for (c = m->clients; c; c = c->next) {
						if (!ISVISIBLE(c) || ispanel(c))
							continue;
						int tw = tabw;
						if (remainder >= 0) {
							if (remainder == 0) tw--;
							remainder--;
						}
						if (ev->x >= x && ev->x < x + tw) {
							found = c;
							break;
						}
						x += tw;
					}
					click = ClkWinTitle;
					arg.v = found;
				}
			}
			//else {
			//	x += TEXTW(selmon->ltsymbol);
			//	c = m->clients;

			//	if (c) {
			//		do {
			//			if (!ISVISIBLE(c))
			//				continue;
			//			else
			//				x +=(1.0 / (double)m->bt) * m->btw;
			//		} while (ev->x > x && (c = c->next));

			//		click = ClkWinTitle;
			//		arg.v = c;
			//	}

			//}
		}
	}
	//else if (ev->window == selmon->dockwin) {
	//	x = 0;
	//	x += TEXTW(buttonbar);
	//	if (ev->x < x)
	//		click = ClkButton;
	//}
	else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			//buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
			buttons[i].func((click == ClkTagBar || click == ClkWinTitle) && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
centeredmaster(Monitor *m)
{
	unsigned int i, n, h, mw, mx, my, oty, ety, tw;
	Client *c;

	/* count number of clients in the selected monitor */
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	/* initialize areas */
	mw = m->ww;
	mx = 0;
	my = 0;
	tw = mw;

	if (n > m->nmaster) {
		/* go mfact box in the center if more than nmaster clients */
		mw = m->nmaster ? m->ww * m->mfact : 0;
		tw = m->ww - mw;

		if (n - m->nmaster > 1) {
			/* only one client */
			mx = (m->ww - mw) / 2;
			tw = (m->ww - mw) / 2;
		}
	}

	oty = 0;
	ety = 0;
	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
	if (i < m->nmaster) {
		/* nmaster clients are stacked vertically, in the center
		 * of the screen */
		h = (m->wh - my) / (MIN(n, m->nmaster) - i);
		resize(c, m->wx + mx, m->wy + my, mw - (2*c->bw),
		       h - (2*c->bw), 0);
		my += HEIGHT(c);
	} else {
		/* stack clients are stacked vertically */
		if ((i - m->nmaster) % 2 ) {
			h = (m->wh - ety) / ( (1 + n - i) / 2);
			resize(c, m->wx, m->wy + ety, tw - (2*c->bw),
			       h - (2*c->bw), 0);
			ety += HEIGHT(c);
		} else {
			h = (m->wh - oty) / ((1 + n - i) / 2);
			resize(c, m->wx + mx + mw, m->wy + oty,
			       tw - (2*c->bw), h - (2*c->bw), 0);
			oty += HEIGHT(c);
		}
	}
}

void
centeredfloatingmaster(Monitor *m)
{
	unsigned int i, n, w, mh, mw, mx, mxo, my, myo, tx;
	Client *c;

	/* count number of clients in the selected monitor */
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	/* initialize nmaster area */
	if (n > m->nmaster) {
		/* go mfact box in the center if more than nmaster clients */
		if (m->ww > m->wh) {
			mw = m->nmaster ? m->ww * m->mfact : 0;
			mh = m->nmaster ? m->wh * 0.9 : 0;
		} else {
			mh = m->nmaster ? m->wh * m->mfact : 0;
			mw = m->nmaster ? m->ww * 0.9 : 0;
		}
		mx = mxo = (m->ww - mw) / 2;
		my = myo = (m->wh - mh) / 2;
	} else {
		/* go fullscreen if all clients are in the master area */
		mh = m->wh;
		mw = m->ww;
		mx = mxo = 0;
		my = myo = 0;
	}

	for(i = tx = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
	if (i < m->nmaster) {
		/* nmaster clients are stacked horizontally, in the center
		 * of the screen */
		w = (mw + mxo - mx) / (MIN(n, m->nmaster) - i);
		resize(c, m->wx + mx, m->wy + my, w - (2*c->bw),
		       mh - (2*c->bw), 0);
		mx += WIDTH(c);
	} else {
		/* stack clients are stacked horizontally */
		w = (m->ww - tx) / (n - i);
		resize(c, m->wx + tx, m->wy, w - (2*c->bw),
		       m->wh - (2*c->bw), 0);
		tx += WIDTH(c);
	}
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	flushimgcache();
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	view(&a);
	selmon->lt[selmon->sellt] = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors) + 1; i++)
		free(scheme[i]);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;
	size_t i;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	for (i = 0; i < LENGTH(tags); i++)
		if (mon->tagmap[i])
			XFreePixmap(dpy, mon->tagmap[i]);
	free(mon->tagmap);
	XUnmapWindow(dpy, mon->barwin);
//	XUnmapWindow(dpy, mon->extrabarwin);
	XDestroyWindow(dpy, mon->barwin);
//	XDestroyWindow(dpy, mon->extrabarwin);
	XUnmapWindow(dpy, mon->tagwin);
	XDestroyWindow(dpy, mon->tagwin);
	free(mon);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				//XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
				XMoveResizeWindow(dpy, m->barwin, m->wx + sp, m->by + vp, m->ww -  2 * sp, bh);
				////XMoveResizeWindow(dpy, m->dockwin, m->wx, m->wh - dh, m->ww, dh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	//m->extrabar = extrabar;
	m->gappx = gappx;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	m->tagmap = ecalloc(LENGTH(tags), sizeof(Pixmap));
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	return m;
}

void
deck(Monitor *m) {
	unsigned int i, n, h, mw, my;
	Client *c;

	for(n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if(n == 0)
		return;

	if(n > m->nmaster) {
		mw = m->nmaster ? m->ww * m->mfact : 0;
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n - m->nmaster);
	}
	else
		mw = m->ww;
	for(i = my = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if(i < m->nmaster) {
			h = (m->wh - my) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), False);
			my += HEIGHT(c);
		}
		else
			resize(c, m->wx + mw, m->wy, m->ww - mw - (2*c->bw), m->wh - (2*c->bw), False);
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}


int
drawstatusbar(Monitor *m, int bh, char *stext)
{
    int   ret, i, w, x, len;
    short isCode = 0;
    char *text;
    char *p;
    char  imgpath[STATUSIMG_PATH_MAX];

    len = strlen(stext) + 1;
    if (!(text = (char *)malloc(sizeof(char) * len)))
        die("malloc");
    p = text;
    memcpy(text, stext, len);

    /* ── pass 1: measure total pixel width ─────────────────────────────── */
    w = 0;
    i = -1;
    while (text[++i]) {
        if (text[i] == '^') {
            if (!isCode) {
                isCode = 1;
                text[i] = '\0';
                w += TEXTW(text) - lrpad;
                text[i] = '^';
                if (text[++i] == 'f') {
                    w += atoi(text + ++i);
                } else if (text[i] == 'i') {
                    int end = collectpath(text, i + 1, imgpath);
                    w += measureimg(imgpath, bh) + 2; /* 2px right gap */
                    i = end - 1;
                }
            } else {
                isCode = 0;
                text = text + i + 1;
                i = -1;
            }
        }
    }
    if (!isCode)
        w += TEXTW(text) - lrpad;
    else
        isCode = 0;
    text = p;

    w += lrpad + 2; /* full font padding + 2px right edge */
    ret = x = m->ww - w;

    /* ── background rectangle ──────────────────────────────────────────── */
    drw_setscheme(drw, scheme[SchemeTagsNorm]);
    drw_rect(drw, x, 0, w, bh, 1, 1);
    x++;

    /* ── pass 2: render ────────────────────────────────────────────────── */
    i = -1;
    while (text[++i]) {
        if (text[i] == '^' && !isCode) {
            isCode = 1;

            /* render accumulated plain text before this command */
            text[i] = '\0';
            w = TEXTW(text) - lrpad;
            drw_text(drw, x, 0, w, bh, 0, text, 0);
            x += w;
            text[i] = '^';

            /* step onto command letter */
            while (text[++i] != '^') {
                if (text[i] == 'c') {
                    char buf[8];
                    memcpy(buf, (char*)text + i + 1, 7);
                    buf[7] = '\0';
                    drw_clr_create(drw, &drw->scheme[ColFg], buf);
                    i += 7;
                } else if (text[i] == 'b') {
                    char buf[8];
                    memcpy(buf, (char*)text + i + 1, 7);
                    buf[7] = '\0';
                    drw_clr_create(drw, &drw->scheme[ColBg], buf);
                    i += 7;
                } else if (text[i] == 'd') {
                    drw->scheme[ColFg] = scheme[SchemeTagsNorm][ColFg];
                    drw->scheme[ColBg] = scheme[SchemeTagsNorm][ColBg];
                } else if (text[i] == 'r') {
                    int rx = atoi(text + ++i);
                    while (text[++i] != ',');
                    int ry = atoi(text + ++i);
                    while (text[++i] != ',');
                    int rw = atoi(text + ++i);
                    while (text[++i] != ',');
                    int rh = atoi(text + ++i);
                    drw_rect(drw, rx + x, ry, rw, rh, 1, 0);
                } else if (text[i] == 'f') {
                    x += atoi(text + ++i);
                } else if (text[i] == 'i') {
                    int end = collectpath(text, i + 1, imgpath);
                    int imgw = drawimg(imgpath, x, bh);
                    x += imgw + 2; /* image + 2px right gap only */
                    i = end - 1;
                }
            }

            /* i is now on the closing '^' */
            text = text + i + 1;
            i = -1;
            isCode = 0;
        }
    }

    /* render any trailing plain text — use full TEXTW so it fits */
    if (!isCode) {
        w = TEXTW(text) - lrpad;
        drw_text(drw, x, 0, w + lrpad, bh, lrpad / 2, text, 0);
    }

    drw_setscheme(drw, scheme[SchemeTagsNorm]);
    free(p);

    return ret;
}

void
drawbar(Monitor *m)
{
	int x, w, tw = 0, n = 0, scm;
	int boxw = drw->fonts->h / 6 + 2;
	unsigned int i, occ = 0, urg = 0;
	Client *c;
	if (!m->showbar)
		return;
	if (m == selmon) {
		tw = m->ww - drawstatusbar(m, bh, stext);
		statusw = tw;
	}
	//if (m == selmon) {
	//	char *text, *s, ch;
	//	drw_setscheme(drw, scheme[SchemeStatus]);
	//	tw = statusw;
	//	x = m->ww - statusw;
	//	for (text = s = stext; *s; s++) {
	//		if ((unsigned char)(*s) < ' ') {
	//			ch = *s;
	//			*s = '\0';
	//			w = TEXTW(text) - lrpad;
	//			drw_text(drw, x, 0, w, bh, 0, text, 0);
	//			x += w;
	//			*s = ch;
	//			text = s + 1;
	//		}
	//	}
	//	w = TEXTW(text);
	//	drw_text(drw, x, 0, w, bh, 0, text, 0);
	//	tw = statusw;
	//}
	for (c = m->clients; c; c = c->next) {
		if (ispanel(c))
			continue;
		if (ISVISIBLE(c))
			n++;
		occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
		w = TEXTW(tags[i]);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeTagsSel : SchemeTagsNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
		if (occ & 1 << i)
			drw_rect(drw, x + boxw, 0, w - (2 * boxw + 1), boxw, m->tagset[m->seltags] & 1 << i, urg & 1 << i);
		x += w;
	}
	w = TEXTW(m->ltsymbol);
	drw_setscheme(drw, scheme[SchemeTagsNorm]);
	x = drw_text(drw, x, 0, w + 1, bh, lrpad / 2, m->ltsymbol, 0);
	int title_x = x;
	if ((w = m->ww - tw - x) > bh) {
		if (n > 0) {
			int remainder = w % n;
			int tabw = (1.0 / (double)n) * w + 1;
			for (c = m->clients; c; c = c->next) {
				if (ispanel(c))
					continue;
				if (!ISVISIBLE(c))
					continue;
				if (m->hov == c)
					scm = SchemeHov;
				else if (HIDDEN(c))
					scm = SchemeHid;
				else if (m->sel == c && m->hov == NULL)
					scm = SchemeSel;
				else
					scm = SchemeNorm;
				drw_setscheme(drw, scheme[scm]);
				if (remainder >= 0) {
					if (remainder == 0)
						tabw--;
					remainder--;
				}
				//drw_text(drw, x, 0, tabw - 1, bh, lrpad / 2 + (c->icon ? c->icw + ICONSPACING : 0), c->name, 0);
				int total = TEXTW(c->name) - lrpad + (c->icon ? c->icw + ICONSPACING : 0);
				int mid = (tabw - 1 - total) / 2;
				if (mid < lrpad / 2) mid = lrpad / 2;
				drw_text(drw, x, 0, tabw - 1, bh, mid + (c->icon ? c->icw + ICONSPACING : 0), c->name, 0);
				if (c->icon)
					drw_pic(drw, x + mid, (bh - c->ich) / 2, c->icw, c->ich, c->icon);
					//drw_pic(drw, x + lrpad / 2, (bh - c->ich) / 2, c->icw, c->ich, c->icon);
				XSetForeground(dpy, drw->gc, 0xFFFFFF);
				XDrawLine(dpy, drw->drawable, drw->gc, x, 0, x, bh);
				XDrawLine(dpy, drw->drawable, drw->gc, x + tabw - 1, 0, x + tabw - 1, bh);
				x += tabw;
			}
		} else {
			drw_setscheme(drw, scheme[SchemeSel]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}
	/* draw white line right of layout symbol */
	XSetForeground(dpy, drw->gc, 0xFFFFFF);
	XDrawLine(dpy, drw->drawable, drw->gc, title_x, 0, title_x, bh);
	/* draw white line left of dwmblocks only on selected monitor */
	if (m == selmon)
		XDrawLine(dpy, drw->drawable, drw->gc, m->ww - statusw, 0, m->ww - statusw, bh);
	m->bt = n;
	m->btw = w;
	drw_map(drw, m->barwin, 0, 0, m->ww, bh);
}

//void
//drawbar(Monitor *m)
//{
//	int x, w, tw = 0, n = 0, scm;
//	int boxw = drw->fonts->h / 6 + 2;
//	unsigned int i, occ = 0, urg = 0;
//	Client *c;
//
//	if (!m->showbar)
//		return;
//
//	if (m == selmon) {
//		char *text, *s, ch;
//		drw_setscheme(drw, scheme[SchemeStatus]);
//		tw = statusw;
//		x = m->ww - statusw;
//		for (text = s = stext; *s; s++) {
//			if ((unsigned char)(*s) < ' ') {
//				ch = *s;
//				*s = '\0';
//				w = TEXTW(text) - lrpad;
//				drw_text(drw, x, 0, w, bh, 0, text, 0);
//				x += w;
//				*s = ch;
//				text = s + 1;
//			}
//		}
//		w = TEXTW(text);
//		drw_text(drw, x, 0, w, bh, 0, text, 0);
//		tw = statusw;
//	}
//
//	for (c = m->clients; c; c = c->next) {
//		if (ispanel(c))
//			continue;
//		if (ISVISIBLE(c))
//			n++;
//		occ |= c->tags;
//		if (c->isurgent)
//			urg |= c->tags;
//	}
//	x = 0;
//	for (i = 0; i < LENGTH(tags); i++) {
//		w = TEXTW(tags[i]);
//		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeTagsSel : SchemeTagsNorm]);
//		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
//		if (occ & 1 << i)
//			drw_rect(drw, x + boxw, 0, w - (2 * boxw + 1), boxw, m->tagset[m->seltags] & 1 << i, urg & 1 << i);
//		x += w;
//	}
//
//	w = TEXTW(m->ltsymbol);
//	drw_setscheme(drw, scheme[SchemeTagsNorm]);
//	x = drw_text(drw, x, 0, w + 1, bh, lrpad / 2, m->ltsymbol, 0);
//
//	if ((w = m->ww - tw - x) > bh) {
//		if (n > 0) {
//			int remainder = w % n;
//			int tabw = (1.0 / (double)n) * w + 1;
//			for (c = m->clients; c; c = c->next) {
//				if (ispanel(c))
//					continue;
//				if (!ISVISIBLE(c))
//					continue;
//				if (m->hov == c)
//					scm = SchemeHov;
//				else if (m->sel == c && m->hov == NULL)
//					scm = SchemeSel;
//				else if (HIDDEN(c))
//					scm = SchemeHid;
//				else
//					scm = SchemeNorm;
//				drw_setscheme(drw, scheme[scm]);
//				if (remainder >= 0) {
//					if (remainder == 0)
//						tabw--;
//					remainder--;
//				}
//				drw_text(drw, x, 0, tabw - 1, bh, lrpad / 2 + (c->icon ? c->icw + ICONSPACING : 0), c->name, 0);
//				if (c->icon)
//					drw_pic(drw, x + lrpad / 2, (bh - c->ich) / 2, c->icw, c->ich, c->icon);
//				XSetForeground(dpy, drw->gc, 0xFFFFFF);
//				/* left border */
//				XDrawLine(dpy, drw->drawable, drw->gc, x, 0, x, bh);
//				/* right border */
//				{
//					Client *next = c->next;
//					while (next && (!ISVISIBLE(next) || ispanel(next)))
//						next = next->next;
//					if (next)
//						XDrawLine(dpy, drw->drawable, drw->gc, x + tabw - 1, 0, x + tabw - 1, bh);
//					else
//						XDrawLine(dpy, drw->drawable, drw->gc, m->ww - tw, 0, m->ww - tw, bh);
//				}
//				x += tabw;
//			}
//		} else {
//			drw_setscheme(drw, scheme[SchemeInfoNorm]);
//			drw_rect(drw, x, 0, w - 2 * sp, bh, 1, 1);
//			XSetForeground(dpy, drw->gc, 0xFFFFFF);
//			XDrawLine(dpy, drw->drawable, drw->gc, x, 0, x, bh);
//			XDrawLine(dpy, drw->drawable, drw->gc, m->ww - tw, 0, m->ww - tw, bh);
//		}
//	}
//	m->bt = n;
//	m->btw = w;
//	drw_map(drw, m->barwin, 0, 0, m->ww, bh);
//}

//void
//drawbar(Monitor *m)
//{
//	int x, w, tw = 0, n = 0, scm; //etwl = 0, etwr = 0, scm;
//	//int boxs = drw->fonts->h / 9;
//	int boxw = drw->fonts->h / 6 + 2;
//	unsigned int i, occ = 0, urg = 0;
//	Client *c;
//
//	if (!m->showbar)
//		return;
//
//	if (m == selmon) { /* status is only drawn on selected monitor */
//		char *text, *s, ch;
//		drw_setscheme(drw, scheme[SchemeStatus]);
//		tw = statusw;
//		x = m->ww - statusw;
//		for (text = s = stext; *s; s++) {
//			if ((unsigned char)(*s) < ' ') {
//				ch = *s;
//				*s = '\0';
//				w = TEXTW(text) - lrpad;
//				drw_text(drw, x, 0, w, bh, 0, text, 0);
//				x += w;
//				*s = ch;
//				text = s + 1;
//			}
//		}
//		w = TEXTW(text);
//		//w = TEXTW(text) - lrpad + 2;
//		drw_text(drw, x, 0, w, bh, 0, text, 0);
//		tw = statusw;
//	}
//
//	/* draw status first so it can be overdrawn by tags later */
//	//if (m == selmon) { /* status is only drawn on selected monitor */
//	//	char *text, *s, ch;
//	//	//drw_setscheme(drw, scheme[SchemeNorm]);
//	//	drw_setscheme(drw, scheme[SchemeStatus]);
//	//	//tw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
//	//	//drw_text(drw, m->ww - tw, 0, tw, bh, 0, stext, 0);
//	//	x = 0;
//	//	for (text = s = stext; *s; s++) {
//	//		if ((unsigned char)(*s) < ' ') {
//	//			ch = *s;
//	//			*s = '\0';
//	//			tw = TEXTW(text) - lrpad;
//	//			//drw_text(drw, m->ww - statusw + x, 0, tw, bh, 0, text, 0);
//	//			drw_text(drw, m->ww - sw - 2 * sp, 0, sw, bh, 0, stext, 0);
//	//			x += tw;
//	//			*s = ch;
//	//			text = s + 1;
//	//		}
//	//	}
//	//	tw = TEXTW(text) - lrpad + 2;
//	//	//drw_text(drw, m->ww - statusw + x, 0, tw, bh, 0, text, 0);
//	//	drw_text(drw, m->ww - sw - 2 * sp, 0, sw, bh, 0, stext, 0);
//	//	tw = statusw;
//	//}
//
//	for (c = m->clients; c; c = c->next) {
//		// prevent showing the panel as active application:
//	        if (ispanel(c))
//			continue;
//		if (ISVISIBLE(c))
//			n++;
//		occ |= c->tags;
//		if (c->isurgent)
//			urg |= c->tags;
//	}
//	x = 0;
//	//w = m->btw = TEXTW(buttonbar);
//	//drw_setscheme(drw, scheme[SchemeNorm]);
//	//x = drw_text(drw, x, 0, w, bh, lrpad / 2, buttonbar, 0);
//	for (i = 0; i < LENGTH(tags); i++) {
//		w = TEXTW(tags[i]);
//		//drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
//		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeTagsSel : SchemeTagsNorm]);
//		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
//		if (occ & 1 << i)
//			//drw_rect(drw, x + boxs, boxs, boxw, boxw, m == selmon && selmon->sel && selmon->sel->tags & 1 << i, urg & 1 << i);
//			drw_rect(drw, x + boxw, 0, w - ( 2 * boxw + 1), boxw, m == selmon && selmon->sel && selmon->sel->tags & 1 << i, urg & 1 << i);
//		x += w;
//	}
//
//
//
//	w = TEXTW(m->ltsymbol);
//	//drw_setscheme(drw, scheme[SchemeNorm]);
//	drw_setscheme(drw, scheme[SchemeTagsNorm]);
//	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);
//
//	if ((w = m->ww - tw - x) > bh) {
//		//if (m->sel) {
//		//	drw_setscheme(drw, scheme[m == selmon ? SchemeSel : SchemeNorm]);
//		//	drw_text(drw, x, 0, w, bh, lrpad / 2, m->sel->name, 0);
//		//	if (m->sel->isfloating)
//		//		drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0);
//		if (n > 0) {
//			int remainder = w % n;
//			int tabw = (1.0 / (double)n) * w + 1;
//			for (c = m->clients; c; c = c->next) {
//				if (ispanel(c))
//					continue;
//				if (!ISVISIBLE(c))
//					continue;
//				if (m->hov == c)
//					scm = SchemeHov;
//				else if (m->sel == c && m->hov == NULL)
//					scm = SchemeSel;
//				else if (HIDDEN(c))
//					scm = SchemeHid;
//				else
//					scm = SchemeNorm;
//				drw_setscheme(drw, scheme[scm]);
//
//				if (remainder >= 0) {
//					if (remainder == 0) {
//						tabw--;
//					}
//					remainder--;
//				}
//				//drw_text(drw, x, 0, tabw, bh, lrpad / 2, c->name, 0);
//				//drw_text(drw, x, 0, tabw, bh, lrpad / 2 + (m->sel->icon ? m->sel->icw + ICONSPACING : 0), m->sel->name, 0);
//				//drw_text(drw, x, 0, tabw, bh, lrpad / 2 + (c->icon ? c->icw + ICONSPACING : 0), c->name, 0);
//				drw_text(drw, x, 0, tabw - 1, bh, lrpad / 2 + (c->icon ? c->icw + ICONSPACING : 0), c->name, 0);
//				if (c->icon)
//					drw_pic(drw, x + lrpad / 2, (bh - c->ich) / 2, c->icw, c->ich, c->icon);
//				XSetForeground(dpy, drw->gc, 0xFFFFFF);
//				/* for last visible client draw line at exact end of title area */
//				{
//					Client *next = c->next;
//					while (next && (!ISVISIBLE(next) || ispanel(next)))
//						next = next->next;
//					if (next)
//						XDrawLine(dpy, drw->drawable, drw->gc, x + tabw - 1, 0, x + tabw - 1, bh);
//					else
//						XDrawLine(dpy, drw->drawable, drw->gc, m->ww - tw - 1, 0, m->ww - tw - 1, bh);
//				}
//				//XDrawLine(dpy, drw->drawable, drw->gc, x, 0, x, bh);
//				XDrawLine(dpy, drw->drawable, drw->gc, x + tabw - 1, 0, x + tabw - 1, bh);
//
//				//if (m->sel->icon)
//				//	drw_pic(drw, x + lrpad / 2, (bh - m->sel->ich) / 2, m->sel->icw, m->sel->ich, m->sel->icon);
//				x += tabw;
//			}
//		} else {
//			drw_setscheme(drw, scheme[SchemeInfoNorm]);
//			drw_rect(drw, x, 0, w - 2 * sp, bh, 1, 1);
//			XSetForeground(dpy, drw->gc, 0xFFFFFF);
//			XDrawLine(dpy, drw->drawable, drw->gc, x, 0, x, bh);
//			//XDrawLine(dpy, drw->drawable, drw->gc, x + w - 2 * sp - 1, 0, x + w - 2 * sp - 1, bh);
//			XDrawLine(dpy, drw->drawable, drw->gc, x + w - 1, 0, x + w - 1, bh);
//			//drw_setscheme(drw, scheme[SchemeNorm]);
//			//drw_setscheme(drw, scheme[SchemeInfoNorm]);
//			//drw_rect(drw, x, 0, w, bh, 1, 1);
//			//drw_rect(drw, x, 0, w - 2 * sp, bh, 1, 1);
//		}
//	}
//	m->bt = n;
//	m->btw = w;
//	drw_map(drw, m->barwin, 0, 0, m->ww, bh);
//
//	//char *docktext = "again";
//
//	//if (m == selmon) { /* extra status is only drawn on selected monitor */
//	//	//rstext = strdup(docktext);
//	//	//if (splitstatus) {
//	//	//	mstext = strsep(&rstext, splitdelim);
//	//	//	msx = (m->ww - TEXTW(mstext) + lrpad) / 2;
//	//	//}
//	//	drw_setscheme(drw, scheme[SchemeNorm]);
//	//	/* clear default bar draw buffer by drawing a blank rectangle */
//	//	drw_rect(drw, 0, 0, m->ww, dh, 1, 1);
//
//	//	int buttonl = TEXTW(buttonbar);
//	//	drw_text(drw, 0, 0, buttonl, dh, 0, buttonbar, 0);
//
//	//	int etwl = TEXTW("hello") - lrpad + 2;
//	//	drw_text(drw, buttonl, 0, etwl, dh, 0, "hello", 0);
//
//	//	int etwm = TEXTW("hello") - lrpad + 2; /* 2px right padding */
//	//	drw_text(drw, (m->ww - etwm + lrpad) / 2, 0, etwm, dh, 0, "world", 0);
//
//	//	int etwr = TEXTW("again") - lrpad + 2;
//	//	drw_text(drw, m->ww - etwm, 0, etwr, dh, 0, docktext, 0);
//	//	drw_map(drw, m->dockwin, 0, 0, m->ww, dh);
//	//}
//}

void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

void
dwindle(Monitor *mon) {
	fibonacci(mon, 1);
}

void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
flushimgcache(void)
{
    int i;
    for (i = 0; i < _imgcache_n; i++) {
        if (_imgcache[i].img) {
            imlib_context_set_image(_imgcache[i].img);
            imlib_free_image();
            _imgcache[i].img = NULL;
        }
        _imgcache[i].path[0] = '\0';
    }
    _imgcache_n = 0;
}


static StatusImgCache *
getimg(const char *path)
{
    int i;

    /* search cache first */
    for (i = 0; i < _imgcache_n; i++)
        if (strncmp(_imgcache[i].path, path, STATUSIMG_PATH_MAX) == 0)
            return &_imgcache[i];

    /* cache full — evict oldest (index 0) by shifting */
    if (_imgcache_n >= STATUSIMG_CACHE_MAX) {
        imlib_context_set_image(_imgcache[0].img);
        imlib_free_image();
        memmove(&_imgcache[0], &_imgcache[1],
                sizeof(StatusImgCache) * (STATUSIMG_CACHE_MAX - 1));
        _imgcache_n--;
    }

    Imlib_Image img = imlib_load_image(path);
    if (!img)
        return NULL;

    imlib_context_set_image(img);
    StatusImgCache *entry = &_imgcache[_imgcache_n++];
    strncpy(entry->path, path, STATUSIMG_PATH_MAX - 1);
    entry->path[STATUSIMG_PATH_MAX - 1] = '\0';
    entry->img    = img;
    entry->orig_w = imlib_image_get_width();
    entry->orig_h = imlib_image_get_height();
    return entry;
}

/* ── helper: draw one image at (x, 1) scaled to bar height ───────────────── */
/* Returns the pixel width consumed so the caller can advance x.              */
static int
drawimg(const char *path, int x, int bh)
{
    StatusImgCache *ic = getimg(path);
    if (!ic)
        return 0;
    int dh = (ic->orig_h > 0 && ic->orig_h < bh - 2) ? ic->orig_h : bh - 2;
    if (dh < 1) dh = 1;
    int dw = (ic->orig_h > 0)
             ? ic->orig_w * dh / ic->orig_h
             : dh;
    if (dw < 1) dw = 1;
    Pixmap pm = XCreatePixmap(dpy, drw->drawable, dw, dh,
                              DefaultDepth(dpy, screen));
    if (!pm)
        return 0;
    /* fill background with bar color so it shows through transparent areas */
    XSetForeground(dpy, drw->gc, drw->scheme[ColBg].pixel);
    XFillRectangle(dpy, pm, drw->gc, 0, 0, dw, dh);
    imlib_context_set_image(ic->img);
    imlib_context_set_display(dpy);
    imlib_context_set_visual(DefaultVisual(dpy, screen));
    imlib_context_set_drawable(pm);
    /* PNG: blend=1 for real alpha transparency
     * SVG: blend=0 because SVG loader ignores alpha, renders with black bg
     *      XFillRectangle already filled bar color so SVG renders on top */
    const char *ext = strrchr(path, '.');
    int is_svg = (ext && strcasecmp(ext, ".svg") == 0);
    imlib_context_set_blend(is_svg ? 0 : 1);
    imlib_render_image_on_drawable_at_size(0, 0, dw, dh);
    /* vertically center the image in the bar */
    int dy = (bh - dh) / 2;
    XCopyArea(dpy, pm, drw->drawable, drw->gc, 0, 0, dw, dh, x, dy);
    XFreePixmap(dpy, pm);
    return dw;
}
/* ── helper: measure image width (same formula as drawimg) ───────────────── */
static int
measureimg(const char *path, int bh)
{
    StatusImgCache *ic = getimg(path);
    if (!ic)
        return 0;

    int dh = (ic->orig_h > 0 && ic->orig_h < bh - 2) ? ic->orig_h : bh - 2;
    if (dh < 1) dh = 1;
    int dw = (ic->orig_h > 0)
             ? ic->orig_w * dh / ic->orig_h
             : dh;
    return (dw < 1) ? 1 : dw;
}

/* ── helper: collect path token up to the closing '^' ───────────────────── */
/* Writes at most (STATUSIMG_PATH_MAX-1) bytes into out[], NUL-terminates.   */
/* Returns the index of the closing '^' in text[] (so the caller can set     */
/* i = that index; the outer loop's ++i will then skip past it).             */
static int
collectpath(const char *text, int start, char *out)
{
    int j = 0;
    int i = start;

    /* skip optional leading whitespace after the 'i' command letter */
    while (text[i] == ' ' || text[i] == '\t')
        i++;

    while (text[i] && text[i] != '^' && j < STATUSIMG_PATH_MAX - 1)
        out[j++] = text[i++];

    out[j] = '\0';
    return i; /* points at '^' or '\0' */
}


void
fibonacci(Monitor *mon, int s) {
	unsigned int i, n, nx, ny, nw, nh;
	Client *c;

	for(n = 0, c = nexttiled(mon->clients); c; c = nexttiled(c->next), n++);
	if(n == 0)
		return;

	nx = mon->wx;
	ny = 0;
	nw = mon->ww;
	nh = mon->wh;

	for(i = 0, c = nexttiled(mon->clients); c; c = nexttiled(c->next)) {
		if((i % 2 && nh / 2 > 2 * c->bw)
		   || (!(i % 2) && nw / 2 > 2 * c->bw)) {
			if(i < n - 1) {
				if(i % 2)
					nh /= 2;
				else
					nw /= 2;
				if((i % 4) == 2 && !s)
					nx += nw;
				else if((i % 4) == 3 && !s)
					ny += nh;
			}
			if((i % 4) == 0) {
				if(s)
					ny += nh;
				else
					ny -= nh;
			}
			else if((i % 4) == 1)
				nx += nw;
			else if((i % 4) == 2)
				ny += nh;
			else if((i % 4) == 3) {
				if(s)
					nx += nw;
				else
					nx -= nw;
			}
			if(i == 0)
			{
				if(n != 1)
					nw = mon->ww * mon->mfact;
				ny = mon->wy;
			}
			else if(i == 1)
				nw = mon->ww - nw;
			i++;
		}
		resize(c, nx, ny, nw - 2 * c->bw, nh - 2 * c->bw, False);
	}
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		//for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
		for (c = selmon->stack; c && (!ISVISIBLE(c) || HIDDEN(c)); c = c->snext);
	//if (selmon->sel && selmon->sel != c)
	if (selmon->sel && selmon->sel != c) {
		unfocus(selmon->sel, 0);
		if (selmon->hidsel) {
			hidewin(selmon->sel);
			if (c)
				arrange(c->mon);
			selmon->hidsel = 0;
		}
	}
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		//detachstack(c);
		//attachstack(c);
		//grabbuttons(c, 1);
		//XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		//setfocus(c);
		// prevents the panel getting focus when tag switching:
		if (!ispanel(c)) {
			detachstack(c);
			attachstack(c);
			grabbuttons(c, 1);
			XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
			setfocus(c);
		}
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
}

//void
//focusstack(const Arg *arg) // May not compile after awesomebar patch
//{
//	Client *c = NULL, *i;
//
//	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
//		return;
//	if (arg->i > 0) {
//		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
//		if (!c)
//			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
//	} else {
//		for (i = selmon->clients; i != selmon->sel; i = i->next)
//			if (ISVISIBLE(i))
//				c = i;
//		if (!c)
//			for (; i; i = i->next)
//				if (ISVISIBLE(i))
//					c = i;
//	}
//	if (c) {
//		focus(c);
//		restack(selmon);
//	}
//}

void
focusstackvis(const Arg *arg) {
	focusstack(arg->i, 0);
}

void
focusstackhid(const Arg *arg) {
	focusstack(arg->i, 1);
}

int focussed_panel = 0; // helper for focusstack, avoids loops when panel is the only client

void
focusstack(int inc, int hid)
{

	Client *c = NULL, *i;

	//if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
	// if no client selected AND exclude hidden client; if client selected but fullscreened
	if ((!selmon->sel && !hid) || (selmon->sel && selmon->sel->isfullscreen && lockfullscreen))
		return;
	if (!selmon->clients)
		return;
	//if (arg->i > 0) {
	//	for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
	if (inc > 0) {
		if (selmon->sel)
			for (c = selmon->sel->next;
					c && (!ISVISIBLE(c) || (!hid && HIDDEN(c)));
					c = c->next);
		if (!c)
			//for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
			for (c = selmon->clients;
					c && (!ISVISIBLE(c) || (!hid && HIDDEN(c)));
					c = c->next);
	} else {
		//for (i = selmon->clients; i != selmon->sel; i = i->next)
		//	if (ISVISIBLE(i))
		//		c = i;
		if (selmon->sel) {
			for (i = selmon->clients; i != selmon->sel; i = i->next)
				if (ISVISIBLE(i) && !(!hid && HIDDEN(i)))
					c = i;
		} else
			c = selmon->clients;
		if (!c)
			for (; i; i = i->next)
				//if (ISVISIBLE(i))
				if (ISVISIBLE(i) && !(!hid && HIDDEN(i)))
					c = i;
	}
	if (c) {
		focus(c);
		restack(selmon);
		if (HIDDEN(c)) {
			showwin(c);
			c->mon->hidsel = 1;
		}
		// skipping the panel when switching focus:
		//if (ispanel(c) && focussed_panel == 0) {
  		//  focussed_panel = 1;
  		//  //focusstack(c->arg->i, focussed_panel);
  		//  //focussed_panel = 0;
  		//}
	}
}


Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

static uint32_t prealpha(uint32_t p) {
	uint8_t a = p >> 24u;
	uint32_t rb = (a * (p & 0xFF00FFu)) >> 8u;
	uint32_t g = (a * (p & 0x00FF00u)) >> 8u;
	return (rb & 0xFF00FFu) | (g & 0x00FF00u) | (a << 24u);
}

Picture
geticonprop(Window win, unsigned int *picw, unsigned int *pich)
{
	int format;
	unsigned long n, extra, *p = NULL;
	Atom real;

	if (XGetWindowProperty(dpy, win, netatom[NetWMIcon], 0L, LONG_MAX, False, AnyPropertyType,
						   &real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return None;
	if (n == 0 || format != 32) { XFree(p); return None; }

	unsigned long *bstp = NULL;
	uint32_t w, h, sz;
	{
		unsigned long *i; const unsigned long *end = p + n;
		uint32_t bstd = UINT32_MAX, d, m;
		for (i = p; i < end - 1; i += sz) {
			if ((w = *i++) >= 16384 || (h = *i++) >= 16384) { XFree(p); return None; }
			if ((sz = w * h) > end - i) break;
			if ((m = w > h ? w : h) >= ICONSIZE && (d = m - ICONSIZE) < bstd) { bstd = d; bstp = i; }
		}
		if (!bstp) {
			for (i = p; i < end - 1; i += sz) {
				if ((w = *i++) >= 16384 || (h = *i++) >= 16384) { XFree(p); return None; }
				if ((sz = w * h) > end - i) break;
				if ((d = ICONSIZE - (w > h ? w : h)) < bstd) { bstd = d; bstp = i; }
			}
		}
		if (!bstp) { XFree(p); return None; }
	}

	if ((w = *(bstp - 2)) == 0 || (h = *(bstp - 1)) == 0) { XFree(p); return None; }

	uint32_t icw, ich;
	if (w <= h) {
		ich = ICONSIZE; icw = w * ICONSIZE / h;
		if (icw == 0) icw = 1;
	}
	else {
		icw = ICONSIZE; ich = h * ICONSIZE / w;
		if (ich == 0) ich = 1;
	}
	*picw = icw; *pich = ich;

	uint32_t i, *bstp32 = (uint32_t *)bstp;
	for (sz = w * h, i = 0; i < sz; ++i) bstp32[i] = prealpha(bstp[i]);

	Picture ret = drw_picture_create_resized(drw, (char *)bstp, w, h, icw, ich);
	XFree(p);

	return ret;
}

pid_t
getstatusbarpid(void)
{
	char buf[32], *str = buf, *c;
	FILE *fp;

	if (statuspid > 0) {
		snprintf(buf, sizeof(buf), "/proc/%u/cmdline", statuspid);
		if ((fp = fopen(buf, "r"))) {
			fgets(buf, sizeof(buf), fp);
			while ((c = strchr(str, '/')))
				str = c + 1;
			fclose(fp);
			if (!strcmp(str, STATUSBAR))
				return statuspid;
		}
	}
	if (!(fp = popen("pidof -s "STATUSBAR, "r")))
		return -1;
	fgets(buf, sizeof(buf), fp);
	pclose(fp);
	return strtol(buf, NULL, 10);
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

void
grid(Monitor *m) {
	unsigned int i, n, cx, cy, cw, ch, aw, ah, cols, rows;
	Client *c;

	for(n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next))
		n++;

	/* grid dimensions */
	for(rows = 0; rows <= n/2; rows++)
		if(rows*rows >= n)
			break;
	cols = (rows && (rows - 1) * rows >= n) ? rows - 1 : rows;

	/* window geoms (cell height/width) */
	ch = m->wh / (rows ? rows : 1);
	cw = m->ww / (cols ? cols : 1);
	for(i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
		cx = m->wx + (i / rows) * cw;
		cy = m->wy + (i % rows) * ch;
		/* adjust height/width of last row/column's windows */
		ah = ((i + 1) % rows == 0) ? m->wh - ch * rows : 0;
		aw = (i >= rows * (cols - 1)) ? m->ww - cw * cols : 0;
		resize(c, cx, cy, cw - 2 * c->bw + aw, ch - 2 * c->bw + ah, False);
		i++;
	}
}

void
hide(const Arg *arg)
{
	hidewin(selmon->sel);
	focus(NULL);
	arrange(selmon);
}

void
hidewin(Client *c) {
	if (!c || HIDDEN(c))
		return;

	if (ispanel(c))
		return;

	Window w = c->win;
	static XWindowAttributes ra, ca;

	// more or less taken directly from blackbox's hide() function
	XGrabServer(dpy);
	XGetWindowAttributes(dpy, root, &ra);
	XGetWindowAttributes(dpy, w, &ca);
	// prevent UnmapNotify events
	XSelectInput(dpy, root, ra.your_event_mask & ~SubstructureNotifyMask);
	XSelectInput(dpy, w, ca.your_event_mask & ~StructureNotifyMask);
	XUnmapWindow(dpy, w);
	setclientstate(c, IconicState);
	XSelectInput(dpy, root, ra.your_event_mask);
	XSelectInput(dpy, w, ca.your_event_mask);
	XUngrabServer(dpy);
}

int
ispanel(Client *c) {
	for (int i = 0; i < LENGTH(panel); i++)
		if (!strcmp(c->name, panel[i]))
			return 1;
	return 0;
    //return !strcmp(c->name, panel[0]);
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg)
{
	//if (!selmon->sel)
	if (!selmon->sel) {
		Client *c = selmon->clients;
		if (c)
			selmon->sel = c;
		else
			return;
	}

	if (!sendevent(selmon->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
layoutmenu(const Arg *arg) {
        FILE *p;
        char c[3], *s;
        int i;
        XUngrabPointer(dpy, CurrentTime);
        XSync(dpy, False);
        if (!(p = popen(layoutmenu_cmd, "r")))
                 return;
        s = fgets(c, sizeof(c), p);
        pclose(p);
        if (!s || *s == '\0' || c[0] == '\0')
                 return;
        i = atoi(c);
        setlayout(&((Arg) { .v = &layouts[i] }));
}

//void
//layoutmenu(const Arg *arg) {
//	FILE *p;
//	char c[3], *s;
//	int i;
//
//	if (!(p = popen(layoutmenu_cmd, "r")))
//		 return;
//	s = fgets(c, sizeof(c), p);
//	pclose(p);
//
//	if (!s || *s == '\0' || c[0] == '\0')
//		 return;
//
//	i = atoi(c);
//	setlayout(&((Arg) { .v = &layouts[i] }));
//}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL, *term = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	c->pid = winpid(w);
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	updateicon(c);
	updatetitle(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		applyrules(c);
		term = termforwin(c);
	}

	if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
		c->x = c->mon->wx + c->mon->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
		c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->wx);
	c->y = MAX(c->y, c->mon->wy);
	c->bw = borderpx;
	// no border - even when active
	//if (ispanel(c)) c->bw = c->oldbw = 0;
	if (ispanel(c)) {
		c->isfloating = 1;
		c->tags = ~0;
    		c->bw = c->oldbw = 0;
    		c->x = c->oldx = c->mon->mx;
    		c->y = c->oldy = c->mon->my + c->mon->mh - wa->height;
    		c->w = c->oldw = wa->width;
    		c->h = c->oldh = wa->height;
		wc.y = c->mon->my + c->mon->mh - wa->height;
		wc.x = c->mon->mx;
		wc.stack_mode = Above;
		XConfigureWindow(dpy, w, CWY | CWX | CWStackMode, &wc);
		//c->bw = c->oldbw = 0;
	}
	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	if (c->iscentered) {
		c->x = c->mon->mx + (c->mon->mw - WIDTH(c)) / 2;
		c->y = c->mon->my + (c->mon->mh - HEIGHT(c)) / 2;
	}
	{
		int format;
		unsigned long *data, n, extra;
		Monitor *m;
		Atom atom;
		if (XGetWindowProperty(dpy, c->win, netatom[NetClientInfo], 0L, 2L, False, XA_CARDINAL,
				&atom, &format, &n, &extra, (unsigned char **)&data) == Success && n == 2) {
			c->tags = *data;
			for (m = mons; m; m = m->next) {
				if (m->num == *(data+1)) {
					c->mon = m;
					break;
				}
			}
		}
		if (n > 0)
			XFree(data);
	}
	setclienttagprop(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	//setclientstate(c, NormalState);
	if (!HIDDEN(c))
		setclientstate(c, NormalState);

	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	arrange(c->mon);
	//XMapWindow(dpy, c->win);
	if (!HIDDEN(c))
		XMapWindow(dpy, c->win);

	if (term)
		swallow(term, c);
	focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
monocle(Monitor *m)
{
	unsigned int n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void
motionnotify(XEvent *e)
{
	int x, i;
	static Monitor *mon = NULL;
	Client *c;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window == selmon->barwin) {
		i = x = 0;
		do
			x += TEXTW(tags[i]);
		while (ev->x >= x && ++i < LENGTH(tags));
		/* FIXME when hovering the mouse over the tags and we view the tag,
		 *       the preview window get's in the preview shot */
		if (i < LENGTH(tags)) {
			if (selmon->previewshow != (i + 1)
			&& !(selmon->tagset[selmon->seltags] & 1 << i)) {
				selmon->previewshow = i + 1;
				showtagpreview(i);
			} else if (selmon->tagset[selmon->seltags] & 1 << i) {
				selmon->previewshow = 0;
				XUnmapWindow(dpy, selmon->tagwin);
			}
		} else if (selmon->previewshow) {
			selmon->previewshow = 0;
			XUnmapWindow(dpy, selmon->tagwin);
		}
	} else if (ev->window == selmon->tagwin) {
		selmon->previewshow = 0;
		XUnmapWindow(dpy, selmon->tagwin);
	} else if (selmon->previewshow) {
		selmon->previewshow = 0;
		XUnmapWindow(dpy, selmon->tagwin);
	}

	if (ev->window != selmon->barwin) {
		if (selmon->hov) {
			if (selmon->hov != selmon->sel)
				XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeNorm][ColBorder].pixel);
			else
				XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeSel][ColBorder].pixel);
			selmon->hov = NULL;
			c = wintoclient(ev->window);
			m = c ? c->mon : wintomon(ev->window);
			drawbar(m);
		}
		if (ev->window == root) {
			if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
				unfocus(selmon->sel, 1);
				selmon = m;
				focus(NULL);
			}
			mon = m;
		}
		return;
	}

	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	c = m->clients;

	x = 0; i = 0;
	do
		x += TEXTW(tags[i]);
	while (ev->x >= x && ++i < LENGTH(tags));

	/* mouse is over tags, layout symbol, or status area — clear hover */
	if (i < LENGTH(tags)
	|| ev->x < x + TEXTW(selmon->ltsymbol)
	|| ev->x > selmon->ww - statusw) {
		if (selmon->hov) {
			if (selmon->hov != selmon->sel)
				XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeNorm][ColBorder].pixel);
			else
				XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeSel][ColBorder].pixel);
			selmon->hov = NULL;
			drawbar(m);
		}
		return;
	}

	/* mouse is over the title area */
	x += TEXTW(selmon->ltsymbol);
	if (m->bt > 0) {
		int remainder = m->btw % m->bt;
		int tabw = (1.0 / (double)m->bt) * m->btw + 1;
		Client *found = NULL;
		for (c = m->clients; c; c = c->next) {
			if (!ISVISIBLE(c) || ispanel(c))
				continue;
			int tw = tabw;
			if (remainder >= 0) {
				if (remainder == 0) tw--;
				remainder--;
			}
			if (ev->x >= x && ev->x < x + tw) {
				found = c;
				break;
			}
			x += tw;
		}
		if (found) {
			if (selmon->hov) {
				if (selmon->hov != selmon->sel)
					XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeNorm][ColBorder].pixel);
				else
					XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeSel][ColBorder].pixel);
			}
			selmon->hov = found;
			focus(found);
			restack(selmon);
			XSetWindowBorder(dpy, found->win, scheme[SchemeHov][ColBorder].pixel);
		} else {
			if (selmon->hov) {
				if (selmon->hov != selmon->sel)
					XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeNorm][ColBorder].pixel);
				else
					XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeSel][ColBorder].pixel);
				selmon->hov = NULL;
			}
		}
	}
	drawbar(m);
}
void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

Client *
nexttiled(Client *c)
{
	//for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	for (; c && (c->isfloating || !ISVISIBLE(c) || HIDDEN(c)); c = c->next);
	return c;
}

void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		} else if (ev->atom == netatom[NetWMIcon]) {
			updateicon(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg)
{
	// fix: reloading dwm keeps all the hidden clients hidden
	Monitor *m;
	Client *c;
	for (m = mons; m; m = m->next) {
		if (m) {
			for (c = m->stack; c; c = c->next)
				if (c && HIDDEN(c)) showwin(c);
		}
	}

	if (arg->i) restart = 1;
	running = 0;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (ispanel(c) || applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	// nail it to no border & y=0:
	//if (ispanel(c)) c->y = c->oldy = c->bw = wc.y = wc.border_width = 0;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
	int ocx, ocy, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (m->lt[m->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
	XEvent ev;
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev)) {
		if (run_preview) {
			run_preview = 0;
			if (!preview_running) {
				preview_running = 1;
				previewallwin(NULL);
				preview_running = 0;
			}
		}
		if (run_recompile) {
			run_recompile = 0;
			Arg a = {.i = 1};
			quit(&a);
		}
		if (handler[ev.type])
			handler[ev.type](&ev);
	}
}

void
runautostart(void)
{
	char path[512];
	struct stat sb;
	const char *dir = "/usr/share/mrrobotos/mrdwm";

	/* check if the autostart directory exists */
	if (!(stat(dir, &sb) == 0 && S_ISDIR(sb.st_mode)))
		return;

	/* try the blocking script first */
	snprintf(path, sizeof(path), "%s/%s", dir, autostartblocksh);
	if (access(path, X_OK) == 0)
		system(path);

	/* now the non-blocking script */
	snprintf(path, sizeof(path), "%s/%s &", dir, autostartsh);
	if (access(path, X_OK) == 0)
		system(path);
}

//void
//runautostart(void)
//{
//	char *pathpfx;
//	char *path;
//	char *xdgdatahome;
//	char *home;
//	struct stat sb;
//
//	if ((home = getenv("HOME")) == NULL)
//		/* this is almost impossible */
//		return;
//
//	/* if $XDG_DATA_HOME is set and not empty, use $XDG_DATA_HOME/dwm,
//	 * otherwise use ~/.local/share/dwm as autostart script directory
//	 */
//	xdgdatahome = getenv("XDG_DATA_HOME");
//	if (xdgdatahome != NULL && *xdgdatahome != '\0') {
//		/* space for path segments, separators and nul */
//		pathpfx = ecalloc(1, strlen(xdgdatahome) + strlen(dwmdir) + 2);
//
//		if (sprintf(pathpfx, "%s/%s", xdgdatahome, dwmdir) <= 0) {
//			free(pathpfx);
//			return;
//		}
//	} else {
//		/* space for path segments, separators and nul */
//		pathpfx = ecalloc(1, strlen(home) + strlen(localshare)
//		                     + strlen(dwmdir) + 3);
//
//		if (sprintf(pathpfx, "%s/%s/%s", home, localshare, dwmdir) < 0) {
//			free(pathpfx);
//			return;
//		}
//	}
//
//	/* check if the autostart script directory exists */
//	if (! (stat(pathpfx, &sb) == 0 && S_ISDIR(sb.st_mode))) {
//		/* the XDG conformant path does not exist or is no directory
//		 * so we try ~/.dwm instead
//		 */
//		char *pathpfx_new = realloc(pathpfx, strlen(home) + strlen(dwmdir) + 3);
//		if(pathpfx_new == NULL) {
//			free(pathpfx);
//			return;
//		}
//		pathpfx = pathpfx_new;
//
//		if (sprintf(pathpfx, "%s/.%s", home, dwmdir) <= 0) {
//			free(pathpfx);
//			return;
//		}
//	}
//
//	/* try the blocking script first */
//	path = ecalloc(1, strlen(pathpfx) + strlen(autostartblocksh) + 2);
//	if (sprintf(path, "%s/%s", pathpfx, autostartblocksh) <= 0) {
//		free(path);
//		free(pathpfx);
//	}
//
//	if (access(path, X_OK) == 0)
//		system(path);
//
//	/* now the non-blocking script */
//	if (sprintf(path, "%s/%s", pathpfx, autostartsh) <= 0) {
//		free(path);
//		free(pathpfx);
//	}
//
//	if (access(path, X_OK) == 0)
//		system(strcat(path, " &"));
//
//	free(pathpfx);
//	free(path);
//}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	setclienttagprop(c);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
}

void
setgaps(const Arg *arg)
{
	if ((arg->i == 0) || (selmon->gappx + arg->i < 0))
		selmon->gappx = 0;
	else
		selmon->gappx += arg->i;
	arrange(selmon);
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
showtagpreview(unsigned int i)
{
	if (!selmon->previewshow || !selmon->tagmap[i]) {
		XUnmapWindow(dpy, selmon->tagwin);
		return;
	}

	XSetWindowBackgroundPixmap(dpy, selmon->tagwin, selmon->tagmap[i]);
	XCopyArea(dpy, selmon->tagmap[i], selmon->tagwin, drw->gc, 0, 0,
			selmon->mw / scalepreview, selmon->mh / scalepreview,
			0, 0);
	XSync(dpy, False);
	XMoveWindow(dpy, selmon->tagwin, selmon->wx, selmon->wy);
	XMapRaised(dpy, selmon->tagwin);
}

void
takepreview(void)
{
	Client *c;
	Imlib_Image image;
	unsigned int occ = 0, i;

	for (c = selmon->clients; c; c = c->next) {
		if (ispanel(c))
			continue;
		occ |= c->tags;
	}
	for (i = 0; i < LENGTH(tags); i++) {
		/* clear stale previews for unoccupied tags */
		if (!(occ & 1 << i)) {
			if (selmon->tagmap[i]) {
				XFreePixmap(dpy, selmon->tagmap[i]);
				selmon->tagmap[i] = 0;
			}
			continue;
		}

		/* only update currently selected tag */
		if (!(selmon->tagset[selmon->seltags] & 1 << i))
			continue;

		if (selmon->tagmap[i]) {
			XFreePixmap(dpy, selmon->tagmap[i]);
			selmon->tagmap[i] = 0;
		}

		selmon->previewshow = 0;
		XUnmapWindow(dpy, selmon->tagwin);
		XSync(dpy, False);

		if (!(image = imlib_create_image(sw, sh))) {
			fprintf(stderr, "mrdwm: imlib: failed to create image, skipping.");
			continue;
		}
		imlib_context_set_image(image);
		imlib_context_set_display(dpy);
		imlib_context_set_visual(DefaultVisual(dpy, screen));
		imlib_context_set_drawable(root);
		imlib_copy_drawable_to_image(0, selmon->mx, selmon->my, selmon->mw, selmon->mh, 0, 0, 1);
		selmon->tagmap[i] = XCreatePixmap(dpy, selmon->tagwin, selmon->mw / scalepreview, selmon->mh / scalepreview, DefaultDepth(dpy, screen));
		imlib_context_set_drawable(selmon->tagmap[i]);
		imlib_render_image_part_on_drawable_at_size(0, 0, selmon->mw, selmon->mh, 0, 0, selmon->mw / scalepreview, selmon->mh / scalepreview);
		imlib_free_image();
	}
}

//void
//takepreview(void)
//{
//	Client *c;
//	Imlib_Image image;
//	unsigned int occ = 0, i;
//
//	for (c = selmon->clients; c; c = c->next)
//		occ |= c->tags;
//		//occ |= c->tags == 255 ? 0 : c->tags; /* hide vacants */
//
//	for (i = 0; i < LENGTH(tags); i++) {
//		/* searching for tags that are occupied && selected */
//		if (!(occ & 1 << i) || !(selmon->tagset[selmon->seltags] & 1 << i))
//			continue;
//
//		if (selmon->tagmap[i]) { /* tagmap exist, clean it */
//			XFreePixmap(dpy, selmon->tagmap[i]);
//			selmon->tagmap[i] = 0;
//		}
//
//		/* try to unmap the window so it doesn't show the preview on the preview */
//		selmon->previewshow = 0;
//		XUnmapWindow(dpy, selmon->tagwin);
//		XSync(dpy, False);
//
//		if (!(image = imlib_create_image(sw, sh))) {
//			fprintf(stderr, "dwm: imlib: failed to create image, skipping.");
//			continue;
//		}
//		imlib_context_set_image(image);
//		imlib_context_set_display(dpy);
//		/* uncomment if using alpha patch */
//		//imlib_image_set_has_alpha(1);
//		//imlib_context_set_blend(0);
//		//imlib_context_set_visual(visual);
//		imlib_context_set_visual(DefaultVisual(dpy, screen));
//		imlib_context_set_drawable(root);
//
//		imlib_copy_drawable_to_image(0, selmon->mx, selmon->my, selmon->mw, selmon->mh, 0, 0, 1);
//		//if (previewbar)
//		//	imlib_copy_drawable_to_image(0, selmon->wx, selmon->wy, selmon->ww, selmon->wh, 0, 0, 1);
//		//else
//		//	imlib_copy_drawable_to_image(0, selmon->mx, selmon->my, selmon->mw ,selmon->mh, 0, 0, 1);
//
//
//		selmon->tagmap[i] = XCreatePixmap(dpy, selmon->tagwin, selmon->mw / scalepreview, selmon->mh / scalepreview, DefaultDepth(dpy, screen));
//		imlib_context_set_drawable(selmon->tagmap[i]);
//		imlib_render_image_part_on_drawable_at_size(0, 0, selmon->mw, selmon->mh, 0, 0, selmon->mw / scalepreview, selmon->mh / scalepreview);
//		imlib_free_image();
//	}
//}

void
previewtag(const Arg *arg)
{
	if (selmon->previewshow != (arg->ui + 1))
		selmon->previewshow = arg->ui + 1;
	else
		selmon->previewshow = 0;
	showtagpreview(arg->ui);
}


static void
sigusr1(int unused)
{
    sigusr1_count++;
    if (!preview_running && (sigusr1_count % 2 == 1))
        run_preview = 1;
}

static void
sigusr2(int unused)
{
    run_recompile = 1;
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	signal(SIGHUP, sighup);
	signal(SIGTERM, sigterm);
	signal(SIGUSR1, sigusr1);
	signal(SIGUSR2, sigusr2);

	while (waitpid(-1, NULL, WNOHANG) > 0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	//bh = drw->fonts->h + 2;
	bh = user_bh ? user_bh : drw->fonts->h + 2;
	dh = user_dh ? user_dh : drw->fonts->h + 2;
	updategeom();
	sp = sidepad;
	vp = (topbar == 1) ? vertpad : - vertpad;
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMIcon] = XInternAtom(dpy, "_NET_WM_ICON", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetClientInfo] = XInternAtom(dpy, "_NET_CLIENT_INFO", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors) + 1, sizeof(Clr *));
	scheme[LENGTH(colors)] = drw_scm_create(drw, colors[0], 3);
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	/* init bars */
	updatestatus();
	updatebars();
	updatebarpos(selmon);
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "mrdwm", 5);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	XDeleteProperty(dpy, root, netatom[NetClientInfo]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
show(const Arg *arg)
{
	if (selmon->hidsel)
		selmon->hidsel = 0;
	showwin(selmon->sel);
}

void
showall(const Arg *arg)
{
	Client *c = NULL;
	selmon->hidsel = 0;
	for (c = selmon->clients; c; c = c->next) {
		if (ISVISIBLE(c))
			showwin(c);
	}
	if (!selmon->sel) {
		for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
		if (c)
			focus(c);
	}
	restack(selmon);
}

void
showwin(Client *c)
{
	if (!c || !HIDDEN(c))
		return;

	XMapWindow(dpy, c->win);
	setclientstate(c, NormalState);
	arrange(c->mon);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
sighup(int unused)
{
	Arg a = {.i = 1};
	quit(&a);
}

void
sigterm(int unused)
{
	Arg a = {.i = 0};
	quit(&a);
}

void
sigstatusbar(const Arg *arg)
{
	union sigval sv;

	if (!statussig)
		return;
	sv.sival_int = arg->i;
	if ((statuspid = getstatusbarpid()) <= 0)
		return;

	sigqueue(statuspid, SIGRTMIN+statussig, sv);
}

void
spawn(const Arg *arg)
{
	struct sigaction sa;

	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("mrdwm: execvp '%s' failed:", ((char **)arg->v)[0]);
	}
}

void
spiral(Monitor *mon) {
	fibonacci(mon, 0);
}

void
setclienttagprop(Client *c)
{
	long data[] = { (long) c->tags, (long) c->mon->num };
	XChangeProperty(dpy, c->win, netatom[NetClientInfo], XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *) data, 2);
}

void
tag(const Arg *arg)
{
	Client *c;
	if (selmon->sel && arg->ui & TAGMASK) {
		c = selmon->sel;
		selmon->sel->tags = arg->ui & TAGMASK;
		setclienttagprop(c);
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

void
tcl(Monitor * m)
{
	int x, y, h, w, mw, sw, bdw;
	unsigned int i, n;
	Client * c;

	for (n = 0, c = nexttiled(m->clients); c;
	        c = nexttiled(c->next), n++);

	if (n == 0)
		return;

	c = nexttiled(m->clients);

	mw = m->mfact * m->ww;
	sw = (m->ww - mw) / 2;
	bdw = (2 * c->bw);
	resize(c,
	       n < 3 ? m->wx : m->wx + sw,
	       m->wy,
	       n == 1 ? m->ww - bdw : mw - bdw,
	       m->wh - bdw,
	       False);

	if (--n == 0)
		return;

	w = (m->ww - mw) / ((n > 1) + 1);
	c = nexttiled(c->next);

	if (n > 1)
	{
		x = m->wx + ((n > 1) ? mw + sw : mw);
		y = m->wy;
		h = m->wh / (n / 2);

		if (h < bh)
			h = m->wh;

		for (i = 0; c && i < n / 2; c = nexttiled(c->next), i++)
		{
			resize(c,
			       x,
			       y,
			       w - bdw,
			       (i + 1 == n / 2) ? m->wy + m->wh - y - bdw : h - bdw,
			       False);

			if (h != m->wh)
				y = c->y + HEIGHT(c);
		}
	}

	x = (n + 1 / 2) == 1 ? mw : m->wx;
	y = m->wy;
	h = m->wh / ((n + 1) / 2);

	if (h < bh)
		h = m->wh;

	for (i = 0; c; c = nexttiled(c->next), i++)
	{
		resize(c,
		       x,
		       y,
		       (i + 1 == (n + 1) / 2) ? w - bdw : w - bdw,
		       (i + 1 == (n + 1) / 2) ? m->wy + m->wh - y - bdw : h - bdw,
		       False);

		if (h != m->wh)
			y = c->y + HEIGHT(c);
	}
}

void
tile(Monitor *m)
{
	unsigned int i, n, h, mw, my, ty;
	Client *c;
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;
	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww - m->gappx;
	for (i = 0, my = ty = m->gappx, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			h = (m->wh - my - m->gappx) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx + m->gappx, m->wy + my, mw - (2*c->bw) - m->gappx, h - (2*c->bw), 0);
			my += HEIGHT(c) + m->gappx;
		} else {
			h = (m->wh - ty - m->gappx) / (n - i);
			resize(c, m->wx + mw + m->gappx, m->wy + ty, m->ww - mw - (2*c->bw) - 2*m->gappx, h - (2*c->bw), 0);
			ty += HEIGHT(c) + m->gappx;
		}
}

//void
//tile(Monitor *m)
//{
//	unsigned int i, n, h, mw, my, ty;
//	Client *c;
//
//	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
//	if (n == 0)
//		return;
//
//	if (n > m->nmaster)
//		mw = m->nmaster ? m->ww * m->mfact : 0;
//	else
////		mw = m->ww;
////	for (i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
////		if (i < m->nmaster) {
////			h = (m->wh - my) / (MIN(n, m->nmaster) - i);
////			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0);
////			if (my + HEIGHT(c) < m->wh)
////				my += HEIGHT(c);
//		mw = m->ww - m->gappx;
//	for (i = 0, my = ty = m->gappx, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
//		if (i < m->nmaster) {
//			h = (m->wh - my) / (MIN(n, m->nmaster) - i) - m->gappx;
//			resize(c, m->wx + m->gappx, m->wy + my, mw - (2*c->bw) - m->gappx, h - (2*c->bw), 0);
//			if (my + HEIGHT(c) + m->gappx < m->wh)
//				my += HEIGHT(c) + m->gappx;
//
//		} else {
//			//h = (m->wh - ty) / (n - i);
//			//resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw), 0);
//			//if (ty + HEIGHT(c) < m->wh)
//			//	ty += HEIGHT(c);
//			h = (m->wh - ty) / (n - i) - m->gappx;
//			resize(c, m->wx + mw + m->gappx, m->wy + ty, m->ww - mw - (2*c->bw) - 2*m->gappx, h - (2*c->bw), 0);
//			if (ty + HEIGHT(c) + m->gappx < m->wh)
//				ty += HEIGHT(c) + m->gappx;
//		}
//}

void
togglebar(const Arg *arg)
{
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	//XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx + sp, selmon->by + vp, selmon->ww - 2 * sp, bh);
	arrange(selmon);
}

//void
//toggleextrabar(const Arg *arg)
//{
//	selmon->extrabar = !selmon->extrabar;
//	updatebarpos(selmon);
//	XMoveResizeWindow(dpy, selmon->extrabarwin, selmon->wx, selmon->eby, selmon->ww, bh);
//	arrange(selmon);
//}

void
togglefloating(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
			selmon->sel->w, selmon->sel->h, 0);
	arrange(selmon);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		setclienttagprop(selmon->sel);
		focus(NULL);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		takepreview();
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
}

void
togglewin(const Arg *arg)
{
	Client *c = (Client*)arg->v;
	if (!c)
		return;
	if (HIDDEN(c)) {
		showwin(c);
		focus(c);
		restack(selmon);
	} else if (c == selmon->sel) {
		hidewin(c);
		focus(NULL);
		arrange(c->mon);
	} else {
		focus(c);
		restack(selmon);
	}
}

//void
//togglewin(const Arg *arg)
//{
//	Client *c = (Client*)arg->v;
//
//	if (!c)
//		return;
//
//	if (c == selmon->sel) {
//		hidewin(c);
//		focus(NULL);
//		arrange(c->mon);
//	} else {
//		if (HIDDEN(c))
//			showwin(c);
//		focus(c);
//		restack(selmon);
//	}
//}

void
freeicon(Client *c)
{
	if (c->icon) {
		XRenderFreePicture(dpy, c->icon);
		c->icon = None;
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	XWindowChanges wc;

	if (c->swallowing) {
		unswallow(c);
		return;
	}

	Client *s = swallowingclient(c->win);
	if (s) {
		free(s->swallowing);
		s->swallowing = NULL;
		arrange(m);
		focus(NULL);
		return;
	}

	detach(c);
	detachstack(c);
	freeicon(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	//focus(NULL);
	//updateclientlist();
	//arrange(m);
	{
                unsigned int occ = 0, i;
                Client *cl;
                for (cl = selmon->clients; cl; cl = cl->next) {
                        if (ispanel(cl))
                                continue;
                        occ |= cl->tags;
                }
                for (i = 0; i < LENGTH(tags); i++) {
                        if (!(occ & 1 << i) && selmon->tagmap[i]) {
                                XFreePixmap(dpy, selmon->tagmap[i]);
                                selmon->tagmap[i] = 0;
                        }
                }
        }
	if (!s) {
		arrange(m);
		focus(NULL);
		updateclientlist();
	}
}

void
updateicon(Client *c)
{
	freeicon(c);
	c->icon = geticonprop(c->win, &c->icw, &c->ich);
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
}

void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		//.event_mask = ButtonPressMask|ExposureMask
		.event_mask = ButtonPressMask|ExposureMask|PointerMotionMask

	};
	XClassHint ch = {"mrdwm"};
	for (m = mons; m; m = m->next) {
		if (!m->tagwin) {
			m->tagwin = XCreateWindow(dpy, root, m->wx, m->wy, m->mw / scalepreview,
			m->mh / scalepreview, 0, DefaultDepth(dpy, screen), CopyFromParent,
			DefaultVisual(dpy, screen), CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
			XDefineCursor(dpy, m->tagwin, cursor[CurNormal]->cursor);
			XUnmapWindow(dpy, m->tagwin);
		}

		if (!m->barwin) {
			//m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
			//		CopyFromParent, DefaultVisual(dpy, screen),
			//		CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
			m->barwin = XCreateWindow(dpy, root, m->wx + sp, m->by + vp, m->ww - 2 * sp, bh, 0, DefaultDepth(dpy, screen),
 				CopyFromParent, DefaultVisual(dpy, screen),
 				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
			XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
			XMapRaised(dpy, m->barwin);
			XSelectInput(dpy, m->barwin, ButtonPressMask|PointerMotionMask);
			XSetClassHint(dpy, m->barwin, &ch);
		}
		//if (!m->dockwin) {
		//	m->dockwin = XCreateWindow(dpy, root, m->wx, m->mh - dh, m->ww, dh, 0, DefaultDepth(dpy, screen),
		//			CopyFromParent, DefaultVisual(dpy, screen),
		//			CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		//	XDefineCursor(dpy, m->dockwin, cursor[CurNormal]->cursor);
		//	XMapRaised(dpy, m->dockwin);
		//	XSelectInput(dpy, m->dockwin, ButtonPressMask|PointerMotionMask);
		//	XSetClassHint(dpy, m->dockwin, &ch);
		//}
	}
}

void
updatebarpos(Monitor *m)
{
	int extragap = (m->mw == 1920 && m->mx == 1920) ? 10 : 0; /* only for HDMI monitor */
	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
		m->wh = m->wh - vertpad - bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh + vertpad;
		m->wy = m->topbar ? m->wy + bh + vp + extragap : m->wy;
	} else
		m->by = -bh - vp;
	m->wh -= dh;
	m->wh -= vp;
	m->wh -= extragap;
}

//void
//updatebarpos(Monitor *m)
//{
//	m->wy = m->my;
//	m->wh = m->mh;
//	if (m->showbar) {
//		//m->wh -= bh;
//		//m->by = m->topbar ? m->wy : m->wy + m->wh;
//		//m->wy = m->topbar ? m->wy + bh : m->wy;
//		m->wh = m->wh - vertpad - bh;
//		m->by = m->topbar ? m->wy : m->wy + m->wh + vertpad;
//		m->wy = m->topbar ? m->wy + bh + vp : m->wy;
//	} else
//		//m->by = -bh;
//		m->by = -bh - vp;
//
//	m->wh -= dh;
//	m->wh -= vp;
//}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				c->mon = mons;
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

void
updatestatus(void)
{
	flushimgcache();
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "mrdwm-"VERSION);
	drawbar(selmon);
}

void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog]) {
		c->iscentered = 1;
		c->isfloating = 1;
	}
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	takepreview();
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focus(NULL);
	arrange(selmon);
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

pid_t
winpid(Window w)
{

	pid_t result = 0;

#ifdef __linux__
	xcb_res_client_id_spec_t spec = {0};
	spec.client = w;
	spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

	xcb_generic_error_t *e = NULL;
	xcb_res_query_client_ids_cookie_t c = xcb_res_query_client_ids(xcon, 1, &spec);
	xcb_res_query_client_ids_reply_t *r = xcb_res_query_client_ids_reply(xcon, c, &e);

	if (!r)
		return (pid_t)0;

	xcb_res_client_id_value_iterator_t i = xcb_res_query_client_ids_ids_iterator(r);
	for (; i.rem; xcb_res_client_id_value_next(&i)) {
		spec = i.data->spec;
		if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
			uint32_t *t = xcb_res_client_id_value_value(i.data);
			result = *t;
			break;
		}
	}

	free(r);

	if (result == (pid_t)-1)
		result = 0;

#endif /* __linux__ */

#ifdef __OpenBSD__
        Atom type;
        int format;
        unsigned long len, bytes;
        unsigned char *prop;
        pid_t ret;

        if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_PID", 0), 0, 1, False, AnyPropertyType, &type, &format, &len, &bytes, &prop) != Success || !prop)
               return 0;

        ret = *(pid_t*)prop;
        XFree(prop);
        result = ret;

#endif /* __OpenBSD__ */
	return result;
}

pid_t
getparentprocess(pid_t p)
{
	unsigned int v = 0;

#ifdef __linux__
	FILE *f;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned)p);

	if (!(f = fopen(buf, "r")))
		return 0;

	fscanf(f, "%*u %*s %*c %u", &v);
	fclose(f);
#endif /* __linux__*/

#ifdef __OpenBSD__
	int n;
	kvm_t *kd;
	struct kinfo_proc *kp;

	kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, NULL);
	if (!kd)
		return 0;

	kp = kvm_getprocs(kd, KERN_PROC_PID, p, sizeof(*kp), &n);
	v = kp->p_ppid;
#endif /* __OpenBSD__ */

	return (pid_t)v;
}

int
isdescprocess(pid_t p, pid_t c)
{
	while (p != c && c != 0)
		c = getparentprocess(c);

	return (int)c;
}

Client *
termforwin(const Client *w)
{
	Client *c;
	Monitor *m;

	if (!w->pid || w->isterminal)
		return NULL;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->isterminal && !c->swallowing && c->pid && isdescprocess(c->pid, w->pid))
				return c;
		}
	}

	return NULL;
}

Client *
swallowingclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->swallowing && c->swallowing->win == w)
				return c;
		}
	}

	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		//if (w == m->barwin)
		//if (w == m->barwin || w == m->extrabarwin)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "mrdwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("mrdwm: another window manager is already running");
	return -1;
}

void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;

	if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
		return;
	if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
}

//void
//previewallwin(){
//	FILE *log = fopen("/tmp/previewallwin.log", "w");
//	fprintf(log, "previewallwin called\n");
//	fclose(log);
//	Monitor *m = selmon;
//	Client *c, *focus_c = NULL;
//	unsigned int n;
//	for (n = 0, c = m->clients; c; c = c->next){
//		if (ispanel(c))
//			continue;
//		n++;
//#ifdef ACTUALFULLSCREEN
//		if (c->isfullscreen)
//			togglefullscr(&(Arg){0});
//#endif
//#ifdef AWESOMEBAR
//		if (HIDDEN(c))
//			continue;
//#endif
//		c->pre.orig_image = getwindowximage(c);
//	}
//	if (n == 0)
//		return;
//	setpreviewwindowsizepositions(n, m, 60, 15);
//	XEvent event;
//	for(c = m->clients; c; c = c->next){
//		if (ispanel(c))
//			continue;
//		if (!c->pre.win)
//			c->pre.win = XCreateSimpleWindow(dpy, root, c->pre.x, c->pre.y, c->pre.scaled_image->width, c->pre.scaled_image->height, 1, BlackPixel(dpy, screen), WhitePixel(dpy, screen));
//		else
//			XMoveResizeWindow(dpy, c->pre.win, c->pre.x, c->pre.y, c->pre.scaled_image->width, c->pre.scaled_image->height);
//		XSetWindowBorder(dpy, c->pre.win, scheme[SchemeNorm][ColBorder].pixel);
//		XUnmapWindow(dpy, c->win);
//		if (c->pre.win){
//			XSelectInput(dpy, c->pre.win, ButtonPress | EnterWindowMask | LeaveWindowMask );
//			XMapWindow(dpy, c->pre.win);
//			GC gc = XCreateGC(dpy, c->pre.win, 0, NULL);
//			XPutImage(dpy, c->pre.win, gc, c->pre.scaled_image, 0, 0, 0, 0, c->pre.scaled_image->width, c->pre.scaled_image->height);
//		}
//	}
//	while (1) {
//		XNextEvent(dpy, &event);
//		if (event.type == ButtonPress)
//			if (event.xbutton.button == Button1){
//				for(c = m->clients; c; c = c->next){
//					XUnmapWindow(dpy, c->pre.win);
//					if (event.xbutton.window == c->pre.win){
//						selmon->seltags ^= 1; /* toggle sel tagset */
//						m->tagset[selmon->seltags] = c->tags;
//						focus_c = c;
//						focus(NULL);
//#ifdef AWESOMEBAR
//						if (HIDDEN(c)){
//							showwin(c);
//							continue;
//						}
//#endif
//					}
//					/* If you hit awesomebar patch Unlock the notes below;
//					 * And you should add the following line to "hidewin" Function
//					 * c->pre.orig_image = getwindowximage(c);
//					 * */
//#ifdef AWESOMEBAR
//					if (HIDDEN(c)){
//						continue;
//					}
//#endif
//					XMapWindow(dpy, c->win);
//					XDestroyImage(c->pre.orig_image);
//					XDestroyImage(c->pre.scaled_image);
//				}
//				break;
//			}
//		if (event.type == EnterNotify)
//			for(c = m->clients; c; c = c->next)
//				if (event.xcrossing.window == c->pre.win){
//					XSetWindowBorder(dpy, c->pre.win, scheme[SchemeSel][ColBorder].pixel);
//					break;
//				}
//		if (event.type == LeaveNotify)
//			for(c = m->clients; c; c = c->next)
//				if (event.xcrossing.window == c->pre.win){
//					XSetWindowBorder(dpy, c->pre.win, scheme[SchemeNorm][ColBorder].pixel);
//					break;
//				}
//	}
//	arrange(m);
//	focus(focus_c);
//}

void
previewallwin(const Arg *arg){
    (void)arg;
    Monitor *m = selmon;
    Client *c, *focus_c = NULL;
    unsigned int n;

    for (n = 0, c = m->clients; c; c = c->next){
        if (ispanel(c)) continue;
        n++;
        c->pre.orig_image = getwindowximage(c);
    }
    if (n == 0) return;

    /* ── search bar ── */
    int sb_w = 500;
    int sb_h = 44;
    int sb_x = m->wx + (m->ww - sb_w) / 2;
    int sb_y = m->wy + 24;

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.background_pixel  = 0xffffff;
    swa.event_mask        = KeyPressMask | ExposureMask | ButtonPressMask;
    Window searchwin = XCreateWindow(dpy, root,
        sb_x, sb_y, sb_w, sb_h, 2,
        DefaultDepth(dpy, screen), InputOutput,
        DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);

    char search_buf[128] = "";
    int  search_len  = 0;
    int  cursor_vis  = 1;  /* blink state */
    int  search_focused = 0; /* becomes 1 after first click on searchwin */

    XMapRaised(dpy, searchwin);

    GC sgc = XCreateGC(dpy, searchwin, 0, NULL);

    /* try larger fonts first */
    XFontStruct *font = XLoadQueryFont(dpy,
        "-*-dejavu sans-medium-r-*-*-16-*-*-*-*-*-*-*");
    if (!font) font = XLoadQueryFont(dpy,
        "-*-liberation sans-medium-r-*-*-16-*-*-*-*-*-*-*");
    if (!font) font = XLoadQueryFont(dpy,
        "-*-helvetica-medium-r-*-*-16-*-*-*-*-*-*-*");
    if (!font) font = XLoadQueryFont(dpy, "fixed");

    /* blink timer via X11 timeout — we use select() on X fd */
    int xfd = ConnectionNumber(dpy);

    #define DRAW_SEARCHBAR() do { \
        XSetForeground(dpy, sgc, 0xffffff); \
        XFillRectangle(dpy, searchwin, sgc, 0, 0, sb_w, sb_h); \
        XSetForeground(dpy, sgc, search_focused ? 0x00cc66 : 0xaaaaaa); \
        XDrawRectangle(dpy, searchwin, sgc, 0, 0, sb_w-1, sb_h-1); \
        if (font) XSetFont(dpy, sgc, font->fid); \
        int _pad = 14; \
        if (search_len == 0 && !search_focused) { \
            XSetForeground(dpy, sgc, 0x444444); \
            XDrawString(dpy, searchwin, sgc, _pad, sb_h/2+6, \
                "Click to search windows...", 26); \
        } else if (search_len == 0 && search_focused) { \
            XSetForeground(dpy, sgc, 0x444444); \
            XDrawString(dpy, searchwin, sgc, _pad, sb_h/2+6, \
                "Type a window name...", 21); \
            if (cursor_vis) { \
                XSetForeground(dpy, sgc, 0x00cc66); \
                XDrawLine(dpy, searchwin, sgc, _pad, 8, _pad, sb_h-8); \
            } \
        } else { \
            XSetForeground(dpy, sgc, 0x111111); \
            XDrawString(dpy, searchwin, sgc, _pad, sb_h/2+6, \
                search_buf, search_len); \
            if (cursor_vis) { \
                int _cw2 = font ? XTextWidth(font, search_buf, search_len) \
                                : search_len * 9; \
                XSetForeground(dpy, sgc, 0x00cc66); \
                XDrawLine(dpy, searchwin, sgc, \
                    _pad + _cw2 + 2, 8, \
                    _pad + _cw2 + 2, sb_h-8); \
            } \
        } \
    } while(0)

    DRAW_SEARCHBAR();

    /* grid area */
    int grid_top    = sb_y + sb_h + 20;
    int grid_bottom = m->wy + m->wh - 60;

    XRenderPictFormat *scrfmt =
        XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen));

    #define BAR_H 56

    /* ── draw one preview window ── */
    #define REDRAW_CLIENT(cl) do { \
        if (!(cl)->pre.win || !(cl)->pre.scaled_image) break; \
        int _pw   = (cl)->pre.scaled_image->width; \
        int _ph   = (cl)->pre.scaled_image->height; \
        int _img_h = _ph - BAR_H; \
        if (_img_h < 1) _img_h = 1; \
        GC _gc = XCreateGC(dpy, (cl)->pre.win, 0, NULL); \
        /* screenshot */ \
        XPutImage(dpy, (cl)->pre.win, _gc, (cl)->pre.scaled_image, \
            0, 0, 0, 0, _pw, _img_h); \
        /* dark bottom bar */ \
        XSetForeground(dpy, _gc, 0x141414); \
        XFillRectangle(dpy, (cl)->pre.win, _gc, 0, _img_h, _pw, BAR_H); \
        XSetForeground(dpy, _gc, 0x2c2c2c); \
        XDrawLine(dpy, (cl)->pre.win, _gc, 0, _img_h, _pw, _img_h); \
        /* icon + title centered */ \
        int _ico_sz  = 30; \
        int _has_ico = ((cl)->icon && (cl)->icw > 0 && (cl)->ich > 0) ? 1 : 0; \
        char _ttl[64]; \
        snprintf(_ttl, sizeof(_ttl), "%.28s", (cl)->name); \
        int _text_w = (font && strlen(_ttl) > 0) \
            ? XTextWidth(font, _ttl, strlen(_ttl)) : 0; \
        int _gap2   = _has_ico ? 10 : 0; \
        int _total2 = (_has_ico ? _ico_sz : 0) + _gap2 + _text_w; \
        int _sx2    = (_pw - _total2) / 2; \
        if (_sx2 < 6) _sx2 = 6; \
        int _cy2    = _img_h + BAR_H / 2; \
        /* scale icon with XRender transform */ \
        if (_has_ico) { \
            Pixmap _pm2 = XCreatePixmap(dpy, root, _ico_sz, _ico_sz, \
                DefaultDepth(dpy, screen)); \
            XRenderPictFormat *_vf = XRenderFindVisualFormat(dpy, \
                DefaultVisual(dpy, screen)); \
            Picture _pd = XRenderCreatePicture(dpy, _pm2, _vf, 0, NULL); \
            XRenderColor _bg2 = {0, 0, 0, 0}; \
            XRenderFillRectangle(dpy, PictOpSrc, _pd, &_bg2, \
                0, 0, _ico_sz, _ico_sz); \
            XTransform _xf2; \
            memset(&_xf2, 0, sizeof(_xf2)); \
            _xf2.matrix[0][0] = XDoubleToFixed((double)(cl)->icw / _ico_sz); \
            _xf2.matrix[1][1] = XDoubleToFixed((double)(cl)->ich / _ico_sz); \
            _xf2.matrix[2][2] = XDoubleToFixed(1.0); \
            XRenderSetPictureTransform(dpy, (cl)->icon, &_xf2); \
            XRenderComposite(dpy, PictOpOver, (cl)->icon, None, _pd, \
                0, 0, 0, 0, 0, 0, _ico_sz, _ico_sz); \
            XTransform _id2; memset(&_id2, 0, sizeof(_id2)); \
            _id2.matrix[0][0] = _id2.matrix[1][1] = _id2.matrix[2][2] = \
                XDoubleToFixed(1.0); \
            XRenderSetPictureTransform(dpy, (cl)->icon, &_id2); \
            Picture _pw2 = XRenderCreatePicture(dpy, (cl)->pre.win, \
                scrfmt, 0, NULL); \
            XRenderComposite(dpy, PictOpOver, _pd, None, _pw2, \
                0, 0, 0, 0, \
                _sx2, _cy2 - _ico_sz / 2, \
                _ico_sz, _ico_sz); \
            XRenderFreePicture(dpy, _pd); \
            XRenderFreePicture(dpy, _pw2); \
            XFreePixmap(dpy, _pm2); \
            _sx2 += _ico_sz + _gap2; \
        } \
        /* title text */ \
        if (font && _text_w > 0) { \
            int _ty2 = _cy2 + (font->ascent - font->descent) / 2; \
            XSetFont(dpy, _gc, font->fid); \
            XSetForeground(dpy, _gc, 0x000000); \
            XDrawString(dpy, (cl)->pre.win, _gc, \
                _sx2+1, _ty2+1, _ttl, strlen(_ttl)); \
            XSetForeground(dpy, _gc, 0xeeeeee); \
            XDrawString(dpy, (cl)->pre.win, _gc, \
                _sx2, _ty2, _ttl, strlen(_ttl)); \
        } \
        XFreeGC(dpy, _gc); \
    } while(0)

    /* max fraction of screen a single window may occupy */
    #define MAX_SINGLE_W 0.55
    #define MAX_SINGLE_H 0.55

    /* ── relayout visible windows ── */
    #define RELAYOUT_VISIBLE(filter_expr) do { \
        unsigned int _nvis = 0; \
        for (c = m->clients; c; c = c->next) { \
            if (ispanel(c) || !c->pre.win || !c->pre.orig_image) continue; \
            if (filter_expr) _nvis++; \
        } \
        /* hide non-matching */ \
        for (c = m->clients; c; c = c->next) { \
            if (ispanel(c) || !c->pre.win) continue; \
            if (!(filter_expr)) XUnmapWindow(dpy, c->pre.win); \
        } \
        if (_nvis > 0) { \
            unsigned int _cols = 1; \
            while (_cols * _cols < _nvis) _cols++; \
            unsigned int _rows = (_nvis + _cols - 1) / _cols; \
            int _gap3 = 16; \
            int _avw3 = m->ww - 60; \
            int _avh3 = grid_bottom - grid_top; \
            int _cw3, _img_h3, _ch3; \
            if (_nvis == 1) { \
                /* single window: cap size */ \
                _cw3   = (int)(m->ww * MAX_SINGLE_W); \
                _img_h3 = _cw3 * 9 / 16; \
                _ch3   = _img_h3 + BAR_H; \
                if (_ch3 > (int)(m->wh * MAX_SINGLE_H)) { \
                    _ch3   = (int)(m->wh * MAX_SINGLE_H); \
                    _img_h3 = _ch3 - BAR_H; \
                    _cw3   = _img_h3 * 16 / 9; \
                } \
            } else { \
                _cw3   = (_avw3 - (int)(_cols-1)*_gap3) / (int)_cols; \
                _img_h3 = _cw3 * 9 / 16; \
                _ch3   = _img_h3 + BAR_H; \
                int _need3 = (int)_rows*_ch3 + (int)(_rows-1)*_gap3; \
                if (_need3 > _avh3) { \
                    _ch3   = (_avh3 - (int)(_rows-1)*_gap3) / (int)_rows; \
                    _img_h3 = _ch3 - BAR_H; \
                    if (_img_h3 < 40) _img_h3 = 40; \
                    _ch3   = _img_h3 + BAR_H; \
                    _cw3   = _img_h3 * 16 / 9; \
                } \
            } \
            if (_cw3 < 120) _cw3 = 120; \
            int _tw3 = (int)_cols*_cw3 + (int)(_cols-1)*_gap3; \
            int _th3 = (int)_rows*_ch3 + (int)(_rows-1)*_gap3; \
            int _sx3 = m->wx + (m->ww - _tw3) / 2; \
            int _sy3 = grid_top + (grid_bottom - grid_top - _th3) / 2; \
            if (_sy3 < grid_top) _sy3 = grid_top; \
            unsigned int _vi3 = 0; \
            for (c = m->clients; c; c = c->next) { \
                if (ispanel(c) || !c->pre.win || !c->pre.orig_image) continue; \
                if (!(filter_expr)) continue; \
                int _col3 = (int)(_vi3 % _cols); \
                int _row3 = (int)(_vi3 / _cols); \
                int _nx3  = _sx3 + _col3 * (_cw3 + _gap3); \
                int _ny3  = _sy3 + _row3 * (_ch3 + _gap3); \
                XMoveResizeWindow(dpy, c->pre.win, _nx3, _ny3, _cw3, _ch3); \
                if (c->pre.scaled_image) { \
                    XDestroyImage(c->pre.scaled_image); \
                    c->pre.scaled_image = NULL; \
                } \
                c->pre.scaled_image = scaleimagetofit( \
                    c->pre.orig_image, \
                    (unsigned int)_cw3, \
                    (unsigned int)_ch3); \
                XMapWindow(dpy, c->pre.win); \
                REDRAW_CLIENT(c); \
                _vi3++; \
            } \
        } \
    } while(0)

    /* ── create preview windows ── */
    for (c = m->clients; c; c = c->next){
        if (ispanel(c) || !c->pre.orig_image) continue;
        XSetWindowAttributes cwa;
        cwa.override_redirect = True;
        cwa.background_pixel  = 0x111111;
        cwa.event_mask        = ButtonPress | EnterWindowMask | LeaveWindowMask;
        if (!c->pre.win)
            c->pre.win = XCreateWindow(dpy, root, 0, 0, 100, 80, 2,
                DefaultDepth(dpy, screen), InputOutput,
                DefaultVisual(dpy, screen),
                CWOverrideRedirect | CWBackPixel | CWEventMask, &cwa);
        XSetWindowBorder(dpy, c->pre.win, scheme[SchemeNorm][ColBorder].pixel);
        XUnmapWindow(dpy, c->win);
        XSelectInput(dpy, c->pre.win,
            ButtonPress | EnterWindowMask | LeaveWindowMask);
    }

    /* initial layout */
    RELAYOUT_VISIBLE(1);

    /* raise panel and search bar */
    for (c = m->clients; c; c = c->next)
        if (ispanel(c)) { XMapWindow(dpy, c->win); XRaiseWindow(dpy, c->win); }
    XRaiseWindow(dpy, searchwin);

    XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    XGrabPointer(dpy, root, True,
        ButtonPressMask | EnterWindowMask | LeaveWindowMask,
        GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    XEvent event;
    XSync(dpy, False);
    while (XCheckMaskEvent(dpy, ButtonPressMask | KeyPressMask, &event));

    int done = 0;
    struct timeval tv;
    fd_set fds;

    while (!done) {
        /* blink cursor every 500ms when search is focused */
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        tv.tv_sec  = 0;
        tv.tv_usec = 500000; /* 500ms */

        int ready = select(xfd + 1, &fds, NULL, NULL, &tv);

        if (ready == 0) {
            /* timeout: blink cursor */
            if (search_focused) {
                cursor_vis = !cursor_vis;
                DRAW_SEARCHBAR();
                XRaiseWindow(dpy, searchwin);
                XFlush(dpy);
            }
            continue;
        }

        while (XPending(dpy)) {
            XNextEvent(dpy, &event);
            switch (event.type) {

            case Expose:
                if (event.xexpose.window == searchwin) {
                    DRAW_SEARCHBAR();
                    XRaiseWindow(dpy, searchwin);
                } else {
                    for (c = m->clients; c; c = c->next) {
                        if (ispanel(c)) continue;
                        if (c->pre.win == event.xexpose.window) {
                            REDRAW_CLIENT(c); break;
                        }
                    }
                }
                break;

            case KeyPress: {
                KeySym ks = XKeycodeToKeysym(dpy, event.xkey.keycode, 0);
                cursor_vis = 1; /* reset blink on keypress */
                if (ks == XK_Escape) {
                    done = 1;
                } else if (ks == XK_BackSpace) {
                    if (search_len > 0) search_buf[--search_len] = '\0';
                    DRAW_SEARCHBAR();
                    XRaiseWindow(dpy, searchwin);
                    if (search_len == 0)
                        RELAYOUT_VISIBLE(1);
                    else
                        RELAYOUT_VISIBLE(
                            strcasestr(c->name, search_buf) != NULL);
                    XRaiseWindow(dpy, searchwin);
                } else if (ks == XK_Return) {
                    Client *match = NULL;
                    for (c = m->clients; c; c = c->next) {
                        if (ispanel(c) || !c->pre.win) continue;
                        if (search_len == 0 ||
                            strcasestr(c->name, search_buf)) {
                            match = c; break;
                        }
                    }
                    if (match) {
                        for (c = m->clients; c; c = c->next) {
                            if (ispanel(c)) continue;
                            if (c->pre.win) XUnmapWindow(dpy, c->pre.win);
                            XMapWindow(dpy, c->win);
                            if (c->pre.orig_image)
                                { XDestroyImage(c->pre.orig_image);
                                  c->pre.orig_image = NULL; }
                            if (c->pre.scaled_image)
                                { XDestroyImage(c->pre.scaled_image);
                                  c->pre.scaled_image = NULL; }
                        }
                        selmon->seltags ^= 1;
                        m->tagset[selmon->seltags] = match->tags;
                        focus_c = match;
                        focus(NULL);
                    }
                    done = 1;
                } else {
                    char buf[8] = {0};
                    XLookupString(&event.xkey, buf, sizeof(buf)-1, &ks, NULL);
                    if (buf[0] >= ' ' && buf[0] < 127 &&
                        search_len < (int)sizeof(search_buf)-1) {
                        search_buf[search_len++] = buf[0];
                        search_buf[search_len]   = '\0';
                        search_focused = 1;
                        DRAW_SEARCHBAR();
                        XRaiseWindow(dpy, searchwin);
                        RELAYOUT_VISIBLE(
                            strcasestr(c->name, search_buf) != NULL);
                        XRaiseWindow(dpy, searchwin);
                    }
                }
                break;
            }

            case ButtonPress:
                if (event.xbutton.window == searchwin) {
                    search_focused = 1;
                    cursor_vis = 1;
                    DRAW_SEARCHBAR();
                    XRaiseWindow(dpy, searchwin);
                    break;
                }
                if (event.xbutton.button == Button1) {
                    int _clicked_preview = False;
                    for (c = m->clients; c; c = c->next){
                        if (ispanel(c)) continue;
                        if (event.xbutton.window == c->pre.win)
                            _clicked_preview = True;
                    }
                    for (c = m->clients; c; c = c->next){
                        if (ispanel(c)) continue;
                        if (c->pre.win) XUnmapWindow(dpy, c->pre.win);
                        if (event.xbutton.window == c->pre.win){
                            selmon->seltags ^= 1;
                            m->tagset[selmon->seltags] = c->tags;
                            focus_c = c;
                            focus(NULL);
                        }
                        XMapWindow(dpy, c->win);
                        if (c->pre.orig_image)
                            { XDestroyImage(c->pre.orig_image);
                              c->pre.orig_image = NULL; }
                        if (c->pre.scaled_image)
                            { XDestroyImage(c->pre.scaled_image);
                              c->pre.scaled_image = NULL; }
                    }
                    if (_clicked_preview) done = 1;
                    /* click outside — just unfocus search */
                    else { search_focused = 0; DRAW_SEARCHBAR(); }
                } else if (event.xbutton.button == Button3) {
                    done = 1;
                }
                break;

            case EnterNotify:
                for (c = m->clients; c; c = c->next){
                    if (ispanel(c)) continue;
                    if (event.xcrossing.window == c->pre.win){
                        XSetWindowBorder(dpy, c->pre.win,
                            scheme[SchemeSel][ColBorder].pixel);
                        break;
                    }
                }
                break;

            case LeaveNotify:
                for (c = m->clients; c; c = c->next){
                    if (ispanel(c)) continue;
                    if (event.xcrossing.window == c->pre.win){
                        XSetWindowBorder(dpy, c->pre.win,
                            scheme[SchemeNorm][ColBorder].pixel);
                        break;
                    }
                }
                break;
            }
        }
    }

    #undef DRAW_SEARCHBAR
    #undef REDRAW_CLIENT
    #undef RELAYOUT_VISIBLE
    #undef BAR_H
    #undef MAX_SINGLE_W
    #undef MAX_SINGLE_H

    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, sgc);
    XDestroyWindow(dpy, searchwin);
    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);

    if (!focus_c) {
        for (c = m->clients; c; c = c->next){
            if (ispanel(c)) continue;
            if (c->pre.win) XUnmapWindow(dpy, c->pre.win);
            XMapWindow(dpy, c->win);
            if (c->pre.orig_image)
                { XDestroyImage(c->pre.orig_image);
                  c->pre.orig_image = NULL; }
            if (c->pre.scaled_image)
                { XDestroyImage(c->pre.scaled_image);
                  c->pre.scaled_image = NULL; }
        }
    }

    arrange(m);
    focus(focus_c);
    XSync(dpy, True);
    run_preview = 0;
}

//void
//previewallwin(const Arg *arg){
//	(void)arg;
//	Monitor *m = selmon;
//	Client *c, *focus_c = NULL;
//	unsigned int n;
//	for (n = 0, c = m->clients; c; c = c->next){
//		if (ispanel(c))
//			continue;
//		n++;
//		c->pre.orig_image = getwindowximage(c);
//	}
//	if (n == 0)
//		return;
//	setpreviewwindowsizepositions(n, m, 60, 15);
//	XEvent event;
//	for(c = m->clients; c; c = c->next){
//		if (ispanel(c))
//			continue;
//		if (!c->pre.win)
//			c->pre.win = XCreateSimpleWindow(dpy, root, c->pre.x, c->pre.y, c->pre.scaled_image->width, c->pre.scaled_image->height, 1, BlackPixel(dpy, screen), WhitePixel(dpy, screen));
//		else
//			XMoveResizeWindow(dpy, c->pre.win, c->pre.x, c->pre.y, c->pre.scaled_image->width, c->pre.scaled_image->height);
//		XSetWindowBorder(dpy, c->pre.win, scheme[SchemeNorm][ColBorder].pixel);
//		XUnmapWindow(dpy, c->win);
//		if (c->pre.win){
//			XSelectInput(dpy, c->pre.win, ButtonPress | EnterWindowMask | LeaveWindowMask);
//			XMapWindow(dpy, c->pre.win);
//			GC gc = XCreateGC(dpy, c->pre.win, 0, NULL);
//			XPutImage(dpy, c->pre.win, gc, c->pre.scaled_image, 0, 0, 0, 0, c->pre.scaled_image->width, c->pre.scaled_image->height);
//		}
//	}
//	/* raise panel above preview windows */
//	for(c = m->clients; c; c = c->next)
//		if (ispanel(c)) {
//			XMapWindow(dpy, c->win);
//			XRaiseWindow(dpy, c->win);
//		}
//	while (1) {
//		XNextEvent(dpy, &event);
//		if (event.type == ButtonPress)
//			if (event.xbutton.button == Button1){
//				for(c = m->clients; c; c = c->next){
//					if (ispanel(c))
//						continue;
//					XUnmapWindow(dpy, c->pre.win);
//					if (event.xbutton.window == c->pre.win){
//						selmon->seltags ^= 1;
//						m->tagset[selmon->seltags] = c->tags;
//						focus_c = c;
//						focus(NULL);
//					}
//					XMapWindow(dpy, c->win);
//					XDestroyImage(c->pre.orig_image);
//					XDestroyImage(c->pre.scaled_image);
//				}
//				break;
//			}
//		if (event.type == EnterNotify)
//			for(c = m->clients; c; c = c->next){
//				if (ispanel(c))
//					continue;
//				if (event.xcrossing.window == c->pre.win){
//					XSetWindowBorder(dpy, c->pre.win, scheme[SchemeSel][ColBorder].pixel);
//					break;
//				}
//			}
//		if (event.type == LeaveNotify)
//			for(c = m->clients; c; c = c->next){
//				if (ispanel(c))
//					continue;
//				if (event.xcrossing.window == c->pre.win){
//					XSetWindowBorder(dpy, c->pre.win, scheme[SchemeNorm][ColBorder].pixel);
//					break;
//				}
//			}
//	}
//	arrange(m);
//	focus(focus_c);
//}

static void
previewallwinwrap(const Arg *arg)
{
    (void)arg;
    previewallwin(NULL);
}

//void
//previewallwin(){
//	Monitor *m = selmon;
//	Client *c, *focus_c = NULL;
//	unsigned int n;
//	for (n = 0, c = m->clients; c; c = c->next){
//		if (ispanel(c))
//			continue;
//		n++;
//		c->pre.orig_image = getwindowximage(c);
//	}
//	if (n == 0)
//		return;
//	setpreviewwindowsizepositions(n, m, 60, 15);
//	XEvent event;
//	for(c = m->clients; c; c = c->next){
//		if (ispanel(c))
//			continue;
//		if (!c->pre.win)
//			c->pre.win = XCreateSimpleWindow(dpy, root, c->pre.x, c->pre.y, c->pre.scaled_image->width, c->pre.scaled_image->height, 1, BlackPixel(dpy, screen), WhitePixel(dpy, screen));
//		else
//			XMoveResizeWindow(dpy, c->pre.win, c->pre.x, c->pre.y, c->pre.scaled_image->width, c->pre.scaled_image->height);
//		XSetWindowBorder(dpy, c->pre.win, scheme[SchemeNorm][ColBorder].pixel);
//		XUnmapWindow(dpy, c->win);
//		if (c->pre.win){
//			XSelectInput(dpy, c->pre.win, ButtonPress | EnterWindowMask | LeaveWindowMask);
//			XMapWindow(dpy, c->pre.win);
//			GC gc = XCreateGC(dpy, c->pre.win, 0, NULL);
//			XPutImage(dpy, c->pre.win, gc, c->pre.scaled_image, 0, 0, 0, 0, c->pre.scaled_image->width, c->pre.scaled_image->height);
//		}
//	}
//	while (1) {
//		XNextEvent(dpy, &event);
//		if (event.type == ButtonPress)
//			if (event.xbutton.button == Button1){
//				for(c = m->clients; c; c = c->next){
//					if (ispanel(c))
//						continue;
//					XUnmapWindow(dpy, c->pre.win);
//					if (event.xbutton.window == c->pre.win){
//						selmon->seltags ^= 1;
//						m->tagset[selmon->seltags] = c->tags;
//						focus_c = c;
//						focus(NULL);
//					}
//					XMapWindow(dpy, c->win);
//					XDestroyImage(c->pre.orig_image);
//					XDestroyImage(c->pre.scaled_image);
//				}
//				break;
//			}
//		if (event.type == EnterNotify)
//			for(c = m->clients; c; c = c->next){
//				if (ispanel(c))
//					continue;
//				if (event.xcrossing.window == c->pre.win){
//					XSetWindowBorder(dpy, c->pre.win, scheme[SchemeSel][ColBorder].pixel);
//					break;
//				}
//			}
//		if (event.type == LeaveNotify)
//			for(c = m->clients; c; c = c->next){
//				if (ispanel(c))
//					continue;
//				if (event.xcrossing.window == c->pre.win){
//					XSetWindowBorder(dpy, c->pre.win, scheme[SchemeNorm][ColBorder].pixel);
//					break;
//				}
//			}
//	}
//	arrange(m);
//	focus(focus_c);
//}

//void
//setpreviewwindowsizepositions(unsigned int n, Monitor *m, unsigned int gappo, unsigned int gappi){
//	unsigned int i, j;
//	unsigned int cx, cy, cw, ch, cmaxh;
//	unsigned int cols, rows;
//	Client *c, *tmpc, *first = NULL, *second = NULL;
//
//	/* find first non-panel client */
//	for (c = m->clients; c; c = c->next)
//		if (!ispanel(c)) { first = c; break; }
//
//	if (n == 1) {
//		c = m->clients;
//		cw = (m->ww - 2 * gappo) * 0.8;
//		ch = (m->wh - 2 * gappo) * 0.9;
//		c->pre.scaled_image = scaledownimage(c->pre.orig_image, cw, ch);
//		c->pre.x = m->mx + (m->mw - c->pre.scaled_image->width) / 2;
//		c->pre.y = m->my + (m->mh - c->pre.scaled_image->height) / 2;
//		return;
//	}
//	if (n == 2) {
//		c = m->clients;
//		cw = (m->ww - 2 * gappo - gappi) / 2;
//		ch = (m->wh - 2 * gappo) * 0.7;
//		c->pre.scaled_image = scaledownimage(c->pre.orig_image, cw, ch);
//		c->next->pre.scaled_image = scaledownimage(c->next->pre.orig_image, cw, ch);
//		c->pre.x = m->mx + (m->mw - c->pre.scaled_image->width - gappi - c->next->pre.scaled_image->width) / 2;
//		c->pre.y = m->my + (m->mh - c->pre.scaled_image->height) / 2;
//		c->next->pre.x = c->pre.x + c->pre.scaled_image->width + gappi;
//		c->next->pre.y = m->my + (m->mh - c->next->pre.scaled_image->height) / 2;
//		return;
//	}
//	for (cols = 0; cols <= n / 2; cols++)
//		if (cols * cols >= n)
//			break;
//	rows = (cols && (cols - 1) * cols >= n) ? cols - 1 : cols;
//	ch = (m->wh - 2 * gappo) / rows;
//	cw = (m->ww - 2 * gappo) / cols;
//	c = m->clients;
//	cy = 0;
//	for (i = 0; i < rows; i++) {
//		cx = 0;
//		cmaxh = 0;
//		tmpc = c;
//		for (int j = 0; j < cols; j++) {
//			if (!c)
//				break;
//			c->pre.scaled_image = scaledownimage(c->pre.orig_image, cw, ch);
//			c->pre.x = cx;
//			cmaxh = c->pre.scaled_image->height > cmaxh ? c->pre.scaled_image->height : cmaxh;
//			cx += c->pre.scaled_image->width + gappi;
//			c = c->next;
//		}
//		c = tmpc;
//		cx = m->wx + (m->ww - cx) / 2;
//		for (j = 0; j < cols; j++) {
//			if (!c)
//				break;
//			c->pre.x += cx;
//			c->pre.y = cy + (cmaxh - c->pre.scaled_image->height) / 2;
//			c = c->next;
//		}
//		cy += cmaxh + gappi;
//	}
//	cy = m->wy + (m->wh - cy) / 2;
//	for (c = m->clients; c; c = c->next)
//		c->pre.y += cy;
//}

//void
//setpreviewwindowsizepositions(unsigned int n, Monitor *m, unsigned int gappo, unsigned int gappi){
//	unsigned int i, j;
//	unsigned int cx, cy, cw, ch, cmaxh;
//	unsigned int cols, rows;
//	Client *c, *tmpc, *first = NULL, *second = NULL;
//
//	/* find first non-panel client */
//	for (c = m->clients; c; c = c->next)
//		if (!ispanel(c)) { first = c; break; }
//
//	if (n == 1) {
//		cw = (m->ww - 2 * gappo) * 0.8;
//		ch = (m->wh - 2 * gappo) * 0.9;
//		first->pre.scaled_image = scaledownimage(first->pre.orig_image, cw, ch);
//		first->pre.x = m->mx + (m->mw - first->pre.scaled_image->width) / 2;
//		first->pre.y = m->my + (m->mh - first->pre.scaled_image->height) / 2;
//		return;
//	}
//	if (n == 2) {
//		/* find second non-panel client */
//		for (c = first->next; c; c = c->next)
//			if (!ispanel(c)) { second = c; break; }
//		cw = (m->ww - 2 * gappo - gappi) / 2;
//		ch = (m->wh - 2 * gappo) * 0.7;
//		first->pre.scaled_image = scaledownimage(first->pre.orig_image, cw, ch);
//		second->pre.scaled_image = scaledownimage(second->pre.orig_image, cw, ch);
//		first->pre.x = m->mx + (m->mw - first->pre.scaled_image->width - gappi - second->pre.scaled_image->width) / 2;
//		first->pre.y = m->my + (m->mh - first->pre.scaled_image->height) / 2;
//		second->pre.x = first->pre.x + first->pre.scaled_image->width + gappi;
//		second->pre.y = m->my + (m->mh - second->pre.scaled_image->height) / 2;
//		return;
//	}
//	for (cols = 0; cols <= n / 2; cols++)
//		if (cols * cols >= n)
//			break;
//	rows = (cols && (cols - 1) * cols >= n) ? cols - 1 : cols;
//	ch = (m->wh - 2 * gappo) / rows;
//	cw = (m->ww - 2 * gappo) / cols;
//	c = first;
//	cy = 0;
//	for (i = 0; i < rows; i++) {
//		cx = 0;
//		cmaxh = 0;
//		tmpc = c;
//		for (j = 0; j < cols; j++) {
//			if (!c)
//				break;
//			if (ispanel(c)) { j--; c = c->next; continue; }
//			c->pre.scaled_image = scaledownimage(c->pre.orig_image, cw, ch);
//			c->pre.x = cx;
//			cmaxh = c->pre.scaled_image->height > cmaxh ? c->pre.scaled_image->height : cmaxh;
//			cx += c->pre.scaled_image->width + gappi;
//			c = c->next;
//		}
//		c = tmpc;
//		cx = m->wx + (m->ww - cx) / 2;
//		for (j = 0; j < cols; j++) {
//			if (!c)
//				break;
//			if (ispanel(c)) { j--; c = c->next; continue; }
//			c->pre.x += cx;
//			c->pre.y = cy + (cmaxh - c->pre.scaled_image->height) / 2;
//			c = c->next;
//		}
//		cy += cmaxh + gappi;
//	}
//	cy = m->wy + (m->wh - cy) / 2;
//	for (c = m->clients; c; c = c->next) {
//		if (ispanel(c))
//			continue;
//		c->pre.y += cy;
//	}
//}

XImage*
scaleimagetofit(XImage *orig_image, unsigned int tw, unsigned int th) {
	XImage *scaled_image = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)),
			orig_image->depth,
			ZPixmap, 0, NULL,
			tw, th,
			32, 0);
	scaled_image->data = malloc(scaled_image->height * scaled_image->bytes_per_line);
	for (unsigned int y = 0; y < th; y++) {
		for (unsigned int x = 0; x < tw; x++) {
			int orig_x = x * orig_image->width  / tw;
			int orig_y = y * orig_image->height / th;
			unsigned long pixel = XGetPixel(orig_image, orig_x, orig_y);
			XPutPixel(scaled_image, x, y, pixel);
		}
	}
	scaled_image->depth = orig_image->depth;
	return scaled_image;
}

//void
//setpreviewwindowsizepositions(unsigned int n, Monitor *m, unsigned int gappo, unsigned int gappi){
//	unsigned int i, j;
//	unsigned int cx, cy, cw, ch;
//	unsigned int cols, rows;
//	Client *c, *first = NULL, *second = NULL;
//
//	for (c = m->clients; c; c = c->next)
//		if (!ispanel(c)) { first = c; break; }
//
//	if (n == 1) {
//		cw = (m->ww - 2 * gappo) * 0.8;
//		ch = (m->wh - 2 * gappo) * 0.9;
//		first->pre.scaled_image = scaleimagetofit(first->pre.orig_image, cw, ch);
//		first->pre.x = m->mx + (m->mw - cw) / 2;
//		first->pre.y = m->my + (m->mh - ch) / 2;
//		return;
//	}
//
//	if (n == 2) {
//		for (c = first->next; c; c = c->next)
//			if (!ispanel(c)) { second = c; break; }
//		cw = (m->ww - 2 * gappo - gappi) / 2;
//		ch = (m->wh - 2 * gappo) * 0.7;
//		first->pre.scaled_image  = scaleimagetofit(first->pre.orig_image,  cw, ch);
//		second->pre.scaled_image = scaleimagetofit(second->pre.orig_image, cw, ch);
//		first->pre.x  = m->mx + (m->mw - cw - gappi - cw) / 2;
//		first->pre.y  = m->my + (m->mh - ch) / 2;
//		second->pre.x = first->pre.x + cw + gappi;
//		second->pre.y = first->pre.y;
//		return;
//	}
//
//	for (cols = 0; cols <= n / 2; cols++)
//		if (cols * cols >= n)
//			break;
//	rows = (cols && (cols - 1) * cols >= n) ? cols - 1 : cols;
//
//	/* fixed cell size for all windows */
//	cw = (m->ww - 2 * gappo - (cols - 1) * gappi) / cols;
//	ch = (m->wh - 2 * gappo - (rows - 1) * gappi) / rows;
//
//	/* calculate total grid size for centering */
//	unsigned int total_w = cols * cw + (cols - 1) * gappi;
//	unsigned int total_h = rows * ch + (rows - 1) * gappi;
//	unsigned int start_x = m->wx + (m->ww - total_w) / 2;
//	unsigned int start_y = m->wy + (m->wh - total_h) / 2;
//
//	c = first;
//	cy = start_y;
//	for (i = 0; i < rows; i++) {
//		cx = start_x;
//		for (j = 0; j < cols; j++) {
//			if (!c) break;
//			if (ispanel(c)) { j--; c = c->next; continue; }
//			/* force exact cell size */
//			c->pre.scaled_image = scaleimagetofit(c->pre.orig_image, cw, ch);
//			c->pre.x = cx;
//			c->pre.y = cy;
//			cx += cw + gappi;
//			c = c->next;
//			while (c && ispanel(c)) c = c->next;
//		}
//		cy += ch + gappi;
//	}
//}

//void
//setpreviewwindowsizepositions(unsigned int n, Monitor *m, unsigned int gappo, unsigned int gappi){
//	unsigned int i, j;
//	unsigned int cx, cy, cw, ch;
//	unsigned int cols, rows;
//	Client *c, *tmpc, *first = NULL, *second = NULL;
//
//	/* find first non-panel client */
//	for (c = m->clients; c; c = c->next)
//		if (!ispanel(c)) { first = c; break; }
//
//	if (n == 1) {
//		cw = (m->ww - 2 * gappo) * 0.8;
//		ch = (m->wh - 2 * gappo) * 0.9;
//		first->pre.scaled_image = scaledownimage(first->pre.orig_image, cw, ch);
//		first->pre.x = m->mx + (m->mw - first->pre.scaled_image->width) / 2;
//		first->pre.y = m->my + (m->mh - first->pre.scaled_image->height) / 2;
//		return;
//	}
//
//	if (n == 2) {
//		for (c = first->next; c; c = c->next)
//			if (!ispanel(c)) { second = c; break; }
//		cw = (m->ww - 2 * gappo - gappi) / 2;
//		ch = (m->wh - 2 * gappo) * 0.7;
//		first->pre.scaled_image  = scaledownimage(first->pre.orig_image,  cw, ch);
//		second->pre.scaled_image = scaledownimage(second->pre.orig_image, cw, ch);
//		first->pre.x  = m->mx + (m->mw - cw - gappi - cw) / 2;
//		first->pre.y  = m->my + (m->mh - first->pre.scaled_image->height) / 2;
//		second->pre.x = first->pre.x + cw + gappi;
//		second->pre.y = m->my + (m->mh - second->pre.scaled_image->height) / 2;
//		return;
//	}
//
//	for (cols = 0; cols <= n / 2; cols++)
//		if (cols * cols >= n)
//			break;
//	rows = (cols && (cols - 1) * cols >= n) ? cols - 1 : cols;
//
//	/* fixed cell size — all windows same width and height */
//	cw = (m->ww - 2 * gappo - (cols - 1) * gappi) / cols;
//	ch = (m->wh - 2 * gappo - (rows - 1) * gappi) / rows;
//
//	c = first;
//	cy = m->wy + gappo;
//	for (i = 0; i < rows; i++) {
//		cx = m->wx + gappo;
//		tmpc = c;
//		for (j = 0; j < cols; j++) {
//			if (!c) break;
//			if (ispanel(c)) { j--; c = c->next; continue; }
//			/* scale image to fixed cell size */
//			c->pre.scaled_image = scaledownimage(c->pre.orig_image, cw, ch);
//			/* center scaled image within the fixed cell */
//			c->pre.x = cx + (cw - c->pre.scaled_image->width) / 2;
//			c->pre.y = cy + (ch - c->pre.scaled_image->height) / 2;
//			cx += cw + gappi;
//			c = c->next;
//		}
//		cy += ch + gappi;
//	}
//}

XImage*
getwindowximage(Client *c) {
	XWindowAttributes attr;
	XGetWindowAttributes( dpy, c->win, &attr );
	XRenderPictFormat *format = XRenderFindVisualFormat( dpy, attr.visual );
	int hasAlpha = ( format->type == PictTypeDirect && format->direct.alphaMask );
	XRenderPictureAttributes pa;
	pa.subwindow_mode = IncludeInferiors;
	Picture picture = XRenderCreatePicture( dpy, c->win, format, CPSubwindowMode, &pa );
	Pixmap pixmap = XCreatePixmap(dpy, root, c->w, c->h, 32);
	XRenderPictureAttributes pa2 = {0};
	XRenderPictFormat *format2 = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	Picture pixmapPicture = XRenderCreatePicture( dpy, pixmap, format2, 0, &pa2 );
	XRenderColor color;
	color.red = 0x0000;
	color.green = 0x0000;
	color.blue = 0x0000;
	color.alpha = 0x0000;
	XRenderFillRectangle (dpy, PictOpSrc, pixmapPicture, &color, 0, 0, c->w, c->h);
	XRenderComposite(dpy, hasAlpha ? PictOpOver : PictOpSrc, picture, 0,
			pixmapPicture, 0, 0, 0, 0, 0, 0,
			c->w, c->h);
	XImage* temp = XGetImage( dpy, pixmap, 0, 0, c->w, c->h, AllPlanes, ZPixmap );
	temp->red_mask = format2->direct.redMask << format2->direct.red;
	temp->green_mask = format2->direct.greenMask << format2->direct.green;
	temp->blue_mask = format2->direct.blueMask << format2->direct.blue;
	temp->depth = DefaultDepth(dpy, screen);
	return temp;
}

//XImage*
//scaledownimage(XImage *orig_image, unsigned int cw, unsigned int ch) {
//	int factor_w = orig_image->width / cw + 1;
//	int factor_h = orig_image->height / ch + 1;
//	int scale_factor = factor_w > factor_h ? factor_w : factor_h;
//	int scaled_width = orig_image->width / scale_factor;
//	int scaled_height = orig_image->height / scale_factor;
//	XImage *scaled_image = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)),
//			orig_image->depth,
//			ZPixmap, 0, NULL,
//			scaled_width, scaled_height,
//			32, 0);
//	scaled_image->data = malloc(scaled_image->height * scaled_image->bytes_per_line);
//	for (int y = 0; y < scaled_height; y++) {
//		for (int x = 0; x < scaled_width; x++) {
//			int orig_x = x * scale_factor;
//			int orig_y = y * scale_factor;
//			unsigned long pixel = XGetPixel(orig_image, orig_x, orig_y);
//			XPutPixel(scaled_image, x, y, pixel);
//		}
//	}
//	scaled_image->depth = orig_image->depth;
//	return scaled_image;
//}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("mrdwm-"VERSION);
	else if (argc != 1)
		die("usage: mrdwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("mrdwm: cannot open display");
	if (!(xcon = XGetXCBConnection(dpy)))
		die("mrdwm: cannot get xcb connection\n");
	checkotherwm();
	setup();
#ifdef __OpenBSD__
	//if (pledge("stdio rpath proc exec", NULL) == -1)
	if (pledge("stdio rpath proc exec ps", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	runautostart();
	run();
	if (restart) execvp(argv[0], argv);
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
