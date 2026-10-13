#ifndef RTSO_TUTORIAL_DATA_H
#define RTSO_TUTORIAL_DATA_H

// The texts of 0 A.D. 0.28's two tutorials (GPL-2.0-or-later), goal by goal:
//   binaries/data/mods/public/maps/tutorials/introductory_tutorial.js
//   binaries/data/mods/public/maps/tutorials/starting_economy_walkthrough.js
// Each goal's "instructions" lines, with the hotkey a "%(hotkey)s" in the line names
// (the session UI fills in the player's own key; 0.28's "selection.queue" names no hotkey, so its
// lines take "session.queue", the order-queue modifier they describe). The goals' logic is C++ in
// simulation.cpp ("Tutorials"), the engine (maps/scripts/Tutorial.js) beside it.

#include <cstdint>

namespace rtso {

enum TutorialId : uint8_t {
	TUTORIAL_NONE = 0,
	TUTORIAL_INTRODUCTORY = 1,
	TUTORIAL_ECONOMY = 2,
	TUTORIAL_COUNT = 3
};

struct TutorialLine {
	const char *text;
	const char *hotkey; // HotkeyTable name for "%(hotkey)s", or nullptr
};

struct TutorialGoal {
	TutorialLine lines[6];
};

// WarningMessage texts; TriggerState::vars[6] holds the one shown (0: none).
enum TutorialWarning : uint8_t {
	TUTORIAL_WARN_NONE = 0,
	TUTORIAL_WARN_BATCH_HOTKEY,     // "Do not forget to press the batch training hotkey ..."
	TUTORIAL_WARN_CLICK_HOPLITE,
	TUTORIAL_WARN_CLICK_SKIRMISHER,
	TUTORIAL_WARN_CLICK_FEMALE,
	TUTORIAL_WARN_RALLY_TREE_INTRO, // "... right-click when you see your cursor change into a wood icon."
	TUTORIAL_WARN_RALLY_TREE,       // starting_economy_walkthrough's wording
	TUTORIAL_WARN_HOLD_SHIFT,       // "Do not forget to hold the hotkey while clicking to train several units."
	TUTORIAL_WARN_SHIFT_HOPLITE,
	TUTORIAL_WARN_HOLD_SHIFT_FEMALE,
	TUTORIAL_WARN_SHIFT_FEMALE,
	TUTORIAL_WARN_RALLY_FIELD,
	TUTORIAL_WARN_NO_SHIFT,
	TUTORIAL_WARN_CLICK_FEMALE_ECO,
	TUTORIAL_WARN_COUNT
};

inline const TutorialLine &tutorial_warning(uint8_t w) {
	static const TutorialLine kWarnings[TUTORIAL_WARN_COUNT] = {
		{"", nullptr},
		{"Do not forget to press the batch training hotkey while clicking to produce multiple units.", nullptr},
		{"Click on the Hoplite icon.", nullptr},
		{"Click on the Skirmisher icon.", nullptr},
		{"Click on the Civilian icon.", nullptr},
		{"Select the Civic Center, then hover the cursor over the tree and right-click when you see your cursor change into a wood icon.", nullptr},
		{"Select the Civic Center, then hover the cursor over a tree and right-click when you see the cursor change into a wood icon.", nullptr},
		{"Do not forget to hold the hotkey while clicking to train several units.", nullptr},
		{"The second icon represents the Hoplites.", nullptr},
		{"Do not forget to hold the hotkey and click to train several units.", nullptr},
		{"The first icon represents the Civilians.", nullptr},
		{"Select the Civic Center and right-click on the Field.", nullptr},
		{"Click without holding a hotkey to train a single unit.", nullptr},
		{"Click on the Civilian icon.", nullptr},
	};
	return kWarnings[w < TUTORIAL_WARN_COUNT ? w : 0];
}

// introductory_tutorial.js tutorialGoals.
inline constexpr int TUTORIAL_INTRO_GOALS = 27;
inline const TutorialGoal *tutorial_intro_goals() {
	static const TutorialGoal kGoals[TUTORIAL_INTRO_GOALS] = {
		{{{"Welcome to the 0 A.D. tutorial.", nullptr}}},
		{{{"Left-click on a Civilian and then right-click on a berry bush to make that Civilian gather food. Civilians gather vegetables faster than other units.", nullptr}}},
		{{{"Select the Citizen Soldier, right-click on a tree near the Civic Center to begin gathering wood. Citizen Soldiers gather wood faster than Civilians.", nullptr}}},
		{{{"Select the Civic Center building and hold %(hotkey)s while clicking on the Hoplite icon once to begin training a batch of Hoplites.", "session.batchtrain"}}},
		{{{"Select the two idle Civilians and build a House nearby by selecting the House icon. Place the House by left-clicking on a piece of land.", nullptr}}},
		{{{"When they are ready, select the newly trained Hoplites and assign them to build a Storehouse beside some nearby trees. They will begin to gather wood when it's constructed.", nullptr}}},
		{{{"Train a batch of Skirmishers by holding %(hotkey)s and clicking on the Skirmisher icon in the Civic Center.", "session.batchtrain"}}},
		{{{"Build a Farmstead in an open space beside the Civic Center using any idle builders.", nullptr}}},
		{{{"Let's wait for the Farmstead to be built.", nullptr}}},
		{{{"Once the Farmstead is constructed, its builders will automatically begin gathering food if there is any nearby. Select the builders and instead make them construct a Field beside the Farmstead.", nullptr}}},
		{{{"The Field's builders will now automatically begin gathering food from the Field. Using the newly created group of skirmishers, get them to build another House nearby.", nullptr}}},
		{{{"Train a batch of Hoplites at the Civic Center. Select the Civic Center and with it selected right-click on a tree nearby. Units from the Civic Center will now automatically gather wood.", nullptr}}},
		{{{"Build a Barracks nearby. Whenever your population limit is reached, build an extra House using any available builder units. This will be the fifth Village Phase structure that you have built, allowing you to advance to the Town Phase.", nullptr}}},
		{{{"Select the Civic Center again and advance to Town Phase by clicking on the II icon (you have to wait for the barracks to be built first). This will allow Town Phase buildings to be constructed.", nullptr}}},
		{{{"While waiting for the phasing up, you may reassign your idle workers to gathering the resources you are short of.", nullptr}}},
		{{{"Order the idle Skirmishers to build an outpost to the north east at the edge of your territory.", nullptr}}},
		{{{"Start training a batch of Civilians in the Civic Center and set its rally point to the Field (right click on it).", nullptr}}},
		{{{"Prepare for an attack by an enemy player. Train more soldiers using the Barracks, and get idle soldiers to build a Tower near your Outpost.", nullptr}}},
		{{{"Build a Forge and research the Infantry Training technology (sword icon) to improve infantry hack attack.", nullptr}}},
		{{{"The enemy is coming. Train more soldiers to fight off the enemies.", nullptr}}},
		{{{"Try to repel the attack.", nullptr}}},
		{{{"The enemy attack has been thwarted. Now build a market and a temple while you assign new units to gather required resources.", nullptr}}},
		{{{"Once you meet the City Phase requirements, select your Civic Center and advance to City Phase.", nullptr}}},
		{{{"While waiting for the phase change, you may train more soldiers at the Barracks.", nullptr}}},
		{{{"Now that you are in City Phase, build the Arsenal nearby and then use it to construct 2 Battering Rams.", nullptr}}},
		{{{"Stop all your soldiers gathering resources and instead task small groups to find the enemy Civic Center on the map. Once the enemy's base has been spotted, send your Siege Engines and all remaining soldiers to destroy it.\n", nullptr},
		  {"Civilians should continue to gather resources.", nullptr}}},
		{{{"The enemy has been defeated. These tutorial tasks are now completed.", nullptr}}},
	};
	return kGoals;
}

// starting_economy_walkthrough.js tutorialGoals.
inline constexpr int TUTORIAL_ECONOMY_GOALS = 32;
inline const TutorialGoal *tutorial_economy_goals() {
	static const TutorialGoal kGoals[TUTORIAL_ECONOMY_GOALS] = {
		{{{"This tutorial will teach the basics of developing your economy. Typically, you will start with a Civic Center and a couple units in Village Phase and ultimately, your goal will be to develop and expand your empire, often by evolving to Town Phase and City Phase afterward.\n", nullptr},
		  {"\nBefore starting, you can toggle between fullscreen and windowed mode using %(hotkey)s.", "togglefullscreen"},
		  {"You can change the level of zoom using the mouse wheel and the camera view using any of your keyboard's arrow keys.\n", nullptr},
		  {"Adjust the game window to your preferences.\n", nullptr},
		  {"\nYou may also toggle between showing and hiding this tutorial panel at any moment using %(hotkey)s.\n", "session.gui.tutorial.toggle"}}},
		{{{"To start off, select your building, the Civic Center, by clicking on it. A selection ring in the color of your player will be displayed after clicking.", nullptr}}},
		{{{"Now that the Civic Center is selected, you will notice that a production panel will appear on the lower right of your screen detailing the actions that the buildings supports. For the production panel, available actions are not masked in any color, while an icon masked in gray indicates that the action has not been unlocked and a red mask indicates that you do not have sufficient resources to perform that action. Additionally, you can hover the cursor over any icon to show a tooltip with more details.\n", nullptr},
		  {"The top row of buttons contains portraits of units that may be trained at the building while the bottom one or two rows will have researchable technologies. Hover the cursor over the II icon. The tooltip will tell us that advancing to Town Phase requires both more constructed structures as well as more food and wood resources.", nullptr}}},
		{{{"You have two main types of starting units: Civilians and Citizen Soldiers. Civilians are purely economic units; they have low health and little to no attack. Citizen Soldiers are workers by default, but in times of need, can utilize a weapon to fight. You have two categories of Citizen Soldiers: Infantry and Cavalry. Civilians and Infantry Citizen Soldiers can gather any land resources while Cavalry Citizen Soldiers can only gather meat from animals.\n", nullptr}}},
		{{{"As a general rule of thumb, left-clicking represents selection while right-clicking with an entity selected represents an order (gather, build, fight, etc.).\n", nullptr}}},
		{{{"At this point, food and wood are the most important resources for developing your economy, so let's start with gathering food. Civilians gather vegetables faster than other units.\n", nullptr},
		  {"There are primarily three ways to select units:\n", nullptr},
		  {"1) Hold the left mouse button and drag a selection rectangle that encloses the units you want to select.\n", nullptr},
		  {"2) Click on one of them and then add additional units to your selection by holding %(hotkey)s and clicking each additional unit (or also via the above selection rectangle).\n", "selection.add"},
		  {"3) Double-click on a unit. This will select every unit of the same type as the specified unit in your visible window. %(hotkey)s+double-click will select all units of the same type on the entire map.\n", "selection.offscreen"},
		  {"You can click on an empty space on the map to reset the selection. Try each of these methods before tasking all of your Civilians to gather the berries to the southeast of your Civic Center by right-clicking on the berries when you have all the Civilians selected.", nullptr}}},
		{{{"Now, let's gather some wood with your Infantry Citizen Soldiers. Select your Infantry Citizen Soldiers and order them to gather wood by right-clicking on the nearest tree.", nullptr}}},
		{{{"Cavalry Citizen Soldiers are good for hunting. Select your Cavalry and order him to slaughter the chickens around your Civic Center in similar fashion.", nullptr}}},
		{{{"All your units are now gathering resources. We should train more units!\n", nullptr},
		  {"First, let's set a rally point. Setting a rally point on a building that can train units will automatically designate a task to the new unit upon completion of training. We want to send the newly trained units to gather wood on the group of trees to the south of the Civic Center. To do so, select the Civic Center by clicking on it and then right-click on one of the trees.\n", nullptr},
		  {"Rally points are indicated by a small flag at the end of the blue line.", nullptr}}},
		{{{"Now that the rally point is set, we can produce additional units and they will do their assigned task automatically.\n", nullptr},
		  {"Citizen Soldiers gather wood faster than Civilians. Select the Civic Center and, while holding %(hotkey)s, click on the second unit icon, the Hoplites (holding %(hotkey)s trains a batch of five units). You can also train units individually by simply clicking, but training 5 units together takes less time than training 5 units individually.", "session.batchtrain"}}},
		{{{"Let's wait for the units to be trained.\n", nullptr},
		  {"While waiting, direct your attention to the panel at the top of your screen. On the upper left, you will see your current resource supply (food, wood, stone, and metal). As each worker brings resources back to the Civic Center (or another dropsite), you will see the amount of the corresponding resource increase.\n", nullptr},
		  {"This is a very important concept to keep in mind: gathered resources have to be brought back to a dropsite to be accounted, and you should always try to minimize the distance between resource and nearest dropsite to improve your gathering efficiency.", nullptr}}},
		{{{"The newly trained units automatically go to the trees and start gathering wood.\n", nullptr},
		  {"But as they have to bring it back to the Civic Center to deposit it, their gathering efficiency suffers from the distance. To fix that, we can build a Storehouse, a dropsite for wood, stone, and metal, close to the trees. To do so, select your five newly trained Citizen Soldiers and look for the construction panel on the bottom right, click on the Storehouse icon, move the mouse as close as possible to the trees you want to gather and click on a valid place to build the dropsite.\n", nullptr},
		  {"Invalid (obstructed) positions will show the building preview overlay in red.", nullptr}}},
		{{{"The selected Citizens will automatically start constructing the building once you place the foundation.", nullptr}}},
		{{{"When construction finishes, the builders default to gathering wood automatically.\n", nullptr},
		  {"In the meantime, we seem to have enough workers gathering wood. We should remove the current rally point of the Civic Center away from gathering wood. For that purpose, right-click on the Civic Center when it is selected (and the flag icon indicating the rally point is crossed out).", nullptr}}},
		{{{"Let's train some Civilians to gather more food. Select the Civic Center, hold %(hotkey)s and click on the Civilian icon to train five Civilians.", "session.batchtrain"}}},
		{{{"The units should be ready soon.\n", nullptr},
		  {"In the meantime, direct your attention to your population count on the top panel. It is the fifth item from the left, after the resources. It would be prudent to keep an eye on it. It indicates your current population (including those being trained) and the current population limit, which is determined by your built structures.", nullptr}}},
		{{{"As you have nearly reached the population limit, you must increase it by building some new structures if you want to train more units. The most cost effective structure to increase your population limit is the House.\n", nullptr},
		  {"Now that the units are ready, let's see how to build several Houses in a row.", nullptr}}},
		{{{"Select two of your newly-trained Civilians and order them to build these Houses in the empty space to the east of the Civic Center. Click on the House icon in the bottom right panel. While holding %(hotkey)s click on positions on the map where you want to build Houses. The Civilians will then start to build the houses in order. (When you give a command while holding %(hotkey)s, you put the command in a queue; units automatically switch to the next command in their queue when they finish their current command). Press Escape to get rid of the House cursor so you don't spam Houses all over the map.\n", "session.queue"},
		  {"Reminder: to select only two Civilians, click on the first one and then hold %(hotkey)s and click on the second one.", "selection.add"}}},
		{{{"You may notice that berries are a finite supply of food. We will need a more lasting food source. Fields produce an unlimited food resource, but are slower to gather than forageable fruits.\n", nullptr},
		  {"But to minimize the distance between a farm and its corresponding food dropsite, we will first build a Farmstead.", nullptr}}},
		{{{"Select the three remaining (idle) Civilians and order them to build a Farmstead in the center of the large open area to the west of the Civic Center.\n", nullptr},
		  {"We will need a decent chunk of space around the Farmstead to build Fields. In addition, we can see goats on the west side to further improve our food gathering efficiency should we ever decide to hunt them.\n", nullptr},
		  {"If you try to select the three idle Civilians by clicking and dragging a selection rectangle over them, you might accidentally select additional units. To avoid that, hold %(hotkey)s while selecting so that only idle units are selected. If you accidentally select a cavalry unit, hold Ctrl and click on the cavalry unit icon of the selection panel at the bottom of the screen to remove the cavalry unit from the current selection.", "selection.idleonly"}}},
		{{{"When the Farmstead construction is finished, its builders will automatically look for food, and in this case, they will go after the nearby goats.\n", nullptr},
		  {"But your House builders will only look for something else to build and, if nothing found, become idle. Let's wait for them to build the Houses.", nullptr}}},
		{{{"When both Houses are built, select your two Civilians and order them to build a Field as close as possible to the Farmstead, which is a dropsite for all types of food.", nullptr}}},
		{{{"When the Field is ready, the builders will automatically start gathering it.\n", nullptr},
		  {"The cavalry unit should have slaughtered all chickens by now. Select it and explore the area to the south of the Civic Center: there is a lake with some camels around. Move your cavalry by right-clicking on the point you want to go, and when you see a herd of camels, right-click on one of them to start hunting for food.", nullptr}}},
		{{{"Up to five Workers can gather from a Field. To add additional Workers, select the Civic Center and set a rally point on a Field by right-clicking on it. If the Field is not yet finished, new Workers sent by a rally point will help building it, and when built, they will gather food.", nullptr}}},
		{{{"Now click three times on the Civilian icon in the bottom right panel to train three additional farmers.", nullptr}}},
		{{{"You can increase the gather rates of your workers by researching new technologies available in some buildings.\n", nullptr},
		  {"The farming rate, for example, can be improved with a researchable technology in the Farmstead. Select the Farmstead and look at its production panel on the bottom right. You will see several researchable technologies. Hover the cursor over them to see their costs and effects and click on the one you want to research.", nullptr}}},
		{{{"We should start preparing to phase up into Town Phase, which will unlock many more units and buildings. Select the Civic Center and hover the cursor over the Town Phase icon to see what is still needed.\n", nullptr},
		  {"We now have enough resources, but one structure is missing. Although this is an economic tutorial, it is nonetheless useful to be prepared for defense in case of attack, so let's build Barracks.\n", nullptr},
		  {"Select four of your soldiers and ask them to build a Barracks: as before, start selecting the soldiers, click on the Barracks icon in the production panel and then lay down a foundation not far from your Civic Center where you want to build.", nullptr}}},
		{{{"Let's wait for the Barracks to be built. As this construction is lengthy, you can add two soldiers to build it faster. To do so, select your Civic Center and set up a rally point on the Barracks foundation by right-clicking on it (you should see a hammer icon). Then produce two more builders by clicking on the Hoplite icon twice.", nullptr}}},
		{{{"You should now be able to research Town Phase. Select the Civic Center and click on the technology icon.\n", nullptr},
		  {"If you still miss some resources (icon with red overlay), wait for them to be gathered by your workers.", nullptr}}},
		{{{"In later phases, you need usually stone and metal to build bigger structures and train better soldiers. Hence, while waiting for the research to be done, you will send half of your idle Citizen Soldiers (who have finished building the Barracks) to gather stone and the other half to gather metal.\n", nullptr},
		  {"To do so, we could select three Citizen Soldiers and right-click on the stone quarry on the west of the Civic Center (the cursor changes when hovering the stone quarry while your soldiers are selected). However, these soldiers were gathering wood, so they may still carry some wood which would be lost when starting to gather another resource.", nullptr}}},
		{{{"Thus, we should order them to deposit their wood in the Civic Center along the way. To do so, we will hold %(hotkey)s while clicking to queue orders: select your soldiers, hold %(hotkey)s and right-click on the Civic Center to deposit their wood and then hold %(hotkey)s and right-click on the stone quarry to gather it.\n", "session.queue"},
		  {"Perform a similar order queue with the remaining soldiers and the metal mine in the west.", nullptr}}},
		{{{"This is the end of the walkthrough. This should give you a good idea of the basics of setting up your economy.", nullptr}}},
	};
	return kGoals;
}

inline int tutorial_goal_count(uint8_t id) {
	return id == TUTORIAL_INTRODUCTORY ? TUTORIAL_INTRO_GOALS : id == TUTORIAL_ECONOMY ? TUTORIAL_ECONOMY_GOALS : 0;
}
inline const TutorialGoal *tutorial_goal(uint8_t id, int goal) {
	if (goal < 0 || goal >= tutorial_goal_count(id)) return nullptr;
	return (id == TUTORIAL_INTRODUCTORY ? tutorial_intro_goals() : tutorial_economy_goals()) + goal;
}

} // namespace rtso

#endif // RTSO_TUTORIAL_DATA_H
