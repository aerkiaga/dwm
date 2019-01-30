/*! \file dwm.c
 * See LICENSE file for copyright and license details.
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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask)) // remove lock bits and only leave modifier keys
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy))) // Area of the intersection between a rectangle and a monitor
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags])) // Whether a particular client's window is visible
#define LENGTH(X)               (sizeof X / sizeof X[0]) // Number of elements in a static array
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw) // Width of a particular window, including border
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw) // Height of a particular window, including border
#define TAGMASK                 ((1 << LENGTH(tags)) - 1) // Bitmask representing all available tags
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad) // Text width (including padding)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; //!< cursor
enum { SchemeNorm, SchemeSel }; //!< color schemes
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; //!< EWMH atoms
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; //!< default atoms
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; //!< clicks

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void* v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg* arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
  /** @name Size hints
   * Window size hints. See applysizehints().
  **/
  /**@{*/
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
  /**@}*/
	int bw; //!< Border width
  int oldbw; //!< Saved border width
	unsigned int tags;
	int isfixed; //!< Client window size is fixed due to its size hints
  int isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client* next;
	Client* snext;
	Monitor* mon;
	Window win;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg*);
	const Arg arg;
} Key;

typedef struct {
	const char* symbol;
	void (*arrange)(Monitor*);
} Layout;

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;               //!< bar geometry
	int mx, my, mw, mh;   //!< screen size
	int wx, wy, ww, wh;   //!< window area
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar; //!< show/hide bar
	int topbar; //!< bar at the top/bottom
	Client* clients;
	Client* sel;
	Client* stack;
	Monitor* next;
	Window barwin;
	const Layout* lt[2];
};

typedef struct {
	const char* class;
	const char* instance;
	const char* title;
	unsigned int tags;
	int isfloating;
	int monitor;
} Rule;

/* function declarations */
static void applyrules(Client* c);
static int applysizehints(Client* c, int* x, int* y, int* w, int* h, int interact);
static void arrange(Monitor* m);
static void arrangemon(Monitor* m);
static void attach(Client* c);
static void attachstack(Client* c);
static void buttonpress(XEvent* e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor* mon);
static void clientmessage(XEvent* e);
static void configure(Client* c);
static void configurenotify(XEvent* e);
static void configurerequest(XEvent* e);
static Monitor* createmon(void);
static void destroynotify(XEvent* e);
static void detach(Client* c);
static void detachstack(Client* c);
static Monitor* dirtomon(int dir);
static void drawbar(Monitor* m);
static void drawbars(void);
static void enternotify(XEvent* e);
static void expose(XEvent* e);
static void focus(Client* c);
static void focusin(XEvent* e);
static void focusmon(const Arg* arg);
static void focusstack(const Arg* arg);
static int getrootptr(int* x, int* y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char* text, unsigned int size);
static void grabbuttons(Client* c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg* arg);
static void keypress(XEvent* e);
static void killclient(const Arg* arg);
static void manage(Window w, XWindowAttributes* wa);
static void mappingnotify(XEvent* e);
static void maprequest(XEvent* e);
static void monocle(Monitor* m);
static void motionnotify(XEvent* e);
static void movemouse(const Arg* arg);
static Client* nexttiled(Client* c);
static void pop(Client*);
static void propertynotify(XEvent* e);
static void quit(const Arg* arg);
static Monitor* recttomon(int x, int y, int w, int h);
static void resize(Client* c, int x, int y, int w, int h, int interact);
static void resizeclient(Client* c, int x, int y, int w, int h);
static void resizemouse(const Arg* arg);
static void restack(Monitor* m);
static void run(void);
static void scan(void);
static int sendevent(Client* c, Atom proto);
static void sendmon(Client* c, Monitor* m);
static void setclientstate(Client* c, long state);
static void setfocus(Client* c);
static void setfullscreen(Client* c, int fullscreen);
static void setlayout(const Arg* arg);
static void setmfact(const Arg* arg);
static void setup(void);
static void seturgent(Client* c, int urg);
static void showhide(Client* c);
static void sigchld(int unused);
static void spawn(const Arg* arg);
static void tag(const Arg* arg);
static void tagmon(const Arg* arg);
static void tile(Monitor*);
static void togglebar(const Arg* arg);
static void togglefloating(const Arg* arg);
static void toggletag(const Arg* arg);
static void toggleview(const Arg* arg);
static void unfocus(Client* c, int setfocus);
static void unmanage(Client* c, int destroyed);
static void unmapnotify(XEvent* e);
static void updatebarpos(Monitor* m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client* c);
static void updatestatus(void);
static void updatetitle(Client* c);
static void updatewindowtype(Client* c);
static void updatewmhints(Client* c);
static void view(const Arg* arg);
static Client* wintoclient(Window w);
static Monitor* wintomon(Window w);
static int xerror(Display* dpy, XErrorEvent* ee);
static int xerrordummy(Display* dpy, XErrorEvent* ee);
static int xerrorstart(Display* dpy, XErrorEvent* ee);
static void zoom(const Arg* arg);

/* variables */
static const char broken[] = "broken"; //!< Default title, name or class for broken clients with no title, name or class
static char stext[256]; //!< Status text
static int screen;
static int sw, sh;           //!< X display screen geometry width, height
static int bh;               //!< Bar height
static int blw = 0;          //!< Bar layout symbol width
static int lrpad;            //!< Sum of left and right padding for text
static int (*xerrorxlib)(Display*, XErrorEvent*); //!< Xlib's default error handler
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent*) = {
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
static int running = 1;
static Cur* cursor[CurLast];
static Clr** scheme; //!< Loaded color scheme
static Display* dpy;
static Drw* drw;
static Monitor* mons; //!< Linked list of all distinct monitors
static Monitor* selmon; //!< Currently selected monitor
static Window root; //!< Root window
static Window wmcheckwin; //!< Dummy window to identify as a compliant WM

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void applyrules(Client* c) {
  /*! \brief Apply rules defined in #rules to a newly created client, or default properties.
  **/

	const char* class, *instance;
	unsigned int i;
	const Rule* r;
	Monitor* m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch); //get name and class
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title)) //title must match (title rule must be included in client title)
		&& (!r->class || strstr(class, r->class)) //class must match (class rule must be included in client class)
		&& (!r->instance || strstr(instance, r->instance))) //name must match (name rule must be included in client name)
		{
			c->isfloating = r->isfloating; //apply floating property
			c->tags |= r->tags; //add custom tags
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m; //put in monitor matching monitor number, if there is a match
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags]; //remove invalid tags; if no tag is set, set all selected tags
}

int applysizehints(Client* c, int* x, int* y, int* w, int* h, int interact) {
  /*! \brief Modifies a window's geometry to satisfy size hints provided by the client.
   * \param c [in] The client which window is to be resized.
   * \param x [in, out] X coordinate of the top-left corner of the window.
   * \param y [in, out] Y coordinate of the top-left corner of the window.
   * \param w [in, out] width of the window (not including border).
   * \param h [in, out] height of the window (not including border).
   * \param interact [in] Whether the window can be completely outside its monitor, yet inside the screen.
   * \return Whether any of the final geometry parameters is different from the ones defined in the client.
   *
   * The process is as follows:
   * 1. Make sure that width, height > 0.
   * 2. If the window is completely outside its monitor, or the whole screen, put it inside.
   * 3. Make sure that width, height >= #bh.
   * 4. If the window is floating, or is tiled but #resizehints is set, shrink
   * it to the largest size that fulfills the client's size hints:
   *   * width == basew + (i * incw), for some integer i
   *   * height == baseh + (j * inch), for some integer j
   *   * minw <= width <= maxw
   *   * minh <= height <= maxh
   *   * mina <= (width - basew) / (height - baseh) <= maxa
  **/

	int baseismin;
	Monitor* m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) { //if the window is completely outside the screen (may be outside its monitor), put it inside
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else { //if the window is completely outside its monitor, put it inside
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh) //minimum window height is that of the bar window
		*h = bh;
	if (*w < bh) //minimum window width is the height of the bar window
		*w = bh;
	if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) { //if floating, or tiled but must respect size hints
		/* see last two sentences in ICCCM 4.1.2.3 */
    /* "If a base size is provided along with the aspect ratio fields, the base
     * size should be subtracted from the window size prior to checking that the
     * aspect ratio falls in range. If a base size is not provided, nothing
     * should be subtracted from the window size. (The minimum size is not to be
     * used in place of the base size for this purpose.)"
     *
     * Restrictions (according to ICCCM 4.1.2.3):
     * *w == basew + (i * incw)
     * *h == baseh + (j * inch)
     * minw <= *w <= maxw
     * minh <= *h <= maxh
     * mina <= (*w - basew) / (*h - baseh) <= maxa (see the sentences above)
    **/
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h) //too long
				*w = *h * c->maxa + 0.5; //shrink it, rounding up
			else if (c->mina < (float)*h / *w) //too tall
				*h = *w * c->mina + 0.5; //shrink it, rounding up
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw; //round down to a multiple of incw
		if (c->inch)
			*h -= *h % c->inch; //round down to a multiple of inch
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw); //also add basew and apply minw
		*h = MAX(*h + c->baseh, c->minh); //also add baseh and apply minh
		if (c->maxw)
			*w = MIN(*w, c->maxw); //apply maxw
		if (c->maxh)
			*h = MIN(*h, c->maxh); //apply maxh
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void arrange(Monitor* m) {
  /*! \brief Update a monitor's layout.
   *
   * This will update visibility of all windows in a monitor, arrange its layout
   * and update the X window stack. If NULL is passed, all monitors are arranged
   * but their X stack is not updated and their windows not redrawn.
   *
   * \sa arrangemon()
  **/

	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next) //all monitors
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void arrangemon(Monitor* m) {
  /*! \brief Update monitor layout symbol to that of the currently selected
   * layout and call arrange callback for that layout.
   *
   * \sa arrange(), layouts
  **/

	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
}

void attach(Client* c) {
  /*! \brief Add client to the client list of the monitor specified in its Client::mon property.
   * \sa detach(), attachstack()
  **/

	c->next = c->mon->clients;
	c->mon->clients = c;
}

void attachstack(Client* c) {
  /*! \brief Place client on top of stack.
   * \sa detachstack(), attach()
  **/

	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void buttonpress(XEvent* e) {
  /*! \brief Handler for ButtonPress events.
   * \sa handler
  **/

	unsigned int i, x, click;
	Arg arg = {0};
	Client* c;
	Monitor* m;
	XButtonPressedEvent* ev = &e->xbutton;

	click = ClkRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) { //if clicked on a non-selected monitor
		unfocus(selmon->sel, 1); //unfocus selected window and make inactive
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) { //if clicked on the bar window
    /* | 1 | 2 | 3 | 4 | 5 |    []=    | title of the current window |    status   |
        ------ClkTagBar---- ClkLtSymbol ---------ClkWinTitle--------- ClkStatusText */
		i = x = 0;
		do
			x += TEXTW(tags[i]);
		while (ev->x >= x && ++i < LENGTH(tags)); //find possible tag being clicked
		if (i < LENGTH(tags)) { //clicked on a tag?
			click = ClkTagBar;
			arg.ui = 1 << i; //set argument bit with same index as tag
		} else if (ev->x < x + blw) //clicked on layout symbol?
			click = ClkLtSymbol;
		else if (ev->x > selmon->ww - TEXTW(stext)) //clicked on status?
			click = ClkStatusText;
		else //clicked on window title
			click = ClkWinTitle;
	} else if ((c = wintoclient(ev->window))) { //clicked on a client window
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime); //pass the click event to the client
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++) //execute all appropriate button callbacks
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg); //pass defined arg, or default one for ClkTagBar if none defined
}

void checkotherwm(void) {
  /*! \brief Check if another WM is running.
   *
   * Also sets xerror() as error handler and sets #xerrorxlib to Xlib's default error handler.
  **/

	xerrorxlib = XSetErrorHandler(xerrorstart);
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask); //this causes an error if some other window manager is running
	XSync(dpy, False); //flush X server, call error handlers
	XSetErrorHandler(xerror);
	XSync(dpy, False); //flush X server, call error handlers
}

void cleanup(void) {
  /*! \brief Cleanup routine to be called when exiting the WM.
  **/

	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor* m;
	size_t i;

	view(&a); //set all tags
	selmon->lt[selmon->sellt] = &foo;
	for (m = mons; m; m = m->next) //for all monitors
		while (m->stack) //while there are clients in the stack
			unmanage(m->stack, 0); //destroy the first client, but not its window
	XUngrabKey(dpy, AnyKey, AnyModifier, root); //release hold of all keys
	while (mons) //destroy all monitors
		cleanupmon(mons);
	for (i = 0; i < CurLast; i++) //destroy all cursors
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		free(scheme[i]); //same length as static color scheme array
	XDestroyWindow(dpy, wmcheckwin); //destroy dummy window
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime); //give focus to the root window
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void cleanupmon(Monitor* mon) {
  /*! \brief Remove a monitor from the linked list and destroy its bar window.
  **/

	Monitor* m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next); //find previous monitor
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon);
}

void clientmessage(XEvent* e) {
  /*! \brief Handler for ClientMessage events.
   * \sa handler
  **/

	XClientMessageEvent* cme = &e->xclient;
	Client* c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) { //received "_NET_WM_STATE"
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen]) //client was maximized
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) { //received "_NET_ACTIVE_WINDOW"
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1); //a window wants to be active but isn't, make it urgent
	}
}

void configure(Client* c) {
  /*! \brief Notify client of changes to its window geometry.
  **/

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
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent*)&ce);
}

void configurenotify(XEvent* e) {
  /*! \brief Handler for ConfigureNotify events.
   * \sa handler
  **/

	Monitor* m;
	Client* c;
	XConfigureEvent* ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height); //must resize screen
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) { //if something changed
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void configurerequest(XEvent* e) {
  /*! \brief Handler for ConfigureRequest events.
   * \sa handler
  **/

	Client* c;
	Monitor* m;
	XConfigureRequestEvent* ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) { //it is a client window
		if (ev->value_mask & CWBorderWidth) //change border width
			c->bw = ev->border_width;
		else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) { //if window is floating
			m = c->mon;
			if (ev->value_mask & CWX) { //set new X-position
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) { //set new Y-position
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) { //set new width
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) { //set new height
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating) //hits right edge and is not floating just because of the layout
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating) //hits bottom edge and is not floating just because of the layout
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight))) //changed position but not size
				configure(c); //notify the client
			if (ISVISIBLE(c)) //otherwise it's pointless
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h); //configure window position and size
		} else //didn't change border width, but window is not floating
			configure(c); //just notify the client, although no changes have been made
	} else { //it is a window created by the WM
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

Monitor* createmon(void) {
  /*! \brief Create monitor from global parameters.
  **/

	Monitor* m;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	return m;
}

void destroynotify(XEvent* e) {
  /*! \brief Handler for DestroyNotify events.
   * \sa handler
  **/

	Client* c;
	XDestroyWindowEvent* ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window))) //if it is a client window
		unmanage(c, 1); //destroy the window and remove the client from the WM
}

void detach(Client* c) {
  /*! \brief Detaches a client from its monitor's client list.
   * \sa attach(), detachstack()
  **/

	Client** tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next); //find previous client in linked list
	*tc = c->next; //link it with the next one
}

void detachstack(Client* c) {
  /*! \brief Detach client from stack.
   *
   * If client is selected, select next client in stack.
   * \sa attachstack(), detach()
  **/

	Client** tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext); //find element before c in stack
	*tc = c->snext; //link it to next, skipping c

	if (c == c->mon->sel) { //if c is selected
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext); //find next visible element
		c->mon->sel = t; //select it
	}
}

Monitor* dirtomon(int dir) {
  /*! \brief Returns the monitor in the specified direction from the selected one.
   * \param dir [in] If negative or zero, the previous monitor
   * is returned; if positive, the next monitor is returned.
  **/

	Monitor* m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next)) //next monitor
			m = mons; //wrap around to first
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next); //wrap around to last
	else
		for (m = mons; m->next != selmon; m = m->next); //previous monitor
	return m;
}

void drawbar(Monitor* m) {
  /*! \brief Draw bar window for the specified monitor.
  **/

	int x, w, sw = 0;
	int boxs = drw->fonts->h / 9; //X and Y position of the small box
	int boxw = drw->fonts->h / 6 + 2; //width and height of the small box
	unsigned int i, occ = 0, urg = 0;
	Client* c;

	/* draw status first so it can be overdrawn by tags later */
	if (m == selmon) { /* status is only drawn on selected monitor */
		drw_setscheme(drw, scheme[SchemeNorm]);
		sw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
		drw_text(drw, m->ww - sw, 0, sw, bh, 0, stext, 0);
	}

  /* draw tags */
	for (c = m->clients; c; c = c->next) { //for all clients in this monitor
		occ |= c->tags; //add all tags that the client has
		if (c->isurgent)
			urg |= c->tags; //add all tags that the urgent client has
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) { //for every tag
		w = TEXTW(tags[i]);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]); //set scheme depending on ehether tag is in selected tag list
		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i); //draw tag text, invert colors if urgent
		if (occ & 1 << i) //if there is a window with such tag
			drw_rect(drw, x + boxs, boxs, boxw, boxw,
				m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
				urg & 1 << i); //draw a small box, filled if the selected window has the tag, inverted if urgent
		x += w;
	}

  /* draw layout symbol */
	w = blw = TEXTW(m->ltsymbol); //set bar layout symbol width to width of current layout symbol
	drw_setscheme(drw, scheme[SchemeNorm]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

	if ((w = m->ww - sw - x) > bh) { //if remaining space > bar height
    /* draw window title */
		if (m->sel) { //window title of the selected window
			drw_setscheme(drw, scheme[m == selmon ? SchemeSel : SchemeNorm]); //scheme indicates if monitor is selected
			drw_text(drw, x, 0, w, bh, lrpad / 2, m->sel->name, 0);
			if (m->sel->isfloating)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0); //draw small box if window is floating, fill it if fixed
		} else { //no title
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}
	drw_map(drw, m->barwin, 0, 0, m->ww, bh); //map to actual window
}

void drawbars(void) {
  /*! \brief Call drawbar() for every monitor.
  **/

	Monitor* m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

void enternotify(XEvent* e) {
  /*! \brief Handler for EnterNotify events.
   * \sa handler
  **/

	Client* c;
	Monitor* m;
	XCrossingEvent* ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root) //if non-root pseudo-motion entry, or mouse moved from child to non-root parent
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) { //into a different monitor
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel) //no client, or already focused
		return;
	focus(c); //focus on enter window
}

void expose(XEvent* e) {
  /*! \brief Handler for Expose events.
   * \sa handler
  **/

	Monitor* m;
	XExposeEvent* ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window))) //only redraw once regardless of the number of Expose events, also only if the window is in a monitor
		drawbar(m); //Expose events are only received for the bar window
}

void focus(Client* c) {
  /*! \brief Focus a particular client's window, or the first visible window in the selected monitor.
   *
   * Note that the first visible window in the selected monitor is usually the
   * already-focused window, unless it has been made no longer visible. Also,
   * this function will redraw all bar windows so as to reflect the new state.
   *
   * \sa setfocus()
  **/

	if (!c || !ISVISIBLE(c)) //find the first visible window
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c) //if another client is focused
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon) //focus on different monitor
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0); //make it not urgent
		detachstack(c);
		attachstack(c); //re-attach on top
		grabbuttons(c, 1); //only grab special mouse actions from focused window
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel); //set focused window border
		setfocus(c);
	} else { //no active window
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

void focusin(XEvent* e) {
  /*! \brief Handler for FocusIn events.
   * \sa handler
  **/
  /* there are some broken focus acquiring clients needing extra handling */

	XFocusChangeEvent* ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win) //give input focus to a new window
		setfocus(selmon->sel);
}

void focusmon(const Arg* arg) {
  /*! \brief Focus the monitor in the specified direction from the selected one.
   * \param arg [in] Its Arg::i field is evaluated. If negative or zero, the
   * previous monitor is selected; if positive, the next monitor is selected.
   *
   * \sa keys
  **/

	Monitor* m;

	if (!mons->next) //there is only one monitor
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL); //focus first window in new monitor
}

void focusstack(const Arg* arg) {
  /*! \brief Switch focus to the previous or next client in the list.
   * \param arg [in] If its Arg::i field is negative or zero, the previous window is selected; if it is positive, the next window is selected.
   * \sa keys
  **/

	Client* c = NULL, *i;

	if (!selmon->sel)
		return;
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next); //next visible window from the selected one
		if (!c) //if we reached the end
			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next); //first visible window
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next) //for all clients up to and not including the selected one
			if (ISVISIBLE(i)) //only those which are visible
				c = i; //at the end, c will be set to the previous visible client to the selected one
		if (!c) //if the selected client is the first
			for (; i; i = i->next) //find the last visible client
				if (ISVISIBLE(i))
					c = i;
	}
	if (c) { //if we found a client
		focus(c);
		restack(selmon);
	}
}

Atom getatomprop(Client* c, Atom prop) {
  /*! \brief Get client window property as atom.
   * \param c [in] Client to get property from
   * \param prop [in] Atom representing property to obtain
   * \return None if the property could not be obtained, or the property otherwise.
  **/

	int di;
	unsigned long dl;
	unsigned char* p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom*)p;
		XFree(p);
	}
	return atom;
}

int getrootptr(int* x, int* y) {
  /*! \brief Get pointer coordinates relative to root window.
   * \return True if pointer is on same screen as root, False otherwise.
  **/

	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long getstate(Window w) {
  /*! \brief Get a certain window's WM_STATE property.
   *
   * \return -1 if the state could not be obtained, the property's value otherwise.
  **/

	int format;
	long result = -1;
	unsigned char* p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char**)&p) != Success)
		return -1;
	if (n != 0) //if the number of items returned is not zero
		result = *p;
	XFree(p);
	return result;
}

int gettextprop(Window w, Atom atom, char* text, unsigned int size) {
  /*! \brief Get window property as text.
   * \param w [in] Window to get property from
   * \param atom [in] Atom representing property to obtain
   * \param text [out] Buffer to hold obtained property
   * \param size [in] Size of the buffer that will hold the string
  **/

	char** list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems) //try to get window property from atom
		return 0;
	if (name.encoding == XA_STRING) //if returned data is a string
		strncpy(text, (char*)name.value, size - 1);
	else {
		if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) { //try to convert property into a list of string
			strncpy(text, *list, size - 1); //use first element
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value); //free original string
	return 1;
}

void grabbuttons(Client* c, int focused) {
  /*! \brief Grab button combinations specified by #buttons on client's window.
   *
   * Only combinations with #ClkClientWin are grabbed in a focused window.
  **/

	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win); //ungrab all buttons for client's window
		if (!focused) //if window is not focused, grab all buttons away from it
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++) //for each button combination in list of mouse actions
			if (buttons[i].click == ClkClientWin) //those which go with client windows
				for (j = 0; j < LENGTH(modifiers); j++) //for each lock combination
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None); //passively grab button combination ignoring locks
	}
}

void grabkeys(void) {
  /*! \brief Grab key combinations specified in #keys.
  **/

	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		KeyCode code;

		XUngrabKey(dpy, AnyKey, AnyModifier, root); //ungrab all keys
		for (i = 0; i < LENGTH(keys); i++) //for each key combination in list of keyboard shorcuts
			if ((code = XKeysymToKeycode(dpy, keys[i].keysym))) //get keycode
				for (j = 0; j < LENGTH(modifiers); j++) //for each lock combination
					XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
						True, GrabModeAsync, GrabModeAsync); //passively grab key combination ignoring locks
	}
}

void incnmaster(const Arg* arg) {
  /*! \brief Increase or decrease the maximum number of windows in the master area by the specified amount.
   * \param arg [in] The amount specified in its Arg::i field is added to the current setting.
   * \sa keys
  **/

	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0); //cannot be negative
	arrange(selmon);
}

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo* unique, size_t n, XineramaScreenInfo* info) {
  /*! \brief Check if Xinerama screen is unique geometry.
   * \param unique [in] List of unique geometry screens to check against
   * \param n [in] Number of screens in *unique*
   * \param info [in] Screen to check
  **/

	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void keypress(XEvent* e) {
  /*! \brief Handler for KeyPress events.
   * \sa handler
  **/

	unsigned int i;
	KeySym keysym;
	XKeyEvent* ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++) //apply all key combination-associated actions
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void killclient(const Arg* arg) {
  /*! \brief Kills the selected window's client.
   * \sa keys
  **/

	if (!selmon->sel)
		return;
	if (!sendevent(selmon->sel, wmatom[WMDelete])) { //send WM_DELETE_WINDOW to the client
    /* the client may handle the message for things like a
     * confirmation dialog. If it doesn't, just destroy its window
    **/
		XGrabServer(dpy); //prevent race conditions
		XSetErrorHandler(xerrordummy); //ignore errors
		XSetCloseDownMode(dpy, DestroyAll); //make sure that all client resources will be destroyed
		XKillClient(dpy, selmon->sel->win); //kill it
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void manage(Window w, XWindowAttributes* wa) {
  /*! \brief Integrates a new window into the WM, creating a client for it.
  **/

	Client* c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client)); //create a client for the window
	c->win = w;
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	updatetitle(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) { //is a transient window of trans
		c->mon = t->mon; //same monitor as top-level window
		c->tags = t->tags; //same tags as top-level window
	} else {
		c->mon = selmon; //put window in selected monitor
		applyrules(c);
	}

	if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
		c->x = c->mon->mx + c->mon->mw - WIDTH(c); //if window crosses right edge, move it left
	if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
		c->y = c->mon->my + c->mon->mh - HEIGHT(c); //if window crosses bottom edge, move it up
	c->x = MAX(c->x, c->mon->mx); //if window crosses left edge, move it right
	/* only fix client y-offset, if the client center might cover the bar */
	c->y = MAX(c->y, ((c->mon->by == c->mon->my) && (c->x + (c->w / 2) >= c->mon->wx)
		&& (c->x + (c->w / 2) < c->mon->wx + c->mon->ww)) ? bh : c->mon->my); //if window crosses top edge or its X-center is on the bar, move it down
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
  /* get other client values from the client's window */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);

	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0); //grab all button presses away from the non-focused window
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed; //transient windows and fixed-size windows are floating
	if (c->isfloating)
		XRaiseWindow(dpy, c->win); //floating windows are visible on top of all others
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char*) &(c->win), 1); //append to client list
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0); //unfocus the previously focused window
	c->mon->sel = c; //and select the new client
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	focus(NULL); //focus the first visible window in the selected monitor
}

void mappingnotify(XEvent* e) {
  /*! \brief Handler for MappingNotify events.
   * \sa handler
  **/

	XMappingEvent* ev = &e->xmapping;

	XRefreshKeyboardMapping(ev); //refresh modifier and keymap information
	if (ev->request == MappingKeyboard) //keyboard mapping changed
		grabkeys(); //update key combination grab
}

void maprequest(XEvent* e) {
  /*! \brief Handler for MapRequest events.
   * \sa handler
  **/

	static XWindowAttributes wa;
	XMapRequestEvent* ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect) //let the window do its own thing
		return;
	if (!wintoclient(ev->window)) //window doesn't have a client yet
		manage(ev->window, &wa);
}

void monocle(Monitor* m) {
  /*! \brief Arrange callback for the monocle layout.
   * \sa layouts
  **/

	unsigned int n = 0;
	Client* c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++; //count visible windows
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0); //maximize all clients
}

void motionnotify(XEvent* e) {
  /*! \brief Handler for MotionNotify events.
   * \sa handler
  **/

	static Monitor* mon = NULL; //this contains the last monitor where the mouse pointer was
	Monitor* m;
	XMotionEvent* ev = &e->xmotion;

	if (ev->window != root) //we only care about movement within the root window
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) { //mouse pointer has moved into a new monitor
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

void movemouse(const Arg* arg) {
  /*! \brief Move the selected window using the mouse.
   *
   * After receiving a ButtonPress event, this function can be called, which
   * allows the user to drag the selected window. The function will block until
   * a ButtonRelease event is received.
   *
   * \sa buttons, resizemouse()
  **/

	int x, y, ocx, ocy, nx, ny;
	Client* c;
	Monitor* m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x; //original client position
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess) //we want to grab the mouse pointer and change its shape
		return;
	if (!getrootptr(&x, &y)) //get initial mouse coordinates
		return;
	do { //now run in a loop until the mouse button is released
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev); //block and receive events
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev); //keep handling other events as usual
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60)) //only handle events up to 60 times per second
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x); //new client position
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx; //snap to left edge of monitor
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c); //snap to right edge of monitor
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy; //snap to top edge of monitor
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c); //snap to bottom edge of monitor
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL); //if a tiled window is "pulled" a certain distance, make it floating
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1); //if the window is floating, move it to the new postion, possibly crossing monitor boundaries
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) { //if the client is lying mostly on another monitor
		sendmon(c, m);
		selmon = m;
		focus(NULL); //focus the first visible window in the selected monitor
	}
}

Client* nexttiled(Client* c) {
  /*! \brief Get the first tiled client starting from, and including c.
  **/

	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next); //skip all floating and invisible windows
	return c;
}

void pop(Client* c) {
  /*! \brief Bring a window to the top of the stack and focus it.
  **/

	detach(c);
	attach(c); //re-attach at the top of the stack
	focus(c);
	arrange(c->mon);
}

void propertynotify(XEvent* e) {
  /*! \brief Handler for PropertyNotify events.
   * \sa handler
  **/

	Client* c;
	Window trans;
	XPropertyEvent* ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME)) //status text needs to change
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) { //if a property of a client window is changed
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) && //window is not floating and is transient of another window
				(c->isfloating = (wintoclient(trans)) != NULL)) //if that window is from a client, make it floating and...
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			updatesizehints(c); //get the new size hints
			break;
		case XA_WM_HINTS:
			updatewmhints(c); //get the new WM hints
			drawbars(); //redraw in case urgent status changed
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) { //window title changed
			updatetitle(c);
			if (c == c->mon->sel) //if the selected window's title changes
				drawbar(c->mon); //the window title bar must be redrawn
		}
		if (ev->atom == netatom[NetWMWindowType]) //window type changed
			updatewindowtype(c);
	}
}

void quit(const Arg* arg) {
  /*! \brief Quit dwm.
   * \sa keys, main()
  **/

	running = 0; //see main()
}

Monitor* recttomon(int x, int y, int w, int h) {
  /*! \brief Get monitor that overlaps the most with a given rectangle.
   *
   * If no monitor overlaps with the rectangle, the currently selected monitor is returned.
  **/

	Monitor* m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next) //for every monitor
		if ((a = INTERSECT(x, y, w, h, m)) > area) { //if the rectangle intersects more area of this monitor
			area = a;
			r = m;
		}
	return r;
}

void resize(Client* c, int x, int y, int w, int h, int interact) {
  /*! \brief Resizes a client's window, taking into account size hints (cf. resizeclient()).
   * \param c [in] The client which window is to be resized.
   * \param x [in] The desired X coordinate of the top-left corner of the window.
   * \param y [in] The desired Y coordinate of the top-left corner of the window.
   * \param w [in] The desired width of the window (not including border).
   * \param h [in] The desired height of the window (not including border).
   * \param interact [in] Whether the window can lie completely outside its monitor.
  **/

	if (applysizehints(c, &x, &y, &w, &h, interact)) //if the obtained values are different from the client's
		resizeclient(c, x, y, w, h);
}

void resizeclient(Client* c, int x, int y, int w, int h) {
  /*! \brief Change client's window geometry. Does not take into account size hints (cf. resize()).
  **/

	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void resizemouse(const Arg* arg) {
  /*! \brief Resize the selected window using the mouse.
   *
   * After receiving a ButtonPress event, this function can be called, which
   * allows the user to resize the selected window. The function will block
   * until a ButtonRelease event is received.
   *
   * \sa buttons, movemouse()
  **/

	int ocx, ocy, nw, nh;
	Client* c;
	Monitor* m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x; //original client position
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess) //we want to grab the mouse pointer and change its shape
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1); //warp pointer to the bottom-right corner of the client
	do { //now run in a loop until the mouse button is released
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev); //block and receive events
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev); //keep handling other events as usual
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60)) //only handle events up to 60 times per second
				continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1); //new client size
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{ //if the mouse pointer is within the selected monitor
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL); //if a tiled window is "pulled" a certain distance, make it floating
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1); //if the window is floating, set it to the new size, possibly crossing monitor boundaries
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1); //warp pointer to the bottom-right corner of the client, in case it isn't there
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev)); //remove all EnterNotify events from the queue
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) { //if the client is lying mostly on another monitor
		sendmon(c, m);
		selmon = m;
		focus(NULL); //focus the first visible window in the selected monitor
	}
}

void restack(Monitor* m) {
  /*! \brief Updates a monitor's X stack according to the stack and layout data it contains.
   *
   * Floating windows will be placed on top of all other windows, followed by
   * the bar window, and finally all the other client windows in the order they
   * appear in Monitor::stack. If the selected layout of the monitor is
   * floating, however, all windows will be floating. All windows are redrawn.
  **/

	Client* c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel) //no client window selected, nothing to do
		return;
	if (m->sel->isfloating || !m->lt[m->sellt]->arrange) //if selected window or whole monitor layout is floating
		XRaiseWindow(dpy, m->sel->win); //raise to top of the X stack
	if (m->lt[m->sellt]->arrange) { //monitor has a selected layout (non-floating)
		wc.stack_mode = Below;  // place first window below...
		wc.sibling = m->barwin; //...the bar window in the X stack
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) { //for all non-floating windows in monitor
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc); //apply settings created before
				wc.sibling = c->win; //place next window below the current one in the X stack
			}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev)); //remove al pending EnterNotify events, as they were created in an outdated layout
}

void run(void) {
  /*! \brief Main program loop.
  **/

	XEvent ev;
	/* main event loop */
	XSync(dpy, False); //flush X server
	while (running && !XNextEvent(dpy, &ev)) //wait for event
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void scan(void) {
  /*! \brief Create clients for all visible or iconified windows.
  **/

	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) { //get list and number of children of root window
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue; //skip transient windows, windows that want to be left alone, and those which attributes cannot be obtained
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState) //if visible (must manage window) or iconified (must manage iconic window)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients (e.g. dialog boxes)*/
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)) //if visible (must manage window) or iconified (must manage iconic window)
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void sendmon(Client* c, Monitor* m) {
  /*! \brief Send a client to another monitor, and unfocus it.
  **/

	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	focus(NULL); //focus the first visible window in the selected monitor
	arrange(NULL);
}

void setclientstate(Client* c, long state) {
  /*! \brief Set a client window's WM_STATE property to a given value.
  **/

	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char*)data, 2);
}

int sendevent(Client* c, Atom proto) {
  /*! \brief Send event to client with a protocol.
   * \return Whether the client accepts the specified protocol.
   *
   * The event will only be sent if the client's window accepts *proto*.
  **/

	int n;
	Atom* protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) { //get list of protocols that the window will accept
		while (!exists && n--) //search for proto in list
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) { //if window accepts proto
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

void setfocus(Client* c) {
  /*! \brief Give input focus to a client and make its window the active window.
   * \sa focus()
  **/

	if (!c->neverfocus) { //can be focused?
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime); //set input focus, revert to root if it becomes not viewable
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char*) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]); //notify the window
}

void setfullscreen(Client* c, int fullscreen) {
  /*! \brief Sets a client window's fullscreen status.
   *
   * Note that all window geometry parameters are preserved.
  **/

	if (fullscreen && !c->isfullscreen) { //must become fullscreen
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1); //make the window fullscreen
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win); //hide all other windows
	} else if (!fullscreen && c->isfullscreen){ //must stop being fullscreen
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0); //make it not fulscreen
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

void setlayout(const Arg* arg) {
  /*! \brief Change the default monitor layout.
   * \param arg [in] Its Arg::v field can be a pointer to the new layout, or NULL.
   *
   * This toggles the default monitor's Monitor::sellt property, which serves to
   * keep track of the two last used layouts. Then, if the passed value is not
   * NULL, the new layout is applied, substituting the oldest layout of the two
   * in Monitor::lt.
   *
   * \sa layouts, keys, buttons
  **/

	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt]) //default action: toggle layout
		selmon->sellt ^= 1;
	if (arg && arg->v) //set new layout
		selmon->lt[selmon->sellt] = (Layout*)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol); //set new layout symbol
	if (selmon->sel) //if there is a selected client
		arrange(selmon); //apply layout (also draws the bar)
	else
		drawbar(selmon); //only draw the bar
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg* arg) {
  /*! \brief Change master area width factor (fraction of the monitor occupied by it).
   * \param arg [in] Its Arg::f field is evaluated. If it is less than 1.0, its
   * value is added to the current setting. If it is higher, 1.0 is subtracted
   * and the value is set absolutely as the new factor.
   * \sa keys
  **/

	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange) //must be >0, and not applicable in floating layouts
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.1 || f > 0.9) //these are the limits on mfact value
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void setup(void) {
  /*! \brief Initialization routine.
  **/

	int i;
	XSetWindowAttributes wa;
	Atom utf8string;

	/* clean up any zombies immediately */
	sigchld(0);

	/* init screen */
	screen = DefaultScreen(dpy); //get default open screen number
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h; //left + right font padding = font height
	bh = drw->fonts->h + 2;
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr); //standard arrow cursor
	cursor[CurResize] = drw_cur_create(drw, XC_sizing); //resizing cursor
	cursor[CurMove] = drw_cur_create(drw, XC_fleur); //moving cursor
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr*));
	for (i = 0; i < LENGTH(colors); i++) //for each scheme in scheme list
		scheme[i] = drw_scm_create(drw, colors[i], 3); //3 colors in a scheme
	/* init bars */
	updatebars();
	updatestatus();
	/* supporting window for NetWMCheck, to indicate that a compliant WM is active */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0); //create dummy 1-pixel window
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char*) &wmcheckwin, 1); //set _NET_SUPPORTING_WM_CHECK property of child to itself
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char*) "dwm", 3); //set _NET_WM_NAME property of child to name of window manager
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char*) &wmcheckwin, 1); //set _NET_SUPPORTING_WM_CHECK property of root to child
	/* EWMH (Extended Window Manager Hints) support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char*) netatom, NetLast); //set _NET_SUPPORTED property of root to list of EWMH atoms it supports
	XDeleteProperty(dpy, root, netatom[NetClientList]); //empty list of managed X windows
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor; //set normal cursor for root
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask //get events related to child window creation, destruction, resizing, mapping...
		|ButtonPressMask|PointerMotionMask|EnterWindowMask //get events for button presses and pointer movement
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask; //get events related to window creation, destruction, resizing, mapping... as well as property changes
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa); //set root window's cursor and attributes
	XSelectInput(dpy, root, wa.event_mask); //receive events that are accepted by root window
	grabkeys();
	focus(NULL);
}


void seturgent(Client* c, int urg) {
  /*! \brief Set urgent status for client.
  **/

	XWMHints* wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win))) //try to get WM hints for the specified client's window
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint); //set/unset XUrgencyHint flag
	XSetWMHints(dpy, c->win, wmh); //update WM hints
	XFree(wmh);
}

void showhide(Client* c) {
  /*! \brief Apply visibility of all clients in a stack, placing them inside or outside the screen.
  **/

	if (!c)
		return;
	if (ISVISIBLE(c)) { //if at least one tag of this window is selected
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y); //move it into the screen
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen) //if floating but not fullscreen
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext); //recursively show next client
	} else {
		/* hide clients bottom up */
		showhide(c->snext); //recursively hide next client
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y); //take it out of the screen
	}
}

void sigchld(int unused) {
  /*! \brief This function sets itself as the handler for SIGCHLD signals.
   * Whenever a child process terminates, it will be removed from the process
   * table. It need only be called once at startup.
  **/

	if (signal(SIGCHLD, sigchld) == SIG_ERR) //set itself as handler for SIGCHLD
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG)); //let the system reap any terminated child processes
}

void spawn(const Arg* arg) {
  /*! \brief Executes a command in a new process.
   * \param arg [in] Its Arg::v field must contain a pointer to an array of
   * char* where the first points to a string containing the path of the program
   * to be run, followed by zero or more strings containing the arguments to
   * pass, and a final NULL pointer.
   *
   * Note that special handling is done for dmenu.
   *
   * \sa keys, buttons
  **/

	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num; //tell dmenu the monitor number
	if (fork() == 0) {
    /* we are in the child process */
		if (dpy)
			close(ConnectionNumber(dpy)); //close connection with the X server
		setsid(); //start a new session
		execvp(((char**)arg->v)[0], (char**)arg->v); //execute some command
    /* we are not running the new program, so an error occurred */
		fprintf(stderr, "dwm: execvp %s", ((char**)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

void tag(const Arg* arg) {
  /*! \brief Replace the selected window's tags.
   * \param arg [in] Its Arg::ui field must contain the new tags to apply.
   *
   * If no tag is specified, the window will be left unchanged.
   *
   * \sa keys, buttons
  **/

	if (selmon->sel && arg->ui & TAGMASK) { //if we are applying at least one tag to the selected window
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL); //focus the first visible window in the selected monitor
		arrange(selmon);
	}
}

void tagmon(const Arg* arg) {
  /*! \brief Send the selected window into the monitor in the selected direction.
   * \param arg	[in] Its Arg::i field is evaluated. If negative or zero, the
   * window is sent into the previous monitor; if positive, the window is sent
   * into the next monitor.
   *
   * \sa keys
  **/

	if (!selmon->sel || !mons->next) //if no selected window or only one monitor
		return;
	sendmon(selmon->sel, dirtomon(arg->i)); //send the selected window in that direction
}

void tile(Monitor* m) {
  /*! \brief Arrange callback for the tiled layout.
   * \sa layouts
  **/

	unsigned int i, n, h, mw, my, ty;
	Client* c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++); //count the number of tiled windows
	if (n == 0)
		return;

	if (n > m->nmaster) //if we need to have a stack area
		mw = m->nmaster ? m->ww * m->mfact : 0; //set master area width
	else
		mw = m->ww; //master will fill whole space
	for (i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) //for every tiled window
		if (i < m->nmaster) { //put it in master area
			h = (m->wh - my) / (MIN(n, m->nmaster) - i); //calculate window height
			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0);
			my += HEIGHT(c); //next master window's position
		} else { //put it in stack area
			h = (m->wh - ty) / (n - i); //calculate window height
			resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw), 0);
			ty += HEIGHT(c); //next stack window's position
		}
}

void togglebar(const Arg* arg) {
  /*! \brief Show or hide the bar window.
   * \sa keys
  **/

	selmon->showbar = !selmon->showbar; //invert status
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh); //actually resize it
	arrange(selmon);
}

void togglefloating(const Arg* arg) {
  /*! \brief Toggles selected window's floating status. If the window is fixed-size, it is always left floating.
   * \sa keys, buttons
  **/

	if (!selmon->sel)
		return;
	if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
			selmon->sel->w, selmon->sel->h, 0); //resize to its true size (i.e. taking size hints into account)
	arrange(selmon);
}

void toggletag(const Arg* arg) {
  /*! \brief Toggle specified tags in the selected window.
   * \param arg [in] Its Arg::ui field must contain the desired tags to invert.
   *
   * All tags will not be unset, at least one must remain. Otherwise, this
   * function will leave all tags in the window unchanged.
   *
   * \sa keys, buttons
  **/

	unsigned int newtags;

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) { //must contain at least one tag
		selmon->sel->tags = newtags;
		focus(NULL);
		arrange(selmon);
	}
}

void toggleview(const Arg* arg) {
  /*! \brief Toggle specified tags in the current tagset.
   * \param arg [in] Its Arg::ui field must contain the desired tags to invert.
   *
   * All tags will not be unselected, at least one must remain. Otherwise, this
   * function will leave all tags unchanged.
   *
   * \sa keys, buttons
  **/

	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) { //must contain at least one tag
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
}

void unfocus(Client* c, int setfocus) {
  /*! \brief Unfocus a given client's window.
   * \param c [in] The client which window is to be unfocused.
   * \param setfocus [in] If True, the client window becomes inactive and loses input focus.
  **/

	if (!c)
		return;
	grabbuttons(c, 0); //grab all button presses away from unfocused window
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel); //set unfocused window border
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void unmanage(Client* c, int destroyed) {
  /*! \brief Remove a client from the WM.
   * \param c [in] The client to unmanage.
   * \param destroyed [in] Whether the window associated with the client should
   * be destroyed, or just withdrawn.
  **/

	Monitor* m = c->mon;
	XWindowChanges wc;

	detach(c);
	detachstack(c);
	if (!destroyed) { //only withdraw window
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL); //focus the first visible window in the selected monitor
	updateclientlist();
	arrange(m);
}

void unmapnotify(XEvent* e) {
  /*! \brief Handler for UnmapNotify events.
   * \sa handler
  **/

	Client* c;
	XUnmapEvent* ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) { //must unmap a client window
		if (ev->send_event) //if this came from a SendEvent request
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0); //unmanage but don't destroy window
	}
}

void updatebars(void) {
  /*! \brief Create a bar for every monitor that doesn't have one.
  **/

	Monitor* m;
	XSetWindowAttributes wa = {
		.override_redirect = True, //don't redirect map/configure requests to parent
		.background_pixmap = ParentRelative, //use parent's background (aligned with parent)
		.event_mask = ButtonPressMask|ExposureMask //get button presses and exposed invalid areas
	};
	XClassHint ch = {"dwm", "dwm"}; //application name and class
	for (m = mons; m; m = m->next) { //make sure every monitor has a bar
		if (m->barwin) //skip already-existing bars
			continue;
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, /*border_width:*/ 0, DefaultDepth(dpy, screen),
				/*class:*/ CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa); //apply attributes defined before
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor); //set normal cursor for bar window
		XMapRaised(dpy, m->barwin); //map bar and raise to top of stack
		XSetClassHint(dpy, m->barwin, &ch); //set name and class defined before
	}
}

void updatebarpos(Monitor* m) {
  /*! \brief Update bar position to show/hide on top/bottom.
  **/

	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) { //show bar
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	} else //hide bar
		m->by = -bh;
}

void updateclientlist() {
  /*! \brief Update the X server client list.
  **/

	Client* c;
	Monitor* m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char*) &(c->win), 1);
}

int updategeom(void) {
  /*! \brief Update monitor geometry.
   * \return True if monitor geometry was actually changed. False otherwise.
   *
   * Also initializes or updates monitor list.
  **/

	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client* c;
		Monitor* m;
		XineramaScreenInfo* info = XineramaQueryScreens(dpy, &nn); //get number of output devices and info about each
		XineramaScreenInfo* unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++); //get number of monitors
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++) //copy unique geometries and leave others out
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j; //new number of screens
		if (n <= nn) { /* new monitors available */
			for (i = 0; i < (nn - n); i++) {
				for (m = mons; m && m->next; m = m->next); //find last monitor
				if (m)
					m->next = createmon();
				else //add first monitor to empty list
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
		} else { /* less monitors available nn < n */
			for (i = nn; i < n; i++) {
				for (m = mons; m && m->next; m = m->next); //find last monitor
				while ((c = m->clients)) { //for all clients in monitor to be removed
					dirty = 1;
					m->clients = c->next;
					detachstack(c);
					c->mon = mons; //attach to first available monitor
					attach(c);
					attachstack(c);
				}
				if (m == selmon) //the selected monitor has been removed
					selmon = mons; //select the first available monitor
				cleanupmon(m);
			}
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup (if no Xinerama or not active)*/
		if (!mons) //if we haven't initialized our monitor yet
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) { //if monitor size != X display size
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

void updatenumlockmask(void) {
  /*! \brief Set #numlockmask to the appropriate modifier mask for numlock.
  **/

	unsigned int i, j;
	XModifierKeymap* modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy); //get keys that are used as modifiers
	for (i = 0; i < 8; i++) //for each modifier
		for (j = 0; j < modmap->max_keypermod; j++) //for each key in modifier
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock)) //if we have found numlock modifier
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void updatesizehints(Client* c) {
  /*! \brief Set a client's size hints according to the ones set by its window.
   * \sa applysizehints()
  **/

	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize)) //get size hints
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize; //?? this will ignore any hints, won't it?
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
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh); //the client size is fixed
}

void updatestatus(void) {
  /*! \brief Update bar status text.
  **/

	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext))) //get name of root window into stext
		strcpy(stext, "dwm-"VERSION); //fallback
	drawbar(selmon);
}

void updatetitle(Client* c) {
  /*! \brief Set client name from its window's WM_NAME property.
  **/

	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

void updatewindowtype(Client* c) {
  /*! \brief Set client's fullscreen or floating status if its window is fullscreen or a dialog.
  **/

	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
}

void updatewmhints(Client* c) {
  /*! \brief Set a client's Client::isurgent and Client::neverfocus values according to its window's WM_HINT property.
  **/

	XWMHints* wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) { //get the window's WM_HINTS property
		if (c == selmon->sel && wmh->flags & XUrgencyHint) { //the window is selected but wants attention
			wmh->flags &= ~XUrgencyHint; //it already has
			XSetWMHints(dpy, c->win, wmh); //tell it so
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint) //client has input mode hint
			c->neverfocus = !wmh->input; //if client doesn't expect input, or expects it but doesn't want the WM to give it input focus (gets input by itself)
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void view(const Arg* arg) {
  /*! \brief If the current tagset is different from the one specified, switch
   * to the other tagset, and, if the specified tagset contains at least one
   * tag, set it as the new tagset.
   *
   * \sa keys, buttons
  **/

	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags]) //if all the specified tags are selected
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK; //select exactly those tags
	focus(NULL); //focus the first visible window in the selected monitor
	arrange(selmon);
}

Client* wintoclient(Window w) {
  /*! \brief Get client associated with a window.
   *
   * If there is none, returns NULL.
  **/

	Client* c;
	Monitor* m;

	for (m = mons; m; m = m->next) //for every monitor
		for (c = m->clients; c; c = c->next) //for every client in monitor
			if (c->win == w)
				return c;
	return NULL;
}

Monitor* wintomon(Window w) {
  /*! \brief Get monitor associated with a window.
   *
   * If no monitor can be found, the current selected monitor is returned.
  **/

	int x, y; //pointer coordinates relative to root
	Client* c;
	Monitor* m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1); //return monitor where the pointer is
	for (m = mons; m; m = m->next)
		if (w == m->barwin) //bar window in a monitor
			return m;
	if ((c = wintoclient(w))) //all other windows have clients
		return c->mon;
	return selmon; //fallback
}

int xerror(Display* dpy, XErrorEvent* ee) {
  /*! \brief Default error handler.
   *
   * There's no way to check accesses to destroyed windows, thus those cases are
   * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
   * default error handler, which may call exit.
  **/

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
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display* dpy, XErrorEvent* ee) {
  /*! \brief Dummy error handler.
  **/

	return 0;
}

int xerrorstart(Display* dpy, XErrorEvent* ee) {
  /*! Startup Error handler to check if another window manager
   * is already running.
  **/

	die("dwm: another window manager is already running");
	return -1;
}

void zoom(const Arg* arg) {
  /*! \brief Toggle window between master and stack areas.
   *
   * If there is more than one master window, toggle between first master
   * position and the rest of the master area plus the stack area.
   *
   * \sa keys, buttons
  **/

	Client* c = selmon->sel;

	if (!selmon->lt[selmon->sellt]->arrange
	|| (selmon->sel && selmon->sel->isfloating)) //if the selected window is floating
		return;
	if (c == nexttiled(selmon->clients)) //if c is already the first tiled window (in master)
		if (!c || !(c = nexttiled(c->next))) //pop the next tiled window
			return;
	pop(c);
}

int main(int argc, char* argv[]) {
  /*! \brief Entry point.
  **/

	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION); //print version and exit
	else if (argc != 1)
		die("usage: dwm [-v]"); //print usage and exit
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) //try to set character locale according to environment variables
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL))) //try to open display from name set in DISPLAY environment variable
		die("dwm: cannot open display");
	checkotherwm();
	setup();
	scan();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
