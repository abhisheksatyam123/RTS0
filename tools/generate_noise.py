#!/usr/bin/env python3
"""Generate tileable terrain_noise + splat for shaders/terrain.gdshader — mirrors 0ad terrain noise. Code/program build."""
import math, random, argparse, os, sys
try:
    from PIL import Image
except ImportError:
    print("Pillow not found, install: pip install Pillow"); raise

def generate_noise(w, h, seed, out):
    random.seed(seed)
    img=Image.new('L', (w,h))
    pix=img.load()
    scale=0.06
    for y in range(h):
     for x in range(w):
      nx=x*scale; ny=y*scale
      v= (math.sin(nx)*0.5+0.5)*0.3 + (math.sin(nx*1.7+ny*0.8)*0.5+0.5)*0.3 + (math.sin(ny*2.3)*0.5+0.5)*0.2 + random.random()*0.2
      pix[x,y]=int(max(0,min(1,v))*255)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    print(f"saved {out} {w}x{h}")

def generate_splat(w, h, seed, out):
    random.seed(seed+1)
    img=Image.new('RGB', (w,h))
    pix=img.load()
    scale=0.015
    for y in range(h):
     for x in range(w):
      nx=x*scale; ny=y*scale
      n = (math.sin(nx)*0.5+0.5)*0.3 + (math.sin(nx*1.7+ny*0.8)*0.5+0.5)*0.3 + random.random()*0.2
      grass = max(0,min(1, 1- abs(n-0.30)*3))
      dirt  = max(0,min(1, 1- abs(n-0.50)*4))
      rock  = max(0,min(1, 1- abs(n-0.75)*3))
      s=grass+dirt+rock+0.001
      pix[x,y]=(int(grass/s*255), int(dirt/s*255), int(rock/s*255))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    print(f"saved {out} {w}x{h}")

if __name__=="__main__":
    p=argparse.ArgumentParser(description="build assets via code — noise + splat")
    p.add_argument("--size", type=int, default=512, help="512 (Android) or 1024 (PC)")
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--check", action="store_true", help="fail if files missing")
    args=p.parse_args()
    noise=f"assets/textures/noise/terrain_noise.png"
    splat=f"assets/textures/terrain/splat.png"
    if args.check:
        ok=os.path.exists(noise) and os.path.exists(splat)
        print(f"check noise={os.path.exists(noise)} splat={os.path.exists(splat)}")
        sys.exit(0 if ok else 1)
    generate_noise(args.size, args.size, args.seed, noise)
    generate_splat(args.size, args.size, args.seed, splat)
