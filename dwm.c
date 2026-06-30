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

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
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

#define TAGSLENGTH (LENGTH(tags))

#define STATUSIMG_CACHE_MAX 64
#define STATUSIMG_PATH_MAX  4096

/* MrRobotOS dark palette */
#define OV_CARD_SOLID    0x0f0f15UL
#define OV_CARD_HOV_S    0x1a1a26UL
#define OV_TITLEBAR_S    0x0b0b12UL
#define OV_TAB_ACTIVE    0xe63946UL
#define OV_TAB_NORM_S    0x111120UL
#define OV_TAB_DIV       0x252535UL
#define OV_ACCENT        0x00ff88UL
#define OV_WIN_SEL_BDR   0xe63946UL
#define OV_BORDER_NORM   0x2a2a3aUL
#define OV_TEXT_HI       0xf4f4f8UL
#define OV_TEXT_MID      0x9090a8UL
#define OV_TEXT_LO       0x3a3a55UL
#define OV_SEARCH_BG     0xf2f2f5UL
#define OV_SEARCH_TEXT   0x0a0a14UL
#define OV_SEARCH_PH     0x777788UL
#define OV_SEARCH_BRD    0xc8c8d8UL
#define OV_PG_BG_S       0x0b0b12UL
#define OV_PG_BTN_S      0x18182aUL
#define OV_PG_BTN_DIS    0x0f0f18UL
#define OV_SEP           0x20203aUL
#define OV_DIM           0x111120UL


/* Layout dimensions (px) */
#define OV_SB_H          46
#define OV_TAB_H         46
#define OV_PG_H          50
#define OV_GAP           14
#define OV_MARGIN        56
#define OV_TOP_PAD       24
#define OV_TITLEBAR_H    46
#define OV_ICON_SZ       56
#define OV_RUN_PILL_W    32
#define OV_RUN_PILL_H     4
#define OV_SB_MAX_W      580
#define OV_TAB_MAX_W     460
#define OV_WIN_PER_PG      6
#define OV_APP_CH        148
#define OV_CORNER_R        8


/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
//enum { SchemeNorm, SchemeSel }; /* color schemes */
//enum { SchemeNorm, SchemeSel, SchemeHov, SchemeHid }; /* color schemes */
enum { SchemeNorm, SchemeSel, SchemeHov, SchemeHid, SchemeStatus, SchemeTagsSel, SchemeTagsNorm, SchemeInfoSel, SchemeInfoNorm }; /* color schemes */
//enum { NetSupported, NetWMName, NetWMIcon, NetWMState, NetWMCheck,
//	NetWMFullscreen, NetActiveWindow, NetWMWindowType,
//	NetWMWindowTypeDialog, NetClientList, NetClientInfo, NetLast }; /* EWMH atoms */
enum { NetSupported, NetWMName, NetWMIcon, NetWMState, NetWMCheck,
    NetWMFullscreen, NetActiveWindow, NetWMWindowType,
    NetWMWindowTypeDialog, NetWMWindowTypeNormal, NetClientList, NetClientInfo,
    NetDesktopNames, NetDesktopViewport, NetNumberOfDesktops, NetCurrentDesktop,
    NetLast };
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

typedef struct {
	char         name[256];
	char         exec[512];
	char         icon_path[512];
	int          running;
	int          run_count;
	Picture      icon_pic;
	unsigned int icw, ich;
} AppEntry;

typedef enum { TAB_WINDOWS = 0, TAB_APPS = 1 } OverviewTab;

/* sub-view shown when clicking a running app */
typedef enum {
	VIEW_MAIN = 0,        /* normal windows/apps grid          */
	VIEW_APP_RUNNING = 1  /* "app is running" picker           */
} OverviewView;

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
static void spawnifdesk(const Arg *arg);
static void spawnifterm(const Arg *arg);
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
static void    ov_draw_searchbar(Window w, GC gc, XFontStruct *f,
				 const char *buf, int len, int cursor_vis,
				 int wb, int hb);
static void    ov_draw_tabs(Window w, GC gc, XFontStruct *fb,
			    OverviewTab cur, int tw, int th);
static void    ov_draw_pager(Window w, GC gc, XFontStruct *f,
			     int page, int total_pages, int ww, int ph);
static Picture ov_load_icon(const char *path, unsigned int sz);
static void    ov_find_icon(const char *name, char *out, size_t outsz);
static void    ov_parse_one_dir(const char *dir,
				AppEntry **apps, int *count, int *cap);
static int     ov_parse_desktop_files(AppEntry **out, int *count);
static void    ov_free_apps(AppEntry *apps, int count);
static void    ov_launch(const char *exec_raw);
static int     ov_count_running(AppEntry *a);
static void    ov_blit_pic(Drawable dst, Picture src,
			   int dx, int dy, unsigned int w, unsigned int h);
static void    ov_draw_win_card(Window cw, GC gc, XFontStruct *f,
				Client *c, int hovered, int selected,
				unsigned int cardw, unsigned int cardh);
static void    ov_draw_app_card(Window cw, GC gc, XFontStruct *f,
				AppEntry *a, int hovered, int selected,
				unsigned int cardw, unsigned int cardh);
static void    ov_draw_run_card(Window cw, GC gc, XFontStruct *f,
				const char *line1, const char *line2,
				Picture icon, int hovered, int selected,
				int special, unsigned long spec_col,
				unsigned int cardw, unsigned int cardh);
static int     ov_app_cmp(const void *a, const void *b);
static Window  ov_make_card_win(Window parent, int cx, int cy,
				int cw, int ch, unsigned long bg);

static void setcurrentdesktop(void);
static void setdesktopnames(void);
static void setnumdesktops(void);
static void setviewport(void);
static void updatecurrentdesktop(void);


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

//static volatile sig_atomic_t sigusr1_count = 0;
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
static XRenderPictFormat *scrfmt = NULL;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

	static inline void
ov_fill(Drawable dr, GC gc, unsigned long col, int x, int y, int w, int h)
{
	XSetForeground(dpy, gc, col);
	XFillRectangle(dpy, dr, gc, x, y, (unsigned)w, (unsigned)h);
}

static inline void
ov_fill_rect(Drawable dr, GC gc, unsigned long col, int x, int y, int w, int h)
{
    XSetForeground(dpy, gc, col);
    XFillRectangle(dpy, dr, gc, x, y, (unsigned)w, (unsigned)h);
}


	static void
ov_fill_round(Drawable dr, GC gc, unsigned long col, unsigned long bg,
	      int x, int y, int w, int h, int r)
{
	XSetForeground(dpy, gc, col);
	XFillRectangle(dpy, dr, gc, x, y, (unsigned)w, (unsigned)h);
	XSetForeground(dpy, gc, bg);
	XFillRectangle(dpy, dr, gc, x,     y,     r, r);
	XFillRectangle(dpy, dr, gc, x+w-r, y,     r, r);
	XFillRectangle(dpy, dr, gc, x,     y+h-r, r, r);
	XFillRectangle(dpy, dr, gc, x+w-r, y+h-r, r, r);
	XSetForeground(dpy, gc, col);
	XFillArc(dpy, dr, gc, x,       y,       2*r, 2*r, 90*64,  90*64);
	XFillArc(dpy, dr, gc, x+w-2*r, y,       2*r, 2*r, 0,      90*64);
	XFillArc(dpy, dr, gc, x,       y+h-2*r, 2*r, 2*r, 180*64, 90*64);
	XFillArc(dpy, dr, gc, x+w-2*r, y+h-2*r, 2*r, 2*r, 270*64, 90*64);
}

static inline void
ov_rect_plain(Drawable dr, GC gc, unsigned long col, int x, int y, int w, int h)
{
    XSetForeground(dpy, gc, col);
    XDrawRectangle(dpy, dr, gc, x, y, (unsigned)w, (unsigned)h);
}



	static void
ov_rect_round(Drawable dr, GC gc, unsigned long col,
	      int x, int y, int w, int h, int r)
{
	XSetForeground(dpy, gc, col);
	XDrawLine(dpy, dr, gc, x+r,   y,     x+w-r-1, y);
	XDrawLine(dpy, dr, gc, x+r,   y+h-1, x+w-r-1, y+h-1);
	XDrawLine(dpy, dr, gc, x,     y+r,   x,       y+h-r-1);
	XDrawLine(dpy, dr, gc, x+w-1, y+r,   x+w-1,   y+h-r-1);
	XDrawArc(dpy, dr, gc, x,       y,       2*r, 2*r, 90*64,  90*64);
	XDrawArc(dpy, dr, gc, x+w-2*r, y,       2*r, 2*r, 0,      90*64);
	XDrawArc(dpy, dr, gc, x,       y+h-2*r, 2*r, 2*r, 180*64, 90*64);
	XDrawArc(dpy, dr, gc, x+w-2*r, y+h-2*r, 2*r, 2*r, 270*64, 90*64);
}

	static void
ov_fill_pill(Drawable dr, GC gc, unsigned long col,
	     int x, int y, int w, int h)
{
	ov_fill_round(dr, gc, col, col, x, y, w, h, h/2);
}

	static void
ov_text_c(Drawable dr, GC gc, XFontStruct *f, unsigned long col,
	  const char *s, int rx, int ry, int rw, int rh)
{
	if (!s || !*s || rw <= 0) return;
	int tw = f ? XTextWidth(f, s, (int)strlen(s)) : (int)strlen(s)*10;
	int tx = rx + (rw - tw) / 2;
	int ty = ry + (rh + (f ? f->ascent : 12) - (f ? f->descent : 3)) / 2;
	if (f) XSetFont(dpy, gc, f->fid);
	XSetForeground(dpy, gc, col);
	XDrawString(dpy, dr, gc, tx, ty, s, (int)strlen(s));
}

/* left-aligned with ... clip */
	static void
ov_text_l(Drawable dr, GC gc, XFontStruct *f, unsigned long col,
	  const char *s, int rx, int ry, int rw, int rh)
{
	if (!s || !*s || rw <= 0) return;
	int ty = ry + (rh + (f ? f->ascent : 12) - (f ? f->descent : 3)) / 2;
	if (f) XSetFont(dpy, gc, f->fid);
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", s);
	if (f) {
		int ew = XTextWidth(f, "...", 3);
		while ((int)strlen(buf) > 3 &&
		       XTextWidth(f, buf, (int)strlen(buf)) > rw)
			buf[strlen(buf)-1] = '\0';
		if (strcmp(buf, s) != 0)
			strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
		(void)ew;
	}
	XSetForeground(dpy, gc, col);
	XDrawString(dpy, dr, gc, rx, ty, buf, (int)strlen(buf));
}


/* centred with ... clip */
	static void
ov_text_cc(Drawable dr, GC gc, XFontStruct *f, unsigned long col,
	   const char *s, int rx, int ry, int rw, int rh)
{
	if (!s || !*s || rw <= 0) return;
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", s);
	if (f) {
		while ((int)strlen(buf) > 3 &&
		       XTextWidth(f, buf, (int)strlen(buf)) > rw)
			buf[strlen(buf)-1] = '\0';
		if (strcmp(buf, s) != 0)
			strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
	}
	ov_text_c(dr, gc, f, col, buf, rx, ry, rw, rh);
}

/* ── search bar ─────────────────────────────────────────────────────────── */
static void
ov_draw_searchbar(Window w, GC gc, XFontStruct *f,
		  const char *buf, int len, int cursor_vis,
		  int wb, int hb)
{
	/* background + border */
	ov_fill_round(w, gc, OV_SEARCH_BG, OV_DIM, 0, 0, wb, hb, OV_CORNER_R);
	ov_rect_round(w, gc, OV_SEARCH_BRD, 0, 0, wb - 1, hb - 1, OV_CORNER_R);

	/* icon: square, fits inside bar with equal top/bottom margin */
	int ico_sz = hb - 18;
	if (ico_sz < 8) ico_sz = 8;
	int ico_x = 10;
	int ico_y = (hb - ico_sz) / 2;

	/* draw a simple magnifier using Xlib — no icon loading, no colour issues */
	{
		int cx = ico_x + ico_sz / 2 - ico_sz / 8;
		int cy = ico_y + ico_sz / 2 - ico_sz / 8;
		int r  = ico_sz / 3;
		int lx1 = cx + (int)(r * 0.65);
		int ly1 = cy + (int)(r * 0.65);
		int lx2 = ico_x + ico_sz - 3;
		int ly2 = ico_y + ico_sz - 3;

		XSetForeground(dpy, gc, OV_SEARCH_TEXT);
		/* circle — draw twice for thickness */
		XDrawArc(dpy, w, gc, cx - r, cy - r, r * 2,     r * 2,     0, 360 * 64);
		XDrawArc(dpy, w, gc, cx - r + 1, cy - r + 1, r * 2 - 2, r * 2 - 2, 0, 360 * 64);
		/* handle */
		XDrawLine(dpy, w, gc, lx1,     ly1,     lx2,     ly2);
		XDrawLine(dpy, w, gc, lx1 + 1, ly1,     lx2 + 1, ly2);
		XDrawLine(dpy, w, gc, lx1,     ly1 + 1, lx2,     ly2 + 1);
	}

	int tx = ico_x + ico_sz + 10;
	int tw = wb - tx - 14;

	if (len == 0) {
		ov_text_l(w, gc, f, OV_SEARCH_PH, "Type to search...", tx, 0, tw, hb);
		if (cursor_vis) {
			XSetForeground(dpy, gc, OV_SEARCH_TEXT);
			XDrawLine(dpy, w, gc, tx,     hb / 4 + 2, tx,     hb * 3 / 4 - 2);
			XDrawLine(dpy, w, gc, tx + 1, hb / 4 + 2, tx + 1, hb * 3 / 4 - 2);
		}
	} else {
		ov_text_l(w, gc, f, OV_SEARCH_TEXT, buf, tx, 0, tw, hb);
		if (cursor_vis) {
			int cx = tx + (f && len > 0 ? XTextWidth(f, buf, len) : 0) + 2;
			XSetForeground(dpy, gc, OV_SEARCH_TEXT);
			XDrawLine(dpy, w, gc, cx,     hb / 4 + 2, cx,     hb * 3 / 4 - 2);
			XDrawLine(dpy, w, gc, cx + 1, hb / 4 + 2, cx + 1, hb * 3 / 4 - 2);
		}
	}
}
/* ── tab bar ────────────────────────────────────────────────────────────── */
static void
ov_draw_tabs(Window w, GC gc, XFontStruct *fb,
	     OverviewTab cur, int tw, int th)
{
	int hw = tw / 2;

    /* full background */
    ov_fill_rect(w, gc, OV_TAB_NORM_S, 0, 0, tw, th);

    if (cur == TAB_WINDOWS) {
        /* left half active */
        ov_fill_rect(w, gc, OV_TAB_ACTIVE, 0, 0, hw, th);
    } else {
        /* right half active */
        ov_fill_rect(w, gc, OV_TAB_ACTIVE, hw, 0, tw - hw, th);
    }

    ov_text_c(w, gc, fb,
              cur == TAB_WINDOWS ? 0x050505UL : OV_TEXT_MID,
              "WINDOWS", 0, 0, hw, th);
    ov_text_c(w, gc, fb,
              cur == TAB_APPS ? 0x050505UL : OV_TEXT_MID,
              "APPLICATIONS", hw, 0, hw, th);

    /* centre divider */
    XSetForeground(dpy, gc, 0x252538UL);
    XDrawLine(dpy, w, gc, hw, 0, hw, th - 1);

	//int hw = tw / 2;
	//int r  = OV_CORNER_R;

	///* full background as one rounded rect */
	//ov_fill_round(w, gc, OV_TAB_NORM_S, OV_DIM, 0, 0, tw, th, r);

	//if (cur == TAB_WINDOWS) {
	//	/* left half active — fill left rounded pill */
	//	ov_fill_round(w, gc, OV_TAB_ACTIVE, OV_TAB_NORM_S, 0, 0, tw, th, r);
	//	/* restore right half dark — square left edge of right half */
	//	XSetForeground(dpy, gc, OV_TAB_NORM_S);
	//	XFillRectangle(dpy, w, gc, hw, 0, tw - hw, th);
	//	/* re-round outer right corners */
	//	XSetForeground(dpy, gc, OV_TAB_NORM_S);
	//	XFillArc(dpy, w, gc, tw - 2*r - 1, 0,       2*r+1, 2*r,   0,      90*64);
	//	XFillArc(dpy, w, gc, tw - 2*r - 1, th-2*r,  2*r+1, 2*r,   270*64, 90*64);
	//	XSetForeground(dpy, gc, OV_DIM);
	//	XFillArc(dpy, w, gc, tw - 2*r - 1, 0,       2*r+1, 2*r,   0,      90*64);
	//	XFillArc(dpy, w, gc, tw - 2*r - 1, th-2*r,  2*r+1, 2*r,   270*64, 90*64);
	//	/* re-draw right outer rounded corners with norm color */
	//	XSetForeground(dpy, gc, OV_TAB_NORM_S);
	//	XFillArc(dpy, w, gc, tw-2*r, 0,      2*r, 2*r, 0,      90*64);
	//	XFillArc(dpy, w, gc, tw-2*r, th-2*r, 2*r, 2*r, 270*64, 90*64);
	//} else {
	//	/* right half active */
	//	ov_fill_round(w, gc, OV_TAB_ACTIVE, OV_TAB_NORM_S, hw, 0, tw-hw, th, r);
	//	/* square left edge of active area */
	//	XSetForeground(dpy, gc, OV_TAB_ACTIVE);
	//	XFillRectangle(dpy, w, gc, hw, 0, r, th);
	//	/* restore left half dark — square right edge of left half */
	//	XSetForeground(dpy, gc, OV_TAB_NORM_S);
	//	XFillRectangle(dpy, w, gc, 0, 0, hw, th);
	//	/* re-round outer left corners */
	//	XSetForeground(dpy, gc, OV_TAB_NORM_S);
	//	XFillArc(dpy, w, gc, 0, 0,      2*r, 2*r, 90*64,  90*64);
	//	XFillArc(dpy, w, gc, 0, th-2*r, 2*r, 2*r, 180*64, 90*64);
	//}

	//ov_text_c(w, gc, fb,
	//	  cur == TAB_WINDOWS ? 0x050505UL : OV_TEXT_MID,
	//	  "WINDOWS", 0, 0, hw, th);
	//ov_text_c(w, gc, fb,
	//	  cur == TAB_APPS ? 0x050505UL : OV_TEXT_MID,
	//	  "APPLICATIONS", hw, 0, hw, th);

	///* single outer border — dark, strictly inside window */
	//unsigned long bdr = 0x252538UL;
	//ov_rect_round(w, gc, bdr, 0, 0, tw, th, r);
	///* centre divider */
	//XSetForeground(dpy, gc, bdr);
	//XDrawLine(dpy, w, gc, hw, r, hw, th - r - 1);
}

/* ── pager ──────────────────────────────────────────────────────────────── */
static void
ov_draw_pager(Window w, GC gc, XFontStruct *f,
	      int page, int total_pages, int ww, int ph)
{
	    ov_fill_rect(w, gc, OV_PG_BG_S, 0, 0, ww, ph);

    XSetForeground(dpy, gc, OV_SEP);
    XDrawLine(dpy, w, gc, 0, 0, ww - 1, 0);

    int can_prev = (page > 0);
    int can_next = (page < total_pages - 1);

    int bw2  = 120;
    int bh2  = ph - 12;
    int by2  = (ph - bh2) / 2;
    int gap  = 20;
    int ind_w = 100;
    int total_row = bw2 + gap + ind_w + gap + bw2;
    int sx   = (ww - total_row) / 2;

    /* PREV */
    {
        int px = sx;
        unsigned long fill = can_prev ? 0x1e2a3aUL : 0x12151aUL;
        unsigned long bdr  = can_prev ? OV_BORDER_NORM : OV_TEXT_LO;
        unsigned long tcol = can_prev ? OV_TEXT_HI     : OV_TEXT_LO;
        ov_fill_rect(w, gc, fill, px, by2, bw2 - 1, bh2 - 1);
        ov_rect_plain(w, gc, bdr, px, by2, bw2 - 1, bh2 - 1);
        ov_text_c(w, gc, f, tcol, "<- PREV", px, by2, bw2 - 1, bh2 - 1);
    }

    /* indicator */
    {
        char ind[32];
        snprintf(ind, sizeof(ind), "%d / %d", page + 1, MAX(1, total_pages));
        int ix = sx + bw2 + gap;
        ov_text_c(w, gc, f, OV_TEXT_MID, ind, ix, by2, ind_w, bh2 - 1);
    }

    /* NEXT */
    {
        int px = sx + bw2 + gap + ind_w + gap;
        unsigned long fill = can_next ? 0x1e2a3aUL : 0x12151aUL;
        unsigned long bdr  = can_next ? OV_BORDER_NORM : OV_TEXT_LO;
        unsigned long tcol = can_next ? OV_TEXT_HI     : OV_TEXT_LO;
        ov_fill_rect(w, gc, fill, px, by2, bw2 - 1, bh2 - 1);
        ov_rect_plain(w, gc, bdr, px, by2, bw2 - 1, bh2 - 1);
        ov_text_c(w, gc, f, tcol, "NEXT ->", px, by2, bw2 - 1, bh2 - 1);
    }

	///* pager strip background */
	//ov_fill(w, gc, OV_PG_BG_S, 0, 0, ww, ph);
	///* subtle top separator line */
	//XSetForeground(dpy, gc, OV_SEP);
	//XDrawLine(dpy, w, gc, 0, 0, ww - 1, 0);

	//int can_prev = (page > 0);
	//int can_next = (page < total_pages - 1);

	///* Button geometry */
	//int bw   = 120;   /* button width  */
	//int bh2  = ph - 12; /* button height (pad 6 top+bottom) */
	//int by   = (ph - bh2) / 2;
	//int r    = OV_CORNER_R;

	///* Indicator geometry — same gap on both sides */
	//int gap   = 20;
	//int ind_w = 100;
	//int total_row = bw + gap + ind_w + gap + bw;
	//int sx    = (ww - total_row) / 2;

	///* ── PREV button ──────────────────────────────────────── */
	//{
	//	int px = sx;
	//	/* fill: bright when active, very dim when disabled */
	//	unsigned long fill = can_prev ? 0x1e2a3aUL : 0x12151aUL;
	//	unsigned long bdr  = can_prev ? OV_BORDER_NORM : OV_TEXT_LO;
	//	unsigned long tcol = can_prev ? OV_TEXT_HI     : OV_TEXT_LO;

	//	/* solid rounded fill */
	//	ov_fill_round(w, gc, fill, OV_PG_BG_S, px, by, bw - 1, bh2 - 1, r);
	//	/* thin border, inset by 1 so it is completely inside the fill */
	//	ov_rect_round(w, gc, bdr, px, by, bw - 1, bh2 - 1, r);

	//	/* left-pointing arrow + label, centred */
	//	ov_text_c(w, gc, f, tcol, "<- PREV", px, by, bw - 1, bh2 - 1);
	//}

	///* ── page indicator ───────────────────────────────────── */
	//{
	//	char ind[32];
	//	snprintf(ind, sizeof(ind), "%d / %d", page + 1, MAX(1, total_pages));
	//	int ix = sx + bw + gap;
	//	ov_text_c(w, gc, f, OV_TEXT_MID, ind, ix, by, ind_w, bh2 - 1);
	//}

	///* ── NEXT button ──────────────────────────────────────── */
	//{
	//	int px = sx + bw + gap + ind_w + gap;
	//	unsigned long fill = can_next ? 0x1e2a3aUL : 0x12151aUL;
	//	unsigned long bdr  = can_next ? OV_BORDER_NORM : OV_TEXT_LO;
	//	unsigned long tcol = can_next ? OV_TEXT_HI     : OV_TEXT_LO;

	//	ov_fill_round(w, gc, fill, OV_PG_BG_S, px, by, bw - 1, bh2 - 1, r);
	//	ov_rect_round(w, gc, bdr, px, by, bw - 1, bh2 - 1, r);

	//	ov_text_c(w, gc, f, tcol, "NEXT ->", px, by, bw - 1, bh2 - 1);
	//}
}



/* ==========================================================================
 * SECTION 5 — icon & desktop helpers
 * ========================================================================== */

	static void
ov_blit_pic(Drawable dst, Picture src,
	    int dx, int dy, unsigned int w, unsigned int h)
{
	if (src == None) return;
	XRenderPictFormat *vf =
		XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen));
	Picture dp = XRenderCreatePicture(dpy, dst, vf, 0, NULL);
	XRenderComposite(dpy, PictOpOver, src, None, dp,
			 0, 0, 0, 0, dx, dy, w, h);
	XRenderFreePicture(dpy, dp);
}

static Picture
ov_load_icon(const char *path, unsigned int sz)
{
	if (!path || !*path) return None;
	Imlib_Image img = imlib_load_image(path);
	if (!img) return None;
	imlib_context_set_image(img);

	XRenderPictFormat *fmt32 = XRenderFindStandardFormat(dpy, PictStandardARGB32);

	Pixmap out_pm  = XCreatePixmap(dpy, root, sz, sz, 32);
	Picture out_pic = XRenderCreatePicture(dpy, out_pm, fmt32, 0, NULL);
	XRenderColor zero = {0, 0, 0, 0};
	XRenderFillRectangle(dpy, PictOpSrc, out_pic, &zero, 0, 0, sz, sz);

	/* probe for 32-bit TrueColor visual */
	Visual *vis32 = NULL;
	XVisualInfo tpl, *lst; int n = 0;
	tpl.depth = 32; tpl.class = TrueColor; tpl.screen = screen;
	lst = XGetVisualInfo(dpy, VisualDepthMask|VisualClassMask|VisualScreenMask, &tpl, &n);
	if (lst && n > 0) vis32 = lst[0].visual;
	if (lst) XFree(lst);

	if (vis32) {
		Pixmap px32 = XCreatePixmap(dpy, root, sz, sz, 32);
		GC gc32 = XCreateGC(dpy, px32, 0, NULL);
		XSetForeground(dpy, gc32, 0x00000000);
		XFillRectangle(dpy, px32, gc32, 0, 0, sz, sz);
		XFreeGC(dpy, gc32);

		imlib_context_set_display(dpy);
		imlib_context_set_visual(vis32);
		imlib_context_set_colormap(DefaultColormap(dpy, screen));
		imlib_context_set_drawable(px32);
		imlib_context_set_blend(1);
		imlib_render_image_on_drawable_at_size(0, 0, (int)sz, (int)sz);
		imlib_free_image_and_decache();

		Picture src = XRenderCreatePicture(dpy, px32, fmt32, 0, NULL);
		XRenderComposite(dpy, PictOpOver, src, None, out_pic, 0,0,0,0,0,0,sz,sz);
		XRenderFreePicture(dpy, src);
		XFreePixmap(dpy, px32);
	} else {
		Pixmap vis_pm = XCreatePixmap(dpy, root, sz, sz, DefaultDepth(dpy,screen));
		XSetForeground(dpy, DefaultGC(dpy, screen), OV_CARD_SOLID);
		XFillRectangle(dpy, vis_pm, DefaultGC(dpy, screen), 0, 0, sz, sz);
		imlib_context_set_display(dpy);
		imlib_context_set_visual(DefaultVisual(dpy, screen));
		imlib_context_set_colormap(DefaultColormap(dpy, screen));
		imlib_context_set_drawable(vis_pm);
		imlib_context_set_blend(1);
		imlib_render_image_on_drawable_at_size(0, 0, (int)sz, (int)sz);
		imlib_free_image_and_decache();

		XRenderPictFormat *vf = XRenderFindVisualFormat(dpy, DefaultVisual(dpy,screen));
		Picture vp = XRenderCreatePicture(dpy, vis_pm, vf, 0, NULL);
		XRenderComposite(dpy, PictOpOver, vp, None, out_pic, 0,0,0,0,0,0,sz,sz);
		XRenderFreePicture(dpy, vp);
		XFreePixmap(dpy, vis_pm);
	}

	XFreePixmap(dpy, out_pm);
	return out_pic;
}



	static void
ov_find_icon(const char *name, char *out, size_t outsz)
{
	out[0] = '\0';
	if (!name || !*name) return;
	if (name[0] == '/') {
		if (access(name, R_OK) == 0) {
			strncpy(out, name, outsz-1); out[outsz-1] = '\0';
		}
		return;
	}
	char base[256];
	snprintf(base, sizeof(base), "%s", name);
	char *dot = strrchr(base, '.');
	if (dot && (!strcmp(dot,".png")||!strcmp(dot,".svg")
		    ||!strcmp(dot,".xpm")||!strcmp(dot,".jpg")))
		*dot = '\0';

	const char *exts[] = {".png", ".svg", ".xpm", NULL};
	char path[2048];

	/* mrrobotos — largest first */
	const char *mr[] = {"512x512","256x256","128x128","96x96",
		"64x64","48x48","scalable",NULL};
	for (int si = 0; mr[si]; si++)
		for (int ei = 0; exts[ei]; ei++) {
			snprintf(path, sizeof(path),
				 "/usr/share/icons/mrrobotos/%s/apps/%s%s",
				 mr[si], base, exts[ei]);
			if (access(path, R_OK) == 0) {
				strncpy(out, path, outsz-1); out[outsz-1] = '\0'; return;
			}
		}
	/* hicolor — largest first */
	const char *hi[] = {"512x512","256x256","128x128","96x96",
		"64x64","48x48","scalable",NULL};
	for (int si = 0; hi[si]; si++)
		for (int ei = 0; exts[ei]; ei++) {
			snprintf(path, sizeof(path),
				 "/usr/share/icons/hicolor/%s/apps/%s%s",
				 hi[si], base, exts[ei]);
			if (access(path, R_OK) == 0) {
				strncpy(out, path, outsz-1); out[outsz-1] = '\0'; return;
			}
		}
	/* pixmaps */
	for (int ei = 0; exts[ei]; ei++) {
		snprintf(path, sizeof(path), "/usr/share/pixmaps/%s%s", base, exts[ei]);
		if (access(path, R_OK) == 0) {
			strncpy(out, path, outsz-1); out[outsz-1] = '\0'; return;
		}
	}
	/* Papirus */
	const char *pp[] = {"64x64","48x48","32x32",NULL};
	for (int si = 0; pp[si]; si++)
		for (int ei = 0; exts[ei]; ei++) {
			snprintf(path, sizeof(path),
				 "/usr/share/icons/Papirus/%s/apps/%s%s",
				 pp[si], base, exts[ei]);
			if (access(path, R_OK) == 0) {
				strncpy(out, path, outsz-1); out[outsz-1] = '\0'; return;
			}
		}
}

	static void
ov_parse_one_dir(const char *dir, AppEntry **apps, int *count, int *cap)
{
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *ent;
	char path[1024];
	while ((ent = readdir(d))) {
		size_t nl = strlen(ent->d_name);
		if (nl < 9 || strcmp(ent->d_name+nl-8, ".desktop") != 0) continue;
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		FILE *fp = fopen(path, "r");
		if (!fp) continue;
		char line[512], name[256]="", exec_v[512]="", icon[256]="";
		int in_entry = 0, skip = 0;
		while (fgets(line, sizeof(line), fp)) {
		    line[strcspn(line, "\r\n")] = '\0';
		    if (line[0] == '[') {
		        in_entry = (strncmp(line, "[Desktop Entry]", 15) == 0);
		        continue;
		    }
		    if (!in_entry) continue;
		    if (!name[0]   && strncmp(line, "Name=",  5) == 0) {
			    strncpy(name, line+5, sizeof(name)-1);
			    name[sizeof(name)-1]='\0';
		    }
		    if (!exec_v[0] && strncmp(line, "Exec=",  5) == 0)
		        snprintf(exec_v, sizeof(exec_v), "%s", line+5);
		    if (!icon[0]   && strncmp(line, "Icon=",  5) == 0) {
			    strncpy(icon, line+5, sizeof(icon)-1);
			    icon[sizeof(icon)-1]='\0';
		    }
		    if (strncmp(line, "NoDisplay=true", 14) == 0) skip = 1;
		    if (strncmp(line, "Hidden=true",    11) == 0) skip = 1;
		    if (strncmp(line, "Type=", 5) == 0
		        && strncmp(line+5, "Application", 11) != 0) skip = 1;
		}

		fclose(fp);
		if (skip || !name[0] || !exec_v[0]) continue;
		int dup = 0;
		for (int i = 0; i < *count; i++)
			if (strcmp((*apps)[i].name, name) == 0) { dup = 1; break; }
		if (dup) continue;
		if (*count >= *cap) {
			*cap = *cap ? *cap*2 : 128;
			*apps = realloc(*apps, (size_t)*cap * sizeof(AppEntry));
		}
		AppEntry *a = &(*apps)[(*count)++];
		memset(a, 0, sizeof(*a));
		snprintf(a->name,      sizeof(a->name),      "%s", name);
		snprintf(a->exec,      sizeof(a->exec),       "%s", exec_v);
		a->icon_pic = None;
		char ipath[512];
		ov_find_icon(icon, ipath, sizeof(ipath));
		snprintf(a->icon_path, sizeof(a->icon_path), "%s", ipath);
	}
	closedir(d);
}


	static int
ov_app_cmp(const void *a, const void *b)
{
	const AppEntry *x = (const AppEntry*)a;
	const AppEntry *y = (const AppEntry*)b;
	int xn = (x->name[0] >= '0' && x->name[0] <= '9');
	int yn = (y->name[0] >= '0' && y->name[0] <= '9');
	if (xn != yn) return yn - xn;
	return strcasecmp(x->name, y->name);
}

	static int
ov_parse_desktop_files(AppEntry **out, int *count)
{
	*out = NULL; *count = 0; int cap = 0;
	ov_parse_one_dir("/usr/share/applications", out, count, &cap);
	qsort(*out, (size_t)*count, sizeof(AppEntry), ov_app_cmp);
	return *count;
}

	static void
ov_free_apps(AppEntry *apps, int count)
{
	for (int i = 0; i < count; i++)
		if (apps[i].icon_pic != None)
			XRenderFreePicture(dpy, apps[i].icon_pic);
	free(apps);
}

	static void
ov_launch(const char *exec_raw)
{
	char clean[1024]; int oi = 0;
	for (const char *p = exec_raw; *p && oi < (int)sizeof(clean)-1; p++) {
		if (*p == '%' && *(p+1)) { p++; continue; }
		clean[oi++] = *p;
	}
	clean[oi] = '\0';
	while (oi > 0 && clean[oi-1] == ' ') clean[--oi] = '\0';
	if (fork() == 0) {
		setsid();
		execl("/bin/sh", "sh", "-c", clean, NULL);
		_exit(127);
	}
}

	static int
ov_count_running(AppEntry *a)
{
	char ln[256];
	snprintf(ln, sizeof(ln), "%s", a->name);
	for (int i = 0; ln[i]; i++)
		ln[i] = (char)tolower((unsigned char)ln[i]);
	int cnt = 0;
	Monitor *mn; Client *c;
	for (mn = mons; mn; mn = mn->next)
		for (c = mn->clients; c; c = c->next) {
			if (ispanel(c)) continue;
			char cn[256];
			snprintf(cn, sizeof(cn), "%s", c->name);
			for (int i = 0; cn[i]; i++)
				cn[i] = (char)tolower((unsigned char)cn[i]);
			if (strstr(cn, ln) || strstr(ln, cn)) cnt++;
		}
	return cnt;
}

	static Window
ov_make_card_win(Window parent, int cx, int cy, int cw, int ch,
		 unsigned long bg)
{
	XSetWindowAttributes wa2;
	wa2.override_redirect = True;
	wa2.background_pixel  = bg;
	wa2.event_mask = ButtonPressMask | EnterWindowMask |
		LeaveWindowMask | ExposureMask;
	return XCreateWindow(dpy, parent, cx, cy,
			     (unsigned)cw, (unsigned)ch, 0,
			     DefaultDepth(dpy, screen), InputOutput,
			     DefaultVisual(dpy, screen),
			     CWOverrideRedirect|CWBackPixel|CWEventMask, &wa2);
}

/* ==========================================================================
 * SECTION 6 — card renderers
 * ========================================================================== */
static void
ov_draw_win_card(Window cw, GC gc, XFontStruct *f,
                 Client *c, int hovered, int selected,
                 unsigned int cardw, unsigned int cardh)
{
    int img_h = (int)cardh - OV_TITLEBAR_H;
    if (img_h < 1) img_h = 1;

    unsigned long bg = hovered ? OV_CARD_HOV_S : OV_CARD_SOLID;

    /* FIX 2: plain rectangle, no rounding */
    ov_fill_rect(cw, gc, bg, 0, 0, (int)cardw, (int)cardh);

    if (c && c->pre.scaled_image)
        XPutImage(dpy, cw, gc, c->pre.scaled_image,
                  0, 0, 0, 0,
                  MIN((unsigned)c->pre.scaled_image->width,  cardw),
                  MIN((unsigned)c->pre.scaled_image->height, (unsigned)img_h));

    /* titlebar background */
    ov_fill_rect(cw, gc, hovered ? OV_CARD_HOV_S : OV_TITLEBAR_S,
                 0, img_h, (int)cardw, OV_TITLEBAR_H);

    if (c) {
        /* FIX 3: icon as large as possible with equal top/bottom padding */
        #define TB_VPAD  6            /* equal top & bottom padding in px */
        int ico_sz = OV_TITLEBAR_H - 2 * TB_VPAD;
        if (ico_sz < 4) ico_sz = 4;
        int gap = 8;

        unsigned long txt_col = hovered ? OV_TEXT_HI : OV_TEXT_MID;
        int has_icon = (c->icon != None);
        int avail    = (int)cardw - TB_VPAD * 2;  /* reuse same small pad for sides */

        /* Vertical centre of icon inside titlebar */
        int ico_y = img_h + TB_VPAD;

        /* Text baseline — align with vertical centre of icon */
        int ico_cy = ico_y + ico_sz / 2;  /* icon centre y */
        int ty     = ico_cy + (f ? (f->ascent - f->descent) / 2 : 6);
        /* clamp */
        if (ty < img_h + (f ? f->ascent : 14)) ty = img_h + (f ? f->ascent : 14);
        if (ty > img_h + OV_TITLEBAR_H - 2)    ty = img_h + OV_TITLEBAR_H - 2;

        if (has_icon) {
            int text_max = avail - ico_sz - gap;
            if (text_max < 20) text_max = 20;

            char tbuf[256];
            snprintf(tbuf, sizeof(tbuf), "%s", c->name);
            if (f) {
                while ((int)strlen(tbuf) > 3 &&
                       XTextWidth(f, tbuf, (int)strlen(tbuf)) > text_max)
                    tbuf[strlen(tbuf)-1] = '\0';
                if (strcmp(tbuf, c->name) != 0)
                    strncat(tbuf, "...", sizeof(tbuf)-strlen(tbuf)-1);
            }
            int text_w = f ? XTextWidth(f, tbuf, (int)strlen(tbuf))
                           : (int)strlen(tbuf) * 10;

            int block_w = ico_sz + gap + text_w;
            if (block_w > avail) block_w = avail;
            int bx = ((int)cardw - block_w) / 2;

            ov_blit_pic(cw, c->icon, bx, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);

            int tx2 = bx + ico_sz + gap;
            if (f) XSetFont(dpy, gc, f->fid);
            XSetForeground(dpy, gc, txt_col);
            XDrawString(dpy, cw, gc, tx2, ty, tbuf, (int)strlen(tbuf));
        } else {
            char tbuf[256];
            snprintf(tbuf, sizeof(tbuf), "%s", c->name);
            if (f) {
                while ((int)strlen(tbuf) > 3 &&
                       XTextWidth(f, tbuf, (int)strlen(tbuf)) > avail)
                    tbuf[strlen(tbuf)-1] = '\0';
                if (strcmp(tbuf, c->name) != 0)
                    strncat(tbuf, "...", sizeof(tbuf)-strlen(tbuf)-1);
            }
            ov_text_c(cw, gc, f, txt_col, tbuf,
                      TB_VPAD, img_h, avail, OV_TITLEBAR_H);
        }
        #undef TB_VPAD
    }

    /* FIX 2: plain rectangle border */
    unsigned long bdr = (selected || hovered) ? OV_WIN_SEL_BDR : OV_BORDER_NORM;
    ov_rect_plain(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1);
}

//static void
//ov_draw_win_card(Window cw, GC gc, XFontStruct *f,
//		 Client *c, int hovered, int selected,
//		 unsigned int cardw, unsigned int cardh)
//{
//	int img_h = (int)cardh - OV_TITLEBAR_H;
//	if (img_h < 1) img_h = 1;
//
//	unsigned long bg = hovered ? OV_CARD_HOV_S : OV_CARD_SOLID;
//	ov_fill_round(cw, gc, bg, OV_DIM, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
//
//	if (c && c->pre.scaled_image)
//		XPutImage(dpy, cw, gc, c->pre.scaled_image,
//			  0, 0, 0, 0,
//			  MIN((unsigned)c->pre.scaled_image->width,  cardw),
//			  MIN((unsigned)c->pre.scaled_image->height, (unsigned)img_h));
//
//	/* titlebar background */
//	ov_fill(cw, gc, hovered ? OV_CARD_HOV_S : OV_TITLEBAR_S,
//		0, img_h, (int)cardw, OV_TITLEBAR_H);
//
//	if (c) {
//		#define TB_PAD 10          /* equal left and right padding */
//		int ico_sz  = OV_TITLEBAR_H - 14;  /* slightly larger icon */
//		int gap     = 8;
//		unsigned long txt_col = hovered ? OV_TEXT_HI : OV_TEXT_MID;
//		int has_icon = (c->icon != None);
//		int avail    = (int)cardw - TB_PAD * 2;
//
//		if (has_icon) {
//			/* Measure truncated text that fits in the available slot */
//			int text_max = avail - ico_sz - gap;
//			if (text_max < 20) text_max = 20;
//
//			/* How wide is the text after truncation? */
//			char tbuf[256];
//			snprintf(tbuf, sizeof(tbuf), "%s", c->name);
//			if (f) {
//				while ((int)strlen(tbuf) > 3 &&
//				       XTextWidth(f, tbuf, (int)strlen(tbuf)) > text_max)
//					tbuf[strlen(tbuf) - 1] = '\0';
//				if (strcmp(tbuf, c->name) != 0)
//					strncat(tbuf, "...", sizeof(tbuf) - strlen(tbuf) - 1);
//			}
//			int text_w = f ? XTextWidth(f, tbuf, (int)strlen(tbuf))
//				       : (int)strlen(tbuf) * 10;
//
//			/* Centre the icon+gap+text block */
//			int block_w = ico_sz + gap + text_w;
//			if (block_w > avail) block_w = avail;
//			int bx = ((int)cardw - block_w) / 2;
//
//			/* vertical centre in titlebar */
//			int ico_y = img_h + (OV_TITLEBAR_H - ico_sz) / 2;
//			ov_blit_pic(cw, c->icon, bx, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);
//
//			/* draw text — vertically centred */
//			int tx = bx + ico_sz + gap;
//			int ty = img_h + (OV_TITLEBAR_H + (f ? f->ascent : 14) - (f ? f->descent : 3)) / 2;
//			if (f) XSetFont(dpy, gc, f->fid);
//			XSetForeground(dpy, gc, txt_col);
//			XDrawString(dpy, cw, gc, tx, ty, tbuf, (int)strlen(tbuf));
//		} else {
//			/* No icon — just centre text */
//			char tbuf[256];
//			snprintf(tbuf, sizeof(tbuf), "%s", c->name);
//			if (f) {
//				while ((int)strlen(tbuf) > 3 &&
//				       XTextWidth(f, tbuf, (int)strlen(tbuf)) > avail)
//					tbuf[strlen(tbuf) - 1] = '\0';
//				if (strcmp(tbuf, c->name) != 0)
//					strncat(tbuf, "...", sizeof(tbuf) - strlen(tbuf) - 1);
//			}
//			ov_text_c(cw, gc, f, txt_col, tbuf,
//				  TB_PAD, img_h, avail, OV_TITLEBAR_H);
//		}
//		#undef TB_PAD
//	}
//
//	unsigned long bdr = (selected || hovered) ? OV_WIN_SEL_BDR : OV_BORDER_NORM;
//	ov_rect_round(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
//}
//

static void
ov_draw_app_card(Window cw, GC gc, XFontStruct *f,
                 AppEntry *a, int hovered, int selected,
                 unsigned int cardw, unsigned int cardh)
{
	unsigned long bg = hovered ? OV_CARD_HOV_S : OV_CARD_SOLID;
	ov_fill(cw, gc, bg, 0, 0, (int)cardw, (int)cardh);
	//ov_fill_round(cw, gc, bg, OV_DIM, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);

	int txt_h      = f ? (f->ascent + f->descent + 10) : 26;
	int pill_h     = OV_RUN_PILL_H + 8;  /* always reserve */
	int bottom_pad = 10;
	int bottom_reserve = txt_h + pill_h + bottom_pad;

	/* icon: same approach as original ov_draw_run_card — size from cardh, cap at 44 */
	int pad    = 14;
	int ico_sz = (int)cardh - pad * 2;
	if (ico_sz > 56) ico_sz = 56;
	if (ico_sz < 4)  ico_sz = 4;

	/* centre horizontally; vertically centre in the space above the text reserve */
	int icon_zone_h = (int)cardh - bottom_reserve;
	int ico_x = ((int)cardw - ico_sz) / 2;
	int ico_y = (icon_zone_h - ico_sz) / 2;
	if (ico_y < 0) ico_y = 0;

	if (a->icon_pic != None) {
		ov_blit_pic(cw, a->icon_pic, ico_x, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);
	} else {
		Picture fb = ov_load_icon(
			"/usr/share/icons/Adwaita/scalable/mimetypes/application-x-addon.svg",
			(unsigned)ico_sz);
		if (fb != None) {
			ov_blit_pic(cw, fb, ico_x, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);
			XRenderFreePicture(dpy, fb);
		} else {
			ov_fill(cw, gc, OV_SEP, ico_x, ico_y, ico_sz, ico_sz);
			ov_text_c(cw, gc, f, OV_TEXT_LO, "?", ico_x, ico_y, ico_sz, ico_sz);
		}
	}

	int text_y = (int)cardh - bottom_reserve;
	ov_text_cc(cw, gc, f,
		   hovered ? OV_TEXT_HI : OV_TEXT_MID,
		   a->name, 4, text_y, (int)cardw - 8, txt_h);

	if (a->running) {
		int py = (int)cardh - OV_RUN_PILL_H - (bottom_pad / 2 + 2);
		int px = ((int)cardw - OV_RUN_PILL_W) / 2;
		ov_fill_pill(cw, gc, OV_ACCENT, px, py, OV_RUN_PILL_W, OV_RUN_PILL_H);
	}

	unsigned long bdr = (selected || hovered) ? OV_WIN_SEL_BDR : OV_BORDER_NORM;
	//ov_rect_round(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
	ov_rect_plain(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1);
}




//static void
//ov_draw_app_card(Window cw, GC gc, XFontStruct *f,
//		 AppEntry *a, int hovered, int selected,
//		 unsigned int cardw, unsigned int cardh)
//{
//	unsigned long bg = hovered ? OV_CARD_HOV_S : OV_CARD_SOLID;
//	/* border inset by 1 */
//	ov_fill_round(cw, gc, bg, OV_DIM, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
//
//	int txt_h          = f ? (f->ascent + f->descent + 10) : 26;
//	int pill_h         = a->running ? OV_RUN_PILL_H + 8 : 0;
//	int bottom_pad     = 10;
//	int bottom_reserve = txt_h + pill_h + bottom_pad;
//	int top_pad        = bottom_reserve;  /* equal top & bottom */
//	int avail_h        = (int)cardh - top_pad - bottom_reserve;
//
//	if (avail_h < 16) {
//		top_pad        = 8;
//		bottom_reserve = txt_h + pill_h + 8;
//		avail_h        = (int)cardh - top_pad - bottom_reserve;
//	}
//	if (avail_h < 16) avail_h = 16;
//
//	int ico_sz = (int)OV_ICON_SZ;
//	if (ico_sz > avail_h) ico_sz = avail_h;
//	if (ico_sz < 16)      ico_sz = 16;
//	int ico_y = top_pad + (avail_h - ico_sz) / 2;
//	int ico_x = ((int)cardw - ico_sz) / 2;
//
//	if (a->icon_pic != None) {
//		ov_blit_pic(cw, a->icon_pic, ico_x, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);
//	} else {
//		ov_fill(cw, gc, OV_SEP, ico_x, ico_y, ico_sz, ico_sz);
//		ov_text_c(cw, gc, f, OV_TEXT_LO, "?", ico_x, ico_y, ico_sz, ico_sz);
//	}
//
//	int text_y = (int)cardh - bottom_reserve;
//	ov_text_cc(cw, gc, f,
//		   hovered ? OV_TEXT_HI : OV_TEXT_MID,
//		   a->name, 4, text_y, (int)cardw - 8, txt_h);
//
//	if (a->running) {
//		int py = (int)cardh - OV_RUN_PILL_H - (bottom_pad / 2 + 2);
//		int px = ((int)cardw - OV_RUN_PILL_W) / 2;
//		ov_fill_pill(cw, gc, OV_ACCENT, px, py, OV_RUN_PILL_W, OV_RUN_PILL_H);
//	}
//
//	unsigned long bdr = (selected || hovered) ? OV_WIN_SEL_BDR : OV_BORDER_NORM;
//	ov_rect_round(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
//}

static void
ov_draw_run_card(Window cw, GC gc, XFontStruct *f,
		 const char *line1, const char *line2,
		 Picture icon, int hovered, int selected,
		 int special, unsigned long spec_col,
		 unsigned int cardw, unsigned int cardh)
{
	unsigned long bg = hovered ? OV_CARD_HOV_S : OV_CARD_SOLID;
	//ov_fill_round(cw, gc, bg, OV_DIM, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
	ov_fill(cw, gc, bg, 0, 0, (int)cardw, (int)cardh);

	int has2       = (line2 && *line2);
	int line_h     = f ? (f->ascent + f->descent + 4) : 20;
	int txt_h      = has2 ? (line_h * 2 + 4) : line_h;
	int pill_h     = OV_RUN_PILL_H + 8;
	int bottom_pad = 10;
	int bottom_reserve = txt_h + pill_h + bottom_pad;

	/* same icon sizing as ov_draw_app_card */
	int pad    = 14;
	int ico_sz = (int)cardh - pad * 2;
	if (ico_sz > 56) ico_sz = 56;
	if (ico_sz < 4)  ico_sz = 4;

	int icon_zone_h = (int)cardh - bottom_reserve;
	int ico_x = ((int)cardw - ico_sz) / 2;
	int ico_y = (icon_zone_h - ico_sz) / 2;
	if (ico_y < 0) ico_y = 0;

	if (icon != None) {
		ov_blit_pic(cw, icon, ico_x, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);
	} else if (special) {
		int ax = ico_x + ico_sz / 2, ay = ico_y + ico_sz / 2;
		int aw = ico_sz * 2 / 3;
		XSetForeground(dpy, gc, spec_col ? spec_col : OV_TEXT_MID);
		XDrawLine(dpy, cw, gc, ax-aw/2, ay,   ax+aw/2,      ay);
		XDrawLine(dpy, cw, gc, ax-aw/2, ay,   ax-aw/2+aw/3, ay-aw/3);
		XDrawLine(dpy, cw, gc, ax-aw/2, ay,   ax-aw/2+aw/3, ay+aw/3);
		XDrawLine(dpy, cw, gc, ax-aw/2, ay+1, ax+aw/2,      ay+1);
		XDrawLine(dpy, cw, gc, ax-aw/2, ay+1, ax-aw/2+aw/3, ay-aw/3+1);
		XDrawLine(dpy, cw, gc, ax-aw/2, ay+1, ax-aw/2+aw/3, ay+aw/3+1);
	} else {
		Picture fb = ov_load_icon(
			"/usr/share/icons/Adwaita/scalable/mimetypes/application-x-addon.svg",
			(unsigned)ico_sz);
		if (fb != None) {
			ov_blit_pic(cw, fb, ico_x, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);
			XRenderFreePicture(dpy, fb);
		} else {
			ov_fill(cw, gc, OV_SEP, ico_x, ico_y, ico_sz, ico_sz);
			ov_text_c(cw, gc, f, OV_TEXT_LO, "?", ico_x, ico_y, ico_sz, ico_sz);
		}
	}

	int text_y = (int)cardh - bottom_reserve;
	if (has2) {
		ov_text_cc(cw, gc, f,
			   hovered ? OV_TEXT_HI : OV_TEXT_MID,
			   line1, 4, text_y, (int)cardw - 8, line_h);
		ov_text_cc(cw, gc, f, OV_TEXT_MID,
			   line2, 4, text_y + line_h + 4, (int)cardw - 8, line_h);
	} else {
		ov_text_cc(cw, gc, f,
			   hovered ? OV_TEXT_HI : OV_TEXT_MID,
			   line1, 4, text_y, (int)cardw - 8, txt_h);
	}

	unsigned long bdr = (selected || hovered) ? OV_WIN_SEL_BDR : OV_BORDER_NORM;
	//ov_rect_round(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
	ov_rect_plain(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1);
}



//static void
//ov_draw_run_card(Window cw, GC gc, XFontStruct *f,
//		 const char *line1, const char *line2,
//		 Picture icon, int hovered, int selected,
//		 int special, unsigned long spec_col,
//		 unsigned int cardw, unsigned int cardh)
//{
//	unsigned long bg = hovered ? OV_CARD_HOV_S : OV_CARD_SOLID;
//	ov_fill_round(cw, gc, bg, OV_DIM, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
//
//	int pad    = 14;
//	int ico_sz = (int)cardh - pad * 2;
//	if (ico_sz > 44) ico_sz = 44;
//	if (ico_sz < 16) ico_sz = 16;
//	/* Icon is truly centred vertically in the card */
//	int ico_x = pad;
//	int ico_y = ((int)cardh - ico_sz) / 2;
//
//	if (icon != None) {
//		ov_blit_pic(cw, icon, ico_x, ico_y, (unsigned)ico_sz, (unsigned)ico_sz);
//	} else if (special) {
//		/* Back arrow, centred in the icon zone */
//		int ax = ico_x + ico_sz / 2, ay = (int)cardh / 2;
//		int aw = ico_sz * 2 / 3;
//		XSetForeground(dpy, gc, spec_col ? spec_col : OV_TEXT_MID);
//		XDrawLine(dpy, cw, gc, ax - aw / 2, ay,     ax + aw / 2,          ay);
//		XDrawLine(dpy, cw, gc, ax - aw / 2, ay,     ax - aw / 2 + aw / 3, ay - aw / 3);
//		XDrawLine(dpy, cw, gc, ax - aw / 2, ay,     ax - aw / 2 + aw / 3, ay + aw / 3);
//		/* thicker second pass */
//		XDrawLine(dpy, cw, gc, ax - aw / 2, ay + 1, ax + aw / 2,          ay + 1);
//		XDrawLine(dpy, cw, gc, ax - aw / 2, ay + 1, ax - aw / 2 + aw / 3, ay - aw / 3 + 1);
//		XDrawLine(dpy, cw, gc, ax - aw / 2, ay + 1, ax - aw / 2 + aw / 3, ay + aw / 3 + 1);
//	}
//
//	int tx   = ico_x + ico_sz + pad;
//	int tw   = (int)cardw - tx - pad;
//	int has2 = (line2 && *line2);
//
//	if (has2) {
//		int line_h  = f ? (f->ascent + f->descent + 4) : 20;
//		int block_h = line_h * 2 + 4;
//		int start_y = ((int)cardh - block_h) / 2;
//		if (start_y < 2) start_y = 2;
//
//		/* line 1 */
//		int ty1 = start_y + (f ? f->ascent : 14);
//		if (f) XSetFont(dpy, gc, f->fid);
//		char buf1[256]; snprintf(buf1, sizeof(buf1), "%s", line1);
//		if (f) {
//			while ((int)strlen(buf1) > 3 && XTextWidth(f, buf1, (int)strlen(buf1)) > tw)
//				buf1[strlen(buf1) - 1] = '\0';
//			if (strcmp(buf1, line1) != 0) strncat(buf1, "...", sizeof(buf1)-strlen(buf1)-1);
//		}
//		XSetForeground(dpy, gc, hovered ? OV_TEXT_HI : OV_TEXT_MID);
//		XDrawString(dpy, cw, gc, tx, ty1, buf1, (int)strlen(buf1));
//
//		/* line 2 */
//		int ty2 = start_y + line_h + 4 + (f ? f->ascent : 14);
//		char buf2[256]; snprintf(buf2, sizeof(buf2), "%s", line2);
//		if (f) {
//			while ((int)strlen(buf2) > 3 && XTextWidth(f, buf2, (int)strlen(buf2)) > tw)
//				buf2[strlen(buf2) - 1] = '\0';
//			if (strcmp(buf2, line2) != 0) strncat(buf2, "...", sizeof(buf2)-strlen(buf2)-1);
//		}
//		XSetForeground(dpy, gc, OV_TEXT_MID);
//		XDrawString(dpy, cw, gc, tx, ty2, buf2, (int)strlen(buf2));
//	} else {
//		ov_text_l(cw, gc, f, hovered ? OV_TEXT_HI : OV_TEXT_MID,
//			  line1, tx, 0, tw, (int)cardh);
//	}
//
//	unsigned long bdr = (selected || hovered) ? OV_WIN_SEL_BDR : OV_BORDER_NORM;
//	ov_rect_round(cw, gc, bdr, 0, 0, (int)cardw - 1, (int)cardh - 1, OV_CORNER_R);
//}


/* ==========================================================================
 * SECTION 7 — previewallwin / previewallwinwrap
 * ========================================================================== */

/* Build the running-app picker text lines without snprintf truncation */
	static void
ov_run_launch_text(char *buf, size_t bufsz, const char *appname)
{
	/* "Launch new  " prefix is 12 chars, leave rest for appname */
	size_t pfx = 12;
	size_t room = bufsz - pfx - 1;
	memcpy(buf, "Launch new ", pfx);
	strncpy(buf + pfx, appname, room);
	buf[pfx + room] = '\0';
}

	static void
ov_run_app_text(char *buf, size_t bufsz, const char *appname)
{
	/* "App: " prefix is 5 chars */
	size_t pfx = 5;
	size_t room = bufsz - pfx - 1;
	memcpy(buf, "App: ", pfx);
	strncpy(buf + pfx, appname, room);
	buf[pfx + room] = '\0';
}

	static void
ov_run_title_text(char *buf, size_t bufsz, const char *title)
{
	size_t pfx = 7;
	size_t room = bufsz - pfx - 1;
	memcpy(buf, "Title: ", pfx);
	strncpy(buf + pfx, title, room);
	buf[pfx + room] = '\0';
}


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
		if (ispanel(c))
			return;
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
			if (strcmp(c->name, "Whisker Menu") == 0) {
				c->x = c->mon->wx + sidepad;
				c->y = c->mon->wy + c->mon->wh - HEIGHT(c) - vertpad;
			}
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
		if (c->isfloating && c->noswallow)
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
				if (c->isfloating && c->noswallow)
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
	if (ispanel(c))
		return;
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
				for (j = 0; j < LENGTH(modifiers); j++) {
					/* don't grab Button3 on focused terminals
					   running nvim so nvim gets right-click natively */
					if (focused && c->isterminal
					    && buttons[i].button == Button3
					    && strncmp(c->name, "nvim:", 5) == 0)
						continue;
					if (focused && !c->isterminal
					    && buttons[i].button == Button3)
						continue;
					XGrabButton(dpy, buttons[i].button,
						    buttons[i].mask | modifiers[j],
						    c->win, False, BUTTONMASK,
						    GrabModeAsync, GrabModeSync, None, None);
				}
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
	if (c->name[0] == '\0')
		strcpy(c->name, c->isterminal ? "mrst" : broken);
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
			//c->tags = *data;
			if (c->tags != (unsigned int)TAGMASK)  /* don't override all-tags windows */
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
	XChangeProperty(dpy, c->win, netatom[NetWMWindowType],
		XA_ATOM, 32, PropModeReplace,
		(unsigned char *)&netatom[NetWMWindowTypeNormal], 1);
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

//	void
//run(void)
//{
//	XEvent ev;
//	XSync(dpy, False);
//	while (running && !XNextEvent(dpy, &ev)) {
//		if (run_preview) {
//			run_preview = 0;
//			if (!preview_running) {
//				preview_running = 1;
//				previewallwin(NULL);
//				preview_running = 0;
//			}
//		}
//		if (run_recompile) {
//			run_recompile = 0;
//			Arg a = {.i = 1};
//			quit(&a);
//		}
//		if (handler[ev.type])
//			handler[ev.type](&ev);
//	}
//}

void
run(void)
{
    XEvent ev;
    int xfd = ConnectionNumber(dpy);
    fd_set fds;

    XSync(dpy, False);
    while (running) {
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

        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (handler[ev.type])
                handler[ev.type](&ev);
        }

        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        if (select(xfd + 1, &fds, NULL, NULL, NULL) == -1 && errno == EINTR)
            continue;
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


//	static void
//sigusr1(int unused)
//{
//	sigusr1_count++;
//	if (!preview_running && (sigusr1_count % 2 == 1))
//		run_preview = 1;
//}

static void
sigusr1(int unused)
{
    (void)unused;
    if (!run_preview)
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

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	signal(SIGHUP, sighup);
	signal(SIGTERM, sigterm);
	//signal(SIGUSR1, sigusr1);
	struct sigaction sa_usr1;
sigemptyset(&sa_usr1.sa_mask);
sa_usr1.sa_flags = 0; /* no SA_RESTART */
sa_usr1.sa_handler = sigusr1;
sigaction(SIGUSR1, &sa_usr1, NULL);
	signal(SIGUSR2, sigusr2);

	while (waitpid(-1, NULL, WNOHANG) > 0);

	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	scrfmt = XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen));
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
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
	netatom[NetWMWindowTypeNormal] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetClientInfo] = XInternAtom(dpy, "_NET_CLIENT_INFO", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	netatom[NetDesktopViewport] = XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
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
	setnumdesktops();
	setcurrentdesktop();
	setdesktopnames();
	setviewport();
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

//void
//spawnifdesk(const Arg *arg)
//{
//    int x, y;
//    getrootptr(&x, &y);
//    Client *c;
//    Monitor *m;
//    for (m = mons; m; m = m->next)
//        for (c = m->clients; c; c = c->next)
//            if (ispanel(c) &&
//                x >= c->x && x < c->x + c->w &&
//                y >= c->y && y < c->y + c->h)
//                return;
//    spawn(arg);
//}

	void
spawnifdesk(const Arg *arg)
{
	if (selmon->sel && !ispanel(selmon->sel))
		return;
	spawn(arg);
}

	void
spawnifterm(const Arg *arg)
{
	if (!selmon->sel || !selmon->sel->isterminal)
		return;
	if (strncmp(selmon->sel->name, "nvim:", 5) == 0)
		return;
	if (strncmp(selmon->sel->name, "mrst", 4) == 0)
		spawn(arg);
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
	updatecurrentdesktop();
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
	updatecurrentdesktop();
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
	char prevname[256];
	strncpy(prevname, c->name, sizeof(prevname) - 1);
	prevname[sizeof(prevname) - 1] = '\0';

	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0')
		strcpy(c->name, c->isterminal ? "mrst" : broken);

	if (c->isterminal) {
		int was_nvim = strncmp(prevname, "nvim:", 5) == 0;
		int is_nvim  = strncmp(c->name,  "nvim:", 5) == 0;
		if (was_nvim != is_nvim) {
			freeicon(c);
			const char *iconpath = is_nvim
				? "/usr/share/pixmaps/nvim.svg"
				: "/usr/share/pixmaps/mrst.png";
			Imlib_Image img = imlib_load_image(iconpath);
			if (img) {
				imlib_context_set_image(img);
				int w = imlib_image_get_width();
				int h = imlib_image_get_height();
				c->icon = drw_picture_create_resized(drw,
								     (char *)imlib_image_get_data(), w, h, ICONSIZE, ICONSIZE);
				c->icw = c->ich = ICONSIZE;
				imlib_free_image();
			}
			/* re-grab buttons based on new title state */
			grabbuttons(c, c == selmon->sel);
		}
	}
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
setcurrentdesktop(void)
{
	long data[] = { 0 };
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)data, 1);
}

void
setdesktopnames(void)
{
	XTextProperty text;
	Xutf8TextListToTextProperty(dpy, (char **)tags, TAGSLENGTH, XUTF8StringStyle, &text);
	XSetTextProperty(dpy, root, &text, netatom[NetDesktopNames]);
}

void
setnumdesktops(void)
{
	long data[] = { TAGSLENGTH };
	XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)data, 1);
}

void
setviewport(void)
{
	long data[] = { 0, 0 };
	XChangeProperty(dpy, root, netatom[NetDesktopViewport], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)data, 2);
}


void
updatecurrentdesktop(void)
{
    long rawdata[] = { selmon->tagset[selmon->seltags] };
    int i = 0;
    while (*rawdata >> (i+1)) i++;
    long data[] = { i };
    XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)data, 1);
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
	updatecurrentdesktop();
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
previewallwin(const Arg *arg)
{
	(void)arg;
	Monitor *m   = selmon;
	Client  *c;
	Client  *focus_c = NULL;

	for (c = m->clients; c; c = c->next) {
		if (ispanel(c)) continue;
		c->pre.orig_image = getwindowximage(c);
	}

	AppEntry *apps = NULL; int nApps = 0;
	ov_parse_desktop_files(&apps, &nApps);
	for (int i = 0; i < nApps; i++) {
		apps[i].run_count = ov_count_running(&apps[i]);
		apps[i].running   = (apps[i].run_count > 0);
	}

	/* fonts */
	XFontStruct *font_n = NULL, *font_b = NULL;
		const char *fn[] = {
		"-*-liberation serif-medium-r-*-*-18-*-*-*-*-*-*-*",
		"-*-dejavu serif-medium-r-*-*-18-*-*-*-*-*-*-*",
		"-*-times-medium-r-*-*-18-*-*-*-*-*-*-*",
		"-*-liberation serif-medium-r-*-*-16-*-*-*-*-*-*-*",
		"-*-dejavu serif-medium-r-*-*-16-*-*-*-*-*-*-*",
		"fixed", NULL
	};
	const char *fb[] = {
		"-*-liberation serif-bold-r-*-*-18-*-*-*-*-*-*-*",
		"-*-dejavu serif-bold-r-*-*-18-*-*-*-*-*-*-*",
		"-*-times-bold-r-*-*-18-*-*-*-*-*-*-*",
		"-*-liberation serif-bold-r-*-*-16-*-*-*-*-*-*-*",
		"fixed", NULL
	};

	for (int i = 0; fn[i] && !font_n; i++) font_n = XLoadQueryFont(dpy, fn[i]);
	for (int i = 0; fb[i] && !font_b; i++) font_b = XLoadQueryFont(dpy, fb[i]);
	if (!font_b) font_b = font_n;

	/* geometry */
	int sw = m->ww, sh = m->wh, sx = m->wx, sy = m->wy;
	#define OV_SECTION_GAP 20
	int sb_w   = MIN(OV_SB_MAX_W, sw - 2*OV_MARGIN);
	int sb_x   = (sw - sb_w) / 2;
	int sb_y   = OV_SECTION_GAP;
	int tab_w  = sb_w;
	int tab_x  = sb_x;
	int tab_y  = sb_y + OV_SB_H + OV_SECTION_GAP;
	int pg_y   = sh - OV_PG_H;
	int grid_y = tab_y + OV_TAB_H + OV_SECTION_GAP;
	int grid_h = pg_y - grid_y - OV_SECTION_GAP;
	int grid_w = sw - 2*OV_MARGIN;
	int app_cols         = 5;
	int app_rows         = 5;
	int app_cw           = (grid_w - (app_cols - 1) * OV_GAP) / app_cols;
	int app_grid_margin  = 0; //OV_GAP;
	int app_usable_h     = grid_h - 2 * app_grid_margin;
	int app_ch_dyn       = (app_usable_h - (app_rows - 1) * OV_GAP) / app_rows;
	if (app_ch_dyn < 80) app_ch_dyn = 80;
	int app_ch           = app_ch_dyn;

	/* overlay */
	XSetWindowAttributes wa;
	wa.override_redirect = True;
	wa.background_pixmap = ParentRelative;
	wa.event_mask        = ExposureMask | KeyPressMask | ButtonPressMask;
	Window ov = XCreateWindow(dpy, root, sx, sy, sw, sh, 0,
				  DefaultDepth(dpy, screen), InputOutput,
				  DefaultVisual(dpy, screen),
				  CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
	XMapRaised(dpy, ov);
	GC gc_ov = XCreateGC(dpy, ov, 0, NULL);

	wa.background_pixel = OV_SEARCH_BG;
	wa.event_mask = ExposureMask | ButtonPressMask;
	Window sb_win = XCreateWindow(dpy, ov, sb_x, sb_y, sb_w, OV_SB_H, 0,
				      DefaultDepth(dpy,screen), InputOutput,
				      DefaultVisual(dpy,screen),
				      CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	XMapWindow(dpy, sb_win);
	GC gc_sb = XCreateGC(dpy, sb_win, 0, NULL);

	wa.background_pixel = OV_TAB_NORM_S;
	Window tab_win = XCreateWindow(dpy, ov, tab_x, tab_y, tab_w, OV_TAB_H, 0,
				       DefaultDepth(dpy,screen), InputOutput,
				       DefaultVisual(dpy,screen),
				       CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	XMapWindow(dpy, tab_win);
	GC gc_tab = XCreateGC(dpy, tab_win, 0, NULL);

	wa.background_pixel = OV_PG_BG_S;
	Window pg_win = XCreateWindow(dpy, ov, 0, pg_y, sw, OV_PG_H, 0,
				      DefaultDepth(dpy,screen), InputOutput,
				      DefaultVisual(dpy,screen),
				      CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	XMapWindow(dpy, pg_win);
	GC gc_pg = XCreateGC(dpy, pg_win, 0, NULL);

	/* state */
	OverviewTab  cur_tab  = TAB_WINDOWS;
	OverviewView cur_view = VIEW_MAIN;
	AppEntry    *sel_app  = NULL;
	int page = 0, total_pages = 1;
	char search_buf[128] = ""; int search_len = 0;
	int cursor_vis = 1, done = 0, kbd_idx = -1;

#define OV_MAX_CARDS 1024
	Window  card_wins[OV_MAX_CARDS];
	void   *card_ptrs[OV_MAX_CARDS];
	int     card_type[OV_MAX_CARDS];
	int     n_cards = 0;
	Window  hov_win = None;
	int     win_cw = 0, win_ch = 0;

#define RAISE_UI() do { \
	XRaiseWindow(dpy, sb_win); \
	XRaiseWindow(dpy, tab_win); \
	XRaiseWindow(dpy, pg_win); \
} while(0)

#define DESTROY_CARDS() do { \
	for (int _i = 0; _i < n_cards; _i++) \
		if (card_wins[_i]) { \
			XDestroyWindow(dpy, card_wins[_i]); \
			card_wins[_i] = 0; \
		} \
	n_cards = 0; hov_win = None; kbd_idx = -1; \
} while(0)

#define DIM_BG() do { \
	XSetForeground(dpy, gc_ov, OV_DIM); \
	XFillRectangle(dpy, ov, gc_ov, 0, 0, sw, sh); \
} while(0)

#define _OV_ADD_RUN_WIN(_cc, _oy2, _cw2, _ch2) do { \
	if (n_cards < OV_MAX_CARDS && (_oy2)+(int)(_ch2) <= pg_y-4) { \
		char _at[256], _tt[256]; \
		ov_run_app_text(_at, sizeof(_at), sel_app->name); \
		ov_run_title_text(_tt, sizeof(_tt), (_cc)->name); \
		int _rox2 = OV_MARGIN + (grid_w - (int)(_cw2)) / 2; \
		Window _cwin = ov_make_card_win(ov, _rox2, (_oy2), (_cw2), (_ch2), OV_CARD_SOLID); \
		XMapWindow(dpy, _cwin); \
		GC _cg = XCreateGC(dpy, _cwin, 0, NULL); \
		ov_draw_run_card(_cwin, _cg, font_n, _at, _tt, \
				 sel_app->icon_pic, 0, kbd_idx==n_cards, 0, 0UL, (_cw2), (_ch2)); \
		XFreeGC(dpy, _cg); \
		card_wins[n_cards] = _cwin; card_ptrs[n_cards] = (_cc); \
		card_type[n_cards] = 4; n_cards++; \
	} \
} while(0)

#define REBUILD_CARDS() do { \
	DESTROY_CARDS(); \
	DIM_BG(); \
	if (cur_view == VIEW_APP_RUNNING && sel_app) { \
		int _ch2 = app_ch, _cw2 = app_cw, _oy2 = grid_y; \
		int _run_ox = OV_MARGIN + (grid_w - _cw2) / 2; \
		/* card 0: go back */ \
		{ Window _cw = ov_make_card_win(ov, _run_ox, _oy2, _cw2, _ch2, OV_CARD_SOLID); \
			XMapWindow(dpy, _cw); \
			GC _g = XCreateGC(dpy, _cw, 0, NULL); \
			ov_draw_run_card(_cw, _g, font_n, \
					 "Go back to Applications", "", None, 0, kbd_idx==0, \
					 1, 0x444466UL, _cw2, _ch2); \
			XFreeGC(dpy, _g); \
			card_wins[n_cards]=_cw; card_ptrs[n_cards]=NULL; \
			card_type[n_cards]=2; n_cards++; _oy2 += _ch2 + OV_GAP; } \
		/* card 1: launch new — line1 = "App: name", line2 = "Launch new name" */ \
		{ char _lt1[256], _lt2[256]; \
			ov_run_app_text(_lt1, sizeof(_lt1), sel_app->name); \
			ov_run_launch_text(_lt2, sizeof(_lt2), sel_app->name); \
			Window _cw = ov_make_card_win(ov, _run_ox, _oy2, _cw2, _ch2, OV_CARD_SOLID); \
			XMapWindow(dpy, _cw); \
			GC _g = XCreateGC(dpy, _cw, 0, NULL); \
			ov_draw_run_card(_cw, _g, font_n, _lt1, _lt2, \
					 sel_app->icon_pic, 0, kbd_idx==1, 0, 0UL, _cw2, _ch2); \
			XFreeGC(dpy, _g); \
			card_wins[n_cards]=_cw; card_ptrs[n_cards]=sel_app; \
			card_type[n_cards]=3; n_cards++; _oy2 += _ch2 + OV_GAP; } \
		/* running windows */ \
		for (c = m->clients; c; c = c->next) { \
			if (ispanel(c)) continue; \
			char _ln[256], _cn[256]; \
			snprintf(_ln, sizeof(_ln), "%s", sel_app->name); \
			for (int _i=0;_ln[_i];_i++) _ln[_i]=(char)tolower((unsigned char)_ln[_i]); \
			snprintf(_cn, sizeof(_cn), "%s", c->name); \
			for (int _i=0;_cn[_i];_i++) _cn[_i]=(char)tolower((unsigned char)_cn[_i]); \
			if (!strstr(_cn,_ln) && !strstr(_ln,_cn)) continue; \
			_OV_ADD_RUN_WIN(c, _oy2, _cw2, _ch2); \
			_oy2 += _ch2 + OV_GAP; \
		} \
		total_pages = 1; page = 0; \
	} else if (cur_tab == TAB_WINDOWS) { \
		void *_items[OV_MAX_CARDS]; int _ni = 0; \
		for (c=m->clients;c;c=c->next) { \
			if (ispanel(c)||!c->pre.orig_image) continue; \
			if (search_len>0&&!strcasestr(c->name,search_buf)) continue; \
			if (_ni<OV_MAX_CARDS) _items[_ni++]=c; \
		} \
		total_pages = (_ni+OV_WIN_PER_PG-1)/OV_WIN_PER_PG; \
		if (total_pages<1) total_pages=1; \
		if (page>=total_pages) page=total_pages-1; \
		int _start=page*OV_WIN_PER_PG, _end=MIN(_start+OV_WIN_PER_PG,_ni); \
		int _vis=_end-_start; \
		int _cols, _rows; \
		if      (_vis<=1){_cols=1;_rows=1;} \
		else if (_vis==2){_cols=2;_rows=1;} \
		else if (_vis==3){_cols=3;_rows=1;} \
		else if (_vis==4){_cols=2;_rows=2;} \
		else             {_cols=3;_rows=2;} \
		int _max_h; \
		if      (_rows==1&&_cols==1) _max_h = (int)(grid_h*0.995); \
		else if (_rows==1&&_cols==2) _max_h=(int)(grid_h*0.70); \
		else if (_rows==1&&_cols==3) _max_h=(int)(grid_h*0.55); \
		else                         _max_h=grid_h; \
		int _cw=(grid_w-(_cols-1)*OV_GAP)/_cols; \
		int _ch=(MAX(1,_max_h)-(_rows-1)*OV_GAP)/_rows; \
		int _img_h=_ch-OV_TITLEBAR_H; if(_img_h<60)_img_h=60; \
		int _cw2=(int)(_img_h*16.0/9.0); if(_cw2<_cw)_cw=_cw2; \
		_ch=_img_h+OV_TITLEBAR_H; \
		int _gw=_cols*_cw+(_cols-1)*OV_GAP; \
		int _gh=_rows*_ch+(_rows-1)*OV_GAP; \
		int _ox=OV_MARGIN+(grid_w-_gw)/2; \
		int _oy=grid_y+(grid_h-_gh)/2; if(_oy<grid_y)_oy=grid_y; \
		win_cw=_cw; win_ch=_ch; \
		if (_ni == 0 && n_cards < OV_MAX_CARDS) { \
		    int _pcw = grid_w * 2 / 3, _pch = grid_h * 3 / 4; \
      		    int _ppx = OV_MARGIN + (grid_w - _pcw) / 2; \
      		    int _ppy = grid_y + (grid_h - _pch) / 2; \
      		    Window _pw = ov_make_card_win(ov, _ppx, _ppy, _pcw, _pch, OV_CARD_SOLID); \
      		    XMapWindow(dpy, _pw); \
      		    GC _pg2 = XCreateGC(dpy, _pw, 0, NULL); \
      		    ov_fill_round(_pw, _pg2, OV_CARD_SOLID, OV_DIM, 0, 0, _pcw - 1, _pch - 1, OV_CORNER_R); \
      		    ov_text_c(_pw, _pg2, font_n, OV_TEXT_LO, "No active windows", 0, 0, _pcw, _pch); \
      		    ov_rect_round(_pw, _pg2, OV_BORDER_NORM, 0, 0, _pcw - 1, _pch - 1, OV_CORNER_R); \
      		    XFreeGC(dpy, _pg2); \
      		    card_wins[n_cards] = _pw; \
      		    card_ptrs[n_cards] = NULL; \
      		    card_type[n_cards] = -1; \
      		    n_cards++; \
      		} \
		for (int _i=_start;_i<_end;_i++) { \
			Client *_cc=(Client*)_items[_i]; \
			int _col=(_i-_start)%_cols, _row=(_i-_start)/_cols; \
			int _cx=_ox+_col*(_cw+OV_GAP), _cy=_oy+_row*(_ch+OV_GAP); \
			Window _cwin=ov_make_card_win(ov,_cx,_cy,_cw,_ch,OV_CARD_SOLID); \
			if(_cc->pre.scaled_image){XDestroyImage(_cc->pre.scaled_image);_cc->pre.scaled_image=NULL;} \
			_cc->pre.scaled_image=scaleimagetofit(_cc->pre.orig_image,MAX(1,(unsigned)_cw),MAX(1,(unsigned)_img_h)); \
			XMapWindow(dpy,_cwin); \
			GC _cg=XCreateGC(dpy,_cwin,0,NULL); \
			ov_draw_win_card(_cwin,_cg,font_n,_cc,0,0,_cw,_ch); \
			XFreeGC(dpy,_cg); \
			card_wins[n_cards]=_cwin; card_ptrs[n_cards]=_cc; \
			card_type[n_cards]=0; n_cards++; \
			XUnmapWindow(dpy,_cc->win); \
		} \
	} else { \
		void *_items[OV_MAX_CARDS]; int _ni=0; \
		for (int _ai=0;_ai<nApps;_ai++) { \
			if (search_len>0&&!strcasestr(apps[_ai].name,search_buf)) continue; \
			if (_ni<OV_MAX_CARDS) _items[_ni++]=&apps[_ai]; \
		} \
		int _per_pg = app_cols * app_rows; \
		total_pages=(_ni+_per_pg-1)/_per_pg; \
		if(total_pages<1)total_pages=1; \
		if(page>=total_pages)page=total_pages-1; \
		int _start=page*_per_pg, _end=MIN(_start+_per_pg,_ni); \
		int _vis2=_end-_start; (void)_vis2; \
		int _total_w = app_cols * app_cw + (app_cols - 1) * OV_GAP; \
		int _total_h = app_rows * app_ch + (app_rows - 1) * OV_GAP; \
		int _ox = OV_MARGIN + (grid_w - _total_w) / 2; \
		int _oy = grid_y + app_grid_margin + \
			  (((grid_h - 2 * app_grid_margin) - _total_h) / 2); \
		if (_oy < grid_y + app_grid_margin) _oy = grid_y + app_grid_margin; \
		for (int _i=_start;_i<_end;_i++) { \
			AppEntry *_ae=(AppEntry*)_items[_i]; \
			if(_ae->icon_pic==None&&_ae->icon_path[0]) \
				_ae->icon_pic=ov_load_icon(_ae->icon_path,OV_ICON_SZ); \
			int _col=(_i-_start)%app_cols, _row=(_i-_start)/app_cols; \
			int _cx=_ox+_col*(app_cw+OV_GAP); \
			int _cy=_oy+_row*(app_ch+OV_GAP); \
			if(_cy+app_ch>pg_y-4) continue; \
			Window _cwin=ov_make_card_win(ov,_cx,_cy,app_cw,app_ch,OV_CARD_SOLID); \
			XMapWindow(dpy,_cwin); \
			GC _cg=XCreateGC(dpy,_cwin,0,NULL); \
			ov_draw_app_card(_cwin,_cg,font_n,_ae,0,0,app_cw,app_ch); \
			XFreeGC(dpy,_cg); \
			card_wins[n_cards]=_cwin; card_ptrs[n_cards]=_ae; \
			card_type[n_cards]=1; n_cards++; \
		} \
	} \
	ov_draw_pager(pg_win,gc_pg,font_n,page,total_pages,sw,OV_PG_H); \
	RAISE_UI(); \
} while(0)

#define REDRAW_CARD(_i, _hov, _sel) do { \
	if ((_i)<0||(_i)>=n_cards||!card_wins[(_i)]) break; \
	int _t = card_type[(_i)]; \
	GC _cg = XCreateGC(dpy, card_wins[(_i)], 0, NULL); \
	if (_t < 0) break; \
	if (_t == 0) { \
		ov_draw_win_card(card_wins[(_i)], _cg, font_n, \
				 (Client*)card_ptrs[(_i)], (_hov), (_sel), win_cw, win_ch); \
	} else if (_t == 1) { \
		ov_draw_app_card(card_wins[(_i)], _cg, font_n, \
				 (AppEntry*)card_ptrs[(_i)], (_hov), (_sel), app_cw, app_ch); \
	} else if (_t == 2) { \
		int _rw2 = app_cw,   _rh2 = app_ch; \
		ov_draw_run_card(card_wins[(_i)], _cg, font_n, \
				 "Go back to Applications", "", None, (_hov), (_sel), \
				 1, 0x444466UL, _rw2, _rh2); \
	} else if (_t == 3 && sel_app) { \
		int _rw2 = app_cw,   _rh2 = app_ch; \
		char _lt1[256], _lt2[256]; \
		ov_run_app_text(_lt1, sizeof(_lt1), sel_app->name); \
		ov_run_launch_text(_lt2, sizeof(_lt2), sel_app->name); \
		ov_draw_run_card(card_wins[(_i)], _cg, font_n, _lt1, _lt2, \
				 sel_app->icon_pic, (_hov), (_sel), 0, 0UL, _rw2, _rh2); \
	} else if (_t == 4 && sel_app && card_ptrs[(_i)]) { \
		int _rw2 = app_cw,   _rh2 = app_ch; \
		Client *_cc = (Client*)card_ptrs[(_i)]; \
		char _at[256], _tt[256]; \
		ov_run_app_text(_at, sizeof(_at), sel_app->name); \
		ov_run_title_text(_tt, sizeof(_tt), _cc->name); \
		ov_draw_run_card(card_wins[(_i)], _cg, font_n, _at, _tt, \
				 sel_app->icon_pic, (_hov), (_sel), 0, 0UL, _rw2, _rh2); \
	} \
	XFreeGC(dpy, _cg); \
} while(0)

	REBUILD_CARDS();
	ov_draw_searchbar(sb_win, gc_sb, font_b,
			  search_buf, search_len, cursor_vis, sb_w, OV_SB_H);
	ov_draw_tabs(tab_win, gc_tab, font_b, cur_tab, tab_w, OV_TAB_H);

	for (c = m->clients; c; c = c->next)
		if (ispanel(c)) { XMapWindow(dpy,c->win); XRaiseWindow(dpy,c->win); }
	RAISE_UI();

	XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
	XGrabPointer(dpy, root, True,
		     ButtonPressMask|EnterWindowMask|LeaveWindowMask,
		     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	XSync(dpy, False);
	{ XEvent _e; while (XCheckMaskEvent(dpy, ButtonPressMask|KeyPressMask, &_e)); }

	int xfd = ConnectionNumber(dpy);
	fd_set fds; struct timeval tv;

	while (!done) {
		FD_ZERO(&fds); FD_SET(xfd, &fds);
		tv.tv_sec = 0; tv.tv_usec = 500000;
		if (select(xfd+1, &fds, NULL, NULL, &tv) == 0) {
			cursor_vis = !cursor_vis;
			ov_draw_searchbar(sb_win, gc_sb, font_b,
					  search_buf, search_len, cursor_vis, sb_w, OV_SB_H);
			XFlush(dpy);
			continue;
		}

		while (XPending(dpy)) {
			XEvent ev; XNextEvent(dpy, &ev);
			switch (ev.type) {

			case Expose:
				if (ev.xexpose.window == ov)
					DIM_BG();
				else if (ev.xexpose.window == sb_win)
					ov_draw_searchbar(sb_win, gc_sb, font_b,
							  search_buf, search_len, cursor_vis,
							  sb_w, OV_SB_H);
				else if (ev.xexpose.window == tab_win)
					ov_draw_tabs(tab_win, gc_tab, font_b, cur_tab, tab_w, OV_TAB_H);
				else if (ev.xexpose.window == pg_win)
					ov_draw_pager(pg_win, gc_pg, font_n, page, total_pages, sw, OV_PG_H);
				else {
					for (int i = 0; i < n_cards; i++) {
						if (card_wins[i] != ev.xexpose.window) continue;
						REDRAW_CARD(i, card_wins[i]==hov_win, i==kbd_idx);
						break;
					}
				}
				break;

			case EnterNotify:
				for (int i = 0; i < n_cards; i++) {
					if (card_wins[i] != ev.xcrossing.window) continue;
					hov_win = card_wins[i];
					REDRAW_CARD(i, 1, i==kbd_idx);
					break;
				}
				break;

			case LeaveNotify:
				for (int i = 0; i < n_cards; i++) {
					if (card_wins[i] != ev.xcrossing.window) continue;
					if (hov_win == card_wins[i]) hov_win = None;
					REDRAW_CARD(i, 0, i==kbd_idx);
					break;
				}
				break;

			case ButtonPress: {
				if (ev.xbutton.window == sb_win) break;

				if (ev.xbutton.window == tab_win) {
					OverviewTab nt = (ev.xbutton.x < tab_w/2)
						? TAB_WINDOWS : TAB_APPS;
					cur_tab = nt; cur_view = VIEW_MAIN; sel_app = NULL; page = 0;
					search_len = 0; search_buf[0] = '\0';
					REBUILD_CARDS();
					ov_draw_tabs(tab_win, gc_tab, font_b, cur_tab, tab_w, OV_TAB_H);
					ov_draw_searchbar(sb_win, gc_sb, font_b,
							  search_buf, 0, cursor_vis, sb_w, OV_SB_H);
					break;
				}

				if (ev.xbutton.window == pg_win) {
					int pad2=10, bw2=110, gap2=24, ind_w2=120, bh2=OV_PG_H-pad2*2, by2=pad2;
					int total_row2=bw2+gap2+ind_w2+gap2+bw2;
					int start_x2=(sw-total_row2)/2;
					int bx=ev.xbutton.x, byc=ev.xbutton.y;
					if (bx>=start_x2&&bx<start_x2+bw2&&byc>=by2&&byc<by2+bh2&&page>0)
					{ page--; REBUILD_CARDS(); }
					else if (bx>=start_x2+bw2+gap2+ind_w2+gap2&&bx<start_x2+total_row2&&byc>=by2&&byc<by2+bh2&&page<total_pages-1)
					{ page++; REBUILD_CARDS(); }
					break;
				}

				if (ev.xbutton.button == Button4) {
					/* scroll up = move selection left */
					if (cur_tab == TAB_APPS || cur_view == VIEW_MAIN) {
						if (kbd_idx < 0) {
							kbd_idx = 0;
						} else {
							int prev = kbd_idx;
							if (kbd_idx > 0) {
								kbd_idx--;
								if (prev >= 0 && prev < n_cards) REDRAW_CARD(prev, 0, 0);
								if (kbd_idx >= 0 && kbd_idx < n_cards) REDRAW_CARD(kbd_idx, 1, 1);
							} else if (page > 0) {
								page--;
								REBUILD_CARDS();
								kbd_idx = n_cards - 1;
								if (kbd_idx >= 0 && kbd_idx < n_cards) REDRAW_CARD(kbd_idx, 1, 1);
							}
						}
					} else {
						if (page > 0) { page--; REBUILD_CARDS(); }
					}
					break;
				}
				if (ev.xbutton.button == Button5) {
					/* scroll down = move selection right */
					if (cur_tab == TAB_APPS || cur_view == VIEW_MAIN) {
						if (kbd_idx < 0) {
							kbd_idx = 0;
							if (kbd_idx < n_cards) REDRAW_CARD(kbd_idx, 1, 1);
						} else {
							int prev = kbd_idx;
							if (kbd_idx < n_cards - 1) {
								kbd_idx++;
								if (prev >= 0 && prev < n_cards) REDRAW_CARD(prev, 0, 0);
								if (kbd_idx >= 0 && kbd_idx < n_cards) REDRAW_CARD(kbd_idx, 1, 1);
							} else if (page < total_pages - 1) {
								page++;
								REBUILD_CARDS();
								kbd_idx = 0;
								if (kbd_idx < n_cards) REDRAW_CARD(kbd_idx, 1, 1);
							}
						}
					} else {
						if (page < total_pages-1) { page++; REBUILD_CARDS(); }
					}
					break;
				}

				if (ev.xbutton.button == Button1) {
					for (int i = 0; i < n_cards; i++) {
						if (card_wins[i] != ev.xbutton.window) continue;
						int t = card_type[i];
						if (t < 0) break;
						if (t == 2) {
							cur_view = VIEW_MAIN; sel_app = NULL;
							REBUILD_CARDS();
							ov_draw_tabs(tab_win,gc_tab,font_b,cur_tab,tab_w,OV_TAB_H);
						} else if (t == 3) {
							ov_launch(((AppEntry*)card_ptrs[i])->exec);
							done = 1;
						} else if (t == 4) {
							Client *cc = (Client*)card_ptrs[i];
							for (c=m->clients;c;c=c->next) {
								if (ispanel(c)) continue;
								XMapWindow(dpy,c->win);
								if (c->pre.orig_image) { XDestroyImage(c->pre.orig_image); c->pre.orig_image=NULL; }
								if (c->pre.scaled_image) { XDestroyImage(c->pre.scaled_image); c->pre.scaled_image=NULL; }
							}
							selmon->seltags ^= 1;
							m->tagset[selmon->seltags] = cc->tags;
							focus_c = cc; focus(NULL); done = 1;
						} else if (t == 0) {
							Client *cc = (Client*)card_ptrs[i];
							for (c=m->clients;c;c=c->next) {
								if (ispanel(c)) continue;
								XMapWindow(dpy,c->win);
								if (c->pre.orig_image) { XDestroyImage(c->pre.orig_image); c->pre.orig_image=NULL; }
								if (c->pre.scaled_image) { XDestroyImage(c->pre.scaled_image); c->pre.scaled_image=NULL; }
							}
							selmon->seltags ^= 1;
							m->tagset[selmon->seltags] = cc->tags;
							focus_c = cc; focus(NULL); done = 1;
						} else if (t == 1) {
							AppEntry *ae = (AppEntry*)card_ptrs[i];
							if (ae->running) {
								sel_app = ae; cur_view = VIEW_APP_RUNNING;
								REBUILD_CARDS(); RAISE_UI();
							} else {
								ov_launch(ae->exec); done = 1;
							}
						}
						break;
					}
				} else if (ev.xbutton.button == Button3) {
					if (cur_view == VIEW_APP_RUNNING) {
						cur_view = VIEW_MAIN; sel_app = NULL;
						REBUILD_CARDS();
						ov_draw_tabs(tab_win,gc_tab,font_b,cur_tab,tab_w,OV_TAB_H);
					} else {
						done = 1;
					}
				}
				break;
			}

			case KeyPress: {
				KeySym ks = XKeycodeToKeysym(dpy, ev.xkey.keycode, 0);

				if (ks==XK_Left||ks==XK_Right||ks==XK_Up||ks==XK_Down) {
					int prev = kbd_idx;
					if (kbd_idx < 0) {
						kbd_idx = 0;
					} else {
						int cols;
						if (cur_view == VIEW_APP_RUNNING) cols = 1;
						else if (cur_tab == TAB_WINDOWS) {
							int v = MIN(n_cards, OV_WIN_PER_PG);
							cols = (v<=1)?1:(v<=2)?2:3;
						} else cols = app_cols;

						if (ks == XK_Right) {
							if (kbd_idx < n_cards-1) kbd_idx++;
							else if (page < total_pages-1) { page++; REBUILD_CARDS(); kbd_idx=0; }
						} else if (ks == XK_Left) {
							if (kbd_idx > 0) kbd_idx--;
							else if (page > 0) { page--; REBUILD_CARDS(); kbd_idx=n_cards-1; }
						} else if (ks == XK_Down) {
							if (kbd_idx + cols < n_cards) kbd_idx += cols;
						} else if (ks == XK_Up) {
							if (kbd_idx - cols >= 0) kbd_idx -= cols;
						}
					}
					if (prev >= 0 && prev < n_cards) REDRAW_CARD(prev, 0, 0);
					if (kbd_idx >= 0 && kbd_idx < n_cards) REDRAW_CARD(kbd_idx, 1, 1);
					break;
				}

				if (ks == XK_Escape) {
					if (cur_view == VIEW_APP_RUNNING) {
						cur_view = VIEW_MAIN; sel_app = NULL;
						REBUILD_CARDS();
						ov_draw_tabs(tab_win,gc_tab,font_b,cur_tab,tab_w,OV_TAB_H);
					} else done = 1;
				} else if (ks == XK_Tab) {
					cur_tab = (cur_tab==TAB_WINDOWS)?TAB_APPS:TAB_WINDOWS;
					cur_view = VIEW_MAIN; sel_app = NULL; page = 0;
					REBUILD_CARDS();
					ov_draw_tabs(tab_win,gc_tab,font_b,cur_tab,tab_w,OV_TAB_H);
				} else if (ks == XK_BackSpace) {
					if (search_len > 0) search_buf[--search_len] = '\0';
					page = 0; REBUILD_CARDS();
					ov_draw_searchbar(sb_win,gc_sb,font_b,
							  search_buf,search_len,cursor_vis,sb_w,OV_SB_H);
				} else if (ks == XK_Return && n_cards > 0) {
					int act = (kbd_idx >= 0 && kbd_idx < n_cards) ? kbd_idx : 0;
					int t = card_type[act];
					if (t == 2) {
						cur_view = VIEW_MAIN; sel_app = NULL;
						REBUILD_CARDS();
						ov_draw_tabs(tab_win,gc_tab,font_b,cur_tab,tab_w,OV_TAB_H);
					} else if (t == 3) {
						ov_launch(((AppEntry*)card_ptrs[act])->exec); done = 1;
					} else if (t == 4 || t == 0) {
						Client *cc = (Client*)card_ptrs[act];
						for (c=m->clients;c;c=c->next) {
							if (ispanel(c)) continue;
							XMapWindow(dpy,c->win);
							if (c->pre.orig_image) { XDestroyImage(c->pre.orig_image); c->pre.orig_image=NULL; }
							if (c->pre.scaled_image) { XDestroyImage(c->pre.scaled_image); c->pre.scaled_image=NULL; }
						}
						selmon->seltags ^= 1;
						m->tagset[selmon->seltags] = cc->tags;
						focus_c = cc; focus(NULL); done = 1;
					} else if (t == 1) {
						AppEntry *ae = (AppEntry*)card_ptrs[act];
						if (ae->running) {
							sel_app = ae; cur_view = VIEW_APP_RUNNING;
							REBUILD_CARDS(); RAISE_UI();
						} else {
							ov_launch(ae->exec); done = 1;
						}
					}
				} else {
					char kbuf[8] = {0};
					XLookupString(&ev.xkey, kbuf, sizeof(kbuf)-1, &ks, NULL);
					if (kbuf[0] >= ' ' && kbuf[0] < 127
					    && search_len < (int)sizeof(search_buf)-1) {
						search_buf[search_len++] = kbuf[0];
						search_buf[search_len]   = '\0';
						page = 0; cur_view = VIEW_MAIN; sel_app = NULL;
						REBUILD_CARDS();
						ov_draw_searchbar(sb_win,gc_sb,font_b,
								  search_buf,search_len,cursor_vis,
								  sb_w,OV_SB_H);
					}
				}
				break;
			}

			} /* switch */
		}
	}

	DESTROY_CARDS();
#undef DESTROY_CARDS
#undef REBUILD_CARDS
#undef RAISE_UI
#undef DIM_BG
#undef REDRAW_CARD
#undef _OV_ADD_RUN_WIN
#undef OV_MAX_CARDS
#undef OV_SECTION_GAP

	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	XFreeGC(dpy, gc_ov);
	XFreeGC(dpy, gc_sb);
	XFreeGC(dpy, gc_tab);
	XFreeGC(dpy, gc_pg);
	XDestroyWindow(dpy, sb_win);
	XDestroyWindow(dpy, tab_win);
	XDestroyWindow(dpy, pg_win);
	XDestroyWindow(dpy, ov);

	if (font_b && font_b != font_n) XFreeFont(dpy, font_b);
	if (font_n) XFreeFont(dpy, font_n);
	ov_free_apps(apps, nApps);

	if (!focus_c) {
		for (c = m->clients; c; c = c->next) {
			if (ispanel(c)) continue;
			XMapWindow(dpy, c->win);
			if (c->pre.orig_image)   { XDestroyImage(c->pre.orig_image);   c->pre.orig_image=NULL; }
			if (c->pre.scaled_image) { XDestroyImage(c->pre.scaled_image); c->pre.scaled_image=NULL; }
		}
	}

	arrange(m);
	focus(focus_c);
	XSync(dpy, True);
	run_preview = 0;
}

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
