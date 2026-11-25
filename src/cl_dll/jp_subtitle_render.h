// ====================================
// PoC-2 Japanese subtitle renderer
//		UTF-8 + stb_truetype rendering for the Paranoia subtitle panels
// ====================================
//
// The panel-side entry points are member functions of CSubtitleTextPanel
// (declared in vgui_subtitles.h, implemented in jp_subtitle_render.cpp):
//   SetJpText()    - keep a copy of the display string, validate it
//   JpMeasureText()- wrapped measurement used instead of getTextSizeWrapped
//   JpPaintText()  - two-pass (shadow + body) textured-glyph paint
//
// This header only carries the one free helper shared with
// vgui_subtitles.cpp logic.

#ifndef JP_SUBTITLE_RENDER_H
#define JP_SUBTITLE_RENDER_H

// True if pText is well-formed UTF-8 and contains at least one non-ASCII
// codepoint. C0 control bytes (other than tab) make it return false.
// A '*' byte terminates the check, mirroring the server's use of '*' as
// the subtitle string terminator / color marker.
bool JpTextIsUtf8Japanese(const char *pText);

// ---------------------------------------------------------------------------
// Shared JP renderer state (implemented in jp_subtitle_render.cpp).
// The glyph cache and the embedded font are process-wide; several panels
// (CSubtitleTextPanel, CHUDControl) draw through these entry points.
// ---------------------------------------------------------------------------

struct JpGlyph
{
	int   cp;
	int   texId;	// VGUI1 texture id; -1 = not uploaded yet, 0 = blank
	int   w, h;		// bitmap size in pixels
	int   xoff;		// bitmap left edge relative to the pen position
	int   yoff;		// bitmap top edge relative to the baseline
	float adv;		// pen advance in pixels
};

// Initialize (or re-initialize for a new pixel height) the shared font.
bool JpSharedInitFont(int pixelHeight);
// Metrics-only glyph lookup (no surface access). NULL if the cache is full.
const JpGlyph *JpSharedGlyphMetrics(int cp);
// Decode one UTF-8 codepoint; same contract as the internal decoder.
bool JpSharedDecodeUtf8(const unsigned char *s, int len, int &pos, int &cp);
// Font ascent in pixels for the current pixel height.
int JpSharedAscentPx();
// Total advance width of a UTF-8 string (stops at '*' or NUL).
// The shared font must be initialized first.
float JpSharedMeasure(const char *text);
// Rasterize one glyph into a freshly malloc'd RGBA buffer (white * alpha).
// Fills the cache entry's w/h/xoff/yoff. *ppRgba is NULL for blank glyphs
// (caller frees the buffer). Returns false only on decode/cache failure.
bool JpSharedGlyphBitmap(int cp, unsigned char **ppRgba);
// Store a texture id created by the caller into the shared cache entry.
void JpSharedSetGlyphTexture(const JpGlyph *g, int texId);

// Japanese text of one note line: file basename (e.g. "apnote1.txt") and
// 1-based line number within that file. NULL when there is no translation.
const char *JpLookupNoteLine(const char *pszFile, int iLine);

// Japanese text of a titles.txt message: full English message text
// (newlines as \n). NULL when there is no translation.
const char *JpLookupTitle(const char *pszEnglish);

#endif // JP_SUBTITLE_RENDER_H
