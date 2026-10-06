#!/usr/bin/env python3
"""Backward-compatible wrapper; combat SFX now live in extract_media.SOUND_ALIASES."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import extract_media

if __name__ == "__main__":
    extract_media.main()
