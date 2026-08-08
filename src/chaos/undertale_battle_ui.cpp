/*
 * Chaos Fork: Undertale Battle UI implementation
 */

#include "chaos/undertale_battle_ui.h"
#include "filefinder.h"
#include "output.h"
#include "options.h"
#include "player.h"
#include "drawable.h"
#include "drawable_mgr.h"

namespace Chaos {

namespace {
	constexpr Color kUndertaleButtonSelectedColor(255, 255, 0, 255);
	constexpr Color kUndertaleButtonNormalColor(255, 176, 0, 255);

	void BuildColoredButton(Bitmap& dst, const Bitmap& src_sheet, int src_x, const Color& color) {
		auto* src_pixels = reinterpret_cast<const uint32_t*>(src_sheet.pixels());
		auto* dst_pixels = reinterpret_cast<uint32_t*>(dst.pixels());
		const auto& format = Bitmap::pixel_format;

		for (int y = 0; y < UndertaleButton::SHEET_HEIGHT; ++y) {
			for (int x = 0; x < UndertaleButton::HALF_WIDTH; ++x) {
				uint8_t red = 0;
				uint8_t green = 0;
				uint8_t blue = 0;
				uint8_t alpha = 0;

				format.uint32_to_rgba(
					src_pixels[y * src_sheet.GetWidth() + (src_x + x)],
					red, green, blue, alpha);

				const bool is_transparent_key = red <= 8 && green <= 8 && blue <= 8;

				if (alpha == 0 || is_transparent_key) {
					dst_pixels[y * UndertaleButton::HALF_WIDTH + x] = format.rgba_to_uint32_t(0, 0, 0, 0);
					continue;
				}

				dst_pixels[y * UndertaleButton::HALF_WIDTH + x] = format.rgba_to_uint32_t(
					color.red,
					color.green,
					color.blue,
					alpha);
			}
		}
	}
}

// ---- UndertaleButton ----

static const char* ButtonFileName(UndertaleButtonType type) {
	switch (type) {
		case UndertaleButtonType::Fight: return "fightbutton";
		case UndertaleButtonType::Act:   return "actbutton";
		case UndertaleButtonType::Item:  return "itembutton";
		case UndertaleButtonType::Mercy: return "mercybutton";
		default: return "fightbutton";
	}
}

UndertaleButton::UndertaleButton(UndertaleButtonType type)
	: Sprite(Drawable::Flags::Default),
	  button_type(type)
{
	SetZ(Priority_Window + 1);
	LoadSprite();
	UpdateAppearance();
}

void UndertaleButton::LoadSprite() {
	auto chaos_fs = FileFinder::ChaosAssets();
	if (!chaos_fs) {
		Output::Debug("UndertaleButton: ChaosAssets not available");
		return;
	}

	auto stream = chaos_fs.OpenFile("UndertaleMode.content/Sprite/GUI", ButtonFileName(button_type), FileFinder::IMG_TYPES);
	if (!stream) {
		Output::Debug("UndertaleButton: Could not find {}", ButtonFileName(button_type));
		return;
	}

	auto sheet = Bitmap::Create(std::move(stream), true);
	if (!sheet) {
		return;
	}

	selected_bitmap = Bitmap::Create(HALF_WIDTH, SHEET_HEIGHT, true);
	normal_bitmap = Bitmap::Create(HALF_WIDTH, SHEET_HEIGHT, true);
	if (!selected_bitmap || !normal_bitmap) {
		return;
	}

	selected_bitmap->Clear();
	normal_bitmap->Clear();

	BuildColoredButton(*selected_bitmap, *sheet, 0, kUndertaleButtonSelectedColor);
	BuildColoredButton(*normal_bitmap, *sheet, HALF_WIDTH, kUndertaleButtonNormalColor);
}

void UndertaleButton::UpdateAppearance() {
	auto bitmap = is_selected ? selected_bitmap : normal_bitmap;
	if (!bitmap) {
		return;
	}

	SetBitmap(bitmap);
	SetSrcRect(Rect(0, 0, HALF_WIDTH, SHEET_HEIGHT));
}

void UndertaleButton::SetSelected(bool selected) {
	if (is_selected != selected) {
		is_selected = selected;
		UpdateAppearance();
	}
}

// ---- UndertaleBattleBox ----

UndertaleBattleBox::UndertaleBattleBox()
	: Drawable(Priority_Window, Drawable::Flags::Default)
{
	DrawableMgr::Register(this);
}

void UndertaleBattleBox::Draw(Bitmap& dst) {
	// Outer white border
	dst.FillRect(Rect(box_x, box_y, box_width, box_height), Color(255, 255, 255, 255));
	// Inner black fill
	dst.FillRect(Rect(box_x + border_thickness, box_y + border_thickness,
					   box_width - border_thickness * 2, box_height - border_thickness * 2),
				 Color(0, 0, 0, 255));
}

void UndertaleBattleBox::SetBounds(int x, int y, int width, int height) {
	box_x = x;
	box_y = y;
	box_width = width;
	box_height = height;
}

} // namespace Chaos
