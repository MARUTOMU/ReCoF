// ====================================
// PoC-2 Japanese subtitle renderer
//		UTF-8 + stb_truetype rendering for the Paranoia subtitle panels
// ====================================
//
// Design: work/poc2/hook-design.md. Summary of the choices made here:
//
// * Text flow: CSubtitle::AddMessage (vgui_subtitles.cpp) hands the panel a
//   prefix-stripped display string. SetJpText() copies it into the panel
//   (the client_textmessage_t buffer is not guaranteed to outlive the
//   panel). Measurement and paint never read the string back out of the
//   VGUI1 TextImage.
// * Gate: the panel is put on the JP path only when cvar jp_poc is 1 AND
//   the string is valid UTF-8 with non-ASCII content (decided once, in
//   AddMessage). Every failure falls back to the retail TextImage path.
// * Drawing: VGUI1 has NO sub-rectangle (UV) texture draw anywhere in
//   Panel / Surface / SurfaceBase (checked against the SDK headers in
//   src/utils/vgui/include), so the atlas design cannot be used. This
//   implements the design's fallback #1: one small VGUI1 texture per
//   glyph, drawn with drawSetTexture + drawTexturedRect. Glyphs are
//   cached process-wide and shared by all panels.
// * Texture upload is deferred to paint() time: the VGUI surface is only
//   guaranteed current during the paint traversal. Measurement (from
//   AddMessage) uses stb_truetype metrics only and touches no surface API.
// * Texture color: glyph bitmaps are expanded to RGBA as white * alpha.
//   VGUI1 textured rects are modulated by drawSetColor, which gives the
//   retail paint's shadow pass (black) and body pass (fg color + fade
//   alpha) exactly like TextImage::doPaint.
//
// Font: subset Noto Sans JP (wght=400), embedded as a byte array in
// jp_font_data.cpp (generated). SIL Open Font License 1.1.

#include "hud.h"
#include "jp_subtitle_table.h"
#include "cl_util.h"
#include "const.h"
#include "entity_types.h"
#include "cdll_int.h"
#include "vgui_TeamFortressViewport.h"
#include "vgui_subtitles.h"
#include "VGUI_TextImage.h"
#include "VGUI_Font.h"
#include "VGUI_SurfaceBase.h"	// full SurfaceBase definition; VGUI_Panel.h only forward-declares it, but JpEnsureGlyph() calls createNewTextureID() through getSurfaceBase()
#include "jp_subtitle_render.h"

#include <string.h>
#include <stdlib.h>

#include "stb_truetype.h"	// declarations only; implementation in jp_stb_impl.cpp

// Embedded subset font (jp_font_data.cpp)
extern const unsigned char g_JpFontData[];
extern const int g_JpFontDataLen;

// ---------------------------------------------------------------------------
// UTF-8 decoding / validation
// ---------------------------------------------------------------------------

// Decode one codepoint at *pos (byte offset into s[0..len)).
// On success returns true and advances pos past the sequence.
// Rejects overlong forms, surrogates and values above U+10FFFF.
static bool JpDecodeUtf8(const unsigned char *s, int len, int &pos, int &cp)
{
	if (pos >= len)
		return false;
	unsigned char c = s[pos];
	if (c < 0x80)
	{
		cp = c;
		pos += 1;
		return true;
	}
	int extra;
	unsigned int minv;
	if ((c & 0xE0) == 0xC0)      { extra = 1; cp = c & 0x1F; minv = 0x80; }
	else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; minv = 0x800; }
	else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; minv = 0x10000; }
	else
		return false;
	if (pos + extra >= len)
		return false;
	for (int i = 1; i <= extra; i++)
	{
		unsigned char cc = s[pos + i];
		if ((cc & 0xC0) != 0x80)
			return false;
		cp = (cp << 6) | (cc & 0x3F);
	}
	if ((unsigned int)cp < minv)					// overlong
		return false;
	if (cp >= 0xD800 && cp <= 0xDFFF)				// surrogate
		return false;
	if (cp > 0x10FFFF)
		return false;
	pos += extra + 1;
	return true;
}

bool JpTextIsUtf8Japanese(const char *pText)
{
	if (!pText)
		return false;
	const unsigned char *s = (const unsigned char *)pText;
	int len = (int)strlen(pText);
	int pos = 0;
	bool bNonAscii = false;
	while (pos < len)
	{
		if (s[pos] == '*')	// server-side terminator / color marker
			break;
		int cp;
		int start = pos;
		if (!JpDecodeUtf8(s, len, pos, cp))
			return false;
		if (cp > 0x7F)
			bNonAscii = true;
		else if (cp < 0x20 && cp != '\t')
			return false;	// control bytes: not displayable text
		(void)start;
	}
	return bNonAscii;
}

// Characters that must not start a line (kinsoku-lite, PoC level).
static bool JpIsLineStartProhibited(int cp)
{
	switch (cp)
	{
	case 0x3001:	// 、
	case 0x3002:	// 。
	case 0x30FB:	// ・
	case 0xFF1A:	// ：
	case 0xFF1B:	// ；
	case 0xFF1F:	// ？
	case 0xFF01:	// ！
	case 0x300D:	// 」
	case 0x300F:	// 』
	case 0xFF09:	// ）
	case 0xFF3D:	// ］
	case 0xFF05:	// ％
	case 0x30FC:	// ー
	case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:	// ぁぃぅぇぉ
	case 0x3063:	// っ
	case 0x3083: case 0x3085: case 0x3087: case 0x308E:					// ゃゅょゎ
	case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:	// ァィゥェォ
	case 0x30C3:	// ッ
	case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE:					// ャュョヮ
		return true;
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Font state and glyph cache (process-wide, shared by all panels)
// ---------------------------------------------------------------------------

// (JpGlyph is declared in jp_subtitle_render.h; the cache is shared with
// CHUDControl for the ProFont message strip.)

#define JP_MAX_GLYPHS 2048

static JpGlyph       s_glyphs[JP_MAX_GLYPHS];
static int           s_iGlyphCount = 0;
static stbtt_fontinfo s_font;
static bool          s_bFontReady = false;
static bool          s_bFontFailed = false;
static float         s_fScale = 0.0f;
static int           s_iAscentPx = 0;
static int           s_iCachePixelHeight = 0;

// Initialize (or re-initialize for a new pixel height) the font.
// Returns false permanently after the first failure; the caller then
// falls back to the retail path.
static bool JpInitFont(int pixelHeight)
{
	if (s_bFontFailed)
		return false;
	if (pixelHeight < 8)
		pixelHeight = 8;
	if (s_bFontReady && pixelHeight == s_iCachePixelHeight)
		return true;

	if (!s_bFontReady)
	{
		int offset = stbtt_GetFontOffsetForIndex(g_JpFontData, 0);
		if (offset < 0 || !stbtt_InitFont(&s_font, g_JpFontData, offset))
		{
			gEngfuncs.Con_Printf("jp_poc: embedded font init failed, JP subtitles disabled\n");
			s_bFontFailed = true;
			return false;
		}
		s_bFontReady = true;
	}

	// Pixel height changed (resolution change): drop the cache. VGUI1 has
	// no texture-release API, so the old texture ids are simply abandoned;
	// subtitle glyph counts are small, this is a PoC-acceptable leak.
	s_iGlyphCount = 0;
	s_iCachePixelHeight = pixelHeight;
	s_fScale = stbtt_ScaleForPixelHeight(&s_font, (float)pixelHeight);
	int ascent, descent, lineGap;
	stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &lineGap);
	s_iAscentPx = (int)(ascent * s_fScale + 0.5f);
	return true;
}

// Metrics-only glyph lookup (no surface access; safe from AddMessage).
// Rasterizes nothing. Returns NULL if the cache is full.
static const JpGlyph *JpGlyphMetrics(int cp)
{
	for (int i = 0; i < s_iGlyphCount; i++)
	{
		if (s_glyphs[i].cp == cp)
			return &s_glyphs[i];
	}
	if (s_iGlyphCount >= JP_MAX_GLYPHS)
		return NULL;
	JpGlyph *g = &s_glyphs[s_iGlyphCount++];
	g->cp = cp;
	g->texId = -1;
	g->w = 0;
	g->h = 0;
	g->xoff = 0;
	g->yoff = 0;
	int adv, lsb;
	stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
	g->adv = adv * s_fScale;
	return g;
}

// ---------------------------------------------------------------------------
// CSubtitleTextPanel members
// ---------------------------------------------------------------------------

void CSubtitleTextPanel::SetJpText(const char *pText)
{
	m_bJpText = false;
	m_szJpText[0] = 0;
	if (!pText)
		return;
	strncpy(m_szJpText, pText, sizeof(m_szJpText) - 1);
	m_szJpText[sizeof(m_szJpText) - 1] = 0;
	m_bJpText = JpTextIsUtf8Japanese(m_szJpText);
}

// Wrapped measurement. Fills the panel's line table as a side effect.
// Mirrors the retail contract of getTextSizeWrapped: tw = widest line,
// th = total height; AddMessage then does th += 10 and setSize as usual.
bool CSubtitleTextPanel::JpMeasureText(int &tw, int &th)
{
	Font *pFont = getTextImage()->getFont();
	int pixelHeight = pFont ? pFont->getTall() : 0;
	if (pixelHeight < 8)
		pixelHeight = 16;
	if (!JpInitFont(pixelHeight))
		return false;

	int wrapWide = getWide();
	if (wrapWide <= 0)
		return false;

	const unsigned char *s = (const unsigned char *)m_szJpText;
	int len = (int)strlen(m_szJpText);

	m_iJpLineCount = 0;
	m_iJpLineTall = pixelHeight;

	int pos = 0;
	int lineStart = 0;
	float lineWide = 0.0f;
	float maxWide = 0.0f;
	bool lineEmpty = true;

	while (pos < len)
	{
		if (s[pos] == '*')	// terminator / color marker: rest is not text
			break;
		int cp;
		int cpStart = pos;
		if (!JpDecodeUtf8(s, len, pos, cp))
			return false;	// was validated in SetJpText; defensive
		const JpGlyph *g = JpGlyphMetrics(cp);
		if (!g)
			return false;	// cache full: retail fallback

		if (cp == ' ' && lineEmpty)
		{
			lineStart = pos;	// never start a line with a space
			continue;
		}

		if (!lineEmpty && lineWide + g->adv > (float)wrapWide
			&& !JpIsLineStartProhibited(cp))
		{
			// close the current line before this codepoint
			if (m_iJpLineCount >= 32)
				return false;
			m_iJpLineStart[m_iJpLineCount] = lineStart;
			m_iJpLineLen[m_iJpLineCount] = cpStart - lineStart;
			m_iJpLineCount++;
			if (lineWide > maxWide)
				maxWide = lineWide;
			lineStart = cpStart;
			lineWide = 0.0f;
			lineEmpty = true;
			if (cp == ' ')
			{
				lineStart = pos;
				continue;
			}
		}

		lineWide += g->adv;
		lineEmpty = false;
	}

	if (!lineEmpty)
	{
		if (m_iJpLineCount >= 32)
			return false;
		m_iJpLineStart[m_iJpLineCount] = lineStart;
		m_iJpLineLen[m_iJpLineCount] = pos - lineStart;
		m_iJpLineCount++;
		if (lineWide > maxWide)
			maxWide = lineWide;
	}

	if (m_iJpLineCount == 0)
		return false;

	tw = (int)(maxWide + 0.5f);
	th = m_iJpLineCount * m_iJpLineTall;
	return true;
}

// Rasterize + upload one glyph's texture. Member function so it can use
// Panel's protected draw helpers and getSurfaceBase(). Called only from
// JpPaintText, i.e. while the surface is current. On failure the glyph is
// marked blank (texId 0) and skipped from then on.
bool CSubtitleTextPanel::JpEnsureGlyph(int cp, const JpGlyph **ppGlyph)
{
	const JpGlyph *found = JpGlyphMetrics(cp);
	if (!found)
		return false;
	*ppGlyph = found;
	if (found->texId >= 0)
		return true;

	// The cache entry is in a static array; keep a mutable pointer.
	JpGlyph *g = const_cast<JpGlyph *>(found);

	int x0, y0, x1, y1;
	stbtt_GetCodepointBitmapBox(&s_font, cp, s_fScale, s_fScale, &x0, &y0, &x1, &y1);
	g->xoff = x0;
	g->yoff = y0;
	g->w = x1 - x0;
	g->h = y1 - y0;
	if (g->w <= 0 || g->h <= 0)
	{
		g->texId = 0;	// blank glyph (space etc.)
		g->w = 0;
		g->h = 0;
		return true;
	}

	unsigned char *bitmap = (unsigned char *)malloc(g->w * g->h);
	unsigned char *rgba = (unsigned char *)malloc(g->w * g->h * 4);
	if (!bitmap || !rgba)
	{
		free(bitmap);
		free(rgba);
		g->texId = 0;
		return true;	// skip this glyph, keep painting the rest
	}
	stbtt_MakeCodepointBitmap(&s_font, bitmap, g->w, g->h, g->w, s_fScale, s_fScale, cp);
	for (int i = 0; i < g->w * g->h; i++)
	{
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = bitmap[i];
	}
	free(bitmap);

	int texId = getSurfaceBase()->createNewTextureID();
	if (texId <= 0)
	{
		free(rgba);
		g->texId = 0;
		return true;
	}
	drawSetTextureRGBA(texId, (const char *)rgba, g->w, g->h);
	free(rgba);
	g->texId = texId;
	return true;
}

// Two-pass paint with the same structure as the retail paint():
// shadow pass at (+1,+1) in black, body pass at the TextImage position,
// both with the fg color alpha that paintBackground() maintains.
bool CSubtitleTextPanel::JpPaintText()
{
	if (!JpInitFont(m_iJpLineTall))
		return false;

	int mr, mg, mb, ma;
	getFgColor(mr, mg, mb, ma);
	int ix, iy;
	getTextImage()->getPos(ix, iy);

	const unsigned char *s = (const unsigned char *)m_szJpText;

	for (int pass = 0; pass < 2; pass++)
	{
		int ox = ix + (pass == 0 ? 1 : 0);
		int oy = iy + (pass == 0 ? 1 : 0);
		int cr = (pass == 0) ? 0 : mr;
		int cg = (pass == 0) ? 0 : mg;
		int cb = (pass == 0) ? 0 : mb;

		int y = oy;
		for (int line = 0; line < m_iJpLineCount; line++)
		{
			int pos = m_iJpLineStart[line];
			int end = pos + m_iJpLineLen[line];
			float penX = (float)ox;
			int baseline = y + s_iAscentPx;

			while (pos < end)
			{
				int cp;
				if (!JpDecodeUtf8(s, end, pos, cp))
					break;	// cannot happen; string was validated
				const JpGlyph *g;
				if (!JpEnsureGlyph(cp, &g))
					return false;	// cache full: retail fallback
				if (g->texId > 0 && g->w > 0 && g->h > 0)
				{
					int dx = (int)(penX + 0.5f) + g->xoff;
					int dy = baseline + g->yoff;
					drawSetTexture(g->texId);
					drawSetColor(cr, cg, cb, ma);
					drawTexturedRect(dx, dy, dx + g->w, dy + g->h);
				}
				penX += g->adv;
			}
			y += m_iJpLineTall;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Shared entry points (used by CHUDControl for the ProFont message strip)
// ---------------------------------------------------------------------------

bool JpSharedInitFont(int pixelHeight)
{
	return JpInitFont(pixelHeight);
}

const JpGlyph *JpSharedGlyphMetrics(int cp)
{
	return JpGlyphMetrics(cp);
}

bool JpSharedDecodeUtf8(const unsigned char *s, int len, int &pos, int &cp)
{
	return JpDecodeUtf8(s, len, pos, cp);
}

int JpSharedAscentPx()
{
	return s_iAscentPx;
}

float JpSharedMeasure(const char *text)
{
	if (!text)
		return 0.0f;
	const unsigned char *s = (const unsigned char *)text;
	int len = (int)strlen(text);
	int pos = 0;
	float w = 0.0f;
	while (pos < len)
	{
		if (s[pos] == '*')
			break;
		int cp;
		if (!JpDecodeUtf8(s, len, pos, cp))
			break;
		const JpGlyph *g = JpGlyphMetrics(cp);
		if (!g)
			break;
		w += g->adv;
	}
	return w;
}

bool JpSharedGlyphBitmap(int cp, unsigned char **ppRgba)
{
	*ppRgba = NULL;
	const JpGlyph *found = JpGlyphMetrics(cp);
	if (!found)
		return false;
	JpGlyph *g = const_cast<JpGlyph *>(found);

	int x0, y0, x1, y1;
	stbtt_GetCodepointBitmapBox(&s_font, cp, s_fScale, s_fScale, &x0, &y0, &x1, &y1);
	g->xoff = x0;
	g->yoff = y0;
	g->w = x1 - x0;
	g->h = y1 - y0;
	if (g->w <= 0 || g->h <= 0)
	{
		g->w = 0;
		g->h = 0;
		return true;	// blank glyph, no bitmap
	}

	unsigned char *bitmap = (unsigned char *)malloc(g->w * g->h);
	unsigned char *rgba = (unsigned char *)malloc(g->w * g->h * 4);
	if (!bitmap || !rgba)
	{
		free(bitmap);
		free(rgba);
		g->w = 0;
		g->h = 0;
		return true;	// skip this glyph
	}
	stbtt_MakeCodepointBitmap(&s_font, bitmap, g->w, g->h, g->w, s_fScale, s_fScale, cp);
	for (int i = 0; i < g->w * g->h; i++)
	{
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = bitmap[i];
	}
	free(bitmap);
	*ppRgba = rgba;
	return true;
}

void JpSharedSetGlyphTexture(const JpGlyph *g, int texId)
{
	if (g)
		const_cast<JpGlyph *>(g)->texId = texId;
}

// ---- EN->JP lookup table (build #12: regenerated after translation review fixes)
// data generated from subtitles.txt + phone_messages.txt translations
static const JpSubPair g_jpSubTable[] =
{
	{ "\"Are you okay? You better come home now, I'm getting worried.\"", "\"\345\244\247\344\270\210\345\244\253\343\201\252\343\201\256\357\274\237\343\200\200\343\202\202\343\201\206\345\270\260\343\201\243\343\201\246\343\201\215\343\201\252\343\201\225\343\201\204\343\200\202\345\277\203\351\205\215\343\201\247\343\201\237\343\201\276\343\202\211\343\201\252\343\201\204\343\202\217\343\200\202\"" },
	{ "\"Fourth floor!!\"", "\"4\351\232\216\357\274\201\357\274\201\"" },
	{ "\"Help me!! Please help!! They are coming! I'm inside the apartments, hurry!!\"", "\"\345\212\251\343\201\221\343\201\246\357\274\201\357\274\201\343\200\200\343\201\212\351\241\230\343\201\204\343\200\201\345\212\251\343\201\221\343\201\246\357\274\201\357\274\201\343\200\200\343\201\202\343\201\204\343\201\244\343\202\211\343\201\214\346\235\245\343\202\213\357\274\201\343\200\200\343\202\242\343\203\221\343\203\274\343\203\210\343\201\256\344\270\255\343\201\253\343\201\204\343\202\213\343\201\256\343\200\201\346\227\251\343\201\217\357\274\201\357\274\201\"" },
	{ "\"In here.\"", "\"\343\201\223\343\201\223\343\202\210\343\200\202\"" },
	{ "\"Look to the right.\"", "\"\345\217\263\343\202\222\350\246\213\343\201\246\343\200\202\"" },
	{ "\"Take the stairs, up, up, up. I've got something to show you\"", "\"\351\232\216\346\256\265\343\202\222\344\270\212\343\201\214\343\201\243\343\201\246\343\200\201\344\270\212\343\201\214\343\201\243\343\201\246\343\200\201\344\270\212\343\201\214\343\201\243\343\201\246\343\200\202\350\246\213\343\201\233\343\201\237\343\201\204\343\202\202\343\201\256\343\201\214\343\201\202\343\202\213\343\201\256\"" },
	{ "\"Where are you? Come home as soon as possible. It's getting dark.\"", "\"\344\273\212\343\201\251\343\201\223\343\201\253\343\201\204\343\202\213\343\201\256\357\274\237\343\200\200\343\201\252\343\202\213\343\201\271\343\201\217\346\227\251\343\201\217\345\270\260\343\201\243\343\201\246\343\201\215\343\201\252\343\201\225\343\201\204\343\200\202\346\232\227\343\201\217\343\201\252\343\201\243\343\201\246\343\201\215\343\201\237\343\202\217\343\200\202\"" },
	{ "...I think I'm going crazy.", "\342\200\246\342\200\246\344\277\272\343\200\201\351\240\255\343\201\214\343\201\212\343\201\213\343\201\227\343\201\217\343\201\252\343\201\243\343\201\246\343\201\215\343\201\246\343\202\213\343\200\202" },
	{ "All this craziness! The darkness and the monsters...", "\343\201\223\343\201\256\347\213\202\343\201\243\343\201\237\347\212\266\346\263\201\357\274\201\343\200\200\346\232\227\351\227\207\343\202\202\345\214\226\343\201\221\347\211\251\343\202\202\342\200\246\342\200\246" },
	{ "Am I going insane? Shit!", "\344\277\272\343\201\257\347\213\202\343\201\243\343\201\246\343\201\215\343\201\246\343\202\213\343\201\256\343\201\213\357\274\237\343\200\200\343\201\217\343\201\235\343\201\243\357\274\201" },
	{ "Are you hiding up here from those things?", "\343\201\202\343\201\204\343\201\244\343\202\211\343\201\213\343\202\211\343\200\201\343\201\223\343\201\223\343\201\253\351\232\240\343\202\214\343\201\246\343\202\213\343\201\256\343\201\213\357\274\237" },
	{ "Are you interested in books, Simon?", "\346\234\254\343\201\253\350\210\210\345\221\263\343\201\257\343\201\202\343\202\212\343\201\276\343\201\231\343\201\213\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\357\274\237" },
	{ "Back to the train, sitting here like a fool...", "\351\233\273\350\273\212\343\201\253\346\210\273\343\201\243\343\201\246\343\200\201\351\246\254\351\271\277\343\201\277\343\201\237\343\201\204\343\201\253\345\272\247\343\201\243\343\201\246\343\202\213\342\200\246\342\200\246" },
	{ "Being lonely does things to you,", "\345\255\244\347\213\254\343\201\257\344\272\272\343\202\222\345\244\211\343\201\210\343\201\246\343\201\204\343\201\217\343\200\202" },
	{ "But now we're here alone, I feel... stronger.", "\343\201\247\343\202\202\344\273\212\343\200\201\343\201\223\343\201\223\343\201\247\344\272\214\344\272\272\343\201\215\343\202\212\343\201\240\343\201\250\342\200\246\342\200\246\345\274\267\343\201\217\343\201\252\343\202\214\343\201\237\346\260\227\343\201\214\343\201\231\343\202\213\343\200\202" },
	{ "CALLER: I ..ed to sp... you Simon... I'm up... roof... Opp...Park", "CALLER: \350\251\261\342\200\246\342\200\246\343\201\227\343\201\237\343\201\213\343\201\243\343\201\237\342\200\246\342\200\246\343\202\265\343\202\244\343\203\242\343\203\263\342\200\246\342\200\246\345\261\213\344\270\212\342\200\246\342\200\246\345\205\254\345\234\222\343\201\256\342\200\246\342\200\246\345\220\221\343\201\213\343\201\204\342\200\246\342\200\246" },
	{ "CALLER: S...mon... It's So... Where ... you?", "CALLER: \343\202\265\342\200\246\342\200\246\343\203\242\343\203\263\342\200\246\342\200\246\343\202\275\342\200\246\342\200\246\343\202\210\343\200\202\342\200\246\342\200\246\343\201\251\343\201\223\342\200\246\342\200\246\343\201\204\343\202\213\343\201\256\357\274\237" },
	{ "CALLER: Si...  Is th... ...ou?", "CALLER: \343\202\265\342\200\246\342\200\246\343\201\202\343\201\252\343\201\237\342\200\246\342\200\246\343\201\252\343\201\256\342\200\246\342\200\246\357\274\237" },
	{ "Christ. It looks like it. What's going on?", "\343\201\206\343\201\235\343\201\240\343\202\215\343\200\202\346\234\254\345\275\223\343\201\253\350\241\200\343\201\240\343\200\202\344\270\200\344\275\223\344\275\225\343\201\214\350\265\267\343\201\215\343\201\246\343\202\213\357\274\237" },
	{ "Could it be that I'm just hallucinating?", "\344\277\272\343\201\257\343\201\237\343\201\240\345\271\273\350\246\232\343\202\222\350\246\213\343\201\246\343\202\213\343\201\240\343\201\221\343\201\252\343\201\256\343\201\213\357\274\237" },
	{ "DAVID: Ooh shit!", "DAVID: \343\201\206\343\202\217\343\200\201\343\202\204\343\201\260\357\274\201" },
	{ "DAVID: Sorry man, I'm fucking stoned.", "DAVID: \343\201\224\343\202\201\343\202\223\343\200\201\344\277\272\343\200\201\345\256\214\345\205\250\343\201\253\343\202\255\343\203\236\343\201\243\343\201\246\343\202\213\343\202\223\343\201\240\343\200\202" },
	{ "DAVID: Wow, you ok?", "DAVID: \343\201\206\343\202\217\343\200\201\345\244\247\344\270\210\345\244\253\343\201\213\357\274\237" },
	{ "DOCTOR: All I wanted to know is where the key was!", "DOCTOR: \347\247\201\343\201\257\351\215\265\343\201\256\345\240\264\346\211\200\343\202\222\350\201\236\343\201\215\343\201\237\343\201\213\343\201\243\343\201\237\343\201\240\343\201\221\343\201\247\343\201\231\357\274\201" },
	{ "DOCTOR: Alright, I just wanted to find out, because I want you to try out something.", "DOCTOR: \343\201\204\343\201\210\343\200\201\347\242\272\350\252\215\343\201\227\343\201\237\343\201\213\343\201\243\343\201\237\343\201\240\343\201\221\343\201\247\343\201\231\343\200\202\350\251\246\343\201\227\343\201\246\343\201\273\343\201\227\343\201\204\343\201\223\343\201\250\343\201\214\343\201\202\343\201\243\343\201\246\343\200\202" },
	{ "DOCTOR: Are you stupid or something?", "DOCTOR: \345\220\233\343\201\257\351\246\254\351\271\277\343\201\252\343\201\256\343\201\247\343\201\231\343\201\213\357\274\237" },
	{ "DOCTOR: As I thought.", "DOCTOR: \343\202\204\343\201\257\343\202\212\343\201\235\343\201\206\343\201\247\343\201\231\343\201\213\343\200\202" },
	{ "DOCTOR: Come closer!", "DOCTOR: \350\277\221\343\201\245\343\201\215\343\201\252\343\201\225\343\201\204\357\274\201" },
	{ "DOCTOR: Come closer.", "DOCTOR: \343\202\202\343\201\243\343\201\250\350\277\221\343\201\245\343\201\204\343\201\246\343\201\217\343\201\240\343\201\225\343\201\204\343\200\202" },
	{ "DOCTOR: Come over here, so we can talk...", "DOCTOR: \343\201\223\343\201\243\343\201\241\343\201\270\346\235\245\343\201\246\343\201\217\343\201\240\343\201\225\343\201\204\343\200\202\350\251\261\343\202\222\343\201\227\343\201\276\343\201\227\343\202\207\343\201\206\342\200\246\342\200\246" },
	{ "DOCTOR: Good, give it to me right now, it's mine!", "DOCTOR: \343\201\204\343\201\204\343\201\247\343\201\227\343\202\207\343\201\206\343\200\201\344\273\212\343\201\231\343\201\220\346\270\241\343\201\227\343\201\252\343\201\225\343\201\204\343\200\202\347\247\201\343\201\256\343\202\202\343\201\256\343\201\247\343\201\231\357\274\201" },
	{ "DOCTOR: I can't trust you, I don't know if you're real...", "DOCTOR: \345\220\233\343\202\222\344\277\241\347\224\250\343\201\247\343\201\215\343\201\276\343\201\233\343\202\223\343\200\202\346\234\254\347\211\251\343\201\213\343\201\251\343\201\206\343\201\213\343\202\202\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\342\200\246\342\200\246" },
	{ "DOCTOR: I need a gun.", "DOCTOR: \351\212\203\343\201\214\345\277\205\350\246\201\343\201\247\343\201\231\343\200\202" },
	{ "DOCTOR: I told you to not come closer! You stupid psycho!", "DOCTOR: \350\277\221\343\201\245\343\201\217\343\201\252\343\201\250\350\250\200\343\201\243\343\201\237\343\201\257\343\201\232\343\201\247\343\201\231\357\274\201\343\200\200\343\201\223\343\201\256\351\246\254\351\271\277\343\201\252\347\213\202\344\272\272\357\274\201" },
	{ "DOCTOR: I want a new gun.", "DOCTOR: \346\226\260\343\201\227\343\201\204\351\212\203\343\201\214\343\201\273\343\201\227\343\201\204\343\202\223\343\201\247\343\201\231\343\200\202" },
	{ "DOCTOR: I'm nobody.", "DOCTOR: \344\275\225\350\200\205\343\201\247\343\202\202\343\201\202\343\202\212\343\201\276\343\201\233\343\202\223\343\200\202" },
	{ "DOCTOR: No, there's a new gun in the bowling alley.", "DOCTOR: \343\201\240\343\202\201\343\201\247\343\201\231\343\200\202\343\203\234\343\202\246\343\203\252\343\203\263\343\202\260\345\240\264\343\201\253\346\226\260\343\201\227\343\201\204\351\212\203\343\201\214\343\201\202\343\202\212\343\201\276\343\201\231\343\200\202" },
	{ "DOCTOR: No...", "DOCTOR: \343\202\204\343\200\201\343\202\204\343\202\201\342\200\246\342\200\246" },
	{ "DOCTOR: Not you again!", "DOCTOR: \343\201\276\343\201\237\345\220\233\343\201\247\343\201\231\343\201\213\357\274\201" },
	{ "DOCTOR: Relax! I'm not one of them.", "DOCTOR: \350\220\275\343\201\241\347\235\200\343\201\215\343\201\252\343\201\225\343\201\204\343\200\202\347\247\201\343\201\257\343\201\202\343\201\256\351\200\243\344\270\255\343\201\250\343\201\257\351\201\225\343\201\204\343\201\276\343\201\231\343\200\202" },
	{ "DOCTOR: Simon, the pills by itself won't make you feel better,", "DOCTOR: \343\202\265\343\202\244\343\203\242\343\203\263\343\200\201\350\226\254\343\201\240\343\201\221\343\201\247\343\201\257\350\211\257\343\201\217\343\201\252\343\202\212\343\201\276\343\201\233\343\202\223\343\200\202" },
	{ "DOCTOR: Thank you Simon, I'm glad you can trust me.", "DOCTOR: \343\201\202\343\202\212\343\201\214\343\201\250\343\201\206\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\343\200\202\344\277\241\351\240\274\343\201\227\343\201\246\343\201\217\343\202\214\343\201\246\345\254\211\343\201\227\343\201\204\343\201\247\343\201\231\343\200\202" },
	{ "DOCTOR: That's why I want you to come closer,", "DOCTOR: \343\201\240\343\201\213\343\202\211\350\277\221\343\201\245\343\201\204\343\201\246\343\201\273\343\201\227\343\201\204\343\202\223\343\201\247\343\201\231\343\200\202" },
	{ "DOCTOR: Well... It's a set of exercises you do, a way to forget the past,", "DOCTOR: \343\201\235\343\201\206\343\201\247\343\201\231\343\201\255\342\200\246\342\200\246\344\270\200\351\200\243\343\201\256\350\250\223\347\267\264\343\201\247\343\201\231\343\200\202\351\201\216\345\216\273\343\202\222\345\277\230\343\202\214\343\202\213\343\201\237\343\202\201\343\201\256\346\226\271\346\263\225\343\201\247\343\200\201" },
	{ "DOCTOR: Yes, I have the key right here.", "DOCTOR: \343\201\257\343\201\204\343\200\201\351\215\265\343\201\257\343\201\223\343\201\223\343\201\253\343\201\202\343\202\212\343\201\276\343\201\231\343\200\202" },
	{ "DOCTOR: Yes, he suffered severe spinal damage in the accident.", "DOCTOR: \343\201\257\343\201\204\343\200\201\345\275\274\343\201\257\344\272\213\346\225\205\343\201\247\346\267\261\345\210\273\343\201\252\350\204\212\351\253\204\346\220\215\345\202\267\343\202\222\350\262\240\343\201\204\343\201\276\343\201\227\343\201\237\343\200\202" },
	{ "DOCTOR: You shouldn't be here, it's very dangerous.", "DOCTOR: \343\201\223\343\201\223\343\201\253\343\201\204\343\201\246\343\201\257\343\201\204\343\201\221\343\201\276\343\201\233\343\202\223\343\200\202\351\235\236\345\270\270\343\201\253\345\215\261\351\231\272\343\201\247\343\201\231\343\200\202" },
	{ "Damn it! No signal.", "\343\201\217\343\201\235\357\274\201\343\200\200\345\234\217\345\244\226\343\201\240\343\200\202" },
	{ "Damn it! What was that? Hallucinations?", "\343\201\217\343\201\235\357\274\201\343\200\200\344\273\212\343\201\256\343\201\257\344\275\225\343\201\240\357\274\237\343\200\200\345\271\273\350\246\232\343\201\213\357\274\237" },
	{ "Damn it, he must have locked it. Why is he doing this?", "\343\201\217\343\201\235\343\200\201\351\215\265\343\202\222\343\201\213\343\201\221\343\202\204\343\201\214\343\201\243\343\201\237\343\200\202\343\201\202\343\201\204\343\201\244\343\201\257\343\201\252\343\201\234\343\201\223\343\202\223\343\201\252\343\201\223\343\201\250\343\202\222\357\274\237" },
	{ "Despite the fact that it causes him tremendous anxiety, ", "\345\275\274\343\201\253\345\274\267\343\201\204\344\270\215\345\256\211\343\202\222\343\202\202\343\201\237\343\202\211\343\201\231\343\201\253\343\202\202\343\201\213\343\201\213\343\202\217\343\202\211\343\201\232\343\200\201" },
	{ "Did he do this to himself?", "\350\207\252\345\210\206\343\201\247\343\202\204\343\201\243\343\201\237\343\201\256\343\201\213\342\200\246\342\200\246\357\274\237" },
	{ "Did he just shoot that guy? I better follow.", "\343\201\202\343\201\204\343\201\244\343\200\201\344\273\212\343\201\256\347\224\267\343\202\222\346\222\203\343\201\243\343\201\237\343\201\256\343\201\213\357\274\237\343\200\200\350\277\275\343\202\217\343\201\252\343\201\204\343\201\250\343\200\202" },
	{ "Do I look like I want to turn into one of them?", "\347\247\201\343\201\214\343\201\202\343\201\256\351\200\243\344\270\255\343\201\277\343\201\237\343\201\204\343\201\253\343\201\252\343\202\212\343\201\237\343\201\204\343\202\210\343\201\206\343\201\253\350\246\213\343\201\210\343\201\276\343\201\231\343\201\213\357\274\237" },
	{ "Do you remember how you got here?", "\343\201\223\343\201\223\343\201\270\343\201\251\343\201\206\343\202\204\343\201\243\343\201\246\346\235\245\343\201\237\343\201\213\343\200\201\350\246\232\343\201\210\343\201\246\343\202\213\343\201\213\357\274\237" },
	{ "Do you think you can do that?", "\343\201\247\343\201\215\343\201\276\343\201\231\343\201\213\357\274\237" },
	{ "Everything is just... fucked.", "\345\205\250\351\203\250\343\201\214\342\200\246\342\200\246\343\202\201\343\201\241\343\202\203\343\201\217\343\201\241\343\202\203\343\201\240\343\200\202" },
	{ "Feels like it was just a few seconds ago I was traveling here.", "\343\201\223\343\201\223\343\201\270\346\235\245\343\201\237\343\201\256\343\201\257\343\201\273\343\202\223\343\201\256\346\225\260\347\247\222\345\211\215\343\201\256\343\201\223\343\201\250\343\201\253\346\204\237\343\201\230\343\202\213\343\200\202" },
	{ "Finally home...!", "\343\202\204\343\201\243\343\201\250\345\256\266\343\201\240\342\200\246\342\200\246\357\274\201" },
	{ "From the waist down however, there is no nervous response at all,", "\343\201\227\343\201\213\343\201\227\350\205\260\343\201\213\343\202\211\344\270\213\343\201\257\343\200\201\347\245\236\347\265\214\343\201\256\345\217\215\345\277\234\343\201\214\345\205\250\343\201\217\343\201\202\343\202\212\343\201\276\343\201\233\343\202\223\343\200\202" },
	{ "From what I can tell, Simon is a deeply disturbed individual", "\347\247\201\343\201\256\350\246\213\347\253\213\343\201\246\343\201\247\343\201\257\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\343\201\257\346\267\261\343\201\217\347\227\205\343\202\223\343\201\240\344\272\272\347\211\251\343\201\247\343\200\201" },
	{ "Get it for me, now!", "\344\273\212\343\201\231\343\201\220\345\217\226\343\201\243\343\201\246\343\201\215\343\201\246\343\201\217\343\201\240\343\201\225\343\201\204\357\274\201" },
	{ "Go and get it.", "\343\201\225\343\201\202\343\200\201\350\241\214\343\201\243\343\201\246\343\201\217\343\201\240\343\201\225\343\201\204\343\200\202" },
	{ "He always goes back to the same place, day after day, just", "\345\275\274\343\201\257\346\257\216\346\227\245\345\220\214\343\201\230\345\240\264\346\211\200\343\201\253\346\210\273\343\202\212\347\266\232\343\201\221\343\200\201\343\201\237\343\201\240" },
	{ "He claims the extra medication helps.", "\345\242\227\343\201\210\343\201\237\350\226\254\343\201\214\345\212\271\343\201\204\343\201\246\343\201\204\343\202\213\343\201\250\345\275\274\343\201\257\350\250\200\343\201\206\343\200\202" },
	{ "He doesn't respond well to questions about his personal life,", "\347\247\201\347\224\237\346\264\273\343\201\253\343\201\244\343\201\204\343\201\246\350\263\252\345\225\217\343\201\225\343\202\214\343\202\213\343\201\223\343\201\250\343\202\222\345\275\274\343\201\257\345\245\275\343\201\276\343\201\232\343\200\201" },
	{ "He insists it's for \"therapeutic\" reasons, but I remain skeptical.", "\343\200\214\346\262\273\347\231\202\343\201\256\343\201\237\343\202\201\343\200\215\343\201\240\343\201\250\345\275\274\343\201\257\344\270\273\345\274\265\343\201\231\343\202\213\343\201\214\343\200\201\347\247\201\343\201\257\346\207\220\347\226\221\347\232\204\343\201\240\343\200\202" },
	{ "He's holding a gun. Better take it with me...", "\351\212\203\343\202\222\346\214\201\343\201\243\343\201\246\343\202\213\343\200\202\346\214\201\343\201\243\343\201\246\350\241\214\343\201\243\343\201\237\346\226\271\343\201\214\343\201\204\343\201\204\343\201\252\342\200\246\342\200\246" },
	{ "He's wearing a gasmask... And I'm not.", "\343\201\202\343\201\204\343\201\244\343\201\257\343\202\254\343\202\271\343\203\236\343\202\271\343\202\257\343\202\222\347\235\200\343\201\221\343\201\246\343\202\213\342\200\246\342\200\246\344\277\272\343\201\257\347\235\200\343\201\221\343\201\246\343\201\252\343\201\204\343\201\256\343\201\253\343\200\202" },
	{ "Hello!", "\343\202\202\343\201\227\343\202\202\343\201\227\357\274\201" },
	{ "His school and home-life are no-go topics when discussing these", "\345\255\246\346\240\241\343\202\204\345\256\266\345\272\255\343\201\256\350\251\261\343\201\257\343\200\201\343\201\223\343\201\206\343\201\227\343\201\237\346\204\237\346\203\205\343\202\204\344\270\215\345\256\211\343\201\253\343\201\244\343\201\204\343\201\246\350\251\261\343\201\231\351\232\233\343\201\256" },
	{ "Hmm, I should probably grab one of those lanterns...", "\343\201\202\343\201\256\343\203\251\343\203\263\343\202\277\343\203\263\343\200\201\344\270\200\343\201\244\346\214\201\343\201\243\343\201\246\350\241\214\343\201\243\343\201\237\346\226\271\343\201\214\343\201\204\343\201\204\343\201\252\342\200\246\342\200\246" },
	{ "Hmm, there's a key and a battery lying on the ground. I better take it.", "\343\202\223\343\200\201\345\272\212\343\201\253\351\215\265\343\201\250\343\203\220\343\203\203\343\203\206\343\203\252\343\203\274\343\201\214\350\220\275\343\201\241\343\201\246\343\202\213\343\200\202\346\213\276\343\201\243\343\201\246\343\201\212\343\201\223\343\201\206\343\200\202" },
	{ "Hmm. There's a key in the tub.", "\343\202\223\343\200\202\346\265\264\346\247\275\343\201\256\344\270\255\343\201\253\351\215\265\343\201\214\343\201\202\343\202\213\343\200\202" },
	{ "Hmm... No phone credit as usual. I'd better get moving, it's getting dark.", "\343\202\223\342\200\246\342\200\246\343\201\276\343\201\237\346\220\272\345\270\257\343\201\256\346\256\213\351\207\217\345\210\207\343\202\214\343\201\213\343\200\202\346\232\227\343\201\217\343\201\252\343\202\213\345\211\215\343\201\253\345\213\225\343\201\204\343\201\237\346\226\271\343\201\214\343\201\204\343\201\204\343\201\252\343\200\202" },
	{ "Hmm... Says here that I can take the subway from Hansson Square to get home.", "\343\201\265\343\202\200\342\200\246\342\200\246\343\201\223\343\201\223\343\201\253\343\202\210\343\202\213\343\201\250\343\200\201\343\203\217\343\203\263\343\202\275\343\203\263\345\272\203\345\240\264\343\201\213\343\202\211\345\234\260\344\270\213\351\211\204\343\201\253\344\271\227\343\202\214\343\201\260\345\256\266\343\201\253\345\270\260\343\202\214\343\202\213\343\202\211\343\201\227\343\201\204\343\200\202" },
	{ "Holy fuck!", "\343\201\217\343\201\235\343\201\243\357\274\201" },
	{ "Holy shit.", "\343\201\217\343\201\235\343\201\243\342\200\246\342\200\246" },
	{ "How did I get here?", "\343\201\251\343\201\206\343\202\204\343\201\243\343\201\246\343\201\223\343\201\223\343\201\253\346\235\245\343\201\237\343\202\223\343\201\240\357\274\237" },
	{ "How have you been?", "\345\205\203\346\260\227\343\201\240\343\201\243\343\201\237\357\274\237" },
	{ "How the fuck did I even get here?!", "\343\201\235\343\202\202\343\201\235\343\202\202\343\200\201\343\201\252\343\202\223\343\201\247\344\277\272\343\201\257\343\201\223\343\202\223\343\201\252\346\211\200\343\201\253\343\201\204\343\202\213\343\202\223\343\201\240\343\202\210\357\274\237\357\274\201" },
	{ "How would that solve any of the crazy shit I've seen?", "\344\277\272\343\201\214\350\246\213\343\201\237\347\213\202\343\201\243\343\201\237\345\207\272\346\235\245\344\272\213\343\201\256\344\275\225\343\201\214\350\247\243\346\261\272\343\201\231\343\202\213\343\202\223\343\201\240\357\274\237" },
	{ "Huh? It's delivered to me? I wonder what's inside.", "\343\202\223\357\274\237\343\200\200\344\277\272\345\256\233\343\201\246\343\201\256\350\215\267\347\211\251\357\274\237\343\200\200\344\270\255\350\272\253\343\201\257\344\275\225\343\201\240\343\202\215\343\201\206\343\200\202" },
	{ "Huh? What was that?", "\343\202\223\357\274\237\343\200\200\344\273\212\343\201\256\344\275\225\343\201\240\357\274\237" },
	{ "I always thought you were just a friend. Nothing more. I'm sorry.", "\343\201\232\343\201\243\343\201\250\345\217\213\351\201\224\343\201\240\343\201\250\346\200\235\343\201\243\343\201\246\343\201\237\343\200\202\343\201\235\343\202\214\344\273\245\344\270\212\343\201\230\343\202\203\343\201\252\343\201\204\343\201\256\343\200\202\343\201\224\343\202\201\343\202\223\343\201\252\343\201\225\343\201\204\343\200\202" },
	{ "I better have a look on those maps.", "\343\201\202\343\201\256\345\234\260\345\233\263\343\202\222\347\242\272\350\252\215\343\201\227\343\201\246\343\201\212\343\201\213\343\201\252\343\201\204\343\201\250\343\200\202" },
	{ "I can't even contact the police here.", "\343\201\223\343\201\223\343\201\230\343\202\203\350\255\246\345\257\237\343\201\253\343\202\202\351\200\243\347\265\241\343\201\247\343\201\215\343\201\252\343\201\204\343\200\202" },
	{ "I don't know if I like it, or if I'm just used to it,", "\343\201\235\343\202\214\343\201\214\345\245\275\343\201\215\343\201\252\343\201\256\343\201\213\343\200\201\343\201\237\343\201\240\346\205\243\343\202\214\343\201\246\343\201\227\343\201\276\343\201\243\343\201\237\343\201\256\343\201\213\343\201\257\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\343\200\202" },
	{ "I don't love you, but I think I could if we got to know each other better.", "\346\204\233\343\201\227\343\201\246\343\202\213\343\202\217\343\201\221\343\201\230\343\202\203\343\201\252\343\201\204\343\200\202\343\201\247\343\202\202\343\200\201\343\202\202\343\201\243\343\201\250\347\237\245\343\202\212\345\220\210\343\201\210\343\201\237\343\202\211\343\200\201\345\245\275\343\201\215\343\201\253\343\201\252\343\202\214\343\201\237\343\201\250\346\200\235\343\201\206\343\200\202" },
	{ "I got hit by a car, woke up, and these... things, have been trying", "\350\273\212\343\201\253\350\275\242\343\201\213\343\202\214\343\201\246\343\200\201\347\233\256\350\246\232\343\202\201\343\201\237\343\202\211\343\200\201\343\201\202\343\201\256\342\200\246\342\200\246\345\214\226\343\201\221\347\211\251\343\201\251\343\202\202\343\201\214\343\200\201\343\201\232\343\201\243\343\201\250" },
	{ "I guess I wanted to protect you.", "\345\256\210\343\202\212\343\201\237\343\201\213\343\201\243\343\201\237\343\202\223\343\201\240\343\201\250\346\200\235\343\201\206\343\200\202" },
	{ "I hope mom's okay.", "\346\257\215\343\201\225\343\202\223\343\201\214\347\204\241\344\272\213\343\201\240\343\201\250\343\201\204\343\201\204\343\201\221\343\201\251\343\200\202" },
	{ "I keep thinking back to that doctor guy.", "\343\201\202\343\201\256\345\214\273\350\200\205\343\201\256\343\201\223\343\201\250\343\201\214\351\240\255\343\201\213\343\202\211\351\233\242\343\202\214\343\201\252\343\201\204\343\200\202" },
	{ "I mean, I wasn't exactly the toughest kid in school, was I?", "\344\277\272\343\200\201\345\255\246\346\240\241\343\201\247\345\274\267\343\201\204\346\226\271\343\201\230\343\202\203\343\201\252\343\201\213\343\201\243\343\201\237\343\201\240\343\202\215\357\274\237" },
	{ "I mean, suddenly you just turn up on these rooftops?", "\343\201\240\343\201\204\343\201\237\343\201\204\343\200\201\343\201\212\345\211\215\343\201\214\343\201\204\343\201\215\343\201\252\343\202\212\343\201\223\343\202\223\343\201\252\345\261\213\344\270\212\343\201\253\347\217\276\343\202\214\343\202\213\343\201\252\343\202\223\343\201\246\357\274\237" },
	{ "I mean, what is actually happening with me?", "\344\270\200\344\275\223\343\200\201\344\277\272\343\201\253\344\275\225\343\201\214\350\265\267\343\201\215\343\201\246\343\202\213\343\202\223\343\201\240\357\274\237" },
	{ "I need to get some help, or at least just... find my way back home.", "\345\212\251\343\201\221\343\202\222\345\221\274\343\201\260\343\201\252\343\201\204\343\201\250\343\200\202\343\201\233\343\202\201\343\201\246\342\200\246\342\200\246\345\256\266\343\201\253\345\270\260\343\202\213\351\201\223\343\202\222\350\246\213\343\201\244\343\201\221\343\201\252\343\201\204\343\201\250\343\200\202" },
	{ "I never said it, but I really want to thank you for making my life that", "\350\250\200\343\201\243\343\201\237\343\201\223\343\201\250\343\201\257\343\201\252\343\201\213\343\201\243\343\201\237\343\201\221\343\201\251\343\200\201\346\204\237\350\254\235\343\201\227\343\201\246\343\202\213\343\201\256\343\200\202\347\247\201\343\201\256\344\272\272\347\224\237\343\202\222\345\260\221\343\201\227\343\201\240\343\201\221" },
	{ "I recommend that you try a new modern treatment called cognitive therapy.", "\346\226\260\343\201\227\343\201\204\347\217\276\344\273\243\347\232\204\343\201\252\346\262\273\347\231\202\346\263\225\343\200\201\343\200\214\350\252\215\347\237\245\347\231\202\346\263\225\343\200\215\343\202\222\350\251\246\343\201\231\343\201\223\343\201\250\343\202\222\345\213\247\343\202\201\343\201\276\343\201\231\343\200\202" },
	{ "I saw no evidence to suggest this was the case, but upped his", "\343\201\235\343\202\214\343\202\222\350\243\217\344\273\230\343\201\221\343\202\213\350\250\274\346\213\240\343\201\257\350\246\213\345\275\223\343\201\237\343\202\211\343\201\252\343\201\213\343\201\243\343\201\237\343\201\214\343\200\201\343\201\235\343\202\214\343\201\247\343\202\202" },
	{ "I should hurry now... Before something else happens.", "\346\200\245\343\201\214\343\201\252\343\201\204\343\201\250\342\200\246\342\200\246\343\201\276\343\201\237\344\275\225\343\201\213\350\265\267\343\201\215\343\202\213\345\211\215\343\201\253\343\200\202" },
	{ "I want to know just what in the fuck is going on here!", "\344\270\200\344\275\223\343\201\223\343\201\223\343\201\247\344\275\225\343\201\214\350\265\267\343\201\215\343\201\246\343\202\213\343\201\256\343\201\213\347\237\245\343\202\212\343\201\237\343\201\204\343\202\223\343\201\240\357\274\201" },
	{ "I want to talk.", "\350\251\261\343\201\214\343\201\227\343\201\237\343\201\204\343\202\223\343\201\247\343\201\231\343\200\202" },
	{ "I want you to write a book, a book about how you feel.", "\346\234\254\343\202\222\346\233\270\343\201\204\343\201\246\343\201\273\343\201\227\343\201\204\343\202\223\343\201\247\343\201\231\343\200\202\345\220\233\343\201\256\346\260\227\346\214\201\343\201\241\343\201\253\343\201\244\343\201\204\343\201\246\343\201\256\346\234\254\343\202\222\343\200\202" },
	{ "I won't give you the gun.", "\351\212\203\343\201\257\346\270\241\343\201\225\343\201\252\343\201\204\343\200\202" },
	{ "I wonder who that sick doctor is. He must have done something to me...", "\343\201\202\343\201\256\343\202\244\343\202\253\343\202\214\343\201\237\345\214\273\350\200\205\343\201\257\344\275\225\350\200\205\343\201\240\357\274\237\343\200\200\344\277\272\343\201\253\344\275\225\343\201\213\343\201\227\343\201\237\343\201\253\351\201\225\343\201\204\343\201\252\343\201\204\342\200\246\342\200\246" },
	{ "I'll help you open it.", "\351\226\213\343\201\221\343\202\213\343\201\256\343\202\222\346\211\213\344\274\235\343\201\204\343\201\276\343\201\231\343\201\213\343\202\211\343\200\202" },
	{ "I'll see what happens when I come home.", "\345\256\266\343\201\253\347\235\200\343\201\204\343\201\237\343\202\211\345\210\206\343\201\213\343\202\213\343\201\240\343\202\215\343\201\206\343\200\202" },
	{ "I'm confused... I'm just... very confused.", "\346\267\267\344\271\261\343\201\227\343\201\246\343\202\213\342\200\246\342\200\246\344\277\272\343\201\257\343\201\237\343\201\240\342\200\246\342\200\246\343\201\231\343\201\224\343\201\217\346\267\267\344\271\261\343\201\227\343\201\246\343\202\213\343\200\202" },
	{ "I'm going to need something big to cut free that key.", "\343\201\202\343\201\256\351\215\265\343\202\222\345\210\207\343\202\212\351\233\242\343\201\231\343\201\253\343\201\257\343\200\201\345\244\247\343\201\215\343\201\252\345\210\203\347\211\251\343\201\214\350\246\201\343\202\213\343\201\252\343\200\202" },
	{ "I'm just confused... You?", "\351\240\255\343\201\214\346\267\267\344\271\261\343\201\227\343\201\246\343\201\246\342\200\246\342\200\246\343\201\212\345\211\215\343\201\257\357\274\237" },
	{ "I'm just... thinking.", "\343\201\237\343\201\240\342\200\246\342\200\246\350\200\203\343\201\210\344\272\213\343\202\222\343\201\227\343\201\246\343\201\237\343\200\202" },
	{ "I'm so glad I found you!", "\343\201\212\345\211\215\343\201\253\344\274\232\343\201\210\343\201\246\346\234\254\345\275\223\343\201\253\350\211\257\343\201\213\343\201\243\343\201\237\357\274\201" },
	{ "I'm tired of all this...", "\344\277\272\343\201\257\343\202\202\343\201\206\347\226\262\343\202\214\343\201\237\343\202\223\343\201\240\342\200\246\342\200\246" },
	{ "I've always felt alone all my life, for as long as I can remember.", "\344\277\272\343\201\257\343\201\232\343\201\243\343\201\250\345\255\244\347\213\254\343\201\240\343\201\243\343\201\237\343\200\202\347\211\251\345\277\203\343\201\244\343\201\204\343\201\237\351\240\203\343\201\213\343\202\211\343\201\232\343\201\243\343\201\250\343\200\202" },
	{ "I've always liked you Sophie.", "\343\201\232\343\201\243\343\201\250\345\245\275\343\201\215\343\201\240\343\201\243\343\201\237\343\200\201\343\202\275\343\203\225\343\202\243\343\203\274\343\200\202" },
	{ "I've always liked you too, but... not that in that way.", "\347\247\201\343\202\202\343\201\232\343\201\243\343\201\250\345\245\275\343\201\215\343\201\240\343\201\243\343\201\237\343\200\202\343\201\247\343\202\202\342\200\246\342\200\246\343\201\235\343\201\206\343\201\204\343\201\206\346\204\217\345\221\263\343\201\230\343\202\203\343\201\252\343\201\217\343\201\246\343\200\202" },
	{ "I've been alone all this time.", "\343\201\232\343\201\243\343\201\250\344\270\200\344\272\272\343\201\240\343\201\243\343\201\237\343\201\213\343\202\211\343\200\202" },
	{ "I've got to call the police.", "\350\255\246\345\257\237\343\201\253\351\233\273\350\251\261\343\201\227\343\201\252\343\201\204\343\201\250\343\200\202" },
	{ "Is he still there?", "\343\201\276\343\201\240\343\201\204\343\202\213\343\201\256\343\201\213\357\274\237" },
	{ "Is it about that doctor guy? In the gas mask?", "\343\201\202\343\201\256\345\214\273\350\200\205\343\201\256\343\201\223\343\201\250\343\201\213\357\274\237\343\200\200\343\202\254\343\202\271\343\203\236\343\202\271\343\202\257\343\201\256\347\224\267\343\201\240\343\202\210\357\274\237" },
	{ "Is that... Blood... on the floor?", "\343\201\202\343\202\214\343\201\257\342\200\246\342\200\246\350\241\200\342\200\246\342\200\246\357\274\237\343\200\200\345\272\212\343\201\253\350\220\275\343\201\241\343\201\246\343\202\213\342\200\246\342\200\246" },
	{ "Is that... Blood?", "\343\201\202\343\202\214\343\201\257\342\200\246\342\200\246\350\241\200\357\274\237" },
	{ "Is that... a body? Christ, it smells bad.", "\343\201\202\343\202\214\343\201\257\342\200\246\342\200\246\346\255\273\344\275\223\343\201\213\357\274\237\343\200\200\343\201\217\343\201\235\343\200\201\343\201\262\343\201\251\343\201\204\350\207\255\343\201\204\343\201\240\343\200\202" },
	{ "It's a miracle he still retains upper body motion.", "\344\270\212\345\215\212\350\272\253\343\201\256\351\201\213\345\213\225\346\251\237\350\203\275\343\201\214\346\256\213\343\201\243\343\201\246\343\201\204\343\202\213\343\201\256\343\201\257\345\245\207\350\267\241\343\201\247\343\201\231\343\200\202" },
	{ "Just leave me alone!", "\344\270\200\344\272\272\343\201\253\343\201\227\343\201\246\343\201\217\343\201\240\343\201\225\343\201\204\357\274\201" },
	{ "Look what you made me do to you.", "\345\220\233\343\201\257\347\247\201\343\201\253\343\201\223\343\202\223\343\201\252\343\201\223\343\201\250\343\202\222\343\201\225\343\201\233\343\201\237\343\202\223\343\201\247\343\201\231\343\200\202" },
	{ "MAN: Help! Somebody!", "MAN: \345\212\251\343\201\221\343\201\246\357\274\201\343\200\200\350\252\260\343\201\213\357\274\201" },
	{ "Maybe I can climb down here?", "\343\201\223\343\201\223\343\201\213\343\202\211\351\231\215\343\202\212\343\202\211\343\202\214\343\202\213\343\201\213\357\274\237" },
	{ "Mom keeps telling me to get home, but, why?", "\346\257\215\343\201\225\343\202\223\343\201\257\345\256\266\343\201\253\345\270\260\343\202\214\343\201\250\350\250\200\343\201\204\347\266\232\343\201\221\343\202\213\343\201\221\343\201\251\343\200\201\343\201\252\343\201\234\357\274\237" },
	{ "Mom!", "\346\257\215\343\201\225\343\202\223\357\274\201" },
	{ "Mom!?", "\346\257\215\343\201\225\343\202\223\357\274\201\357\274\237" },
	{ "Mom...?", "\346\257\215\343\201\225\343\202\223\342\200\246\342\200\246\357\274\237" },
	{ "NO!", "\343\201\240\343\202\201\343\201\240\357\274\201" },
	{ "No no no!", "\343\201\240\343\202\201\343\201\240\343\200\201\343\201\240\343\202\201\343\201\240\343\200\201\343\201\240\343\202\201\343\201\240\357\274\201" },
	{ "No, no!", "DOCTOR: \343\202\204\343\202\201\343\201\246\343\201\217\343\202\214\343\200\201\343\202\204\343\202\201\343\201\246\343\201\217\343\202\214\357\274\201" },
	{ "No, seriously. Am I?", "\346\234\254\346\260\227\343\201\247\350\201\236\343\201\217\343\200\202\343\201\235\343\201\206\343\201\252\343\201\256\343\201\213\357\274\237" },
	{ "No, think straight...", "\343\201\240\343\202\201\343\201\240\343\200\201\345\206\267\351\235\231\343\201\253\343\201\252\343\202\214\342\200\246\342\200\246" },
	{ "No... No no no, something is obviously not right.", "\351\201\225\343\201\206\342\200\246\342\200\246\351\201\225\343\201\206\343\200\201\346\230\216\343\202\211\343\201\213\343\201\253\344\275\225\343\201\213\343\201\212\343\201\213\343\201\227\343\201\204\343\200\202" },
	{ "Now in return, here!", "\343\201\212\350\277\224\343\201\227\343\201\253\343\200\201\343\201\257\343\201\204\357\274\201" },
	{ "Now, give me the key.", "\343\201\225\343\201\202\343\200\201\351\215\265\343\202\222\343\201\217\343\202\214\343\200\202" },
	{ "Oh no...", "\343\201\235\343\202\223\343\201\252\342\200\246\342\200\246" },
	{ "Oh well. You probably would have done it to yourself anyway.", "\343\201\276\343\201\202\343\201\204\343\201\204\343\201\247\343\201\227\343\202\207\343\201\206\343\200\202\343\201\251\343\201\206\343\201\233\350\207\252\345\210\206\343\201\247\343\202\204\343\201\243\343\201\246\343\201\204\343\201\237\343\201\247\343\201\227\343\202\207\343\201\206\343\201\213\343\202\211\343\200\202" },
	{ "Oh, I'm at Kirkville now, my hometown. I hope everything is okay here...", "\343\201\202\343\201\202\343\200\201\343\201\223\343\201\223\343\201\257\343\202\253\343\203\274\343\202\257\343\203\223\343\203\253\343\200\202\344\277\272\343\201\256\345\234\260\345\205\203\343\201\240\343\200\202\343\201\223\343\201\223\343\201\257\347\204\241\344\272\213\343\201\240\343\201\250\343\201\204\343\201\204\343\201\221\343\201\251\342\200\246\342\200\246" },
	{ "Or is all this just not real?", "\343\201\235\343\202\214\343\201\250\343\202\202\343\200\201\343\201\223\343\202\214\343\201\257\345\205\250\351\203\250\347\217\276\345\256\237\343\201\230\343\202\203\343\201\252\343\201\204\343\201\256\343\201\213\357\274\237" },
	{ "Or, is there something else?", "\343\201\235\343\202\214\343\201\250\343\202\202\343\200\201\344\275\225\343\201\213\345\210\245\343\201\256\343\202\202\343\201\256\343\201\252\343\201\256\343\201\213\357\274\237" },
	{ "Pills...?", "\350\226\254\342\200\246\342\200\246\357\274\237" },
	{ "Please stand well clear of the platform and the gates,", "\343\203\233\343\203\274\343\203\240\343\201\250\343\202\262\343\203\274\343\203\210\343\201\213\343\202\211\345\215\201\345\210\206\343\201\253\351\233\242\343\202\214\343\201\246\343\201\212\345\276\205\343\201\241\343\201\217\343\201\240\343\201\225\343\201\204\343\200\202" },
	{ "SIMON: ...What the fuck?", "SIMON: \342\200\246\342\200\246\344\275\225\343\201\240\343\202\210\357\274\237" },
	{ "SIMON: Ah shit!", "SIMON: \343\201\202\343\201\202\343\200\201\343\201\217\343\201\235\357\274\201" },
	{ "SIMON: Argh!", "SIMON: \343\201\202\343\201\202\343\201\243\357\274\201" },
	{ "SIMON: But what about yourself? Why are you here?", "SIMON: \343\201\247\343\202\202\343\201\212\345\211\215\343\201\257\343\201\251\343\201\206\343\201\252\343\202\223\343\201\240\357\274\237\343\200\200\343\201\252\343\202\223\343\201\247\343\201\223\343\201\223\343\201\253\343\201\204\343\202\213\357\274\237" },
	{ "SIMON: But... Shit! Damn, so much stuff has happened lately.", "SIMON: \343\201\247\343\202\202\342\200\246\342\200\246\343\201\217\343\201\235\357\274\201\343\200\200\346\234\200\350\277\221\343\201\204\343\202\215\343\202\223\343\201\252\343\201\223\343\201\250\343\201\214\343\201\202\343\202\212\343\201\231\343\201\216\343\201\246\343\200\202" },
	{ "SIMON: Come back here! Come back!", "SIMON: \346\210\273\343\201\243\343\201\246\343\201\223\343\201\204\357\274\201\343\200\200\351\200\203\343\201\222\343\202\213\343\201\252\357\274\201" },
	{ "SIMON: Fine...", "SIMON: \345\210\206\343\201\213\343\201\243\343\201\237\343\202\210\342\200\246\342\200\246" },
	{ "SIMON: God damn it! Help me!", "SIMON: \343\201\217\343\201\235\357\274\201\343\200\200\345\212\251\343\201\221\343\201\246\343\201\217\343\202\214\357\274\201" },
	{ "SIMON: Hello?", "SIMON: \343\202\202\343\201\227\343\202\202\343\201\227\357\274\237" },
	{ "SIMON: Hello? Hello! Are you still there?", "SIMON: \343\202\202\343\201\227\343\202\202\343\201\227\357\274\237\343\200\200\343\202\202\343\201\227\343\202\202\343\201\227\357\274\201\343\200\200\343\201\276\343\201\240\343\201\204\343\202\213\343\201\256\343\201\213\357\274\237" },
	{ "SIMON: Hello? I can't hear you! Who is this?", "SIMON: \343\202\202\343\201\227\343\202\202\343\201\227\357\274\237\343\200\200\350\201\236\343\201\223\343\201\210\343\201\252\343\201\204\357\274\201\343\200\200\350\252\260\343\201\240\357\274\237" },
	{ "SIMON: Here.", "SIMON: \343\201\273\343\202\211\343\200\202" },
	{ "SIMON: Hey! What the fuck are you doing?!", "SIMON: \343\201\212\343\201\204\357\274\201\343\200\200\344\275\225\343\201\227\343\201\246\343\202\204\343\201\214\343\202\213\357\274\201\357\274\237" },
	{ "SIMON: How did you get my number? Can you help me? What the fuck is going on in this town?!", "SIMON: \343\201\252\343\202\223\343\201\247\344\277\272\343\201\256\347\225\252\345\217\267\343\202\222\347\237\245\343\201\243\343\201\246\343\202\213\357\274\237\343\200\200\345\212\251\343\201\221\343\201\246\343\201\217\343\202\214\343\202\213\343\201\256\343\201\213\357\274\237\343\200\200\343\201\223\343\201\256\350\241\227\343\201\247\344\270\200\344\275\223\344\275\225\343\201\214\350\265\267\343\201\215\343\201\246\343\202\213\343\202\223\343\201\240\357\274\237\357\274\201" },
	{ "SIMON: How?", "SIMON: \343\201\251\343\201\206\343\202\204\343\201\243\343\201\246\357\274\237" },
	{ "SIMON: Huh? Who's that!?", "SIMON: \343\202\223\357\274\237\343\200\200\350\252\260\343\201\240\357\274\201\357\274\237" },
	{ "SIMON: I can't! There's a big fucking gate in the way!", "SIMON: \347\204\241\347\220\206\343\201\240\343\202\210\357\274\201\343\200\200\343\201\247\343\201\213\343\201\204\351\226\200\343\201\214\351\202\252\351\255\224\343\201\227\343\201\246\343\202\213\357\274\201" },
	{ "SIMON: I don't know... Why?", "SIMON: \345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\342\200\246\342\200\246\343\201\252\343\201\234\357\274\237" },
	{ "SIMON: I don't think I can trust you.", "SIMON: \343\201\202\343\202\223\343\201\237\343\201\257\344\277\241\347\224\250\343\201\247\343\201\215\343\201\252\343\201\204\343\200\202" },
	{ "SIMON: I fucking love you!", "SIMON: \346\234\254\345\275\223\343\201\253\346\204\233\343\201\227\343\201\246\343\202\213\343\202\223\343\201\240\357\274\201" },
	{ "SIMON: I guess I helped you because I liked you.", "SIMON: \344\277\272\343\201\214\345\212\251\343\201\221\343\201\237\343\201\256\343\201\257\343\200\201\343\201\237\343\201\266\343\202\223\343\201\212\345\211\215\343\201\214\345\245\275\343\201\215\343\201\240\343\201\243\343\201\237\343\201\213\343\202\211\343\201\240\343\200\202" },
	{ "SIMON: I guess so... I can try...", "SIMON: \343\201\237\343\201\266\343\202\223\342\200\246\342\200\246\343\202\204\343\201\243\343\201\246\343\201\277\343\201\276\343\201\231\342\200\246\342\200\246" },
	{ "SIMON: I know... I have it already.", "SIMON: \347\237\245\343\201\243\343\201\246\343\202\213\342\200\246\342\200\246\343\202\202\343\201\206\346\214\201\343\201\243\343\201\246\343\202\213\343\202\210\343\200\202" },
	{ "SIMON: I love... I love you Sophie!", "SIMON: \345\245\275\343\201\215\343\201\240\342\200\246\342\200\246\346\204\233\343\201\227\343\201\246\343\202\213\343\200\201\343\202\275\343\203\225\343\202\243\343\203\274\357\274\201" },
	{ "SIMON: I never had the courage!", "SIMON: \345\213\207\346\260\227\343\201\214\343\201\252\343\201\213\343\201\243\343\201\237\343\202\223\343\201\240\343\202\210\357\274\201" },
	{ "SIMON: I'm close enough.", "SIMON: \345\215\201\345\210\206\350\277\221\343\201\204\343\201\240\343\202\215\343\200\202" },
	{ "SIMON: I... If I do..,", "SIMON: \344\277\272\343\201\214\342\200\246\342\200\246\343\202\202\343\201\227\343\202\204\343\201\243\343\201\237\343\202\211\343\200\201" },
	{ "SIMON: Look, just open the gate will you?", "SIMON: \343\201\204\343\201\204\343\201\213\343\202\211\343\200\201\351\226\200\343\202\222\351\226\213\343\201\221\343\201\246\343\201\217\343\202\214\343\202\210\357\274\237" },
	{ "SIMON: Oh shit!", "SIMON: \343\201\217\343\201\235\343\201\243\357\274\201" },
	{ "SIMON: Oh shit! Isn't that the key he was talking about?", "SIMON: \343\201\217\343\201\235\343\201\243\357\274\201\343\200\200\343\201\202\343\202\214\343\201\214\343\201\202\343\201\204\343\201\244\343\201\256\350\250\200\343\201\243\343\201\246\343\201\237\351\215\265\343\201\213\357\274\237" },
	{ "SIMON: Oh you're gonna regret this.", "SIMON: \345\276\214\346\202\224\343\201\225\343\201\233\343\201\246\343\202\204\343\202\213\343\200\202" },
	{ "SIMON: Oh, because I woke up myself, but it was in an alley and... ", "SIMON: \344\277\272\343\201\257\347\233\256\350\246\232\343\202\201\343\201\237\343\202\211\350\267\257\345\234\260\343\201\253\343\201\204\343\201\246\343\200\201\343\201\235\343\202\214\343\201\247\342\200\246\342\200\246" },
	{ "SIMON: Oh, but what, out here?", "SIMON: \343\201\202\343\201\202\343\200\201\343\201\247\343\202\202\343\200\201\343\201\223\343\201\223\343\201\247\343\201\213\357\274\237" },
	{ "SIMON: Oh...", "SIMON: \343\201\235\343\201\243\343\201\213\342\200\246\342\200\246" },
	{ "SIMON: Oh... Yeah... Sure.", "SIMON: \343\201\202\342\200\246\342\200\246\343\201\202\343\201\202\342\200\246\342\200\246\343\201\204\343\201\204\343\202\210\343\200\202" },
	{ "SIMON: Oh... Yeah... What?", "SIMON: \343\201\202\342\200\246\342\200\246\343\201\202\343\201\202\342\200\246\342\200\246\344\275\225\343\201\240\357\274\237" },
	{ "SIMON: Scaring? What the fuck, this is just sick!...", "SIMON: \346\200\226\343\201\204\357\274\237\343\200\200\344\275\225\350\250\200\343\201\243\343\201\246\343\202\213\343\200\201\343\201\223\343\201\256\347\212\266\346\263\201\343\200\201\347\213\202\343\201\243\343\201\246\343\202\213\343\201\240\343\202\215\357\274\201\342\200\246\342\200\246" },
	{ "SIMON: Sophie!", "SIMON: \343\202\275\343\203\225\343\202\243\343\203\274\357\274\201" },
	{ "SIMON: There you are!", "SIMON: \350\246\213\343\201\244\343\201\221\343\201\237\343\201\236\357\274\201" },
	{ "SIMON: Thinking... Yeah, I've been doing a lot of that too.", "SIMON: \350\200\203\343\201\210\344\272\213\342\200\246\342\200\246\343\201\202\343\201\202\343\200\201\344\277\272\343\202\202\346\234\200\350\277\221\343\201\232\343\201\243\343\201\250\343\201\235\343\201\206\343\201\240\343\200\202" },
	{ "SIMON: Uh...", "SIMON: \343\201\202\342\200\246\342\200\246" },
	{ "SIMON: Uh... No I don't know.", "SIMON: \343\201\202\342\200\246\342\200\246\343\201\204\343\202\204\343\200\201\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\343\200\202" },
	{ "SIMON: Wait a sec... You mean all...", "SIMON: \345\276\205\343\201\243\343\201\246\342\200\246\342\200\246\343\201\212\345\211\215\343\200\201\343\201\276\343\201\225\343\201\213\342\200\246\342\200\246" },
	{ "SIMON: Wait!", "SIMON: \345\276\205\343\201\246\357\274\201" },
	{ "SIMON: Wait. Why are you wearing a gasmask?", "SIMON: \345\276\205\343\201\246\343\202\210\343\200\202\343\201\252\343\202\223\343\201\247\343\202\254\343\202\271\343\203\236\343\202\271\343\202\257\343\201\252\343\202\223\343\201\213\347\235\200\343\201\221\343\201\246\343\202\213\343\202\223\343\201\240\357\274\237" },
	{ "SIMON: What do you think!", "SIMON: \346\261\272\343\201\276\343\201\243\343\201\246\343\202\213\343\201\240\343\202\215\357\274\201" },
	{ "SIMON: What the fuck are you talking about?! I need help, please!", "SIMON: \344\275\225\343\202\222\350\250\200\343\201\243\343\201\246\343\202\213\343\202\223\343\201\240\357\274\237\357\274\201\343\200\200\345\212\251\343\201\221\343\201\246\343\201\217\343\202\214\343\200\201\343\201\212\351\241\230\343\201\204\343\201\240\357\274\201" },
	{ "SIMON: What the fuck do you want? You're crazy!", "SIMON: \344\275\225\343\201\256\347\224\250\343\201\240\343\202\210\357\274\201\343\200\200\343\201\202\343\202\223\343\201\237\343\201\257\347\213\202\343\201\243\343\201\246\343\202\213\357\274\201" },
	{ "SIMON: What the? Sophie?!", "SIMON: \343\201\252\343\201\243\342\200\246\342\200\246\343\202\275\343\203\225\343\202\243\343\203\274\357\274\201\357\274\237" },
	{ "SIMON: What's happened?", "SIMON: \344\275\225\343\201\214\343\201\202\343\201\243\343\201\237\343\202\223\343\201\240\357\274\237" },
	{ "SIMON: What's... that?", "SIMON: \343\201\235\343\202\214\343\201\243\343\201\246\342\200\246\342\200\246\344\275\225\343\201\240\357\274\237" },
	{ "SIMON: What... I don't... I don't understand anything now...", "SIMON: \344\275\225\343\201\214\342\200\246\342\200\246\344\277\272\343\201\257\342\200\246\342\200\246\343\202\202\343\201\206\344\275\225\343\202\202\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\342\200\246\342\200\246" },
	{ "SIMON: What?", "SIMON: \344\275\225\343\201\240\343\201\243\343\201\246\357\274\237" },
	{ "SIMON: What? What do you mean?", "SIMON: \344\275\225\343\201\240\343\201\243\343\201\246\357\274\237\343\200\200\343\201\251\343\201\206\343\201\204\343\201\206\346\204\217\345\221\263\343\201\240\357\274\237" },
	{ "SIMON: What?!", "SIMON: \344\275\225\343\201\240\343\201\243\343\201\246\357\274\237\357\274\201" },
	{ "SIMON: Who are you then?", "SIMON: \343\201\230\343\202\203\343\201\202\343\200\201\343\201\202\343\202\223\343\201\237\343\201\257\344\275\225\350\200\205\343\201\240\357\274\237" },
	{ "SIMON: Yeah really... Was it you who tried to phone me by the way?", "SIMON: \346\234\254\345\275\223\343\201\253\343\201\252\342\200\246\342\200\246\343\201\235\343\201\206\343\201\204\343\201\210\343\201\260\343\200\201\344\277\272\343\201\253\351\233\273\350\251\261\343\202\222\343\201\213\343\201\221\343\202\210\343\201\206\343\201\250\343\201\227\343\201\237\343\201\256\343\201\257\343\201\212\345\211\215\343\201\213\357\274\237" },
	{ "SIMON: Yeah, I'm... I'm glad you're here too Sophie.", "SIMON: \343\201\202\343\201\202\343\200\201\344\277\272\343\202\202\342\200\246\342\200\246\343\201\212\345\211\215\343\201\214\343\201\204\343\201\246\343\201\217\343\202\214\343\201\246\345\254\211\343\201\227\343\201\204\343\202\210\343\200\201\343\202\275\343\203\225\343\202\243\343\203\274\343\200\202" },
	{ "SIMON: Yeah... Yeah, you did have it pretty rough.... Thank you.", "SIMON: \343\201\202\343\201\202\342\200\246\342\200\246\343\201\202\343\201\202\343\200\201\343\201\212\345\211\215\343\201\257\346\234\254\345\275\223\343\201\253\350\276\233\343\201\213\343\201\243\343\201\237\343\202\210\343\201\252\342\200\246\342\200\246\343\201\202\343\202\212\343\201\214\343\201\250\343\201\206\343\200\202" },
	{ "SIMON: Yes, yes I do!", "SIMON: \343\201\202\343\201\202\343\200\201\350\201\236\343\201\215\343\201\237\343\201\204\357\274\201" },
	{ "SIMON: You got a gun!", "SIMON: \351\212\203\343\201\252\343\202\211\346\214\201\343\201\243\343\201\246\343\202\213\343\201\240\343\202\215\357\274\201" },
	{ "SIMON: You're... What? You're up on the roof? I can't hear what you're saying!", "SIMON: \343\201\212\345\211\215\343\201\257\342\200\246\342\200\246\344\275\225\343\201\240\343\201\243\343\201\246\357\274\237\343\200\200\345\261\213\344\270\212\343\201\253\343\201\204\343\202\213\357\274\237\343\200\200\344\275\225\343\202\222\350\250\200\343\201\243\343\201\246\343\202\213\343\201\256\343\201\213\350\201\236\343\201\215\345\217\226\343\202\214\343\201\252\343\201\204\357\274\201" },
	{ "SOPHIE: Ah. Hi Simon.", "SOPHIE: \343\201\202\343\202\211\343\200\202\343\201\223\343\202\223\343\201\253\343\201\241\343\201\257\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\343\200\202" },
	{ "SOPHIE: Do you want to know something?", "SOPHIE: \343\201\255\343\201\210\343\200\201\347\237\245\343\202\212\343\201\237\343\201\204\357\274\237" },
	{ "SOPHIE: Doctor? Simon please. No, I mean about school.", "SOPHIE: \345\214\273\350\200\205\357\274\237\343\200\200\343\201\212\351\241\230\343\201\204\343\201\240\343\201\213\343\202\211\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\343\200\202\351\201\225\343\201\206\343\202\217\343\200\201\345\255\246\346\240\241\343\201\256\343\201\223\343\201\250\343\202\210\343\200\202" },
	{ "SOPHIE: Hmm. Let's talk about something else.", "SOPHIE: \343\201\255\343\201\210\343\200\201\345\210\245\343\201\256\350\251\261\343\202\222\343\201\227\343\201\276\343\201\227\343\202\207\343\201\206\343\200\202" },
	{ "SOPHIE: I don't know...", "SOPHIE: \345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\342\200\246\342\200\246" },
	{ "SOPHIE: I have to leave, please!", "SOPHIE: \350\241\214\343\201\213\343\201\252\343\201\204\343\201\250\343\200\201\343\201\212\351\241\230\343\201\204\357\274\201" },
	{ "SOPHIE: I know, a lot of things have been going on lately.", "SOPHIE: \345\210\206\343\201\213\343\202\213\343\200\202\346\234\200\350\277\221\343\200\201\346\234\254\345\275\223\343\201\253\343\201\204\343\202\215\343\201\204\343\202\215\343\201\202\343\201\243\343\201\237\343\202\202\343\201\256\343\201\255\343\200\202" },
	{ "SOPHIE: I see, sounds tough.", "SOPHIE: \343\201\235\343\201\206\343\200\201\345\244\247\345\244\211\343\201\240\343\201\243\343\201\237\343\201\255\343\200\202" },
	{ "SOPHIE: Just let me go Simon!", "SOPHIE: \351\233\242\343\201\227\343\201\246\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\357\274\201" },
	{ "SOPHIE: Mmm.", "SOPHIE: \343\201\206\343\202\223\343\200\202" },
	{ "SOPHIE: No, I really don't understand what you're talking about.", "SOPHIE: \343\201\204\343\201\204\343\201\210\343\200\201\343\201\202\343\201\252\343\201\237\343\201\214\344\275\225\343\202\222\350\250\200\343\201\243\343\201\246\343\202\213\343\201\256\343\201\213\346\234\254\345\275\223\343\201\253\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\343\201\256\343\200\202" },
	{ "SOPHIE: No, hush Simon. Can't we just sit here for a while?", "SOPHIE: \343\201\240\343\202\201\343\200\201\351\235\231\343\201\213\343\201\253\343\201\227\343\201\246\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\343\200\202\343\201\227\343\201\260\343\202\211\343\201\217\343\201\223\343\201\223\343\201\253\345\272\247\343\201\243\343\201\246\343\201\204\343\201\276\343\201\227\343\202\207\343\201\206\357\274\237" },
	{ "SOPHIE: No... Away from everything, away from all this.", "SOPHIE: \351\201\225\343\201\206\343\201\256\342\200\246\342\200\246\345\205\250\351\203\250\343\201\213\343\202\211\351\201\240\343\201\217\343\201\270\343\200\202\343\201\223\343\201\256\345\205\250\351\203\250\343\201\213\343\202\211\343\200\202" },
	{ "SOPHIE: Not too bad thank you. Better now that you're here.", "SOPHIE: \346\202\252\343\201\217\343\201\252\343\201\204\343\202\217\343\200\202\343\201\202\343\201\252\343\201\237\343\201\214\346\235\245\343\201\246\343\201\217\343\202\214\343\201\246\343\200\201\343\202\202\343\201\243\343\201\250\350\211\257\343\201\217\343\201\252\343\201\243\343\201\237\343\201\221\343\201\251\343\200\202" },
	{ "SOPHIE: Oh, Simon... I don't know what to say...", "SOPHIE: \343\201\202\343\201\202\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\342\200\246\342\200\246\344\275\225\343\201\246\350\250\200\343\201\210\343\201\260\343\201\204\343\201\204\343\201\213\342\200\246\342\200\246" },
	{ "SOPHIE: Simon, let me go!", "SOPHIE: \343\202\265\343\202\244\343\203\242\343\203\263\343\200\201\351\233\242\343\201\227\343\201\246\357\274\201" },
	{ "SOPHIE: Uh... Monsters? Simon? You're scaring me...", "SOPHIE: \343\201\210\342\200\246\342\200\246\345\214\226\343\201\221\347\211\251\357\274\237\343\200\200\343\202\265\343\202\244\343\203\242\343\203\263\357\274\237\343\200\200\346\200\226\343\201\204\343\202\210\342\200\246\342\200\246" },
	{ "SOPHIE: Well, I dunno if you knew but I always had a hard time at school.", "SOPHIE: \347\237\245\343\201\243\343\201\246\343\201\237\343\201\213\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\343\201\221\343\201\251\343\200\201\347\247\201\343\200\201\345\255\246\346\240\241\343\201\247\343\201\257\343\201\232\343\201\243\343\201\250\350\276\233\343\201\213\343\201\243\343\201\237\343\201\256\343\200\202" },
	{ "SOPHIE: What is it that you don't understand?", "SOPHIE: \344\275\225\343\201\214\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\343\201\256\357\274\237" },
	{ "SOPHIE: Why haven't you said anything?", "SOPHIE: \343\201\252\343\202\223\343\201\247\344\275\225\343\202\202\350\250\200\343\201\243\343\201\246\343\201\217\343\202\214\343\201\252\343\201\213\343\201\243\343\201\237\343\201\256\357\274\237" },
	{ "SOPHIE: Yeah...", "SOPHIE: \343\201\206\343\202\223\342\200\246\342\200\246" },
	{ "SOPHIE: Yeah... I wanted to talk to you. To get away from it all.", "SOPHIE: \343\201\206\343\202\223\342\200\246\342\200\246\343\201\202\343\201\252\343\201\237\343\201\250\350\251\261\343\201\227\343\201\237\343\201\213\343\201\243\343\201\237\343\201\256\343\200\202\345\205\250\351\203\250\343\201\213\343\202\211\351\200\203\343\201\222\343\201\237\343\201\217\343\201\246\343\200\202" },
	{ "SOPHIE: You know full well what I mean.", "SOPHIE: \345\210\206\343\201\213\343\201\243\343\201\246\343\202\213\343\201\257\343\201\232\343\202\210\343\200\201\347\247\201\343\201\256\350\250\200\343\201\204\343\201\237\343\201\204\343\201\223\343\201\250\343\200\202" },
	{ "SPEAKER: The train to Norcastle is delayed due to a signal malfunction.", "SPEAKER: \343\203\216\343\202\242\343\202\255\343\203\243\343\203\203\343\202\271\343\203\253\350\241\214\343\201\215\343\201\256\351\233\273\350\273\212\343\201\257\343\200\201\344\277\241\345\217\267\346\225\205\351\232\234\343\201\256\343\201\237\343\202\201\351\201\205\343\202\214\343\201\246\343\201\204\343\201\276\343\201\231\343\200\202" },
	{ "Seeing as he wears a gasmask.", "\343\201\202\343\201\204\343\201\244\343\201\257\343\202\254\343\202\271\343\203\236\343\202\271\343\202\257\343\202\222\347\235\200\343\201\221\343\201\246\343\202\213\343\201\213\343\202\211\343\201\252\343\200\202" },
	{ "Shit, my bag!", "\343\201\217\343\201\235\343\200\201\344\277\272\343\201\256\343\203\220\343\203\203\343\202\260\357\274\201" },
	{ "So I need to head to Saxon Avenue first...", "\343\201\276\343\201\232\343\201\257\343\202\265\343\202\257\343\202\275\343\203\263\343\203\273\343\202\242\343\203\231\343\203\213\343\203\245\343\203\274\343\201\253\350\241\214\343\201\213\343\201\252\343\201\204\343\201\250\342\200\246\342\200\246" },
	{ "So many questions, but no answers.", "\347\226\221\345\225\217\343\201\260\343\201\213\343\202\212\343\201\247\343\200\201\347\255\224\343\201\210\343\201\214\343\201\252\343\201\204\343\200\202" },
	{ "Sophie... Why did she?", "\343\202\275\343\203\225\343\202\243\343\203\274\342\200\246\342\200\246\343\201\252\343\201\234\345\275\274\345\245\263\343\201\214\357\274\237" },
	{ "Still no change...", "\343\201\276\343\201\240\344\275\225\343\202\202\342\200\246\342\200\246" },
	{ "Take the key.", "\351\215\265\343\202\222\345\217\227\343\201\221\345\217\226\343\202\212\343\201\252\343\201\225\343\201\204\343\200\202" },
	{ "That is one creepy fucking painting.", "\343\201\252\343\202\223\343\201\246\344\270\215\346\260\227\345\221\263\343\201\252\347\265\265\343\201\240\343\202\210\343\200\202" },
	{ "That was close.", "\345\215\261\343\201\252\343\201\213\343\201\243\343\201\237\343\200\202" },
	{ "The power's charging. I'd better get to somewhere safe.", "\345\205\205\351\233\273\343\201\214\345\247\213\343\201\276\343\201\243\343\201\237\343\200\202\345\256\211\345\205\250\343\201\252\345\240\264\346\211\200\343\201\253\350\241\214\343\201\213\343\201\252\343\201\204\343\201\250\343\200\202" },
	{ "The subway, the guy you shot, remember?", "\345\234\260\344\270\213\351\211\204\343\201\247\343\200\201\343\201\202\343\202\223\343\201\237\343\201\214\346\222\203\343\201\243\343\201\237\347\224\267\343\201\256\343\202\204\343\201\244\343\201\240\343\200\202\350\246\232\343\201\210\343\201\246\343\202\213\343\201\213\357\274\237" },
	{ "The whole fucking city is crawling with... monsters, and I've just...", "\350\241\227\344\270\255\343\201\214\342\200\246\342\200\246\345\214\226\343\201\221\347\211\251\343\201\240\343\202\211\343\201\221\343\201\247\343\200\201\344\277\272\343\201\257\343\201\225\343\201\243\343\201\215\342\200\246\342\200\246" },
	{ "Then, I'll give you the key.", "\343\201\235\343\202\214\343\201\213\343\202\211\351\215\265\343\202\222\346\270\241\343\201\227\343\201\276\343\201\231\343\200\202" },
	{ "There is still no change in you Simon...", "\345\220\233\343\201\257\343\201\276\343\201\240\344\275\225\343\202\202\345\244\211\343\202\217\343\201\243\343\201\246\343\201\204\343\201\252\343\201\204\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\342\200\246\342\200\246" },
	{ "There's no ammo in it though.", "\343\201\247\343\202\202\345\274\276\343\201\257\345\205\245\343\201\243\343\201\246\343\201\252\343\201\204\343\200\202" },
	{ "There's something in the ceiling.", "\345\244\251\344\272\225\343\201\253\344\275\225\343\201\213\343\201\204\343\202\213\343\200\202" },
	{ "This place doesn't feel right.", "\343\201\223\343\201\256\345\240\264\346\211\200\343\200\201\344\275\225\343\201\213\343\201\212\343\201\213\343\201\227\343\201\204\343\200\202" },
	{ "This... this isn't making any sense!", "\343\201\223\343\202\214\342\200\246\342\200\246\343\201\223\343\202\223\343\201\252\343\201\256\343\200\201\346\204\217\345\221\263\343\201\214\345\210\206\343\201\213\343\202\211\343\201\252\343\201\204\357\274\201" },
	{ "Uh..Oh... Did I pass out?", "\343\201\202\342\200\246\342\200\246\343\201\206\342\200\246\342\200\246\346\260\227\347\265\266\343\201\227\343\201\246\343\201\237\343\201\256\343\201\213\357\274\237" },
	{ "VOICE: 5...2...4...7", "VOICE: 5\342\200\246\342\200\2462\342\200\246\342\200\2464\342\200\246\342\200\2467" },
	{ "VOICE: Follow the path of inequity.", "VOICE: \344\270\215\347\276\251\343\201\256\351\201\223\343\202\222\351\200\262\343\202\201\343\200\202" },
	{ "VOICE: The number... Remember the number, and you shall proceed.", "VOICE: \346\225\260\345\255\227\343\202\222\342\200\246\342\200\246\346\225\260\345\255\227\343\202\222\350\246\232\343\201\210\343\202\210\343\200\202\343\201\235\343\201\206\343\201\231\343\202\214\343\201\260\351\200\262\343\202\201\343\202\213\343\200\202" },
	{ "Wait, I can probably show you!", "\345\276\205\343\201\243\343\201\246\343\200\201\350\246\213\343\201\233\343\202\211\343\202\214\343\202\213\343\201\213\343\202\202\343\201\227\343\202\214\343\201\252\343\201\204\357\274\201" },
	{ "Warning: Low battery", "\350\255\246\345\221\212\357\274\232\343\203\220\343\203\203\343\203\206\343\203\252\343\203\274\346\256\213\351\207\217\344\275\216\344\270\213" },
	{ "Was it you who made her do it?", "\343\201\212\345\211\215\343\201\214\345\275\274\345\245\263\343\201\253\343\202\204\343\202\211\343\201\233\343\201\237\343\201\256\343\201\213\357\274\237" },
	{ "Was it you who made her do it? Why? Why?! You sick fuck!", "\343\201\212\345\211\215\343\201\214\345\275\274\345\245\263\343\201\253\343\202\204\343\202\211\343\201\233\343\201\237\343\201\256\343\201\213\357\274\237\343\200\200\343\201\252\343\201\234\343\201\240\357\274\237\343\200\200\343\201\252\343\201\234\343\201\252\343\202\223\343\201\240\357\274\201\357\274\237\343\200\200\343\201\223\343\201\256\347\213\202\343\201\243\343\201\237\351\207\216\351\203\216\357\274\201" },
	{ "Wasn't this where I came from?", "\343\201\223\343\201\223\343\201\257\344\277\272\343\201\214\346\235\245\343\201\237\345\240\264\346\211\200\343\201\230\343\202\203\343\201\252\343\201\213\343\201\243\343\201\237\343\201\213\357\274\237" },
	{ "Well I... Maybe it's just...", "\343\201\204\343\202\204\343\200\201\344\277\272\343\201\257\342\200\246\342\200\246\343\202\202\343\201\227\343\201\213\343\201\227\343\201\237\343\202\211\342\200\246\342\200\246" },
	{ "Well he is crazy alright, but could he have done something to me?", "\343\201\202\343\201\204\343\201\244\343\201\257\347\242\272\343\201\213\343\201\253\347\213\202\343\201\243\343\201\246\343\202\213\343\200\202\343\201\247\343\202\202\343\200\201\344\277\272\343\201\253\344\275\225\343\201\213\343\201\227\343\201\237\343\201\256\343\201\213\357\274\237" },
	{ "What are you doing here? Are you hurt?", "\344\275\225\343\201\227\343\201\246\343\202\213\343\202\223\343\201\240\357\274\237\343\200\200\346\200\252\346\210\221\343\201\257\343\201\252\343\201\204\343\201\213\357\274\237" },
	{ "What the fuck...", "\344\275\225\343\201\240\343\202\210\343\200\201\343\201\223\343\202\214\343\201\257\342\200\246\342\200\246" },
	{ "What the fuck?", "\344\275\225\343\201\240\343\202\210\357\274\237" },
	{ "What the hell are you doing here?!", "\343\201\212\345\211\215\343\200\201\343\201\252\343\202\223\343\201\247\343\201\223\343\202\223\343\201\252\346\211\200\343\201\253\343\201\204\343\202\213\343\202\223\343\201\240\343\202\210\357\274\237\357\274\201" },
	{ "What the hell just happened, what is all this? What the fuck!", "\344\273\212\343\201\256\343\201\257\344\275\225\343\201\240\343\200\201\344\275\225\343\201\214\350\265\267\343\201\215\343\201\237\343\202\223\343\201\240\357\274\237\343\200\200\343\201\265\343\201\226\343\201\221\343\202\213\343\201\252\357\274\201" },
	{ "What the hell was that?", "\344\273\212\343\201\256\343\201\257\344\270\200\344\275\223\344\275\225\343\201\240\343\201\243\343\201\237\343\202\223\343\201\240\357\274\237" },
	{ "What the hell! What is this place?", "\344\275\225\343\201\240\343\202\210\357\274\201\343\200\200\343\201\223\343\201\223\343\201\257\344\270\200\344\275\223\344\275\225\343\201\252\343\202\223\343\201\240\357\274\237" },
	{ "What the hell! Who was he?", "\344\275\225\343\201\252\343\202\223\343\201\240\343\202\210\357\274\201\343\200\200\343\201\202\343\201\204\343\201\244\343\201\257\350\252\260\343\201\240\357\274\237" },
	{ "What the...", "\343\201\252\343\200\201\343\201\252\343\202\223\343\201\240\342\200\246\342\200\246" },
	{ "Where am I?", "\343\201\223\343\201\223\343\201\257\342\200\246\342\200\246\343\201\251\343\201\223\343\201\240\357\274\237" },
	{ "Where are you!?", "\343\201\251\343\201\223\343\201\253\343\201\204\343\202\213\343\201\256\357\274\201\357\274\237" },
	{ "Who is this?", "\350\252\260\343\201\240\357\274\237" },
	{ "Who was he? Nobody?", "\343\201\202\343\201\204\343\201\244\343\200\201\344\275\225\350\200\205\343\201\240\357\274\237\343\200\200\344\275\225\350\200\205\343\201\247\343\202\202\343\201\252\343\201\204\343\200\201\343\201\240\343\201\250\357\274\237" },
	{ "Why!?", "\343\201\252\343\201\234\343\201\240\357\274\201\357\274\237" },
	{ "Why? Why!?", "\343\201\252\343\201\234\343\201\240\357\274\237\343\200\200\343\201\252\343\201\234\343\201\252\343\202\223\343\201\240\357\274\201\357\274\237" },
	{ "Yield not to the light,", "\345\205\211\343\201\253\345\261\210\343\201\231\343\202\213\343\201\252\343\200\202" },
	{ "You crazy bastard, I'll kill you!", "\343\201\223\343\201\256\347\213\202\343\201\243\343\201\237\351\207\216\351\203\216\343\200\201\346\256\272\343\201\227\343\201\246\343\202\204\343\202\213\357\274\201" },
	{ "You sick fuck!", "\343\201\223\343\201\256\347\213\202\343\201\243\343\201\237\351\207\216\351\203\216\357\274\201" },
	{ "You unlocked the lock.", "\351\214\240\343\201\214\351\226\213\343\201\204\343\201\237\343\200\202" },
	{ "You were one of the prettiest girls at school and I dunno, I just...", "\343\201\212\345\211\215\343\201\257\345\255\246\346\240\241\343\201\247\344\270\200\347\225\252\345\217\257\346\204\233\343\201\204\345\255\220\343\201\256\344\270\200\344\272\272\343\201\240\343\201\243\343\201\237\343\201\227\343\200\201\344\277\272\343\201\257\343\201\237\343\201\240\342\200\246\342\200\246" },
	{ "You were... Well, you were pretty much the only guy who was kind to me.", "\343\201\202\343\201\252\343\201\237\343\201\257\342\200\246\342\200\246\343\201\206\343\202\223\343\200\201\347\247\201\343\201\253\345\204\252\343\201\227\343\201\217\343\201\227\343\201\246\343\201\217\343\202\214\343\201\237\343\201\256\343\201\257\343\200\201\343\201\202\343\201\252\343\201\237\343\201\217\343\202\211\343\201\204\343\201\240\343\201\243\343\201\237\343\200\202" },
	{ "You're dead!", "\346\255\273\343\201\255\357\274\201" },
	{ "You're gonna regret this BIG TIME!", "\347\265\266\345\257\276\343\201\253\345\276\214\346\202\224\343\201\225\343\201\233\343\201\246\343\202\204\343\202\213\357\274\201" },
	{ "You're so beautiful Sophie...", "\346\234\254\345\275\223\343\201\253\347\266\272\351\272\227\343\201\240\343\202\210\343\200\201\343\202\275\343\203\225\343\202\243\343\203\274\342\200\246\342\200\246" },
	{ "You're so fucking dead!", "\347\265\266\345\257\276\343\201\253\346\256\272\343\201\227\343\201\246\343\202\204\343\202\213\357\274\201" },
	{ "You...?", "\343\201\212\345\211\215\342\200\246\342\200\246\357\274\237" },
	{ "all the bad memories and anxiety you have.", "\345\220\233\343\201\256\346\202\252\343\201\204\350\250\230\346\206\266\343\202\204\344\270\215\345\256\211\343\202\222\346\266\210\343\201\227\343\201\246\343\201\204\343\201\217\343\202\202\343\201\256\343\201\247\343\201\231\343\200\202" },
	{ "and became extremely angry when I mentioned events prior to what", "\345\275\274\343\201\214\343\200\214\351\273\222\343\201\256\346\227\245\343\200\215\343\201\250\345\221\274\343\201\263\347\266\232\343\201\221\343\202\213\345\207\272\346\235\245\344\272\213\343\202\210\343\202\212\345\211\215\343\201\256\343\201\223\343\201\250\343\201\253\350\247\246\343\202\214\343\202\213\343\201\250" },
	{ "and feeling shit and bitter and angry all the time", "\346\203\250\343\202\201\343\201\247\343\200\201\350\213\246\343\201\217\343\201\246\343\200\201\346\200\222\343\202\212\343\201\253\346\272\200\343\201\241\343\201\237\346\260\227\346\214\201\343\201\241\343\201\214\343\201\232\343\201\243\343\201\250\347\266\232\343\201\217\343\201\250\343\200\201" },
	{ "as the next train is not for public use.", "\346\254\241\343\201\256\351\233\273\350\273\212\343\201\257\344\270\200\350\210\254\343\201\256\346\226\271\343\201\257\343\201\224\345\210\251\347\224\250\343\201\247\343\201\215\343\201\276\343\201\233\343\202\223\343\200\202" },
	{ "but I do know this:", "\343\201\247\343\202\202\343\200\201\343\201\223\343\202\214\343\201\240\343\201\221\343\201\257\345\210\206\343\201\213\343\202\213\343\200\202" },
	{ "darkness is your new master.", "\351\227\207\343\201\223\343\201\235\343\201\214\343\201\212\345\211\215\343\201\256\346\226\260\343\201\227\343\201\204\344\270\273\344\272\272\343\201\240\343\200\202" },
	{ "feelings and anxieties. He told me the other day that he'd been", "\347\246\201\346\255\242\344\272\213\351\240\205\343\201\240\343\200\202\345\205\210\346\227\245\345\275\274\343\201\257\343\200\201\345\271\273\350\246\232\343\202\222\350\246\213\343\201\246\343\201\204\343\202\213\343\201\250\347\247\201\343\201\253\350\250\200\343\201\243\343\201\237\343\201\214\343\200\201" },
	{ "he insists on describing as \"the black day\". ", "\346\277\200\343\201\227\343\201\217\346\200\222\343\202\212\345\207\272\343\201\227\343\201\237\343\200\202" },
	{ "he insists on returning.", "\345\275\274\343\201\257\346\210\273\343\202\213\343\201\223\343\201\250\343\202\222\343\202\204\343\202\201\343\202\210\343\201\206\343\201\250\343\201\227\343\201\252\343\201\204\343\200\202" },
	{ "just eats away at you.", "\345\260\221\343\201\227\343\201\232\343\201\244\350\207\252\345\210\206\343\202\222\350\235\225\343\202\223\343\201\247\343\201\204\343\201\217\343\200\202" },
	{ "little bit more bearable. You're a cute guy, Simon.", "\347\224\237\343\201\215\343\202\204\343\201\231\343\201\217\343\201\227\343\201\246\343\201\217\343\202\214\343\201\246\343\200\202\343\201\202\343\201\252\343\201\237\343\201\243\343\201\246\347\264\240\346\225\265\343\201\252\344\272\272\343\202\210\343\200\201\343\202\265\343\202\244\343\203\242\343\203\263\343\200\202" },
	{ "not to mention the mental trauma he's suffered.", "\345\275\274\343\201\214\350\262\240\343\201\243\343\201\237\347\262\276\347\245\236\347\232\204\343\203\210\343\203\251\343\202\246\343\203\236\343\201\257\350\250\200\343\201\206\343\201\276\343\201\247\343\202\202\343\201\202\343\202\212\343\201\276\343\201\233\343\202\223\343\200\202" },
	{ "prescription anyway. ", "\345\207\246\346\226\271\343\202\222\345\242\227\343\202\204\343\201\227\343\201\246\343\201\212\343\201\204\343\201\237\343\200\202" },
	{ "seeing hallucinations, but couldn't give a clear description of", "\344\275\225\343\202\222\350\246\213\343\201\246\343\201\204\343\202\213\343\201\256\343\201\213\343\201\257\343\201\243\343\201\215\343\202\212\343\201\250\343\201\257\350\252\254\346\230\216\343\201\247\343\201\215\343\201\252\343\201\213\343\201\243\343\201\237\343\200\202" },
	{ "testing 1", "testing 1" },
	{ "testing 2", "testing 2" },
	{ "testing 3", "testing 3" },
	{ "testing 4", "testing 4" },
	{ "to kill me since.", "\344\277\272\343\202\222\346\256\272\343\201\235\343\201\206\343\201\250\343\201\227\343\201\246\343\202\213\343\200\202" },
	{ "watching it like it was yesterday. ", "\343\201\235\343\202\214\343\202\222\346\230\250\346\227\245\343\201\256\343\201\223\343\201\250\343\201\256\343\202\210\343\201\206\343\201\253\350\246\213\343\201\244\343\202\201\343\201\246\343\201\204\343\202\213\343\200\202" },
	{ "what he'd been seeing. Naturally, I didn't take him seriously.", "\345\275\223\347\204\266\343\200\201\347\247\201\343\201\257\346\234\254\346\260\227\343\201\253\343\201\227\343\201\252\343\201\213\343\201\243\343\201\237\343\200\202" },
	{ "who had mental problems even prior to the accident.", "\344\272\213\346\225\205\343\201\256\345\211\215\343\201\213\343\202\211\347\262\276\347\245\236\347\232\204\343\201\252\345\225\217\351\241\214\343\202\222\346\212\261\343\201\210\343\201\246\343\201\204\343\201\276\343\201\227\343\201\237\343\200\202" },
	{ "will you open the gate for me?", "\351\226\200\343\202\222\351\226\213\343\201\221\343\201\246\343\201\217\343\202\214\343\202\213\343\201\256\343\201\213\357\274\237" },
	{ "you have to do something more than that.", "\343\201\235\343\202\214\344\273\245\344\270\212\343\201\256\343\201\223\343\201\250\343\202\222\343\201\227\343\201\252\343\201\221\343\202\214\343\201\260\343\200\202" },
};

const char *JpLookupSubtitle( const char *pszEnglish )
{
	const unsigned int n = sizeof(g_jpSubTable) / sizeof(g_jpSubTable[0]);
	unsigned int lo = 0, hi = n;
	while ( lo < hi )
	{
		unsigned int mid = ( lo + hi ) / 2;
		const char *a = pszEnglish, *b = g_jpSubTable[mid].en;
		while ( *a && *b && *a == *b ) { a++; b++; }
		int c = (int)(unsigned char)*a - (int)(unsigned char)*b;
		if ( c == 0 ) return g_jpSubTable[mid].jp;
		if ( c < 0 ) hi = mid; else lo = mid + 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// JP note lines (translation/ja/notes, keyed by file basename + line number)
// ---------------------------------------------------------------------------

struct JpNoteEntry
{
	const char *pszFile;
	int         iLine;
	const char *pszText;
};

// JP note lines: { file basename, 1-based line number, UTF-8 text }
static const JpNoteEntry g_jpNotes[] = {
	{ "apnote1.txt", 1, "\347\247\230\345\257\206\343\201\256\343\203\241\343\203\242 #1" },
	{ "apnote1.txt", 2, "\345\205\254\345\234\222\343\201\247\351\201\212\343\201\2663\344\272\272\343\201\256\345\260\217\343\201\225\343\201\252\345\255\220\344\276\233\343\201\237\343\201\241" },
	{ "apnote1.txt", 3, "\344\270\200\344\272\272\347\233\256\343\201\256\345\255\220\343\201\214\350\250\200\343\201\243\343\201\237\343\200\202\343\200\214\346\232\227\343\201\217\343\201\252\343\202\213\345\211\215\343\201\253\345\270\260\343\202\215\343\201\206\343\202\210\343\200\215" },
	{ "apnote1.txt", 4, "\346\234\200\345\210\235\343\201\256\344\272\214\344\272\272\343\201\257\346\245\275\343\201\227\343\201\235\343\201\206\343\201\253\345\256\266\343\201\270\345\270\260\343\201\243\343\201\246\343\201\204\343\201\243\343\201\237\343\200\202" },
	{ "apnote1.txt", 5, "\344\270\211\344\272\272\347\233\256\343\201\257\346\256\213\343\201\243\343\201\246\351\201\212\343\202\223\343\201\247\343\201\204\343\201\237\343\200\202\343\201\237\343\201\243\343\201\237\344\270\200\344\272\272\343\201\247" },
	{ "apnote1.txt", 6, "\343\201\223\343\201\243\343\201\241\343\201\270\343\201\212\343\201\204\343\201\247\343\200\201\345\260\217\343\201\225\343\201\252\345\255\220\343\200\202\347\247\201\343\201\256\343\201\250\343\201\223\343\202\215\343\201\270\343\201\212\343\201\204\343\201\247\357\274\201" },
	{ "apnote1.txt", 7, "\350\214\202\343\201\277\343\201\214\343\201\235\343\201\243\343\201\250\343\201\225\343\201\225\343\202\204\343\201\204\343\201\237" },
	{ "apnote1.txt", 8, "\345\245\275\345\245\207\345\277\203\343\201\257\347\214\253\343\202\222\343\202\202\346\256\272\343\201\231\343\201\250\350\250\200\343\201\206\343\201\221\343\202\214\343\201\251\342\200\246\342\200\246" },
	{ "apnote2.txt", 1, "\347\247\230\345\257\206\343\201\256\343\203\241\343\203\242 #2" },
	{ "apnote2.txt", 2, "\343\201\202\343\201\256\345\260\217\343\201\225\343\201\252\345\255\220\344\276\233\343\201\237\343\201\241\343\201\257\346\234\254\345\275\223\343\201\253\343\201\213\343\202\217\343\201\204\343\201\204\343\200\202" },
	{ "apnote2.txt", 3, "\343\201\250\343\201\246\343\202\202\346\204\233\343\202\211\343\201\227\343\201\204\357\274\201\343\200\200\350\247\246\343\202\212\343\201\237\343\201\217\343\201\246\343\201\237\343\201\276\343\202\211\343\201\252\343\201\204\342\200\246\342\200\246" },
	{ "apnote2.txt", 4, "\343\201\247\343\202\202\343\200\201\343\201\202\343\201\256\350\246\252\343\201\251\343\202\202\343\201\214\346\235\245\343\202\213\342\200\246\342\200\246\343\201\240\343\201\213\343\202\211\346\256\272\343\201\225\343\201\252\343\201\215\343\202\203\343\201\204\343\201\221\343\201\252\343\201\204\343\202\223\343\201\240\357\274\201" },
	{ "apnote3.txt", 1, "\347\247\230\345\257\206\343\201\256\343\203\241\343\203\242 #3" },
	{ "apnote3.txt", 2, "\343\201\225\343\201\246\342\200\246\342\200\246\345\255\220\344\276\233\343\202\222\344\275\225\344\272\272\343\201\213\346\256\272\343\201\225\343\201\252\343\201\215\343\202\203\343\201\204\343\201\221\343\201\252\343\201\204\343\201\213\343\202\211\343\200\201" },
	{ "apnote3.txt", 3, "\343\203\233\343\203\274\343\203\253\343\201\256\343\202\250\343\203\254\343\203\231\343\203\274\343\202\277\343\203\274\343\202\222\346\255\242\343\202\201\343\201\246\343\201\212\343\201\204\343\201\237\343\200\202\343\201\223\343\202\214\343\201\247\350\201\267\345\223\241\343\201\240\343\201\221\343\201\214" },
	{ "apnote3.txt", 4, "\347\247\230\345\257\206\343\201\256\343\202\263\343\203\274\343\203\211\343\201\247\344\275\277\343\201\210\343\202\213\343\202\210\343\201\206\343\201\253\343\201\252\343\202\213\343\200\202\345\255\220\344\276\233\343\201\253\343\203\220\343\203\254\343\202\213\343\201\257\343\201\232\343\201\214\343\201\252\343\201\204\343\200\202" },
	{ "apnote3.txt", 5, "\343\201\240\343\201\213\343\202\211\343\200\201\343\201\202\343\201\204\343\201\244\343\202\211\343\201\257\351\232\216\346\256\265\343\202\222\344\275\277\343\201\206\343\201\227\343\201\213\343\201\252\343\201\204\343\201\243\343\201\246\343\202\217\343\201\221\343\201\240\343\200\202" },
	{ "apnote4.txt", 1, "\347\247\230\345\257\206\343\201\256\343\203\241\343\203\242 #4" },
	{ "apnote4.txt", 2, "\343\201\223\343\201\256\345\255\220\343\201\257\343\201\204\343\201\204\345\255\220\343\201\240\343\201\243\343\201\237\343\200\202\346\256\272\343\201\227\343\201\214\343\201\204\343\201\214\343\201\202\343\201\243\343\201\237\343\200\202" },
	{ "apnote4.txt", 3, "\343\203\212\343\202\244\343\203\225\343\201\247\347\260\241\345\215\230\343\201\253\345\210\207\343\202\214\343\201\237\343\200\202\351\243\237\343\201\271\347\211\251\343\202\222\345\210\207\343\202\213\343\201\277\343\201\237\343\201\204\343\201\253\343\200\202" },
	{ "janitornote.txt", 1, "\347\256\241\347\220\206\344\272\272\343\201\225\343\202\223\343\201\270" },
	{ "janitornote.txt", 2, "\347\205\247\346\230\216\343\201\256\344\270\200\351\203\250\343\201\253\344\270\215\345\205\267\345\220\210\343\201\214\345\207\272\343\201\246\343\201\204\343\201\276\343\201\231\343\200\202" },
	{ "janitornote.txt", 3, "\343\201\227\343\201\260\343\202\211\343\201\217\343\201\256\351\226\223\343\200\201\351\233\273\346\260\227\343\202\222\346\255\242\343\202\201\343\201\246\343\201\204\343\201\237\343\201\240\343\201\221\343\201\276\343\201\233\343\202\223\343\201\213\357\274\237" },
	{ "janitornote.txt", 4, "\344\277\256\347\220\206\343\201\227\343\201\276\343\201\231\343\201\256\343\201\247\343\200\202" },
	{ "magazine.txt", 1, "\347\240\264\343\202\214\343\201\237\351\233\221\350\252\214\343\201\256\350\250\230\344\272\213" },
	{ "magazine.txt", 2, "\343\201\273\343\201\250\343\202\223\343\201\251\343\201\214\346\261\232\343\202\214\343\201\246\343\201\204\343\201\246\350\252\255\343\201\277\345\217\226\343\202\214\343\201\252\343\201\204\343\200\202\343\201\206\343\201\244\347\227\205\343\201\253\343\201\244\343\201\204\343\201\246\343\201\256\350\250\230\344\272\213\343\201\256\343\202\210\343\201\206\343\201\240\343\200\202" },
	{ "magazine.txt", 3, "\343\200\214\345\277\203\345\275\223\343\201\237\343\202\212\343\201\257\343\201\202\343\202\212\343\201\276\343\201\233\343\202\223\343\201\213\357\274\237\343\200\215" },
	{ "magazine.txt", 4, "\343\200\214\345\205\210\345\273\266\343\201\260\343\201\227\343\201\253\343\201\233\343\201\232\343\200\201\344\273\212\343\201\231\343\201\220 543-214 \343\201\270\343\201\212\351\233\273\350\251\261\343\202\222\343\200\215" },
	{ "purnellnote.txt", 1, "\343\203\221\343\203\274\343\203\215\343\203\253\343\201\256\343\203\241\343\203\242" },
	{ "purnellnote.txt", 2, "\343\200\214\343\203\221\343\203\274\343\203\215\343\203\253\345\214\273\345\270\253\343\201\253\343\201\257\343\200\201\344\275\225\343\201\213\343\201\212\343\201\213\343\201\227\343\201\252\343\201\250\343\201\223\343\202\215\343\201\214\343\201\202\343\202\213\342\200\246\342\200\246\343\200\215" },
	{ "purnellnote.txt", 3, "\343\203\221\343\203\274\343\203\215\343\203\253\345\214\273\345\270\253\342\200\246\342\200\246\343\201\276\343\201\225\343\201\213\342\200\246\342\200\246" },
	{ "sewernote.txt", 1, "\345\200\213\344\272\272\347\232\204\343\201\252\343\203\241\343\203\242" },
	{ "sewernote.txt", 2, "\343\202\270\343\202\247\343\203\225\343\200\201\343\203\201\343\203\274\343\203\240\343\201\270\343\202\210\343\201\206\343\201\223\343\201\235\357\274\201" },
	{ "sewernote.txt", 3, "\345\237\272\346\234\254\347\232\204\343\201\252\343\201\223\343\201\250\343\201\257\343\200\201\343\202\202\343\201\206\344\270\212\345\217\270\343\201\213\343\202\211\350\201\236\343\201\204\343\201\246\343\201\204\343\202\213\343\201\250\346\200\235\343\201\206\343\201\221\343\201\251\343\200\201" },
	{ "sewernote.txt", 4, "\347\237\245\343\201\243\343\201\246\343\201\212\343\201\204\343\201\237\346\226\271\343\201\214\343\201\204\343\201\204\343\201\223\343\201\250\343\202\222\343\201\204\343\201\217\343\201\244\343\201\213\346\233\270\343\201\204\343\201\246\343\201\212\343\201\217\357\274\201" },
	{ "sewernote.txt", 5, "1) \343\201\223\343\201\256\346\264\236\347\252\237\343\201\257\346\271\277\343\201\243\343\201\246\343\201\204\343\201\246\345\257\222\343\201\217\343\200\201\346\260\264\343\201\237\343\201\276\343\202\212\343\201\240\343\202\211\343\201\221\343\201\240\343\200\202\350\266\263\345\205\203\343\201\253\346\260\227\343\202\222\343\201\244\343\201\221\343\202\215\357\274\201" },
	{ "sewernote.txt", 6, "2) \343\202\277\343\203\220\343\202\263\344\274\221\346\206\251\343\201\2572\346\231\202\351\226\223\343\201\224\343\201\250\357\274\210\344\270\200\347\225\252\345\244\247\344\272\213\357\274\201\357\274\211" },
	{ "sewernote.txt", 7, "3) \344\270\212\345\217\270\343\201\257\343\202\215\343\201\217\343\201\247\343\201\252\343\201\227\343\201\240" },
	{ "sewernote.txt", 8, "4) D\343\202\273\343\202\257\343\202\267\343\203\247\343\203\263\343\201\256\343\202\263\343\203\274\343\203\211\343\202\222\350\246\232\343\201\210\343\201\246\343\201\212\343\201\221\343\200\202" },
	{ "sewernote.txt", 9, "\344\270\200\346\227\245\344\270\255\350\241\214\343\201\243\343\201\237\343\202\212\346\235\245\343\201\237\343\202\212\343\201\231\343\202\213\343\201\223\343\201\250\343\201\253\343\201\252\343\202\213\343\201\213\343\202\211\343\200\201\346\232\227\350\250\230\343\201\227\343\201\246\343\201\212\343\201\217\345\277\205\350\246\201\343\201\214\343\201\202\343\202\213\343\200\202" },
	{ "sewernote.txt", 10, "\343\202\263\343\203\274\343\203\211\343\201\257 279\357\274\210\346\234\200\345\276\214\343\201\256\346\225\260\345\255\227\343\201\257\343\202\244\343\203\263\343\202\257\343\201\247\346\261\232\343\202\214\343\201\246\343\201\204\343\201\246\350\252\255\343\202\201\343\201\252\343\201\204\357\274\211" },
	{ "subnote.txt", 1, "\347\247\201\343\201\256\344\272\272\347\224\237\343\200\202" },
	{ "subnote.txt", 2, "\343\201\202\343\201\256\351\201\213\345\221\275\343\201\256\345\244\234\343\200\201\345\255\220\344\276\233\343\201\257\351\201\223\343\201\253\345\200\222\343\202\214\343\201\246\343\201\204\343\201\237\343\200\202\345\243\212\343\202\214\343\201\237\344\275\223\343\201\247\343\200\202" },
	{ "subnote.txt", 3, "\343\200\214\343\201\251\343\201\206\343\201\227\343\201\246\343\201\202\343\202\223\343\201\252\345\244\234\351\201\205\343\201\217\343\201\253\343\200\201\343\201\202\343\202\223\343\201\252\351\201\223\343\202\222\346\255\251\343\201\204\343\201\237\343\202\212\343\201\227\343\201\237\343\202\223\343\201\240\343\201\204\343\200\201\345\235\212\343\202\204\357\274\237" },
	{ "subnote.txt", 4, "\343\201\251\343\201\206\343\201\227\343\201\246\343\201\202\343\202\223\343\201\252\346\204\232\343\201\213\343\201\252\347\234\237\344\274\274\343\202\222\357\274\237\343\200\215\344\270\241\350\246\252\343\201\256\350\250\200\350\221\211\343\201\253\345\271\273\346\273\205\343\201\227\343\200\201" },
	{ "subnote.txt", 5, "\350\213\246\343\201\227\343\201\277\343\202\222\345\213\237\343\202\211\343\201\233\343\201\237\345\255\220\344\276\233\343\201\257\343\200\201\344\275\223\343\201\214\345\243\212\343\202\214\343\200\201" },
	{ "subnote.txt", 6, "\345\277\203\343\202\202\345\243\212\343\202\214\343\201\237\343\200\202\347\231\275\350\241\243\343\201\256\347\224\267\343\201\237\343\201\241\343\201\257\345\275\274\343\202\222" },
	{ "subnote.txt", 7, "\350\273\212\346\244\205\345\255\220\343\201\253\344\271\227\343\201\233\343\201\237\343\200\202\345\275\274\343\201\257\347\254\221\343\201\243\343\201\237\357\274\201\343\200\200\343\201\252\343\202\223\343\201\250\347\254\221\343\201\243\343\201\237\343\201\223\343\201\250\343\201\213\357\274\201" },
	{ "subnote.txt", 8, "\343\201\223\343\201\206\343\201\227\343\201\246\345\275\274\343\201\257\346\256\213\343\202\212\343\201\256\344\272\272\347\224\237\343\202\222\347\224\237\343\201\215\347\266\232\343\201\221\343\201\237\343\200\202\343\201\202\343\202\213\346\227\245\343\200\201\351\201\213\345\221\275\343\201\214" },
	{ "subnote.txt", 9, "\345\255\220\344\276\233\343\201\253\345\276\256\347\254\221\343\201\277\343\201\213\343\201\221\343\200\201\345\275\274\343\201\257\350\207\252\343\202\211\343\202\222\346\273\205\343\201\274\343\201\231\351\201\223\345\205\267\343\202\222\346\211\213\343\201\253\343\201\227\343\201\237\343\200\202" },
	{ "subnote.txt", 10, "\346\261\272\346\226\255\357\274\201\343\200\200\346\261\272\346\226\255\357\274\201\343\200\200\343\201\235\343\201\256\347\265\220\346\234\253\343\201\257" },
	{ "subnote.txt", 11, "\345\256\232\343\201\213\343\201\247\343\201\257\343\201\252\343\201\213\343\201\243\343\201\237 - Pro captu lectoris, habent sua fata libelli" },
	{ "bizarrenote.txt", 1, "\345\245\207\345\246\231\343\201\252\343\203\241\343\203\242 #1" },
	{ "bizarrenote.txt", 2, "\343\201\212\345\211\215\343\201\257\343\201\223\343\201\223\343\201\253\351\226\211\343\201\230\350\276\274\343\202\201\343\202\211\343\202\214\343\201\246\343\201\204\343\202\213\343\200\202" },
	{ "bizarrenote.txt", 3, "\346\260\270\351\201\240\343\201\253\343\200\202" },
	{ "bizarrenote.txt", 4, "\343\201\225\343\201\202\343\200\201\347\247\201\343\201\250\351\201\212\343\201\274\343\201\206\343\200\202" },
	{ "bizarrenote.txt", 5, "\343\201\206\343\202\213\343\201\225\343\201\204\343\202\262\343\203\274\343\203\240\343\202\222\343\201\227\343\202\210\343\201\206\343\200\202" },
	{ "bizarrenote2.txt", 1, "\345\245\207\345\246\231\343\201\252\343\203\241\343\203\242 #2" },
	{ "bizarrenote2.txt", 2, "\343\201\223\343\202\214\343\202\222\345\217\227\343\201\221\345\217\226\343\202\214\343\200\202" },
};


// ---------------------------------------------------------------------------
// JP titles (titles.txt messages, keyed by full English message text)
// ---------------------------------------------------------------------------

struct JpTitleEntry
{
	const char *en;
	const char *jp;
};

static const JpTitleEntry g_jpTitles[] = {
	{ "\"FORGET ABOUT FREEMAN!\"", "\343\200\214\343\203\225\343\203\252\343\203\274\343\203\236\343\203\263\343\201\257\346\224\276\343\201\243\343\201\246\343\201\212\343\201\221\357\274\201\343\200\215" },
	{ "\"WE'VE GOT HOSTILES\"", "\343\200\214\346\225\265\350\245\262\357\274\201\343\200\215" },
	{ "ACTIVATE WHEELS AND DIALS\nBY HOLDING DOWN THE USE KEY\nHOLD USE KEY UNTIL BRIDGE IS IN POSITION", "\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\347\266\232\343\201\221\343\201\246\n\343\203\217\343\203\263\343\203\211\343\203\253\343\202\204\343\203\200\343\202\244\343\203\244\343\203\253\343\202\222\345\233\236\343\201\231\n\346\251\213\343\201\214\346\211\200\345\256\232\343\201\256\344\275\215\347\275\256\343\201\253\346\235\245\343\202\213\343\201\276\343\201\247\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\347\266\232\343\201\221\343\202\213" },
	{ "ADMINISTRATIVE SPONSOR:\nClassified", "\347\256\241\347\220\206\350\262\254\344\273\273\350\200\205\357\274\232\n\346\251\237\345\257\206\344\272\213\351\240\205" },
	{ "ANOMALOUS MATERIALS", "\347\225\260\345\270\270\347\211\251\350\263\252" },
	{ "APPREHENSION", "\351\200\256\346\215\225" },
	{ "APPROACH SCIENTIST\nPRESS USE KEY TO RECEIVE HEALTH FROM SCIENTIST", "\347\247\221\345\255\246\350\200\205\343\201\253\350\277\221\343\201\245\343\201\217\n\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\347\247\221\345\255\246\350\200\205\343\201\213\343\202\211\345\233\236\345\276\251\343\202\222\345\217\227\343\201\221\343\202\213" },
	{ "APPROACH SECURITY GUARD\nPRESS YOUR USE KEY TO GET HIS HELP\nWALK TOWARD DOOR AND GUARD\nWILL ACTIVATE BUTTONS", "\350\255\246\345\202\231\345\223\241\343\201\253\350\277\221\343\201\245\343\201\217\n\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\212\251\343\201\221\343\201\246\343\202\202\343\202\211\343\201\206\n\343\203\211\343\202\242\343\201\253\345\220\221\343\201\213\343\201\243\343\201\246\346\255\251\343\201\217\343\201\250\350\255\246\345\202\231\345\223\241\343\201\214\n\343\203\234\343\202\277\343\203\263\343\202\222\346\212\274\343\201\227\343\201\246\343\201\217\343\202\214\343\202\213" },
	{ "ASSIGNMENT:\nAnomalous Materials Laboratory", "\351\205\215\345\261\236\345\205\210\357\274\232\n\347\225\260\345\270\270\347\211\251\350\263\252\347\240\224\347\251\266\346\211\200" },
	{ "Auto", "\350\207\252\345\213\225" },
	{ "BE SURE YOU PICKED UP THE LONG-JUMP MODULE\nAT THE BEGINNING OF THE OBSTACLE", "\351\232\234\345\256\263\347\211\251\343\201\256\346\234\200\345\210\235\343\201\247\343\203\255\343\203\263\343\202\260\343\202\270\343\203\243\343\203\263\343\203\227\343\203\242\343\202\270\343\203\245\343\203\274\343\203\253\343\202\222\n\345\277\205\343\201\232\346\213\276\343\201\243\343\201\246\343\201\212\343\201\217\343\201\223\343\201\250" },
	{ "BLACK MESA INBOUND", "\343\203\226\343\203\251\343\203\203\343\202\257\343\203\241\343\202\265\345\210\260\347\235\200" },
	{ "BLAST PIT", "\343\203\226\343\203\251\343\202\271\343\203\210\343\203\273\343\203\224\343\203\203\343\203\210" },
	{ "Black Mesa Research Facility\nBlack Mesa, New Mexico", "\343\203\226\343\203\251\343\203\203\343\202\257\343\203\241\343\202\265\347\240\224\347\251\266\346\211\200\n\343\203\213\343\203\245\343\203\274\343\203\241\343\202\255\343\202\267\343\202\263\345\267\236 \343\203\226\343\203\251\343\203\203\343\202\257\343\203\241\343\202\265" },
	{ "CANCEL", "\343\202\255\343\203\243\343\203\263\343\202\273\343\203\253" },
	{ "CERTAIN TYPES OF DAMAGE WILL REGISTER\nON YOUR HEADS-UP DISPLAY.\nDIRECTION OF DAMAGE IS INDICATED BY RED\nFLASHES IN THE CENTER OF YOUR SCREEN", "\343\203\200\343\203\241\343\203\274\343\202\270\343\201\256\347\250\256\351\241\236\343\201\253\343\202\210\343\201\243\343\201\246\343\201\257\n\343\203\230\343\203\203\343\203\211\343\202\242\343\203\203\343\203\227\343\203\207\343\202\243\343\202\271\343\203\227\343\203\254\343\202\244\343\201\253\350\241\250\347\244\272\343\201\225\343\202\214\343\202\213\343\200\202\n\343\203\200\343\203\241\343\203\274\343\202\270\343\201\256\346\226\271\345\220\221\343\201\257\347\224\273\351\235\242\344\270\255\345\244\256\343\201\256\350\265\244\343\201\204\n\347\202\271\346\273\205\343\201\247\347\244\272\343\201\225\343\202\214\343\202\213" },
	{ "CLASS", "\343\202\257\343\203\251\343\202\271" },
	{ "CLEARANCE:\nLevel 3", "\346\250\251\351\231\220\357\274\232\n\343\203\254\343\203\231\343\203\2533" },
	{ "CONGRATULATIONS!\nYOU HAVE COMPLETED\nTHE BLACK MESA HAZARD COURSE.\nCOME BACK ANY TIME.", "\343\201\212\343\202\201\343\201\247\343\201\250\343\201\206\357\274\201\n\343\203\226\343\203\251\343\203\203\343\202\257\343\203\241\343\202\265\n\343\203\217\343\202\266\343\203\274\343\203\211\343\202\263\343\203\274\343\202\271\343\202\222\345\256\214\344\272\206\343\201\227\343\201\237\343\200\202\n\343\201\204\343\201\244\343\201\247\343\202\202\346\210\273\343\201\243\343\201\246\343\201\215\343\201\246\343\202\210\343\201\204\343\200\202" },
	{ "Camera Options", "\343\202\253\343\203\241\343\203\251\350\250\255\345\256\232" },
	{ "Chase Map Overview", "\343\203\201\343\202\247\343\202\244\343\202\271\343\203\236\343\203\203\343\203\227\344\277\257\347\236\260" },
	{ "Chase-Cam Overview", "\343\203\201\343\202\247\343\202\244\343\202\271\343\202\253\343\203\241\343\203\251\344\277\257\347\236\260" },
	{ "DEATHS", "\343\203\207\343\202\271" },
	{ "DISASTER RESPONSE PRIORITY:\nDiscretionary", "\347\201\275\345\256\263\345\257\276\345\277\234\345\204\252\345\205\210\345\272\246\357\274\232\n\344\273\273\346\204\217" },
	{ "EDUCATION:\nPh.D., MIT, Theoretical Physics", "\345\255\246\346\255\264\357\274\232\n\345\215\232\345\243\253\345\217\267\357\274\210MIT \347\220\206\350\253\226\347\211\251\347\220\206\345\255\246\357\274\211" },
	{ "ENDGAME", "\343\202\250\343\203\263\343\203\211\343\202\262\343\203\274\343\203\240" },
	{ "First Person", "\344\270\200\344\272\272\347\247\260\350\246\226\347\202\271" },
	{ "Free Chase-Cam", "\343\203\225\343\203\252\343\203\274\343\203\201\343\202\247\343\202\244\343\202\271\343\202\253\343\203\241\343\203\251" },
	{ "Free Chase-Camera", "\343\203\225\343\203\252\343\203\274\343\203\201\343\202\247\343\202\244\343\202\271\343\202\253\343\203\241\343\203\251" },
	{ "Free Map Overview", "\343\203\225\343\203\252\343\203\274\343\203\236\343\203\203\343\203\227\344\277\257\347\236\260" },
	{ "Free Overview", "\343\203\225\343\203\252\343\203\274\344\277\257\347\236\260" },
	{ "Free-Look", "\343\203\225\343\203\252\343\203\274\343\203\253\343\203\203\343\202\257" },
	{ "GONARCH'S LAIR", "\343\202\264\343\203\212\343\203\274\343\202\257\343\201\256\345\267\243\347\252\237" },
	{ "HAZARD COURSE", "\343\203\217\343\202\266\343\203\274\343\203\211\343\202\263\343\203\274\343\202\271" },
	{ "INTERLOPER", "\344\276\265\345\205\245\350\200\205" },
	{ "Instant Replay", "\343\202\244\343\203\263\343\202\271\343\202\277\343\203\263\343\203\210\343\203\252\343\203\227\343\203\254\343\202\244" },
	{ "LAMBDA CORE", "\343\203\251\343\203\240\343\203\200\343\203\273\343\202\263\343\202\242" },
	{ "Locked Chase-Cam", "\345\233\272\345\256\232\343\203\201\343\202\247\343\202\244\343\202\271\343\202\253\343\203\241\343\203\251" },
	{ "Locked Chase-Camera", "\345\233\272\345\256\232\343\203\201\343\202\247\343\202\244\343\202\271\343\202\253\343\203\241\343\203\251" },
	{ "MOVE FORWARD BY PRESSING FORWARD KEY\nMOVE BACKWARD BY PRESSING BACKWARD KEY\nMOVE LEFT BY PRESSING MOVELEFT\nMOVE RIGHT BY PRESSING MOVERIGHT", "\345\211\215\351\200\262\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\211\215\351\200\262\n\345\276\214\351\200\200\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\276\214\351\200\200\n\345\267\246\347\247\273\345\213\225\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\267\246\343\201\270\347\247\273\345\213\225\n\345\217\263\347\247\273\345\213\225\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\217\263\343\201\270\347\247\273\345\213\225" },
	{ "MOVE UP TO CROWBAR\nPRESS ATTACK1 KEY TO BREAK OBJECTS", "\343\203\220\343\203\274\343\203\253\343\201\253\350\277\221\343\201\245\343\201\217\n\346\224\273\346\222\2031\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\347\211\251\343\202\222\345\243\212\343\201\231" },
	{ "MOVE UP TO WEAPON TO PICK IT UP", "\346\255\246\345\231\250\343\201\253\350\277\221\343\201\245\343\201\204\343\201\246\346\213\276\343\201\206" },
	{ "Map", "\343\203\236\343\203\203\343\203\227" },
	{ "Map Description not available.", "\343\203\236\343\203\203\343\203\227\343\201\256\350\252\254\346\230\216\343\201\214\343\201\202\343\202\212\343\201\276\343\201\233\343\202\223\343\200\202" },
	{ "Mic Volume", "\343\203\236\343\202\244\343\202\257\351\237\263\351\207\217" },
	{ "No valid targets. Cannot switch to Chase-Camera Mode.", "\346\234\211\345\212\271\343\201\252\345\257\276\350\261\241\343\201\214\343\201\204\343\201\276\343\201\233\343\202\223\343\200\202\343\203\201\343\202\247\343\202\244\343\202\271\343\202\253\343\203\241\343\203\251\343\203\242\343\203\274\343\203\211\343\201\253\345\210\207\343\202\212\346\233\277\343\201\210\343\202\213\343\201\223\343\201\250\343\201\214\343\201\247\343\201\215\343\201\276\343\201\233\343\202\223\343\200\202" },
	{ "OFFICE COMPLEX", "\343\202\252\343\203\225\343\202\243\343\202\271\346\243\237" },
	{ "ON A RAIL", "\343\202\252\343\203\263\343\203\273\343\202\242\343\203\273\343\203\254\343\203\274\343\203\253" },
	{ "Options", "\350\250\255\345\256\232" },
	{ "PLAYERS", "\343\203\227\343\203\254\343\202\244\343\203\244\343\203\274" },
	{ "POSITION:\nResearch Associate", "\350\201\267\344\275\215\357\274\232\n\347\240\224\347\251\266\345\223\241" },
	{ "POWER UP", "\343\203\221\343\203\257\343\203\274\343\202\242\343\203\203\343\203\227" },
	{ "PRESS AND HOLD YOUR USE KEY\nHOLD DOWN UNTIL SUIT ARMOR IS CHARGED\nOR CHARGER IS DEPLETED", "\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\347\266\232\343\201\221\343\202\213\n\343\202\271\343\203\274\343\203\204\343\201\256\350\243\205\347\224\262\343\201\214\345\205\205\351\233\273\343\201\225\343\202\214\343\202\213\343\201\213\n\345\205\205\351\233\273\345\231\250\343\201\214\345\260\275\343\201\215\343\202\213\343\201\276\343\201\247\346\212\274\343\201\227\347\266\232\343\201\221\343\202\213" },
	{ "PRESS ATTACK1 KEY FOR PRIMARY ATTACK\nPRESS ATTACK2 KEY FOR ALTERNATE ATTACK\nPRESS RELOAD KEY TO RELOAD AT WILL", "\346\224\273\346\222\2031\343\202\255\343\203\274\343\201\247\351\200\232\345\270\270\346\224\273\346\222\203\n\346\224\273\346\222\2032\343\202\255\343\203\274\343\201\247\347\211\271\346\256\212\346\224\273\346\222\203\n\343\203\252\343\203\255\343\203\274\343\203\211\343\202\255\343\203\274\343\201\247\344\273\273\346\204\217\343\201\253\343\203\252\343\203\255\343\203\274\343\203\211" },
	{ "PRESS FLASHLIGHT KEY\nTO TURN FLASHLIGHT ON", "\346\207\220\344\270\255\351\233\273\347\201\257\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\n\346\207\220\344\270\255\351\233\273\347\201\257\343\202\222\343\201\244\343\201\221\343\202\213" },
	{ "PRESS FLASHLIGHT KEY AGAIN\nTO TURN FLASHLIGHT OFF", "\346\207\220\344\270\255\351\233\273\347\201\257\343\202\255\343\203\274\343\202\222\343\202\202\343\201\206\344\270\200\345\272\246\346\212\274\343\201\227\343\201\246\n\346\207\220\344\270\255\351\233\273\347\201\257\343\202\222\346\266\210\343\201\231" },
	{ "PRESS FORWARD + JUMP TOGETHER,\nTHEN THE DUCK KEY", "\345\211\215\351\200\262\357\274\213\343\202\270\343\203\243\343\203\263\343\203\227\343\202\222\345\220\214\346\231\202\343\201\253\346\212\274\343\201\227\343\200\201\n\347\266\232\343\201\221\343\201\246\343\201\227\343\202\203\343\201\214\343\201\277\343\202\255\343\203\274" },
	{ "PRESS FORWARD KEY TO MOVE AGAINST BOX\nKEEP PRESSING FORWARD TO PUSH BOX", "\345\211\215\351\200\262\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\347\256\261\343\201\253\346\212\274\343\201\227\345\275\223\343\201\246\343\202\213\n\346\212\274\343\201\227\347\266\232\343\201\221\343\201\246\347\256\261\343\202\222\346\212\274\343\201\231" },
	{ "PRESS FORWARD KEY TO MOVE UP LADDERS\nPRESS BACKWARD KEY TO MOVE DOWN LADDERS", "\345\211\215\351\200\262\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\201\257\343\201\227\343\201\224\343\202\222\344\270\212\343\202\213\n\345\276\214\351\200\200\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\201\257\343\201\227\343\201\224\343\202\222\344\270\213\343\202\212\343\202\213" },
	{ "PRESS FORWARD KEY TO RUN FORWARD\nPRESS DUCK KEY TO DUCK\nPRESS FORWARD + DUCK KEYS TOGETHER\nTO MOVE IN STEALTH MODE", "\345\211\215\351\200\262\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\211\215\343\201\270\350\265\260\343\202\213\n\343\201\227\343\202\203\343\201\214\343\201\277\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\201\227\343\202\203\343\201\214\343\202\200\n\345\211\215\351\200\262\357\274\213\343\201\227\343\202\203\343\201\214\343\201\277\343\202\255\343\203\274\343\202\222\345\220\214\346\231\202\343\201\253\346\212\274\343\201\231\343\201\250\n\343\202\271\343\203\206\343\203\253\343\202\271\343\203\242\343\203\274\343\203\211\343\201\247\347\247\273\345\213\225" },
	{ "PRESS FORWARD KEY TO RUN FORWARD\nPRESS JUMP KEY TO JUMP", "\345\211\215\351\200\262\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\211\215\343\201\270\350\265\260\343\202\213\n\343\202\270\343\203\243\343\203\263\343\203\227\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\202\270\343\203\243\343\203\263\343\203\227" },
	{ "PRESS FORWARD KEY TO RUN FORWARD\nPRESS JUMP KEY TO JUMP\nPRESS DUCK KEY TO DUCK\nPRESS JUMP KEY TO JUMP", "\345\211\215\351\200\262\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\211\215\343\201\270\350\265\260\343\202\213\n\343\202\270\343\203\243\343\203\263\343\203\227\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\202\270\343\203\243\343\203\263\343\203\227\n\343\201\227\343\202\203\343\201\214\343\201\277\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\201\227\343\202\203\343\201\214\343\202\200\n\343\202\270\343\203\243\343\203\263\343\203\227\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\202\270\343\203\243\343\203\263\343\203\227" },
	{ "PRESS USE + BACKWARD KEYS TOGETHER\nTO PULL BOX BACKWARD", "\344\275\277\347\224\250\357\274\213\345\276\214\351\200\200\343\202\255\343\203\274\343\202\222\345\220\214\346\231\202\343\201\253\346\212\274\343\201\227\343\201\246\n\347\256\261\343\202\222\345\276\214\343\202\215\343\201\270\345\274\225\343\201\217" },
	{ "PRESS USE KEY TO ENGAGE TRAIN\nPRESS FORWARD KEY TO ACCELERATE\nPRESS BACKWARD KEY TO DECELERATE\nPRESS USE KEY AGAIN TO DISENGAGE", "\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\345\210\227\350\273\212\343\202\222\345\213\225\343\201\213\343\201\231\n\345\211\215\351\200\262\343\202\255\343\203\274\343\201\247\345\212\240\351\200\237\n\345\276\214\351\200\200\343\202\255\343\203\274\343\201\247\346\270\233\351\200\237\n\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\343\202\202\343\201\206\344\270\200\345\272\246\346\212\274\343\201\227\343\201\246\351\231\215\343\202\212\343\202\213" },
	{ "PRESS YOUR FORWARD KEY\nAIM WITH THE MOUSE AS YOU SWIM\nFIND AIR IF YOU BEGIN TO LOSE HEALTH\nWAIT IN THE AIR UNTIL HEALTH\nRETURNS TO FORMER LEVEL", "\345\211\215\351\200\262\343\202\255\343\203\274\343\202\222\346\212\274\343\201\231\n\346\263\263\343\201\216\343\201\252\343\201\214\343\202\211\343\203\236\343\202\246\343\202\271\343\201\247\347\205\247\346\272\226\343\202\222\345\220\210\343\202\217\343\201\233\343\202\213\n\344\275\223\345\212\233\343\201\214\346\270\233\343\202\212\345\247\213\343\202\201\343\201\237\343\202\211\347\251\272\346\260\227\343\201\256\343\201\202\343\202\213\345\240\264\346\211\200\343\202\222\346\216\242\343\201\231\n\344\275\223\345\212\233\343\201\214\345\205\203\343\201\256\346\260\264\346\272\226\343\201\253\n\346\210\273\343\202\213\343\201\276\343\201\247\347\251\272\346\260\227\343\201\256\343\201\202\343\202\213\345\240\264\346\211\200\343\201\247\345\276\205\343\201\244" },
	{ "PRESS YOUR USE KEY TO PUSH A BUTTON", "\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\343\201\246\343\203\234\343\202\277\343\203\263\343\202\222\346\212\274\343\201\231" },
	{ "Press DUCK for Spectator Menu", "DUCK\343\202\255\343\203\274\343\201\247\350\246\263\346\210\246\343\203\241\343\203\213\343\203\245\343\203\274" },
	{ "QUESTIONABLE ETHICS", "\347\226\221\343\202\217\343\201\227\343\201\204\345\200\253\347\220\206" },
	{ "RADIATION HAZARDS\nACTIVATE A GEIGER COUNTER", "\346\224\276\345\260\204\347\267\232\343\201\256\345\215\261\351\231\272\343\201\214\343\201\202\343\202\213\345\240\264\346\211\200\343\201\247\343\201\257\n\343\202\254\343\202\244\343\202\254\343\203\274\343\202\253\343\202\246\343\203\263\343\202\277\343\203\274\343\201\214\344\275\234\345\213\225\343\201\231\343\202\213" },
	{ "RESIDUE PROCESSING", "\346\256\213\347\225\231\347\211\251\345\207\246\347\220\206" },
	{ "Roaming", "\343\203\255\343\203\274\343\203\237\343\203\263\343\202\260" },
	{ "SCORE", "\343\202\271\343\202\263\343\202\242" },
	{ "SUBJECT:\nGordon Freeman\nMale, age 27", "\345\257\276\350\261\241\357\274\232\n\343\202\264\343\203\274\343\203\211\343\203\263\343\203\273\343\203\225\343\203\252\343\203\274\343\203\236\343\203\263\n\347\224\267\346\200\247\343\200\20127\346\255\263" },
	{ "SUBJECT:  FREEMAN\nSTATUS:  EVALUATION TERMINATED\nPOSTMORTEM:\nSubject failed to effectively utilize\nhuman assets in achievement of goal.", "\345\257\276\350\261\241\357\274\232 \343\203\225\343\203\252\343\203\274\343\203\236\343\203\263\n\347\212\266\346\205\213\357\274\232 \350\251\225\344\276\241\347\265\202\344\272\206\n\346\244\234\350\246\226\347\265\220\346\236\234\357\274\232\n\345\257\276\350\261\241\343\201\257\347\233\256\346\250\231\351\201\224\346\210\220\343\201\253\343\201\202\343\201\237\343\202\212\344\272\272\347\232\204\350\263\207\347\224\243\343\202\222\n\345\212\271\346\236\234\347\232\204\343\201\253\346\264\273\347\224\250\343\201\247\343\201\215\343\201\252\343\201\213\343\201\243\343\201\237\343\200\202" },
	{ "SUBJECT:  FREEMAN\nSTATUS:  HIRED\nAWAITING ASSIGNMENT", "\345\257\276\350\261\241\357\274\232 \343\203\225\343\203\252\343\203\274\343\203\236\343\203\263\n\347\212\266\346\205\213\357\274\232 \351\233\207\347\224\250\346\261\272\345\256\232\n\344\273\273\345\213\231\343\201\256\345\211\262\343\202\212\345\275\223\343\201\246\343\202\222\345\276\205\346\251\237\344\270\255" },
	{ "SUBJECT:  FREEMAN\nSTATUS:  OBSERVATION TERMINATED\nPOSTMORTEM:\nSubject declined offer of employment.", "\345\257\276\350\261\241\357\274\232 \343\203\225\343\203\252\343\203\274\343\203\236\343\203\263\n\347\212\266\346\205\213\357\274\232 \350\246\263\345\257\237\347\265\202\344\272\206\n\346\244\234\350\246\226\347\265\220\346\236\234\357\274\232\n\345\257\276\350\261\241\343\201\257\351\233\207\347\224\250\343\201\256\347\224\263\343\201\227\345\207\272\343\202\222\350\276\236\351\200\200\343\201\227\343\201\237\343\200\202" },
	{ "SURFACE TENSION", "\350\241\250\351\235\242\345\274\265\345\212\233" },
	{ "Slow Motion", "\343\202\271\343\203\255\343\203\274\343\203\242\343\203\274\343\202\267\343\203\247\343\203\263" },
	{ "Speaker Volume", "\343\202\271\343\203\224\343\203\274\343\202\253\343\203\274\351\237\263\351\207\217" },
	{ "Spectator Mode", "\350\246\263\346\210\246\343\203\242\343\203\274\343\203\211" },
	{ "Spectators", "\350\246\263\346\210\246\350\200\205" },
	{ "TEAMS", "\343\203\201\343\203\274\343\203\240" },
	{ "UNFORESEEN CONSEQUENCES", "\344\272\210\346\234\237\343\201\233\343\201\254\347\265\220\346\236\234" },
	{ "USE MED-KITS BY HOLDING DOWN THE USE KEY\nHOLD DOWN USE KEY UNTIL HEALTH IS\nAT 100 OR KIT IS DEPLETED", "\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\347\266\232\343\201\221\343\201\246\345\214\273\347\231\202\343\202\255\343\203\203\343\203\210\343\202\222\344\275\277\343\201\206\n\344\275\223\345\212\233\343\201\214100\343\201\253\343\201\252\343\202\213\343\201\213\343\202\255\343\203\203\343\203\210\343\201\214\345\260\275\343\201\215\343\202\213\343\201\276\343\201\247\n\344\275\277\347\224\250\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\347\266\232\343\201\221\343\202\213" },
	{ "Unassigned", "\346\234\252\345\211\262\343\202\212\345\275\223\343\201\246" },
	{ "Use the following keys to change view styles:\nFIRE1 - Chase next player\nFIRE2 - Chase previous player\nJUMP - Change view modes\nUSE - Change inset window mode\nDUCK  - Enable spectator menu\nIn Overview Map Mode move around with:\nMOVELEFT - move left\nMOVERIGHT - move right\nFORWARD - zoom in\nBACK - zoom out\nMOUSE - rotate around map/target", "\344\273\245\344\270\213\343\201\256\343\202\255\343\203\274\343\201\247\350\246\226\347\202\271\343\202\271\343\202\277\343\202\244\343\203\253\343\202\222\345\244\211\346\233\264\357\274\232\nFIRE1 - \346\254\241\343\201\256\343\203\227\343\203\254\343\202\244\343\203\244\343\203\274\343\202\222\350\277\275\350\267\241\nFIRE2 - \345\211\215\343\201\256\343\203\227\343\203\254\343\202\244\343\203\244\343\203\274\343\202\222\350\277\275\350\267\241\nJUMP - \350\246\226\347\202\271\343\203\242\343\203\274\343\203\211\345\244\211\346\233\264\nUSE - \343\202\244\343\203\263\343\202\273\343\203\203\343\203\210\343\202\246\343\202\243\343\203\263\343\203\211\343\202\246\343\203\242\343\203\274\343\203\211\345\244\211\346\233\264\nDUCK  - \350\246\263\346\210\246\343\203\241\343\203\213\343\203\245\343\203\274\343\202\222\346\234\211\345\212\271\345\214\226\n\344\277\257\347\236\260\343\203\236\343\203\203\343\203\227\343\203\242\343\203\274\343\203\211\343\201\247\343\201\257\344\273\245\344\270\213\343\201\247\347\247\273\345\213\225\357\274\232\nMOVELEFT - \345\267\246\343\201\270\347\247\273\345\213\225\nMOVERIGHT - \345\217\263\343\201\270\347\247\273\345\213\225\nFORWARD - \343\202\272\343\203\274\343\203\240\343\202\244\343\203\263\nBACK - \343\202\272\343\203\274\343\203\240\343\202\242\343\202\246\343\203\210\nMOUSE - \343\203\236\343\203\203\343\203\227\357\274\217\345\257\276\350\261\241\343\202\222\344\270\255\345\277\203\343\201\253\345\233\236\350\273\242" },
	{ "VOICE", "\343\203\234\343\202\244\343\202\271" },
	{ "Valve is:", "Valve\357\274\232" },
	{ "Voice Properties", "\343\203\234\343\202\244\343\202\271\350\250\255\345\256\232" },
	{ "WHILE MOVING FORWARD,\nHOLD DOWN DUCK KEY--\nTHEN PRESS JUMP", "\345\211\215\351\200\262\343\201\227\343\201\252\343\201\214\343\202\211\343\200\201\n\343\201\227\343\202\203\343\201\214\343\201\277\343\202\255\343\203\274\343\202\222\346\212\274\343\201\227\347\266\232\343\201\221\343\201\246\342\200\225\342\200\225\n\343\202\270\343\203\243\343\203\263\343\203\227\343\202\222\346\212\274\343\201\231" },
	{ "XEN", "\343\202\274\343\203\263" },
	{ "You have muted %s.", "%s \343\202\222\343\203\237\343\203\245\343\203\274\343\203\210\343\201\227\343\201\276\343\201\227\343\201\237\343\200\202" },
	{ "You have unmuted %s.", "%s \343\201\256\343\203\237\343\203\245\343\203\274\343\203\210\343\202\222\350\247\243\351\231\244\343\201\227\343\201\276\343\201\227\343\201\237\343\200\202" },
	{ "You will no longer hear that player speak.", "\343\201\235\343\201\256\343\203\227\343\203\254\343\202\244\343\203\244\343\203\274\343\201\256\345\243\260\343\201\257\350\201\236\343\201\223\343\201\210\343\201\252\343\201\217\343\201\252\343\202\212\343\201\276\343\201\231\343\200\202" },
	{ "player", "\343\203\227\343\203\254\343\202\244\343\203\244\343\203\274" },
	{ "players", "\343\203\227\343\203\254\343\202\244\343\203\244\343\203\274" },
};

const char *JpLookupTitle( const char *pszEnglish )
{
	if ( !pszEnglish )
		return 0;
	static char szBuf[1024];
	strncpy( szBuf, pszEnglish, sizeof(szBuf) - 1 );
	szBuf[sizeof(szBuf) - 1] = 0;
	int nLen = (int)strlen( szBuf );
	while ( nLen > 0 && ( szBuf[nLen-1] == '\n' || szBuf[nLen-1] == '\r' || szBuf[nLen-1] == ' ' || szBuf[nLen-1] == '\t' ) )
		szBuf[--nLen] = 0;
	const unsigned int n = sizeof(g_jpTitles) / sizeof(g_jpTitles[0]);
	unsigned int lo = 0, hi = n;
	while ( lo < hi )
	{
		unsigned int mid = ( lo + hi ) / 2;
		const char *a = szBuf, *b = g_jpTitles[mid].en;
		while ( *a && *b && *a == *b ) { a++; b++; }
		int c = (int)(unsigned char)*a - (int)(unsigned char)*b;
		if ( c == 0 ) return g_jpTitles[mid].jp;
		if ( c < 0 ) hi = mid; else lo = mid + 1;
	}
	return 0;
}

const char *JpLookupNoteLine( const char *pszFile, int iLine )
{
	if ( !pszFile )
		return NULL;
	for ( size_t i = 0; i < sizeof(g_jpNotes) / sizeof(g_jpNotes[0]); i++ )
	{
		if ( g_jpNotes[i].iLine == iLine && strcmp( g_jpNotes[i].pszFile, pszFile ) == 0 )
			return g_jpNotes[i].pszText;
	}
	return NULL;
}
