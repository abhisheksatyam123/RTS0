#!/usr/bin/env python3
"""Generate tileable terrain_noise.png for shaders/terrain.gdshader — mirrors 0ad terrain noise."""
import math, random
try:
    from PIL import Image
except ImportError:
    print("Pillow not found, install: pip install Pillow"); raise

w=h=512
img=Image.new('L', (w,h))
pix=img.load()
scale=0.06
for y in range(h):
 for x in range(w):
  nx=x*scale; ny=y*scale
  v= (math.sin(nx)*0.5+0.5)*0.3 + (math.sin(nx*1.7+ny*0.8)*0.5+0.5)*0.3 + (math.sin(ny*2.3)*0.5+0.5)*0.2 + random.random()*0.2
  pix[x,y]=int(max(0,min(1,v))*255)
out="assets/textures/noise/terrain_noise.png"
img.save(out)
print(f"saved {out} {w}x{h}")
