#!/usr/bin/env python3
"""Plays the trigger-scripted maps headless to their end and checks the scripted events.

Each map goes through tools/probe_trigger_maps.cpp, which loads the map the way the session
does (session_setup.cpp load_map_seeded), seats players, and runs FIXED_STEP ticks. This
driver compiles the probe once (objects under build/scripted_maps/), runs each map with fixed
arguments, and asserts what its 0 A.D. script does: timers that fire, notifications,
dialogs, cinema paths that end, script spawns, and the match end (game over and winners).

Maps that are not meant to end (sandboxes and unit tests) assert that they run to the end
of their time with their scripts going, and no victory is claimed.

Usage: python3 tools/test_scripted_maps.py [--keep-build]
Exit status 0 when every check passes, 1 otherwise.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD = os.path.join(ROOT, "build", "scripted_maps")
PROBE = os.path.join(BUILD, "probe")

# triggers.h TriggerEvent order (the probe's "event calls:" line).
EVENTS = ["INIT_GAME", "INTERVAL", "RANGE", "STRUCTURE_BUILT", "CONSTRUCTION_STARTED",
          "TRAINING_QUEUED", "TRAINING_FINISHED", "RESEARCH_QUEUED", "RESEARCH_FINISHED",
          "OWNERSHIP_CHANGED", "PLAYER_DEFEATED", "PLAYER_WON", "DIPLOMACY_CHANGED",
          "TREASURE_COLLECTED", "PLAYER_COMMAND", "DESERIALIZED", "CINEMA_PATH_ENDED",
          "CINEMA_QUEUE_ENDED", "ATTACK_DETECTED"]

failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("  [FAIL] " + msg)
    else:
        print("  ok   " + msg)


def compile_probe(keep):
    os.makedirs(BUILD, exist_ok=True)
    srcs = {"sim": "src/simulation/simulation.cpp", "rms": "src/simulation/rms_generator.cpp"}
    objs = []
    for name, src in srcs.items():
        obj = os.path.join(BUILD, name + ".o")
        objs.append(obj)
        if not os.path.exists(obj) or os.path.getmtime(obj) < os.path.getmtime(os.path.join(ROOT, src)):
            subprocess.run(["g++", "-std=c++20", "-O2", "-Isrc", "-c", os.path.join(ROOT, src), "-o", obj],
                           cwd=ROOT, check=True)
    subprocess.run(["g++", "-std=c++20", "-O2", "-Isrc", "tools/probe_trigger_maps.cpp", *objs,
                    "src/core/job_system.cpp", "src/network/turn_manager.cpp", "src/network/net_host.cpp",
                    "-lpthread", "-o", PROBE], cwd=ROOT, check=True)


def run_map(name, args):
    proc = subprocess.run([PROBE, "--json", name, *args], cwd=ROOT, capture_output=True, text=True,
                          timeout=900)
    out = proc.stdout + proc.stderr
    log = os.path.join(BUILD, name + ".log")
    with open(log, "w") as f:
        f.write(out)
    return proc.returncode, out


def events(out):
    m = re.search(r"event calls:((?: \d+)+)", out)
    if not m:
        return None
    counts = [int(x) for x in m.group(1).split()]
    return dict(zip(EVENTS, counts))


def timers(out):
    m = re.search(r"timers fired:([^\n]*)", out)
    res = {}
    if m:
        for k, v in re.findall(r"(\w+)=(\d+)", m.group(1)):
            res[k] = int(v)
    return res


def end_line(out):
    m = re.search(r"END at ([\d.]+)s: game_over (\d) victory (\d) winners ([0-9a-f]+) defeated ([0-9a-f]+); "
                  r"script spawns (\d+)", out)
    if not m:
        return None
    return {"time": float(m.group(1)), "game_over": int(m.group(2)), "victory": int(m.group(3)),
            "winners": int(m.group(4), 16), "spawns": int(m.group(6))}


def vars_at_60(out):
    m = re.search(r"summary: water[^\n]*vars=\[([^\]]*)\]", out)
    return [int(x) for x in m.group(1).split(",")] if m else None


def common(out, code):
    check(code == 0, "probe exit status 0 (got %d)" % code)
    check("[FAIL]" not in out, "the run printed no failure marker")
    check("UNPORTED" not in out, "every trigger script ported")
    check(end_line(out) is not None, "the run reached its end line")


def scenario_cinema_demo():
    print("cinema_demo: notice at 1 s, cutscene at 6 s, the path ends")
    code, out = run_map("cinema_demo", ["--minutes", "6", "--quiet"])
    common(out, code)
    t, e, end = timers(out), events(out), end_line(out)
    check(t.get("CINEMA_DEMO") == 2, "CINEMA_DEMO fired twice (got %s)" % t.get("CINEMA_DEMO"))
    check("Cutscene starts after 5 seconds" in out, "the 1 s notice was shown")
    check(e and e["CINEMA_PATH_ENDED"] == 1 and e["CINEMA_QUEUE_ENDED"] == 1,
          "OnCinemaPathEnded and OnCinemaQueueEnded fired once each")
    check(end is not None and end["game_over"] == 0, "sandbox: no match end")


def scenario_triggers_demo():
    print("triggers_demo: OnInterval three times, yes-no dialog, OnAttackDetected")
    code, out = run_map("triggers_demo", ["--minutes", "6", "--quiet"])
    common(out, code)
    t, e = timers(out), events(out)
    check(t.get("TD_INTERVAL") == 3, "the interval timer fired three times (got %s)" % t.get("TD_INTERVAL"))
    check(out.count("DIALOG to ff: Testing the yes-no dialog") == 3, "the yes-no dialog was sent three times")
    check(e and e["ATTACK_DETECTED"] >= 1, "OnAttackDetected fired")


def scenario_pickup_test_map():
    print("pickup_test_map: the garrison experiments run, a ship gets its passengers")
    code, out = run_map("pickup_test_map", ["--minutes", "3", "--quiet"])
    common(out, code)
    t, v = timers(out), vars_at_60(out)
    check(t.get("PICKUP_TEST", 0) >= 3, "the experiment timers fired (got %s)" % t.get("PICKUP_TEST"))
    check(v is not None and v[0] != 0, "the erratic ship was spawned (vars[0]=%s)" % (v[0] if v else None))
    end = end_line(out)
    check(end is not None and end["game_over"] == 0, "sandbox: no match end")


def scenario_unit_chasing():
    print("unit_chasing_test: the chase experiments spawn and move")
    code, out = run_map("unit_chasing_test", ["--minutes", "6", "--quiet"])
    common(out, code)
    t, end = timers(out), end_line(out)
    check(t.get("UNIT_CHASING") == 1, "SetupUnits fired once")
    check(end is not None and end["spawns"] >= 2, "script spawns appear (%s)" % (end["spawns"] if end else None))
    check(end is not None and end["game_over"] == 0, "sandbox: no match end")


def scenario_unit_dancing():
    print("unit_dancing_test: experiments report, EndGame at 300 s, player 2 wins")
    code, out = run_map("unit_dancing_test", ["--minutes", "6", "--quiet"])
    common(out, code)
    t, end = timers(out), end_line(out)
    check(out.count("finished in") == 52, "all 52 experiments reported (got %d)" % out.count("finished in"))
    check(t.get("UNIT_DANCING") == 4, "the four timers fired (got %s)" % t.get("UNIT_DANCING"))
    check(end is not None and end["game_over"] == 1 and end["winners"] == 0b10 and abs(end["time"] - 300.0) < 0.5,
          "match ends at 300 s with player 2 the winner")


def scenario_unit_pushing():
    print("unit_pushing_test: EndGame at 300 s, player 1 wins")
    code, out = run_map("unit_pushing_test", ["--minutes", "6", "--quiet"])
    common(out, code)
    t, end = timers(out), end_line(out)
    check(t.get("UNIT_PUSHING") == 2, "both timers fired (got %s)" % t.get("UNIT_PUSHING"))
    check(end is not None and end["game_over"] == 1 and end["victory"] == 1 and end["winners"] == 0b01
          and abs(end["time"] - 300.0) < 0.5, "match ends at 300 s with player 1 the winner")


def scenario_unit_motion():
    print("unit_motion_integration_test: experiments spawn, the enclosed timeout fires")
    code, out = run_map("unit_motion_integration_test", ["--minutes", "6", "--quiet"])
    common(out, code)
    t, end = timers(out), end_line(out)
    check(t.get("UNIT_MOTION") == 2, "SetupUnits and the enclosed timeout fired (got %s)" % t.get("UNIT_MOTION"))
    check(end is not None and end["spawns"] >= 4, "script spawns appear (%s)" % (end["spawns"] if end else None))


def scenario_death_canyon():
    print("death_canyon_invasion_force: conquest ends the match, a winner is named")
    # The AI's fights decide the winner and how long they take, so this runs 40 game minutes and
    # checks that conquest ends the match with some side named, not which side it is.
    code, out = run_map("death_canyon_invasion_force", ["--minutes", "40", "--quiet"])
    common(out, code)
    end = end_line(out)
    check(end is not None and end["game_over"] == 1 and end["winners"] != 0, "conquest ends the match with a winner named")
    check("DEFEATED p4" in out or "DEFEATED p3" in out, "a defeat is reported")


def scenario_treasure_islands():
    print("treasure_islands: intro, treasures collected, the match ends")
    code, out = run_map("treasure_islands", ["--minutes", "30", "--quiet"])
    common(out, code)
    t, e, end = timers(out), events(out), end_line(out)
    check(t.get("TI_INTRO") == 1, "the intro message fired once")
    check(e and e["TREASURE_COLLECTED"] >= 5, "treasures collected (events %s)" % (e and e["TREASURE_COLLECTED"]))
    check(end is not None and end["game_over"] == 1, "the match ends within 30 minutes")


def scenario_sandboxes():
    print("units_demo, campaign_test_map, campaign_test_map_2_heightmap: run to the end of their time")
    for name in ["units_demo", "campaign_test_map", "campaign_test_map_2_heightmap"]:
        code, out = run_map(name, ["--minutes", "6", "--quiet"])
        common(out, code)
        end = end_line(out)
        check(end is not None and end["game_over"] == 0, "%s: no match end (sandbox)" % name)


def main():
    keep = "--keep-build" in sys.argv
    compile_probe(keep)
    for fn in [scenario_cinema_demo, scenario_triggers_demo, scenario_pickup_test_map, scenario_unit_chasing,
               scenario_unit_dancing, scenario_unit_pushing, scenario_unit_motion, scenario_death_canyon,
               scenario_treasure_islands, scenario_sandboxes]:
        fn()
    if not keep:
        import shutil
        shutil.rmtree(BUILD, ignore_errors=True)
    print("scripted maps: %s" % ("ok" if failures == 0 else "%d failure(s)" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
