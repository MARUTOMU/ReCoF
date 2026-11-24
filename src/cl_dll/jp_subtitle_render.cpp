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
#include "cl_util.h"
#include "const.h"
#include "entity_types.h"
#include "cdll_int.h"
#include "vgui_TeamFortressViewport.h"
#include "vgui_subtitles.h"
#include "VGUI_TextImage.h"
#include "VGUI_Font.h"
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

struct JpGlyph
{
	int   cp;
	int   texId;	// VGUI1 texture id; -1 = metrics only, 0 = blank (no bitmap)
	int   w, h;		// bitmap size in pixels
	int   xoff;		// bitmap left edge relative to the pen position
	int   yoff;		// bitmap top edge relative to the baseline
	float adv;		// pen advance in pixels
};

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
