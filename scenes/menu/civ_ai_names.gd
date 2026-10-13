# 0 A.D. simulation/data/civs/<civ>.json "AINames", indexed by rtso::Civilization
# (athen, spart, pers, rome, cart, mace, brit, gaul, iber, maur, kush, ptol, sele, han, Gaia, germ).
# Game Setup names each AI slot from its civ (gamesettings/attributes/PlayerName.js pickRandomItems).
extends RefCounted

const AI_NAMES := [
	["Cimon", "Aristides", "Xenophon", "Hippias", "Cleisthenes", "Thucydides", "Alcibiades", "Miltiades", "Cleon", "Cleophon", "Thrasybulus", "Demosthenes"],
	["Dienekes", "Agis II", "Archidamus", "Lysander", "Pausanias", "Agesilaus", "Echestratus", "Eurycrates", "Eucleidas", "Agesipolis"],
	["Cambyses II", "Bardiya", "Artaxshacha I", "Darayavahush II", "Darayavahush III", "Artaxshacha II", "Artaxshacha III", "Haxamanish", "Xsayarsa II"],
	["Lucius Junius Brutus", "Lucius Tarquinius Collatinus", "Gaius Julius Caesar Octavianus", "Marcus Vipsanius Agrippa", "Gaius Iulius Iullus", "Gaius Servilius Structus Ahala", "Publius Cornelius Rufinus", "Lucius Papirius Cursor", "Aulus Manlius Capitolinus", "Publius Sempronius Tuditanus", "Marcus Cornelius Cethegus", "Quintus Caecilius Metellus Pius", "Marcus Licinius Crassus"],
	["Hasdrubal Barca", "Hasdrubal Gisco", "Hanno the Elder", "Mago Barca", "Hasdrubal the Fair", "Hanno the Great", "Himilco", "Hampsicora", "Hannibal Gisco", "Dido", "Xanthippus", "Himilco Phameas", "Hasdrubal the Boetharch"],
	["Antipater", "Philip IV", "Lysander", "Lysimachus", "Pyrrhus of Epirus", "Antigonus II Gonatas", "Demetrius II Aetolicus", "Philip V", "Perseus", "Craterus", "Meleager"],
	["Prasutagus", "Venutius", "Cogidubnus", "Commius", "Comux", "Adminius", "Dubnovellaunus", "Vosenius"],
	["Cativolcus", "Cingetorix", "Divico", "Ambiorix", "Liscus", "Orgetorix", "Valetiacus", "Viridovix"],
	["Audax", "Calcus", "Ditalcus", "Minurus", "Olyndicus", "Orison", "Tanginus", "Tautalus"],
	["Bindusara Maurya", "Brihadratha Maurya", "Dasharatha Maurya", "Devavarman Maurya", "Kunala Maurya", "Samprati Maurya", "Shalishuka Maurya", "Shatadhanvan Maurya", "Tivala Maurya"],
	["Kashta", "Alara", "Pebatjma", "Shabaka", "Shebitku", "Qalhata", "Takahatenamun", "Tantamani", "Atlanersa", "Nasalsa", "Malewiebamani", "Shanakdakhete", "Amanishakheto"],
	["Ptolemy Philadelphus", "Ptolemy Epigone", "Ptolemy Eurgetes", "Ptolemy Epiphanes", "Ptolemy Philometor", "Ptolemy Eupator", "Ptolemy Alexander", "Ptolemy Neos Dionysos", "Ptolemy Neos Philopater", "Berenice Philopater", "Cleopatra Tryphaena", "Berenice Epiphaneia", "Cleopatra Philopater", "Cleopatra Selene", "Cleopatra II Philometora Soteira", "Arsinoe IV", "Arsinoe II"],
	["Antiochus I Soter", "Antiochus II Theos", "Seleucus II Callinicus", "Seleucus III Ceraunus", "Seleucus IV Philopator", "Antiochus V Eupator", "Demetrius I Soter", "Alexander I Balas", "Demetrius II Nicator", "Antiochus VI Dionysus", "Diodotus Tryphon", "Antiochus VII Sidetes", "Demetrius II Nicator", "Alexander II Zabinas", "Cleopatra Thea", "Seleucus V Philometor", "Antiochus VIII Grypus", "Antiochus IX Cyzicenus", "Seleucus VI Epiphanes", "Antiochus X Eusebes", "Demetrius III Eucaerus", "Antiochus XI Epiphanes", "Philip I Philadelphus", "Antiochus XII Dionysus", "Seleucus VII Kybiosaktes", "Antiochus XIII Asiaticus", "Philip II Philoromaeus"],
	["Liu Ying", "Liu Gong", "Liu Hong", "Liu Heng", "Liu Qi", "Liu Che", "Liu Fuling", "Liu He", "Liu Bingyi", "Liu Shi", "Liu Ao", "Liu Xin", "Liu Kan", "Liu Xuan", "Liu Xiu", "Liu Zhuang", "Liu Da", "Liu Zhao", "Liu Long", "Liu Hu", "Liu Yi", "Liu Bao", "Liu Bing", "Liu Zuan", "Liu Zhi", "Liu Hong", "Liu Bian", "Liu Xie"],
	[],
	["Claodicus", "Divico", "Caesorix"],
]
