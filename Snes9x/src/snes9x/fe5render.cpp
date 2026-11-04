// fe5render.cpp — Snes9x PPU glue implementation (see fe5render.h).

#include "fe5render.h"
#include "fe5menu.h"

namespace FE5Render
{
	static uint8_t s_buf[64];
	static bool s_pending = false;

	// Convert a 16B glyph half (VRAM 2bpp row-word format: row y at bytes 2y/2y+1,
	// low byte = plane 0, bit 7 = leftmost pixel) into Snes9x's 64B tile-cache format
	// (8 rows x 8 pixel bytes, values 0-3 = palette-relative color index).
	static void ConvertHalfToCache(const uint8_t* half)
	{
		for(int y = 0; y < 8; y++) {
			uint16_t w = (uint16_t)(half[y * 2] | (half[y * 2 + 1] << 8));
			for(int x = 0; x < 8; x++) {
				s_buf[y * 8 + x] = (uint8_t)(((w >> (7 - x)) & 1) | (((w >> (15 - x)) & 1) << 1));
			}
		}
	}

	uint32_t TranslateEntry(uint32_t tile, int column, int scanline,
		uint16_t gateTma, uint16_t chrBaseWords, const uint16_t* vram)
	{
		s_pending = false;
		//CNREP/CNRPG/CNRPH label replacement FIRST: the font-page screens' fixed
		//Japanese labels are BAKED, non-magic entries, so this lookup must run
		//before any bit15 test - MesenCE applies it ahead of the magic branch and
		//keys each record on the FULL entry value (palette/flip bits included).
		const uint8_t* half = FE5Menu::CnrepLookup((uint16_t)tile, column, scanline,
			gateTma, chrBaseWords, vram);
		if(half != nullptr) {
			ConvertHalfToCache(half);
			s_pending = true;
			//Mesen leaves TilemapData exactly as the game wrote it on a CNREP hit
			//(attrs incl. flips flow through); GetCachedTile serves the record's
			//16B half from the glyph buffer.
			return tile;
		}
		if(!(tile & 0x8000)) {
			return tile;
		}
		uint32_t glyph = 0, h = 0;
		int cls = FE5Menu::ClassifyEntry((uint16_t)tile, &glyph, &h);
		if(cls == 1) {
			//ENTEX-reserved: native decoration, its flips are legitimate.
			return tile;
		}
		if(cls != 2) {
			return tile;
		}
		half = FE5Menu::FontHalf(glyph, h);
		if(half == nullptr) {
			return tile;
		}
		ConvertHalfToCache(half);
		s_pending = true;
		//Strip bit15 (magic/V_FLIP) and bit14 (block/H_FLIP); keep bits13-10
		//(priority + palette — DrawTile16 consumes them from the Tile parameter)
		//and the slot in bits9-0 (GetCachedTile bypasses VRAM on this path).
		return (tile & 0x3FFF) | 0x0000;
	}

	bool GlyphPending()
	{
		return s_pending;
	}

	uint8_t* GlyphBuffer64()
	{
		return s_buf;
	}
}
