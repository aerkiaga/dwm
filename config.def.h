/* \file config.def.h
 * See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 1;        //!< border pixel of windows
static const unsigned int snap      = 32;       //!< snap pixel
static const int showbar            = 1;        //!< 0 means no bar
static const int topbar             = 1;        //!< 0 means bottom bar
static const char *fonts[]          = { "monospace:size=10" };
static const char dmenufont[]       = "monospace:size=10";
static const char col_gray1[]       = "#222222";
static const char col_gray2[]       = "#444444";
static const char col_gray3[]       = "#bbbbbb";
static const char col_gray4[]       = "#eeeeee";
static const char col_cyan[]        = "#005577";
static const char *colors[][3]      = { //!< Color scheme settings
	/*               fg         bg         border   */
	[SchemeNorm] = { col_gray3, col_gray1, col_gray2 },
	[SchemeSel]  = { col_gray4, col_cyan,  col_cyan  },
};

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	{ "Gimp",     NULL,       NULL,       0,            1,           -1 },
	{ "Firefox",  NULL,       NULL,       1 << 8,       0,           -1 },
};

/* layout(s) */
static const float mfact     = 0.55; //!< factor of master area size [0.05..0.95]
static const int nmaster     = 1;    //!< number of clients in master area
static const int resizehints = 1;    //!< 1 means respect size hints in tiled resizals

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* key definitions */
#define MODKEY Mod1Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, /*Mod-[1..n] selects only one tag*/\
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, /*Mod-Control-[1..n] toggles a tag in the current tagset*/\
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, /*Mod-Shift-[1..n] applies only a single tag to the selected window*/\
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} }, /*Mod-Control-Shift-[1..n] toggles a tag in the selected window*/

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-m", dmenumon, "-fn", dmenufont, "-nb", col_gray1, "-nf", col_gray3, "-sb", col_cyan, "-sf", col_gray4, NULL }; //!< Command to open dmenu, see spawn()
static const char *termcmd[]  = { "st", NULL }; //!< Command to open terminal, see spawn()

static Key keys[] = { //!< Keyboard shortcuts
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_p,      spawn,          {.v = dmenucmd } }, //Mod-p launches dmenu
	{ MODKEY|ShiftMask,             XK_Return, spawn,          {.v = termcmd } }, //Mod-Shift-Return launches the terminal
	{ MODKEY,                       XK_b,      togglebar,      {0} }, //Mod-b shows/hides the bar
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } }, //Mod-j focuses the next window
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } }, //Mod-k focuses the previous window
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } }, //Mod-i increases number of windows in the master area by one
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1 } }, //Mod-i decreases number of windows in the master area by one
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} }, //Mod-h decreases master area size
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} }, //Mod-l increases master area size
	{ MODKEY,                       XK_Return, zoom,           {0} }, //Mod-Return toggles selected window between master and stack
	{ MODKEY,                       XK_Tab,    view,           {0} }, //Mod-Tab switches between the two last used tagsets
	{ MODKEY|ShiftMask,             XK_c,      killclient,     {0} }, //Mod-Shift-c kills the selected window
	{ MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0]} }, //Mod-t changes to tiling layout
	{ MODKEY,                       XK_f,      setlayout,      {.v = &layouts[1]} }, //Mod-f changes to floating layout
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[2]} }, //Mod-m changes to monocle layout
	{ MODKEY,                       XK_space,  setlayout,      {0} }, //Mod-Space toggles between the two last used layouts
	{ MODKEY|ShiftMask,             XK_space,  togglefloating, {0} }, //Mod-Shift-Space toggles selected window's floating status
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } }, //Mod-0 selects all tags
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } }, //Mod-Shift-0 applies all tags to the selected window
	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } }, //Mod-, focuses previous monitor
	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } }, //Mod-. focuses next monitor
	{ MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } }, //Mod-Shift-, sends the selected window into the previous monitor
	{ MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } }, //Mod-Shift-, sends the selected window into the next monitor
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY|ShiftMask,             XK_q,      quit,           {0} },
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = { //!< Mouse shortcuts
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} }, //left-click on layout symbol toggles between the two last used layouts
	{ ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} }, //right-click on layout symbol changes to monocle layout
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} }, //middle-click on window title toggles selected window between master and stack
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } }, //middle-click on status text launches the terminal
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} }, //Mod-left-click on client window allows to drag it with the mouse
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} }, //Mod-middle-click on client window toggles its floating status
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} }, //Mod-right-click on client window allows to resize it with the mouse
	{ ClkTagBar,            0,              Button1,        view,           {0} }, //left-click on tag list switches between the two last used tagsets
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} }, //?? right-click on tag list does nothing
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} }, //?? Mod-left-click on tag list does nothing
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} }, //?? Mod-right-click on tag list does nothing
};
