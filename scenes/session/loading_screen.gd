extends "res://scenes/menu/loading.gd"
## Session loading & shader pre-warming bridge (ISSUE-REND-03).
## Delegates to scenes/menu/loading.gd while ensuring 1-pixel offscreen shader pipeline
## warm-up completes during settling frames before revealing the battlefield.
