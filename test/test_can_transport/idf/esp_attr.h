// On the host there is no IRAM. The attribute is kept as a token so the production
// source compiles unchanged -- whether the real driver ENFORCES it is a separate
// matter (esp_twai_onchip.c:362-367, gated on CONFIG_TWAI_ISR_CACHE_SAFE).
#pragma once
#define IRAM_ATTR
#define DRAM_ATTR
