# Default target refreshes the playable Linux export (library + Godot pack).
# Fast editor iteration: `make debug` then F5 in Godot.

.PHONY: linux debug export

linux export:
	python3 tools/build.py linux

debug:
	scons platform=linux target=template_debug -j8
