/*
 * Chaos Fork: Undertale Font Loader
 * Loads TTF fonts from ChaosAssets for Undertale battle UI rendering.
 */

#ifndef EP_CHAOS_UNDERTALE_FONT_H
#define EP_CHAOS_UNDERTALE_FONT_H

#include "memory_management.h"

namespace Chaos {

/**
 * Provides cached Undertale fonts loaded from ChaosAssets TTF files.
 * Fonts are loaded lazily on first access and cached for the session.
 */
namespace UndertaleFont {
	/** DialogueFont.ttf - main dialogue text */
	FontRef Dialogue();

	/** DialogueFont_Sans.ttf - Sans-style dialogue */
	FontRef DialogueSans();

	/** HP_NAME_Font.ttf - stats/name display */
	FontRef Stats();

	/** Damage_Font.ttf - damage numbers */
	FontRef Damage();

	/** Release all cached fonts */
	void Dispose();
}

} // namespace Chaos

#endif
