// Smoke test FabGL IDF 6.2 : barres 16 couleurs en écriture brute, texte, VScroll, touches PS/2
#include <cstdio>
#include "esp_heap_caps.h"
#include "fabgl.h"

static fabgl::VGA16Controller s_vga;
static fabgl::PS2Controller s_ps2;

static inline void put4(uint8_t *row, int x, uint8_t c)
{
  uint8_t &b = row[x >> 1];
  b = (x & 1) ? ((b & 0xF0) | c) : ((b & 0x0F) | (c << 4));
}

extern "C" void app_main()
{
  printf("DMA libre avant VGA : %u\n", (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA));
  s_ps2.begin(PS2Preset::KeyboardPort0);
  s_vga.begin();
  s_vga.setResolution(VGA_512x384_60Hz);
  for (int i = 0; i < 16; ++i)
    s_vga.setPaletteItem(i, RGB888((i & 1) ? 255 : 0, (i & 2) ? 255 : 0, (i & 4) ? 255 : (i * 16)));
  fabgl::Canvas canvas(&s_vga);
  canvas.waitCompletion();
  for (int y = 0; y < 384; ++y) {
    uint8_t *row = s_vga.getScanline(y);
    for (int x = 0; x < 512; ++x) put4(row, x, (uint8_t)(x / 32));
  }
  canvas.selectFont(&fabgl::FONT_8x8);
  canvas.setGlyphOptions(GlyphOptions().FillBackground(true));
  canvas.setScrollingRegion(0, 200, 511, 383);
  canvas.drawText(4, 4, "FabGL smoke IDF 6.2");
  printf("DMA libre après VGA : %u\n", (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA));
  for (;;) {
    fabgl::VirtualKeyItem item;
    auto *kbd = s_ps2.keyboard();
    if (kbd && kbd->getNextVirtualKey(&item, 100) && item.down)
      canvas.drawText(4, 20, kbd->virtualKeyToString(item.vk));
    canvas.scroll(0, 1);
  }
}
