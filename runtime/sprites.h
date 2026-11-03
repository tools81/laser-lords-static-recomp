/* Sprite interpolation from the game's own sprite layer (HD edition). */
#pragma once
#include <stdint.h>

typedef struct SpriteLayer SpriteLayer;

SpriteLayer *sprites_new(void);
/* capture the layer of the picture just rendered (video_fb = pic): which
   scene pixels are sprites and what the background is.  Returns 0 if the
   screen has no sprite layer (anything but a planet scene). */
int  sprites_capture(SpriteLayer *l, const uint32_t *pic);
void sprites_invalidate(SpriteLayer *l);
int  sprites_layer_valid(const SpriteLayer *l);
/* p and n: the pictures shown now and next, with their layers.  Returns 1
   if the pair is interpolated from the layers (then sprites_render draws
   it), 0 if not (same background required). */
int  sprites_prepare(const SpriteLayer *lp, const SpriteLayer *ln);
void sprites_render(uint32_t *out, const uint32_t *p, const uint32_t *n,
                    const SpriteLayer *lp, const SpriteLayer *ln, float alpha);
