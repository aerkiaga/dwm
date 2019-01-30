/*! \file drw.c
 * See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

#define UTF_INVALID 0xFFFD //character to be used when character is invalid
#define UTF_SIZ     4 //amximum number of bytes that a character can span

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0}; //!< Masked bits for continuation byte and first bytes of characters with different lengths
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8}; //!< Masks for continuation byte and first bytes of characters with different lengths
static const long utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000}; //!< Minimum code point for characters of different lengths
static const long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF}; //!< Maximum code point for characters of different lengths

static long utf8decodebyte(const char c, size_t* i) {
	/*! \brief Extracts coding bits and size info in a byte from a UTF-8 string.
	 * \param c [in] One byte from a UTF-8 encoded string.
	 * \param i [out] For the first byte in a character, will output the total number of bytes in it. For the rest, 0.
  **/

	for (*i = 0; *i < (UTF_SIZ + 1); ++(*i)) //try each size up to UTF_SIZE
		if (((unsigned char)c & utfmask[*i]) == utfbyte[*i]) //if mask matches
			return (unsigned char)c & ~utfmask[*i]; //remove masked bits
	return 0;
}

static size_t utf8validate(long* u, size_t i) {
	/*! \brief Validate an Unicode character.
	 * \param u [in] Pointer to Unicode character to validate.
	 * \param i [in] Number of bytes taken up by the character in UTF-8.
	 * \return The specified size if correct, or the correct size otherwise.
	 *
	 * Will substitute *u with UTF_INVALID if the original value is not valid.
  **/

	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF)) //if not within Unicode bounds
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i) //find correct size
		;
	return i;
}

static size_t utf8decode(const char* c, long* u, size_t clen) {
	/*! \brief Decodes a single UTF-8 character into its Unicode codepoint.
	 * \param c [in] Pointer to the start of an UTF-8 character in a string.
	 * \param u [out] Will be set to the appropriate Unicode codepoint, or UTF_INVALID.
	 * \param clen [in] Maximum number of bytes to read.
	 * \return Number of bytes that were read.
	 *
	 * If the character is incomplete or badly formatted, *u will be unchanged.
  **/

	size_t i, j, len, type;
	long udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len); //decode first byte, get total character length
	if (!BETWEEN(len, 1, UTF_SIZ)) //character has weird length
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) { //for every remaining byte
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type); //concatenate next 6 bits
		if (type) //badly formatted UTF-8 character
			return j;
	}
	if (j < len) //unable to complete whole character
		return 0;
	*u = udecoded;
	utf8validate(u, len); //substitute with UTF_INVALID if invalid

	return len;
}

Drw* drw_create(Display* dpy, int screen, Window root, unsigned int w, unsigned int h) {
	/*! \brief Create drawing context.
  **/

	Drw* drw = ecalloc(1, sizeof(Drw));

	drw->dpy = dpy;
	drw->screen = screen;
	drw->root = root;
	drw->w = w;
	drw->h = h;
	drw->drawable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen)); //create pixmap with given parameters
	drw->gc = XCreateGC(dpy, root, 0, NULL); //create graphics context
	XSetLineAttributes(dpy, drw->gc, 1, LineSolid, CapButt, JoinMiter); //configure how lines will look like

	return drw;
}

void drw_resize(Drw* drw, unsigned int w, unsigned int h) {
	/*! \brief Resize a given drawing context.
  **/

	if (!drw)
		return;

	drw->w = w;
	drw->h = h;
	if (drw->drawable)
		XFreePixmap(drw->dpy, drw->drawable);
	drw->drawable = XCreatePixmap(drw->dpy, drw->root, w, h, DefaultDepth(drw->dpy, drw->screen));
}

void drw_free(Drw* drw) {
	/*! \brief Destroy a drawing context.
  **/

	XFreePixmap(drw->dpy, drw->drawable);
	XFreeGC(drw->dpy, drw->gc);
	free(drw);
}

static Fnt* xfont_create(Drw* drw, const char* fontname, FcPattern* fontpattern) {
	/*! \brief Create a font from either name or pattern.
	 *
	 * This function is an implementation detail. Library users should use
	 * drw_fontset_create() instead.
  **/

	Fnt* font;
	XftFont* xfont = NULL;
	FcPattern* pattern = NULL;

	if (fontname) { //use name
		/* Using the pattern found at font->xfont->pattern does not yield the
		 * same substitution results as using the pattern returned by
		 * FcNameParse; using the latter results in the desired fallback
		 * behaviour whereas the former just results in missing-character
		 * rectangles being drawn, at least with some fonts. */
		if (!(xfont = XftFontOpenName(drw->dpy, drw->screen, fontname))) { //try to load font from name
			fprintf(stderr, "error, cannot load font from name: '%s'\n", fontname);
			return NULL;
		}
		if (!(pattern = FcNameParse((FcChar8*) fontname))) { //get pattern from name
			fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n", fontname);
			XftFontClose(drw->dpy, xfont);
			return NULL;
		}
	} else if (fontpattern) { //use pattern
		if (!(xfont = XftFontOpenPattern(drw->dpy, fontpattern))) { //try to load font from pattern
			fprintf(stderr, "error, cannot load font from pattern.\n");
			return NULL;
		}
	} else {
		die("no font specified.");
	}

	font = ecalloc(1, sizeof(Fnt));
	font->xfont = xfont;
	font->pattern = pattern;
	font->h = xfont->ascent + xfont->descent;
	font->dpy = drw->dpy;

	return font;
}

static void xfont_free(Fnt* font) {
	/*! \brief Destroy a font.
  **/

	if (!font)
		return;
	if (font->pattern)
		FcPatternDestroy(font->pattern);
	XftFontClose(font->dpy, font->xfont);
	free(font);
}

Fnt* drw_fontset_create(Drw* drw, const char* fonts[], size_t fontcount) {
	/*! \brief Create a linked list of fonts and associates it with a drawing context.
  **/

	Fnt* cur, *ret = NULL;
	size_t i;

	if (!drw || !fonts)
		return NULL;

	for (i = 1; i <= fontcount; i++) {
		if ((cur = xfont_create(drw, fonts[fontcount - i], NULL))) {
			cur->next = ret;
			ret = cur;
		}
	}
	return (drw->fonts = ret);
}

void drw_fontset_free(Fnt* set) {
	/*! \brief Destroy a fontset.
  **/

	if (set) {
		drw_fontset_free(set->next); //call recursively to start from the end of the list
		xfont_free(set);
	}
}

void drw_clr_create(Drw* drw, Clr* dest, const char* clrname) {
	/*! \brief Populates color from name.
  **/

	if (!drw || !dest || !clrname)
		return;

	if (!XftColorAllocName(drw->dpy, DefaultVisual(drw->dpy, drw->screen),
	                       DefaultColormap(drw->dpy, drw->screen),
	                       clrname, dest)) //allocate color on server with default visual and colormap
		die("error, cannot allocate color '%s'", clrname);
}

Clr* drw_scm_create(Drw* drw, const char* clrnames[], size_t clrcount) {
	/*! \brief Wrapper to create color schemes.
	 *
	 * The caller has to call *free(3)* on the
	 * returned color scheme when done using it.
  **/

	size_t i;
	Clr* ret;

	/* need at least two colors for a scheme */
	if (!drw || !clrnames || clrcount < 2 || !(ret = ecalloc(clrcount, sizeof(Clr))))
		return NULL;

	for (i = 0; i < clrcount; i++)
		drw_clr_create(drw, &ret[i], clrnames[i]);
	return ret;
}

void drw_setfontset(Drw* drw, Fnt* set) {
	/*! \brief Set the fontset for a drawing context.
  **/

	if (drw)
		drw->fonts = set;
}

void drw_setscheme(Drw* drw, Clr* scm) {
	/*! \brief Set color scheme of a drawing context.
  **/

	if (drw)
		drw->scheme = scm;
}

void drw_rect(Drw* drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert) {
	/*! \brief Draw a rectangle.
	 * \param drw [in] Drawing context.
	 * \param x [in] X coordinate of the top-left corner.
	 * \param y [in] Y coordinate of the top-left corner.
	 * \param w [in] Rectangle width.
	 * \param h [in] Rectangle height.
	 * \param filled [in] Whether or not the rectangle should be filled.
	 * \param invert [in] Invert foreground and background colors.
  **/

	if (!drw || !drw->scheme)
		return;
	XSetForeground(drw->dpy, drw->gc, invert ? drw->scheme[ColBg].pixel : drw->scheme[ColFg].pixel);
	if (filled)
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
	else
		XDrawRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w - 1, h - 1);
}

int drw_text(Drw* drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char* text, int invert) {
	/*! \brief Draw text, or measure its width without drawing.
	 * \param drw [in] Drawing context.
	 * \param x [in] X coordinate of the top-left corner.
	 * \param y [in] Y coordinate of the top-left corner.
	 * \param w [in] Desired width of the text. Width of enclosing rectangle and maximum width of text.
	 * \param h [in] Height of the rectangle where the text will be vertically centered.
	 * \param lpad [in] Left padding.
	 * \param text [in] UTF-8 encoded string of text to be drawn.
	 * \param invert [in] Invert foreground and background colors.
	 * \return The specified width. If x = y = w = h = 0, nothing will be drawn, and the actual width will be returned.
  **/

	char buf[1024];
	int ty;
	unsigned int ew;
	XftDraw* d = NULL;
	Fnt* usedfont, *curfont, *nextfont;
	size_t i, len;
	int utf8strlen, utf8charlen, render = x || y || w || h; //all set to 0 means only get width, don't render
	long utf8codepoint = 0;
	const char* utf8str;
	FcCharSet* fccharset;
	FcPattern* fcpattern;
	FcPattern* match;
	XftResult result;
	int charexists = 0;

	if (!drw || (render && !drw->scheme) || !text || !drw->fonts)
		return 0;

	if (!render) {
		w = ~w; //set width to highest possible value so that all text is measured, with no limit
	} else {
		XSetForeground(drw->dpy, drw->gc, drw->scheme[invert ? ColFg : ColBg].pixel); //set foreground for GC
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h); //fill rectangle around text
		d = XftDrawCreate(drw->dpy, drw->drawable,
		                  DefaultVisual(drw->dpy, drw->screen),
		                  DefaultColormap(drw->dpy, drw->screen));
		x += lpad;
		w -= lpad;
	}

	usedfont = drw->fonts; //use first font in linked list
	while (1) { //draw each chunk of text with the first font that supports it
		utf8strlen = 0;
		utf8str = text; //draw from where last iteration finished
		nextfont = NULL;
		while (*text) { //scan text to find largest chunk of it that can be rendered with the current font
			utf8charlen = utf8decode(text, &utf8codepoint, UTF_SIZ); //get one UTF-8 character
			for (curfont = drw->fonts; curfont; curfont = curfont->next) { //try all fonts in order
				charexists = charexists || XftCharExists(drw->dpy, curfont->xfont, utf8codepoint); //does character exist in font?
				if (charexists) {
					if (curfont == usedfont) { //the font we are using is the first that has this character
						utf8strlen += utf8charlen;
						text += utf8charlen; //advance to next UTF-8 character
					} else {
						nextfont = curfont; //must use another font for this character, only draw up to here
					}
					break;
				}
			}

			if (!charexists || nextfont) //the font we are using will not work
				break; //stop scanning
			else //go on scanning
				charexists = 0;
		}

		if (utf8strlen) { //if the chunk of text actually exists
			drw_font_getexts(usedfont, utf8str, utf8strlen, &ew, NULL); //get text width
			/* shorten text if necessary */
			for (len = MIN(utf8strlen, sizeof(buf) - 1); len && ew > w; len--) //shorten tet until it fits
				drw_font_getexts(usedfont, utf8str, len, &ew, NULL);

			if (len) {
				memcpy(buf, utf8str, len);
				buf[len] = '\0';
				if (len < utf8strlen) //if we shortened it
					for (i = len; i && i > len - 3; buf[--i] = '.') //add ellipsis at the end
						; /* NOP */

				if (render) {
					ty = y + (h - usedfont->h) / 2 + usedfont->xfont->ascent; //center text vertically in given rectangle
					XftDrawStringUtf8(d, &drw->scheme[invert ? ColBg : ColFg],
					                  usedfont->xfont, x, ty, (XftChar8*)buf, len); //draw text
				}
				x += ew; //next chunk of text
				w -= ew; //less space available
			}
		} //if chunk exists

		if (!*text) { //reached NULL terminator
			break;
		} else if (nextfont) { //must change font for next chunk
			charexists = 0;
			usedfont = nextfont;
		} else { //there is no font for next chunk
			/* Regardless of whether or not a fallback font is found, the
			 * character must be drawn. */
			charexists = 1; //just use the usedfont that will be set later

			fccharset = FcCharSetCreate();
			FcCharSetAddChar(fccharset, utf8codepoint); //add the problematic character

			if (!drw->fonts->pattern) {
				/* Refer to the comment in xfont_create for more information. */
				die("the first font in the cache must be loaded from a font string.");
			}

			fcpattern = FcPatternDuplicate(drw->fonts->pattern); //use the default font pattern
			FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset); //but must contain the desired character
			FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue); //also must be scalable

			FcConfigSubstitute(NULL, fcpattern, FcMatchPattern); //substitute pattern using default configuration
			FcDefaultSubstitute(fcpattern); //substitute default values for nonspecified font patterns
			match = XftFontMatch(drw->dpy, drw->screen, fcpattern, &result); //get matching font

			FcCharSetDestroy(fccharset);
			FcPatternDestroy(fcpattern);

			if (match) {
				usedfont = xfont_create(drw, NULL, match); //make font that matches pattern
				if (usedfont && XftCharExists(drw->dpy, usedfont->xfont, utf8codepoint)) {
					for (curfont = drw->fonts; curfont->next; curfont = curfont->next) //find last font
						; /* NOP */
					curfont->next = usedfont; //add new font at the end
				} else {
					xfont_free(usedfont);
					usedfont = drw->fonts; //use default font instead
				}
			}
		}
	}
	if (d) //cleanup
		XftDrawDestroy(d);

	return x + (render ? w : 0); //return text width, or specified width
}

void drw_map(Drw* drw, Window win, int x, int y, unsigned int w, unsigned int h) {
	/*! \brief Map the specified drawing context area to a window.
	 * \param drw [in] Drawing context.
	 * \param x [in] X coordinate of the top-left corner of the area that will be mapped.
	 * \param y [in] Y coordinate of the top-left corner of the area that will be mapped.
	 * \param w [in] Width of the area that will be mapped.
	 * \param h [in] Height of the area that will be mapped.
  **/

	if (!drw)
		return;

	XCopyArea(drw->dpy, drw->drawable, win, drw->gc, x, y, w, h, x, y);
	XSync(drw->dpy, False);
}

unsigned int drw_fontset_getwidth(Drw* drw, const char* text) {
	/*! \brief Get text width (not including any padding).
  **/

	if (!drw || !drw->fonts || !text)
		return 0;
	return drw_text(drw, 0, 0, 0, 0, 0, text, 0);
}

void drw_font_getexts(Fnt* font, const char* text, unsigned int len, unsigned int* w, unsigned int* h) {
	/*! \brief Get text extents when drawn with a particular font.
	 * \param Fnt [in] The font to be used.
	 * \param text [in] The text of which the extents will be calculated.
	 * \param len [in] Length of the given text.
	 * \param w [out] If not NULL, will get the width of the text with the specified font.
	 * \param h [out] If not NULL, will get the height of the used font.
  **/

	XGlyphInfo ext;

	if (!font || !text)
		return;

	XftTextExtentsUtf8(font->dpy, font->xfont, (XftChar8*)text, len, &ext); //get text pixel extents when drawn with font
	if (w)
		*w = ext.xOff; //text x bearing
	if (h)
		*h = font->h; //font height
}

Cur* drw_cur_create(Drw* drw, int shape) {
	/*! \brief Create font cursor of standard shape.
  **/

	Cur* cur;

	if (!drw || !(cur = ecalloc(1, sizeof(Cur))))
		return NULL;

	cur->cursor = XCreateFontCursor(drw->dpy, shape); //create font cursor

	return cur;
}

void drw_cur_free(Drw* drw, Cur* cursor) {
	/*! \brief Destroy a cursor.
  **/

	if (!cursor)
		return;

	XFreeCursor(drw->dpy, cursor->cursor);
	free(cursor);
}
