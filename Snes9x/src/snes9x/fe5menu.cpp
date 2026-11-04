// fe5menu.cpp — FE5 menu hanzi engine ("entry-is-glyph" v30.x), ported from MesenCE
// Core/SNES/RegisterHandlerB.cpp (branch mesen221, HEAD e4fafc94+eb1bd2ce) to Snes9x.
//
// Faithful translation of: HanziMenuEngine (LoadFontTable/LoadRangeTable/LoadExclTable/
// LoadCnrepTable/LoadCnrepSigTable/InPatchedString/EntryForGlyph/ResolveCode/ReadReg/
// WriteReg/ClearBlankTile) plus the SnesPpu::GetChrData hanzi branches re-exposed as
// ClassifyEntry/CnrepLookup. Mesen-only glue (serializer blob, IMemoryHandler plumbing,
// trace logging) is intentionally not ported: v30 keeps no functional engine state, the
// entries live in the tilemap itself, so Snes9x snapshots need nothing here.
//
// The host (S9xSetPPU/S9xGetPPU hook + gfx.cpp DrawBackground hook) owns the routing:
//   - CPU write/read to $xx:$2FA0-$2FA6  -> HandleWrite/HandleRead (synchronous; the
//     stub reads the latches back immediately after writing the code high byte)
//   - BG3 (=$2109 layer) 2bpp tile fetch -> CnrepLookup first, then ClassifyEntry

#include "fe5menu.h"

namespace FE5Menu
{
	static constexpr uint32_t MaxExclEntries = 32;

	static const uint8_t* _rom = nullptr;
	static uint32_t _romSize = 0;
	static uint32_t _fontData = 0;
	static uint32_t _fontCount = 0;
	static uint32_t _cnrepData = 0;
	static uint32_t _cnrepCount = 0;
	static uint16_t _cnrepGate = 0;
	static uint32_t _cnrepSigData = 0;
	static uint32_t _cnrepSigCount = 0;
	static uint16_t _cnrepSigGate = 0;
	static uint16_t _excl[MaxExclEntries] = {};
	static uint32_t _exclCount = 0;
	static bool _exclFound = false;
	static uint32_t _glyphsPerClass = 0;
	static uint32_t _rangeData = 0;
	static uint32_t _rangeCount = 0;
	static uint32_t _romBase = 0;   //0x200 when the image still carries a copier header

	static uint8_t _codeLow = 0;
	static uint8_t _codeHigh = 0;
	static uint16_t _latch = 0xFFFF;
	static uint16_t _botLatch = 0xFFFF;
	static bool _hit = false;
	static uint32_t _index = 0;
	static uint8_t _strPage = 0;
	static uint16_t _topEntry = 0;
	static uint16_t _botEntry = 0;

	static const HostHooks* _hooks = nullptr;
	static void (*_probe)(uint16_t addr, uint8_t value, bool isWrite) = nullptr;

	//---------------------------------------------------------------------------------
	// ROM table loaders (magic scans, verbatim semantics)
	//---------------------------------------------------------------------------------

	static void LoadFontTable()
	{
		//Scan the ROM image for the "FE5HX" magic (robust to copier-header presence)
		_fontData = 0;
		_fontCount = 0;
		if(_rom == nullptr || _romSize < 32) {
			return;
		}
		for(uint32_t i = 0; i + 16 <= _romSize; i++) {
			if(_rom[i] == 'F' && _rom[i+1] == 'E' && _rom[i+2] == '5' && _rom[i+3] == 'H' && _rom[i+4] == 'X' && _rom[i+5] == 0x01) {
				uint32_t count = _rom[i+6] | (_rom[i+7] << 8);
				if(count > 0 && i + 16 + count * 32 <= _romSize) {
					_fontData = i + 16;
					_fontCount = count;
					return;
				}
			}
		}
	}

	static void LoadCnrepTable()
	{
		//v37/v38: find "CNRPG" (gated) first, fall back to "CNREP" (ungated).
		//24-byte records [tm u16][colMin][colMax][slMin u16][slMax u16][16B glyph half].
		_cnrepData = 0;
		_cnrepCount = 0;
		_cnrepGate = 0;
		if(_rom == nullptr || _romSize < 16) {
			return;
		}
		for(uint32_t i = 0; i + 9 <= _romSize; i++) {
			if(_rom[i] == 'C' && _rom[i+1] == 'N' && _rom[i+2] == 'R' && _rom[i+3] == 'P' && _rom[i+4] == 'G') {
				uint32_t count = _rom[i+5] | (_rom[i+6] << 8);
				if(count == 0 || i + 9 + count * 24 > _romSize) {
					continue;   //not a plausible table: keep scanning
				}
				_cnrepData = i + 9;
				_cnrepCount = count;
				_cnrepGate = (uint16_t)(_rom[i+7] | (_rom[i+8] << 8));
				return;
			}
		}
		for(uint32_t i = 0; i + 7 <= _romSize; i++) {
			if(_rom[i] == 'C' && _rom[i+1] == 'N' && _rom[i+2] == 'R' && _rom[i+3] == 'P' && _rom[i+4] == 'E') {
				uint32_t count = _rom[i+5] | (_rom[i+6] << 8);
				if(count == 0 || i + 7 + count * 24 > _romSize) {
					continue;
				}
				_cnrepData = i + 7;
				_cnrepCount = count;
				_cnrepGate = 0;
				return;
			}
		}
	}

	static void LoadCnrepSigTable()
	{
		//v39: "CNRPH" roster-header block, 40-byte records with a 16B VRAM signature.
		_cnrepSigData = 0;
		_cnrepSigCount = 0;
		_cnrepSigGate = 0;
		if(_rom == nullptr || _romSize < 16) {
			return;
		}
		for(uint32_t i = 0; i + 9 <= _romSize; i++) {
			if(_rom[i] == 'C' && _rom[i+1] == 'N' && _rom[i+2] == 'R' && _rom[i+3] == 'P' && _rom[i+4] == 'H') {
				uint32_t count = _rom[i+5] | (_rom[i+6] << 8);
				if(count == 0 || i + 9 + count * 40 > _romSize) {
					continue;   //not a plausible table: keep scanning
				}
				_cnrepSigData = i + 9;
				_cnrepSigCount = count;
				_cnrepSigGate = (uint16_t)(_rom[i+7] | (_rom[i+8] << 8));
				return;
			}
		}
	}

	static void LoadRangeTable()
	{
		//Scan for the "HZRNG" table: u16 count, then 6-byte entries
		//(bank, start lo/hi, end lo/hi, pad) covering every string the tool rewrote.
		_rangeData = 0;
		_rangeCount = 0;
		_romBase = (_romSize % 0x8000 == 0x200) ? 0x200 : 0;
		if(_rom == nullptr || _romSize < 16) {
			return;
		}
		for(uint32_t i = 0; i + 8 <= _romSize; i++) {
			if(_rom[i] == 'H' && _rom[i+1] == 'Z' && _rom[i+2] == 'R' && _rom[i+3] == 'N' && _rom[i+4] == 'G') {
				uint32_t count = _rom[i+5] | (_rom[i+6] << 8);
				if(count > 0 && i + 8 + count * 6 <= _romSize) {
					_rangeData = i + 8;
					_rangeCount = count;
					return;
				}
			}
		}
	}

	static void LoadExclTable()
	{
		//Scan for the "ENTEX" table: u16 count, then u16 tile indices (sorted ascending).
		//Presence is the v30 protocol marker: without it the engine stays inert.
		_exclCount = 0;
		_exclFound = false;
		_glyphsPerClass = (0x400 - _exclCount) / 2;
		if(_rom == nullptr || _romSize < 16) {
			return;
		}
		for(uint32_t i = 0; i + 7 <= _romSize; i++) {
			if(_rom[i] == 'E' && _rom[i+1] == 'N' && _rom[i+2] == 'T' && _rom[i+3] == 'E' && _rom[i+4] == 'X') {
				uint32_t count = _rom[i+5] | (_rom[i+6] << 8);
				if(count > MaxExclEntries || i + 7 + count * 2 > _romSize) {
					continue;   //not a plausible table: keep scanning
				}
				for(uint32_t k = 0; k < count; k++) {
					_excl[k] = (uint16_t)(_rom[i + 7 + k * 2] | (_rom[i + 8 + k * 2] << 8));
				}
				_exclCount = count;
				_exclFound = true;
				for(uint32_t a = 0; a + 1 < _exclCount; a++) {
					for(uint32_t b = 0; b + 1 < _exclCount - a; b++) {
						if(_excl[b] > _excl[b + 1]) {
							uint16_t tmp = _excl[b];
							_excl[b] = _excl[b + 1];
							_excl[b + 1] = tmp;
						}
					}
				}
				_glyphsPerClass = (0x400 - _exclCount) / 2;
				return;
			}
		}
	}

	//---------------------------------------------------------------------------------
	// Code resolution
	//---------------------------------------------------------------------------------

	static bool InPatchedString()
	{
		//The stub sends the page (X >> 8) of the text pointer right before the code bytes.
		//A synthetic code is only accepted when that page belongs to a string the tool
		//rewrote AND the code bytes really occur inside that string on that page.
		if(_rangeCount == 0) {
			return true;
		}
		for(uint32_t i = 0; i < _rangeCount; i++) {
			const uint8_t* e = _rom + _rangeData + i * 6;
			uint16_t start = (uint16_t)(e[1] | (e[2] << 8));
			uint16_t end = (uint16_t)(e[3] | (e[4] << 8));
			if(end <= start) {
				continue;
			}
			uint16_t pageBase = (uint16_t)(_strPage << 8);
			if((uint32_t)pageBase + 0xFF < start || pageBase > end - 1) {
				continue;
			}
			uint32_t from = (uint32_t)start > (uint32_t)pageBase ? (uint32_t)start : (uint32_t)pageBase;
			//A synthetic code may START on the page's last byte ($xxFF) and straddle onto
			//the next page; clamp to end-1 (not end-2) so the second code byte stays inside
			//the exclusive range [start, end).
			uint32_t pageEnd = (uint32_t)pageBase + 0x100;
			uint32_t rangeEnd = (uint32_t)end - 1;
			uint32_t to = pageEnd < rangeEnd ? pageEnd : rangeEnd;
			//LoROM mapping used by this ROM: pc = ((bank | 0x80) << 15) | (addr & 0x7FFF)
			uint32_t pcBase = (((uint32_t)((e[0] | 0x80) & 0xFF)) << 15) & 0x7FFFFF;
			for(uint32_t k = from; k < to; k++) {
				uint32_t romIdx = _romBase + pcBase + (k & 0x7FFF);
				if(romIdx + 1 < _romSize && _rom[romIdx] == _codeLow && _rom[romIdx + 1] == _codeHigh) {
					return true;
				}
			}
		}
		return false;
	}

	static uint16_t EntryForGlyph(uint32_t index, bool bottom)
	{
		//v30.4 entry encoding: bit15 = magic, bit14 = glyph block, bits9-0 = half-glyph
		//slot, bits13-10 left zero for the stub to OR the game's palette/priority into.
		//The slot walk inserts the ENTEX gaps: rank r maps to the r-th non-excluded slot.
		uint32_t block = index / _glyphsPerClass;
		uint32_t rank = (index % _glyphsPerClass) * 2 + (bottom ? 1u : 0u);
		uint32_t slot = rank;
		for(uint32_t i = 0; i < _exclCount; i++) {
			if(_excl[i] <= slot) {
				slot++;
			}
		}
		return (uint16_t)(0x8000 | (block << 14) | slot);
	}

	static void ClearBlankTile()
	{
		//Tile $1DF of the 2bpp window font is the game's blank/padding tile; the settings
		//screen's font preload dumps a kanji bottom half into it. Zero it after each
		//resolved glyph. Only 2bpp layer views are touched: in a 4bpp view tile $1DF is
		//live map terrain graphics.
		if(_hooks == nullptr || _hooks->vram == nullptr || _hooks->getBgMode == nullptr || _hooks->getLayerChrBase == nullptr) {
			return;
		}
		uint32_t cell = 0x1DF;
		uint8_t bgMode = _hooks->getBgMode();
		for(uint32_t layer = 0; layer < 4; layer++) {
			bool bpp4;
			switch(bgMode) {
				case 0: bpp4 = false; break;
				case 1:
				case 2: bpp4 = layer < 2; break;
				case 3: bpp4 = layer >= 1; break;
				case 4: bpp4 = layer >= 2; break;
				default: bpp4 = layer < 2; break;
			}
			if(bpp4) {
				continue;
			}
			uint32_t base = (uint32_t)_hooks->getLayerChrBase((int)layer) * 2 + cell * 16;   //byte address
			for(uint32_t i = 0; i < 16; i++) {
				((uint8_t*)_hooks->vram)[base + i] = 0;
			}
			if(_hooks->invalidateTile != nullptr) {
				//2bpp tile number = byte address >> 4 (16 bytes per 2bpp tile)
				_hooks->invalidateTile(base >> 4);
			}
		}
	}

	static void ResolveCode()
	{
		//Resolve the code straight into the two tilemap entry values. The stub writes
		//them into the window tilemap buffer; the PPU fetch hook recognizes the magic
		//bits and serves the pixels from the ROM font table. Nothing is drawn here and
		//no VRAM byte is touched by the entry mechanism itself.
		_hit = false;
		_latch = 0xFFFF;
		_botLatch = 0xFFFF;
		if(_fontCount == 0 || !IsLoaded() || !InPatchedString()) {
			return;
		}
		uint8_t lead = _codeLow, trail = _codeHigh;
		if(lead < 0x98 || lead > 0xA0 || trail < 0x84 || trail > 0xFC) {
			return;
		}
		uint32_t index = (uint32_t)(lead - 0x98) * 121 + (uint32_t)(trail - 0x84);
		if(index >= _fontCount) {
			return;
		}
		if(_glyphsPerClass == 0 || index >= _glyphsPerClass * 2) {
			//The exclusion set ate too much slot space for this font: refuse rather than
			//mis-encode (961 glyphs fit as long as ENTEX stays below ~55 entries).
			return;
		}
		_hit = true;
		_index = index;
		_topEntry = EntryForGlyph(index, false);
		_botEntry = EntryForGlyph(index, true);
		_latch = _topEntry;
		_botLatch = _botEntry;
		ClearBlankTile();
	}

	//---------------------------------------------------------------------------------
	// Public interface
	//---------------------------------------------------------------------------------

	void SetHostHooks(const HostHooks* hooks)
	{
		_hooks = hooks;
	}

	void SetProbe(void (*fn)(uint16_t addr, uint8_t value, bool isWrite))
	{
		_probe = fn;
	}

	void Init(const uint8_t* rom, uint32_t romSize)
	{
		_rom = rom;
		_romSize = romSize;
		LoadFontTable();
		LoadRangeTable();
		LoadExclTable();
		LoadCnrepTable();
		LoadCnrepSigTable();
		//v30 requires the ENTEX table: without it the engine stays completely inert
		//(pre-v30 ROMs can never emit entries the PPU misreads).
		Reset();
	}

	void Reset()
	{
		_codeLow = 0;
		_codeHigh = 0;
		_latch = 0xFFFF;
		_botLatch = 0xFFFF;
		_hit = false;
		_index = 0;
		_strPage = 0;
		_topEntry = 0;
		_botEntry = 0;
	}

	bool IsLoaded()
	{
		return _exclFound;
	}

	bool HandleWrite(uint16_t addr, uint8_t value)
	{
		switch(addr) {
			case 0x2FA0: _codeLow = value; if(_probe) _probe(addr, value, true); return true;
			case 0x2FA1:
				//High byte of the code completes it: resolve synchronously into the
				//entry latches. The stub reads them back on the very next instructions.
				_codeHigh = value;
				ResolveCode();
				if(_probe) _probe(addr, value, true);
				return true;
			case 0x2FA3: _topEntry = (_topEntry & 0xFF00) | value; return true;
			case 0x2FA4:
				//v30.2 attrs side-channel: reader-less since v30.4 (each entry carries
				//its own palette/priority in bits 13-10). Keep the protocol shape only.
				_topEntry = (_topEntry & 0x00FF) | (uint16_t)(value << 8);
				return true;
			case 0x2FA5: _botEntry = (_botEntry & 0xFF00) | value; return true;
			case 0x2FA6:
				_botEntry = (_botEntry & 0x00FF) | (uint16_t)(value << 8);
				_hit = false;
				return true;
			case 0x2FA2:
				//Page (X >> 8) of the text pointer; HZRNG validation context.
				_strPage = value;
				return true;
		}
		return false;
	}

	bool HandleRead(uint16_t addr, uint8_t* value)
	{
		switch(addr) {
			case 0x2FA0: *value = (uint8_t)(_latch & 0xFF); return true;
			case 0x2FA1: *value = (uint8_t)(_latch >> 8); return true;
			case 0x2FA5: *value = (uint8_t)(_botLatch & 0xFF); return true;
			case 0x2FA6: *value = (uint8_t)(_botLatch >> 8); return true;
		}
		return false;
	}

	const uint8_t* FontHalf(uint32_t glyph, uint32_t half)
	{
		if(_fontCount == 0 || glyph >= _fontCount || half > 1) {
			return nullptr;
		}
		return _rom + _fontData + glyph * 32 + half * 16;
	}

	uint32_t FontCount()
	{
		return _fontCount;
	}

	int ClassifyEntry(uint16_t entry, uint32_t* glyphOut, uint32_t* halfOut)
	{
		//SnesPpu::GetChrData magic-entry branch. Call only for the $2109 layer (BG3)
		//2bpp fetch. Returns 0 = native (render from VRAM, entry untouched),
		//1 = ENTEX-reserved (native decoration; flips legitimate),
		//2 = our glyph (glyphOut/halfOut valid).
		if(_fontCount == 0 || (entry & 0x8000) == 0) {
			return 0;
		}
		uint16_t slot = entry & 0x3FF;
		uint32_t rank = slot;
		bool reserved = false;
		for(uint32_t i = 0; i < _exclCount; i++) {
			if(_excl[i] == slot) {
				reserved = true;
				break;
			}
			if(_excl[i] < slot) {
				rank--;
			}
		}
		uint32_t glyphsPerBlock = (0x400 - _exclCount) / 2;
		uint32_t block = (entry >> 14) & 1;
		uint32_t glyph = block * glyphsPerBlock + (rank >> 1);
		if(!reserved && glyph < _fontCount) {
			*glyphOut = glyph;
			*halfOut = rank & 1;
			return 2;
		}
		return reserved ? 1 : 0;
	}

	const uint8_t* CnrepLookup(uint16_t entry, int column, int scanline,
		uint16_t gateTma, uint16_t chrBaseWords, const uint16_t* vram)
	{
		//v37/v38/v39 label replacement (SnesPpu::GetChrData pre-magic branches).
		//Keys match the FULL entry value (palette/flip bits included). On a hit the
		//caller renders the returned 16B half (8 rows, low byte = plane 0) as the whole
		//8x8 cell; the entry's own attrs flow through untouched.
		if(_cnrepSigCount > 0 && (_cnrepSigGate == 0 || gateTma == _cnrepSigGate)) {
			for(uint32_t i = 0; i < _cnrepSigCount; i++) {
				const uint8_t* rec = _rom + _cnrepSigData + i * 40;
				uint16_t rtm = (uint16_t)(rec[0] | (rec[1] << 8));
				if(rtm != entry || column < rec[2] || column > rec[3]) {
					continue;
				}
				uint16_t slMin = (uint16_t)(rec[4] | (rec[5] << 8));
				uint16_t slMax = (uint16_t)(rec[6] | (rec[7] << 8));
				if(scanline < slMin || scanline > slMax) {
					continue;
				}
				//CNRPH: the live font-page tile must still match the captured signature
				//(the roster reloads its font page per tab over a shared header row).
				uint16_t tileStart = (uint16_t)(chrBaseWords + (uint16_t)((entry & 0x3FF) * 4 * 2));
				const uint8_t* sig = rec + 8;
				bool sigOk = true;
				for(uint32_t w = 0; w < 8; w++) {
					if(vram[(tileStart + w) & 0x7FFF] != (uint16_t)(sig[w * 2] | (sig[w * 2 + 1] << 8))) {
						sigOk = false;
						break;
					}
				}
				if(!sigOk) {
					continue;
				}
				return rec + 24;
			}
		}
		if(_cnrepCount > 0 && (_cnrepGate == 0 || gateTma == _cnrepGate)) {
			for(uint32_t i = 0; i < _cnrepCount; i++) {
				const uint8_t* rec = _rom + _cnrepData + i * 24;
				uint16_t rtm = (uint16_t)(rec[0] | (rec[1] << 8));
				if(rtm != entry || column < rec[2] || column > rec[3]) {
					continue;
				}
				uint16_t slMin = (uint16_t)(rec[4] | (rec[5] << 8));
				uint16_t slMax = (uint16_t)(rec[6] | (rec[7] << 8));
				if(scanline < slMin || scanline > slMax) {
					continue;
				}
				return rec + 8;
			}
		}
		return nullptr;
	}
}
