// Runs one of 0 A.D.'s random map scripts (maps/random/<script>.js with rmgen, rmgen2,
// rmgen-common and rmbiome) under node, without the engine, and prints the Gaia
// resource entities it placed outside the player bases as JSON:
//   {"<template>": {"n": count, "amount": ResourceSupply/Max, "type": "metal.ore", "domestic": false}, ...}
// The engine's side is stubbed: Engine.GetTemplate reads a JSON dump of the
// simulation templates (tools/extract_0ad_resources.py writes it), heightmap images
// come pre-decoded as $RMGEN_HEIGHTMAPS/<png name>.json (grey x 257), Math.random is a
// seeded xorshift.
// Entities placed by placePlayerBase (starting mines, berries, trees, hunt) count as
// the bases'.
//
// Usage: node tools/rmgen_harness.js <public dir> <templates.json> <script> <tiles> <players> <seed> [biome]
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const [ROOT, TPL, SCRIPT, SIZE, NP, SEED, BIOME] = process.argv.slice(2);
const tpl = JSON.parse(fs.readFileSync(TPL, "utf8"));
let state = (+SEED >>> 0) || 1;
Math.random = () => {
	state ^= state << 13; state >>>= 0; state ^= state >>> 17; state ^= state << 5; state >>>= 0;
	return state / 4294967296;
};
global.warn = () => {};
global.log = () => {};
global.print = () => {};
global.error = (m) => { throw new Error(m); };
global.uneval = (x) => JSON.stringify(x);
global.clone = (x) => structuredClone(x);
global.deepfreeze = (x) => x;
global.TERRAIN_TILE_SIZE = 4;
global.MAP_BORDER_WIDTH = 3;
const loaded = new Set(), imported = new Set();
const runFile = (f) => {
	// ES module maps (rmgen2) import from other files: run those once in the shared global scope.
	let src = fs.readFileSync(f, "utf8").replace(/^import[\s\S]*?from\s*"([^"]+)";?/mg, (m, p) => {
		const full = path.join(ROOT, p);
		if (!imported.has(full)) { imported.add(full); runFile(full); }
		return "";
	});
	return vm.runInThisContext(src.replace(/^export (default )?/mg, ""), { "filename": f });
};
const jsIn = (d) => fs.readdirSync(d).sort().filter(n => n.endsWith(".js")).map(n => path.join(d, n));
global.Engine = {
	"LoadLibrary": (name) => {
		if (loaded.has(name)) return;
		loaded.add(name);
		for (const f of jsIn(path.join(ROOT, "maps/random", name))) runFile(f);
	},
	"ReadJSONFile": (p) => JSON.parse(fs.readFileSync(path.join(ROOT, p), "utf8")),
	"FileExists": (p) => fs.existsSync(path.join(ROOT, p)),
	"ListDirectoryFiles": (p, filter) => {
		const ext = (filter || "").replace("*", "");
		return fs.readdirSync(path.join(ROOT, p)).filter(n => n.endsWith(ext)).sort().map(n => path.join(p, n));
	},
	"GetTemplate": (n) => {
		const t = tpl[n.replace(/^[a-z_]+\|/, "")];
		if (!t) throw new Error("no template " + n);
		return t;
	},
	"TemplateExists": (n) => !!tpl[n],
	"GetMicroseconds": () => Number(process.hrtime.bigint() / 1000n),
	"SetProgress": () => {},
	"LoadHeightmapImage": (p) => JSON.parse(fs.readFileSync(path.join(process.env.RMGEN_HEIGHTMAPS || "", path.basename(p) + ".json"), "utf8")),
	"LoadMapTerrain": () => { throw new Error("LoadMapTerrain is not supported"); },
	"SerializationRoundTrip": (x) => x,
};
const walk = (d) => fs.readdirSync(d).sort().flatMap(n => {
	const p = path.join(d, n);
	return fs.statSync(p).isDirectory() ? (n == "tests" ? [] : walk(p)) : p.endsWith(".js") ? [p] : [];
});
for (const f of walk(path.join(ROOT, "globalscripts"))) runFile(f);
const civs = ["athen", "brit", "cart", "gaul", "iber", "mace", "maur", "pers"];
const PlayerData = [null];
for (let i = 0; i < +NP; ++i) PlayerData.push({ "Civ": civs[i % civs.length], "Team": -1 });
const settings = JSON.parse(fs.readFileSync(path.join(ROOT, "maps/random", SCRIPT + ".json"), "utf8")).settings;
global.g_MapSettings = {
	"Size": +SIZE, "Seed": +SEED, "PlayerData": PlayerData, "Biome": BIOME || undefined,
	"CircularMap": !!settings.CircularMap, "PlayerPlacement": (settings.PlayerPlacements || [])[0],
	"Nomad": false, "Name": settings.Name, "TriggerDifficulty": 3, "Daytime": "day"
};
runFile(path.join(ROOT, "maps/random", SCRIPT + ".js"));
const baseIds = new Set();
if (typeof placePlayerBase === "function") {
	const placeBase = placePlayerBase;
	global.placePlayerBase = function(...args) {
		const start = g_Map.entities.length;
		const ret = placeBase.apply(this, args);
		for (let i = start; i < g_Map.entities.length; ++i) baseIds.add(g_Map.entities[i].id);
		return ret;
	};
}
const gen = generateMap(g_MapSettings);
let step;
do step = gen.next(); while (!step.done);
const out = {};
for (const e of (step.value || g_Map).exportEntityList()) {
	const t = tpl[e.templateName];
	const rs = t && t.ResourceSupply;
	if (!rs || e.player != 0 || baseIds.has(e.id)) continue;
	const r = out[e.templateName] || (out[e.templateName] = {
		"n": 0, "amount": isFinite(+rs.Max) ? +rs.Max : 0, "type": rs.Type,
		"domestic": /Domestic/.test(JSON.stringify(t.Identity || {}))
	});
	++r.n;
}
console.log(JSON.stringify(out));
