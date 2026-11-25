#pragma once

struct JpSubPair
{
	const char *en;
	const char *jp;
};

// Returns the Japanese translation for an English subtitle/phone line
// exactly as delivered by the server (subtitle "*" terminator stripped),
// or 0 when there is no translation.
const char *JpLookupSubtitle( const char *pszEnglish );
