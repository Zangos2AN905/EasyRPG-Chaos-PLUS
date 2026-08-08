/*
 * Chaos Fork: Undertale Font Loader implementation
 */

#include "chaos/undertale_font.h"
#include "filefinder.h"
#include "font.h"
#include "output.h"
#include "utils.h"

namespace Chaos {

namespace {
	FontRef dialogue_font;
	FontRef dialogue_sans_font;
	FontRef stats_font;
	FontRef damage_font;

	FontRef LoadChaosFont(const char* filename, int size) {
		auto chaos_fs = FileFinder::ChaosAssets();
		if (!chaos_fs) {
			Output::Debug("UndertaleFont: ChaosAssets not available");
			return nullptr;
		}

		auto stream = chaos_fs.OpenFile("UndertaleMode.content/Font", filename, FileFinder::FONTS_TYPES);
		if (!stream) {
			Output::Debug("UndertaleFont: Could not find {}", filename);
			return nullptr;
		}

		auto font = Font::CreateFtFont(std::move(stream), size, false, false);
		if (!font) {
			Output::Debug("UndertaleFont: Failed to create font from {}", filename);
		}
		return font;
	}
}

FontRef UndertaleFont::Dialogue() {
	if (!dialogue_font) {
		dialogue_font = LoadChaosFont("DialogueFont", 15);
	}
	return dialogue_font;
}

FontRef UndertaleFont::DialogueSans() {
	if (!dialogue_sans_font) {
		dialogue_sans_font = LoadChaosFont("DialogueFont_Sans", 15);
	}
	return dialogue_sans_font;
}

FontRef UndertaleFont::Stats() {
	if (!stats_font) {
		stats_font = LoadChaosFont("HP_NAME_Font", 12);
	}
	return stats_font;
}

FontRef UndertaleFont::Damage() {
	if (!damage_font) {
		damage_font = LoadChaosFont("Damage_Font", 16);
	}
	return damage_font;
}

void UndertaleFont::Dispose() {
	dialogue_font.reset();
	dialogue_sans_font.reset();
	stats_font.reset();
	damage_font.reset();
}

} // namespace Chaos
