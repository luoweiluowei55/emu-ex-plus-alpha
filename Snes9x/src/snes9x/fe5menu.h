// fe5menu.h — FE5 menu hanzi engine ("entry-is-glyph" v30.x) ported from MesenCE
// (RegisterHandlerB.h/.cpp + SnesPpu.cpp hanzi paths) to Snes9x.
//
// Protocol (CPU register-page mirror $xx:$2FA0-$2FA6 — open-bus window, maps to
// B-bus $21A0-$21A6 on real hardware; NOT the WRAM address $7E:2FA0):
//   $2FA0 w  code low byte (lead)
//   $2FA1 w  code high byte (trail) — write triggers synchronous ResolveCode
//   $2FA0/$2FA1 r  top tilemap entry value ($FFFF = miss)
//   $2FA5/$2FA6 r  bottom tilemap entry value ($FFFF = miss)
//   $2FA2 w  text-pointer page (X>>8) — HZRNG validation context
//   $2FA3/$2FA4 w  top entry mirror ($2FA4 also feeds SetHanziAttrs — reader-less in v30.4)
//   $2FA5/$2FA6 w  bottom entry mirror ($2FA6 write clears pending-hit)
//
// Entry encoding (tilemap word as stored by the stub into the window tilemap):
//   bit15 = magic (never set by native text)      bit14 = glyph block
//   bits13-10 = game's own palette/priority (OR'd by the stub, engine keeps them)
//   bits9-0  = half-glyph slot (0..1023 minus ENTEX holes; two halves = consecutive ranks)
//
// ROM tables (all located by magic scan, no fixed offsets):
//   FE5HX  font: 5B magic + 0x01 + u16 count + count*32 bytes (8x16 glyph = two
//          16B halves, 8 rows x 2 bytes, low byte = plane 0 — identical to the
//          Snes9x/Mesen VRAM 2bpp row-word format)
//   ENTEX  native-flip exclusion set: 5B magic + u16 count + count*2 bytes (u16 slots)
//   HZRNG  patched-string ranges: 5B magic + u16 count + count*6 bytes
//          [bank][start lo][start hi][end lo][end hi][pad]
//   CNREP/CNRPG label records: 5B magic + u16 count [+ u16 gate for CNRPG] +
//          count*24 [tm u16][colMin][colMax][slMin u16][slMax u16][16B glyph half]
//   CNRPH  signature label records: 5B magic + u16 count + u16 gate +
//          count*40 [tm u16][colMin][colMax][slMin u16][slMax u16][16B VRAM sig][16B glyph half]
//
// Layer note: everything called "layer 2" here is the layer driven by $2109 (BG3SC)
// and the high nibble of $210C (BG34NBA) — Snes9x BG index 2, mode-1 2bpp.
// Do NOT map by the Mesen comment's "BG2" name; map by register.

#pragma once
#include <stdint.h>

namespace FE5Menu
{
	// Magic-entry classification for the BG3 2bpp fetch path (Mesen GetChrData equivalent).
	//   0 = native entry (no magic)          -> render from VRAM
	//   1 = ENTEX-reserved (native decoration with flips) -> render from VRAM, flips legit
	//   2 = our glyph (*glyphOut = glyph index, *halfOut = 0 top / 1 bottom)
	int ClassifyEntry(uint16_t entry, uint32_t* glyphOut, uint32_t* halfOut);

	// Phase 2 accessors: font table for the glyph pixel cache.
	// Returns the 16B half (8 rows x 2 bytes, low byte = plane 0 — VRAM 2bpp row format),
	// or nullptr if out of range. half: 0 = top, 1 = bottom.
	const uint8_t* FontHalf(uint32_t glyph, uint32_t half);
	uint32_t FontCount();

	// CNREP lookup (Mesen GetChrData pre-magic branch). Call for every BG3 2bpp tile
	// drawn at (column, scanline) with the FULL tilemap entry (palette/flip bits intact).
	// gateTma = live $2109-layer tilemap address in the same form Mesen stores
	// ((value & 0x7C) << 8 — identical to Snes9x PPU.BG[2].SCBase).
	// chrBaseWords = live $2109-layer char base in the Mesen form ((value & 0x07) << 12
	// — identical to Snes9x PPU.BG[2].NameBase), used by the CNRPH VRAM signature check.
	// vram = word-addressable VRAM (uint16 array, 0x8000 words).
	// Returns pointer to 16B glyph half (8 rows x 2 bytes, low=plane0), or nullptr.
	const uint8_t* CnrepLookup(uint16_t entry, int column, int scanline,
		uint16_t gateTma, uint16_t chrBaseWords, const uint16_t* vram);

	// ClearBlankTile: on every successful resolve, zero the $01DF blank/fill tile in
	// every 2bpp view. Implemented internally on resolve-hit; needs live PPU facts via
	// the host hooks below.
	struct HostHooks
	{
		uint8_t (*getBgMode)();                    // PPU.BGMode (0..7)
		uint16_t (*getLayerChrBase)(int layer);    // per-layer char base, Mesen ChrAddress
		                                           // form (words): $210B low nibble -> layer0
		                                           // (v&0x07)<<12; $210C high nibble -> layer3
		                                           // (v&0x70)<<8; etc.
		uint16_t* vram;                            // word-addressable VRAM (0x8000 words)
		void (*invalidateTile)(uint32_t tileNumber); // pixel-cache flush for a 2bpp tile number
		                                             // (null = skip invalidation, e.g. pre-cache build)
	};
	void SetHostHooks(const HostHooks* hooks);

	// Optional traffic probe (host sets; null = silent). Fires for every
	// intercepted $2FA0-$2FA6 access, after the access is processed.
	void SetProbe(void (*fn)(uint16_t addr, uint8_t value, bool isWrite));

	// Host wiring
	void Init(const uint8_t* rom, uint32_t romSize);
	void Reset();
	bool IsLoaded();   // true once the ENTEX table was found (v30 protocol marker)

	// CPU register-page window hooks. Return true if the access was consumed.
	// addr = (CPU address & 0xFFFF) as seen by S9xSetPPU/S9xGetPPU ($2000-$3FFF window).
	bool HandleWrite(uint16_t addr, uint8_t value);
	bool HandleRead(uint16_t addr, uint8_t* value);
}
