// fe5render.h — Snes9x PPU glue for the FE5 menu hanzi engine (fe5menu.h).
//
// Integration points (see PATCHES.md for exact insertion snippets):
//   1. gfx.cpp DrawBackground, right after each `Tile = READ_WORD(t);`
//      (main path :947/:984/:1021 — mosaic/offset variants intentionally not wired):
//          Tile = FE5Render::TranslateEntry(Tile, column, scanline,
//                                           bg3TilemapAddr, bg3ChrBase,
//                                           (const uint16_t*) Memory.VRAM);
//   2. tileimpl.h CachedTile::GetCachedTile(), top of function:
//          if (FE5Render::GlyphPending()) { pCache = FE5Render::GlyphBuffer64();
//                                           TileNumber = 0; return; }
//      and CachedTile::IsBlankTile(): return false when GlyphPending().
//
// Design notes:
//  - The magic entry's bit15/bit14 (glyph class/block) collide with Snes9x's
//    V_FLIP/H_FLIP and DrawTile16::Draw reads flips from the Tile parameter, so
//    TranslateEntry strips them and keeps bits13-10 (priority + palette) and the
//    slot in bits9-0. CNREP keys use the FULL original entry and are checked first.
//  - Glyph pixels come from the ROM font table (FE5HX, 16B halves in VRAM 2bpp
//    row-word format) and are converted to Snes9x's 64B tile-cache format
//    (8 rows x 8 pixels, values 0-3 indexing the entry's 4-color palette) on the
//    fly — same as Mesen serving pixels at fetch time; no VRAM is ever written.
//  - Pending state is set fresh by every TranslateEntry call (1:1 with a DrawTile
//    call), so no explicit clear is needed.

#pragma once
#include <stdint.h>

namespace FE5Render
{
	// BG3 2bpp fetch path only. Returns the entry to pass down to DrawTile*:
	// native/ENTEX entries unchanged, our glyphs stripped to (palette|slot).
	// gateTma/chrBaseWords: live $2109-layer tilemap/char base in the Mesen forms
	// ((reg & 0x7C) << 8 / ((reg & 0x70) << 8)); vram: word-addressable VRAM.
	uint32_t TranslateEntry(uint32_t tile, int column, int scanline,
		uint16_t gateTma, uint16_t chrBaseWords, const uint16_t* vram);

	bool GlyphPending();          // true for the tile just translated as ours
	uint8_t* GlyphBuffer64();     // 64B pixel buffer in Snes9x tile-cache format
}
