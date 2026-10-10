#!/usr/bin/env python3
"""Append Champion-and-above Digimon that have a Dawn/Dusk sprite sheet.

Starts from HEAD data so a repeated run replaces the same append-only block.
Sheets come from the local MediaFire zip. No GIF repo, no network, no device I/O.
"""
from __future__ import annotations

import argparse
import importlib.util
import io
import json
import re
import subprocess
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZIP_PATH = Path('/tmp/digivice-sheets/wtw/sprite_thread.zip')
MAX_FORM_ID = 512
SOURCE_URL = 'https://www.mediafire.com/file/lk0pjupv8n88006/sprite_thread.zip/file'

MAPS = {
    'accessglacier', 'anglertunnel', 'colosseum', 'digibasefront', 'dssm_digieggs',
    'gurabocentral', 'labelforest', 'leaderroom', 'limitvalley', 'loginmountain',
    'loopswamp', 'magnetmine', 'nleaderrm', 'northcave', 'northmarine',
    'palacelaboratory', 'paletteamazon', 'pixeldesert', 'proxyisland', 'registerjungle',
    'riskfactory', 'shadowhell', 'taskcanyon', 'thrillerruins', 'wizardtemple',
    'wleaderroom',     'packetcoast', 'dssmdigieggs',
}
SKIP_EXTRAS = {
    'agumonoriginal', 'bommon', 'chikurimon', 'cutemon', 'gaossmon', 'hyokomon',
    'pickmon', 'shoutmon', 'shoutmonblack', 'monitamon', 'spadamon', 'impmonxros',
    'dondokomon', 'lucemon', 'jetsparrow', 'gigadevast', 'exeraseralpha', 'exeraserbeta',
    'shifteraser', 'spinomonxrosboss',
}
# Same creature under the Dawn/Dusk romanization. Applied only when the target
# is already on the roster, so a missing English name does not drop a new sheet.
ALIASES = {
    'tailmon': 'gatomon', 'galgomon': 'gargomon', 'fladramon': 'flamedramon',
    'prariemon': 'prairiemon', 'gawappamon': 'gwappamon', 'dinohumon': 'dinohyumon',
    'grizmon': 'grizzmon', 'leppamon': 'reppamon', 'veggiemon': 'vegiemon',
    'ookuwamon': 'okuwamon', 'tonosamagekomon': 'shogungekomon', 'tylomon': 'plesiomon',
    'hangyomon': 'divermon', 'chimeramon': 'kimeramon', 'archnemon': 'arukenimon',
    'antiramon': 'antylamon', 'weregarurumonblack': 'blackweregarurumon',
    'megalogrowlmon': 'wargrowlmon', 'blackmegalogrowlmon': 'blackwargrowlmon',
    'risegreymon': 'rizegreymon', 'piccolomon': 'piximon',
    'alturkabuterimon': 'megakabuterimonred', 'neptunmon': 'neptunemon',
    'beelzebumon': 'beelzemon', 'chaosdukemon': 'chaosgallantmon',
    'imperialdramondragon': 'imperialdramondragonmode',
    'imperialdramonfighter': 'imperialdramonfightermode',
    'imperaildramonpaladin': 'imperialdramonpaladinmode',
    'imperialdramondragonblack': 'blackimperialdramon',
    'bantyoleomon': 'bancholeomon', 'chronomonholy': 'chronomonholymode',
    'garbamon': 'garbagemon', 'dorugremon': 'dorugreymon',
    'holyangemon': 'magnaangemon', 'vamdemon': 'myotismon', 'piemon': 'piedmon',
    'mugendramon': 'machinedramon', 'omegamon': 'omnimon', 'dukemon': 'gallantmon',
    'dukemoncrimsonupdate': 'gallantmoncrimsonmode', 'saintgalgomon': 'megagargomon',
    'blacksaintgalgomon': 'blackmegagargomon', 'diablomon': 'diaboromon',
    'belialvamdemon': 'malomyotismon', 'venomvamdemon': 'venommyotismon',
    'pinnochimon': 'puppetmon', 'demon': 'creepymon', 'hououmon': 'phoenixmon',
    'sleipmon': 'kentaurosmon', 'valdurmon': 'varodurumon',
    'qinglongmon': 'azulongmon', 'xuanwumon': 'ebonwumon',
    'vdramon': 'veedramon', 'aerovdramon': 'aeroveedramon',
    'mammon': 'mammothmon', 'jureimon': 'cherrymon', 'skullmammon': 'skullmammothmon',
    'gryphomon': 'gryphonmon', 'kentarumon': 'centarumon',
    'lucemonfdm': 'lucemonchaosmode',
    'mercurymon': 'mercurimon', 'gaioumon': 'gaiomon',
    'moonmilleniummon': 'moonmilleniumon', 'skullbaluchimon': 'skullbarukimon',
    'alterkabuterimonred': 'megakabuterimonred', 'lilithmonxros': 'lilithmon',
    'skullknightmonred': 'skullknightmon',
}
ARMOR = {'shadramon', 'pipismon', 'kabukimon', 'nefertimon', 'baromon', 'honeybeemon'}
EXTRA_STAGE = {
    'ballistamon': 'Champion', 'dorulumon': 'Champion', 'deadlyaxemon': 'Champion',
    'troopmon': 'Champion', 'mailbirdramon': 'Champion', 'greymonxros': 'Champion',
    'strikedramon': 'Champion', 'sparrowmon': 'Champion', 'shoutmonx2': 'Champion',
    'bluemeramon': 'Champion', 'buraimon': 'Champion',     'skullknightmon': 'Champion', 'agnimon': 'Champion', 'butenmon': 'Mega',
    'madleomon': 'Ultimate', 'wisemon': 'Ultimate', 'bastemon': 'Ultimate',
    'mastertyrannomon': 'Ultimate',
    'cerberumon': 'Ultimate', 'mermaimon': 'Ultimate', 'scorpiomon': 'Ultimate',
    'grademon': 'Ultimate', 'metalgreymonxros': 'Ultimate', 'shoutmonx3': 'Ultimate',
    'shoutmonx3gm': 'Ultimate', 'shoutmonx3sd': 'Ultimate', 'waruseadramon': 'Ultimate',
    'armamon': 'Ultimate',
    'shoutmonx4': 'Mega', 'shoutmonx4b': 'Mega', 'shoutmonx4s': 'Mega',
    'shoutmonx5': 'Mega', 'shoutmonx5s': 'Mega', 'starswordshoutmon': 'Mega',
    'dorulucannonshoutmon': 'Mega', 'tactimon': 'Mega', 'blastmon': 'Mega',
    'darkknightmon': 'Mega', 'greyknightsmon': 'Mega', 'alforcevdramon': 'Mega',
    'examon': 'Mega', 'dynasmon': 'Mega', 'craniummon': 'Mega', 'chaosdramon': 'Mega',
    'gaioumon': 'Mega', 'aegisdramon': 'Mega', 'grandracmon': 'Mega', 'daipenmon': 'Mega',
    'spinomon': 'Mega', 'mercurymon': 'Mega', 'tiger vespamon': 'Mega',
    'tigervespamon': 'Mega', 'ancientbeetmon': 'Mega', 'ancientgarurumon': 'Mega',
    'ancientgreymon': 'Mega', 'ancientirismon': 'Mega', 'ancientmegatheriummon': 'Mega',
    'ancientmermaimon': 'Mega', 'ancientsphinxmon': 'Mega', 'ancienttroiamon': 'Mega',
    'ancientvolcamon': 'Mega', 'ancientwisemon': 'Mega',
    'rhodoknightmon': 'Mega',
}
ROLE_PATTERN = {
    'Champion': ['Striker'] * 23 + ['Mystic'] * 14 + ['Bulwark'] * 11 + ['Balanced'] * 7 + ['Warden'] * 5,
    'Ultimate': ['Striker'] * 15 + ['Mystic'] * 13 + ['Bulwark'] * 11 + ['Warden'] * 11 + ['Balanced'] * 8,
    'Mega': ['Bulwark'] * 19 + ['Mystic'] * 16 + ['Striker'] * 15 + ['Warden'] * 13 + ['Balanced'] * 5,
    'Armor': ['Striker'] * 3 + ['Bulwark'] * 2 + ['Warden'] * 2 + ['Mystic'],
}
RARITY_PATTERN = {
    'Champion': ['common'] * 36 + ['uncommon'] * 19 + ['rare'] * 5,
    'Ultimate': ['common'] * 27 + ['uncommon'] * 24 + ['rare'] * 7,
    'Mega': ['uncommon'] * 29 + ['rare'] * 21 + ['common'] * 18,
    'Armor': ['uncommon'] * 5 + ['common'] * 2 + ['rare'],
}
GATES = {'Champion': (5, 20), 'Ultimate': (10, 50), 'Mega': (15, 80), 'Armor': (5, 20)}
# Real lines only. An edge is kept when both names resolve, the parent still has
# an open choice, and the destination stage is higher. Old-to-old edges are not added.
LINES = [
    ('veemon', 'vdramon'), ('veemon', 'veedramon'), ('biyomon', 'saberdramon'),
    ('aruraumon', 'woodmon'), ('floramon', 'woodmon'), ('palmon', 'woodmon'),
    ('gomamon', 'gesomon'), ('betamon', 'gesomon'), ('gotsumon', 'monochromon'),
    ('hagurumon', 'mechanorimon'), ('hagurumon', 'clockmon'), ('patamon', 'unimon'),
    ('kunemon', 'flymon'), ('tentomon', 'flymon'), ('tentomon', 'snimon'),
    ('wormmon', 'snimon'), ('salamon', 'tailmon'), ('gaomon', 'gaogamon'),
    ('goburimon', 'fugamon'), ('goburimon', 'ogremon'), ('agumon', 'tyrannomon'),
    ('gabumon', 'garurumon'), ('dorumon', 'raptordramon'), ('dorumon', 'reptiledramon'),
    ('lalamon', 'sunflowmon'), ('kudamon', 'leppamon'), ('falcomon', 'peckmon'),
    ('kamemon', 'gawappamon'), ('hawkmon', 'aquilamon'), ('guilmon', 'growlmon'),
    ('blackagumon', 'darktyrannomon'), ('toyagumon', 'guardromon'),
    ('snowagumon', 'yukidarumon'), ('snowagumon', 'frigimon'),
    ('vdramon', 'aerovdramon'), ('veedramon', 'aeroveedramon'),
    ('greymon', 'metalgreymon'), ('greymon', 'skullgreymon'),
    ('birdramon', 'garudamon'), ('garurumon', 'weregarurumon'),
    ('angemon', 'holyangemon'), ('angemon', 'magnaangemon'),
    ('gatomon', 'angewomon'), ('tailmon', 'angewomon'),
    ('gargomon', 'rapidmon'), ('growlmon', 'wargrowlmon'),
    ('exveemon', 'paildramon'), ('firamon', 'flaremon'), ('lekismon', 'crescemon'),
    ('gaogamon', 'machgaogamon'), ('tyrannomon', 'metaltyrannomon'),
    ('devimon', 'vamdemon'), ('devimon', 'myotismon'),
    ('seadramon', 'megaseadramon'), ('airdramon', 'megadramon'),
    ('kabuterimon', 'megakabuterimon'), ('kuwagamon', 'okuwamon'),
    ('leomon', 'grappleomon'), ('monochromon', 'vermilimon'),
    ('woodmon', 'jureimon'), ('woodmon', 'cherrymon'), ('vegiemon', 'redvegiemon'),
    ('clockmon', 'knightmon'), ('mojyamon', 'mammon'), ('mojyamon', 'mammothmon'),
    ('numemon', 'monzaemon'), ('meramon', 'blumeramon'), ('centarumon', 'kentarumon'),
    ('bakemon', 'phantomon'), ('sukamon', 'etemon'), ('devidramon', 'megadramon'),
    ('devidramon', 'gigadramon'), ('fladramon', 'flamedramon'),
    ('peckmon', 'yatagaramon'), ('reppamon', 'tyilinmon'), ('sunflowmon', 'lilamon'),
    ('dorugamon', 'dorugreymon'), ('geogreymon', 'rizegreymon'),
    ('angewomon', 'holydramon'), ('angewomon', 'ophanimon'), ('angewomon', 'orphanimon'),
    ('metalgreymon', 'wargreymon'), ('weregarurumon', 'metalgarurumon'),
    ('myotismon', 'venomvamdemon'), ('myotismon', 'venommyotismon'),
    ('myotismon', 'belialvamdemon'), ('myotismon', 'malomyotismon'),
    ('magnaangemon', 'seraphimon'), ('paildramon', 'imperialdramon'),
    ('megakabuterimon', 'herculeskabuterimon'), ('lilamon', 'rosemon'),
    ('taomon', 'sakuyamon'), ('rapidmon', 'megagargomon'), ('wargrowlmon', 'gallantmon'),
    ('machgaogamon', 'miragegaogamon'), ('rizegreymon', 'shinegreymon'),
    ('yatagaramon', 'valdurmon'), ('yatagaramon', 'varodurumon'),
    ('flaremon', 'apollomon'), ('crescemon', 'dianamon'),
    ('metaltyrannomon', 'rusttyranomon'), ('phantomon', 'pharaohmon'),
    ('mammothmon', 'skullmammon'), ('mammon', 'skullmammothmon'),
    ('cherrymon', 'pupumon'), ('aeroveedramon', 'ulforceveedramon'),
    ('wargreymon', 'omegamon'), ('wargreymon', 'omnimon'),
    ('metalgarurumon', 'omegamon'), ('metalgarurumon', 'omnimon'),
    ('shinegreymon', 'shinegreymonburst'), ('miragegaogamon', 'miragegaogamonburst'),
    ('ravemon', 'ravemonburst'), ('rosemon', 'rosemonburst'),
    ('gallantmon', 'dukemoncrimsonupdate'), ('gallantmon', 'gallantmoncrimsonmode'),
    ('imperialdramondragonmode', 'imperialdramonfightermode'),
    ('imperialdramonfightermode', 'imperialdramonpaladinmode'),
    ('andromon', 'hiandromon'), ('megaseadramon', 'metalseadramon'),
    ('etemon', 'metaletemon'), ('garudamon', 'hououmon'), ('garudamon', 'phoenixmon'),
    ('lillymon', 'rosemon'), ('angemon', 'shakkoumon'), ('ankylomon', 'shakkoumon'),
    ('stingmon', 'jewelbeemon'), ('xvmon', 'paildramon'),
    ('coronamon', 'firamon'), ('lunamon', 'lekismon'),
    ('agumon', 'greymon'), ('gabumon', 'garurumon'), ('biyomon', 'birdramon'),
    ('tentomon', 'kabuterimon'), ('gomamon', 'ikakkumon'), ('palmon', 'togemon'),
    ('patamon', 'angemon'), ('salamon', 'gatomon'),
    ('blackgarurumon', 'weregarurumonblack'), ('ice devimon', 'ladydevimon'),
    ('icedevimon', 'ladydevimon'), ('saberdramon', 'karatenmon'),
    ('knightmon', 'lordknightmon'), ('grappleomon', 'bancholeomon'),
    ('grappleomon', 'bantyoleomon'), ('apollomon', 'grace novamon'),
    ('volcamon', 'pilevolcamon'), ('tankmon', 'andromon'),
    ('clockmon', 'andromon'), ('revolmon', 'gigadramon'),
    ('musyamon', 'asuramon'), ('ogremon', 'digitamamon'),
    ('shellmon', 'scorpiomon'), ('coelamon', 'coelamon'),
    ('gesomon', 'marine devimon'), ('gesomon', 'marinedevimon'),
    ('woodmon', 'cherrymon'), ('redvegiemon', 'cherrymon'),
    ('flymon', 'okuwamon'), ('snimon', 'okuwamon'),
    ('unimon', 'hippogriffomon'), ('centarumon', 'pandamon'),
    ('mojyamon', 'pandamon'), ('fridgimon', 'yukidarumon'),
    ('yukidarumon', 'zudomon'), ('icemon', 'zudomon'),
    ('seadramon', 'megaseadramon'), ('megaseadramon', 'metal seadramon'),
    ('whamon', 'neptunmon'), ('zudomon', 'vikemon'), ('zudomon', 'plesiomon'),
    ('lilymon', 'rosemon'), ('lillymon', 'rosemon'),
    ('taomon', 'sakuyamon'), ('kyubimon', 'taomon'),
    ('growlmon', 'wargrowlmon'), ('wargrowlmon', 'dukemon'),
    ('gargomon', 'rapidmon'), ('rapidmon', 'saintgalgomon'),
    ('flamedramon', 'raidramon'), ('lighdramon', 'magnamon'),
    ('shadramon', 'knightmon'), ('submarimon', 'zudomon'),
    ('pegasmon', 'magnamon'), ('nefertimon', 'piximon'),
    ('silphymon', 'valkyrimon'), ('shakkoumon', 'slashangemon'),
    ('paildramon', 'imperialdramondragon'),
    ('dinobeemon', 'imperialdramondragon'),
    ('metalgreymon', 'wargreymon'), ('skullgreymon', 'machinedramon'),
    ('skullgreymon', 'mugendramon'), ('megadramon', 'mugendramon'),
    ('gigadramon', 'mugendramon'), ('metalgreymon', 'blackwargreymon'),
    ('devimon', 'venomvamdemon'), ('myotismon', 'daemon'),
    ('ladydevimon', 'lilithmon'), ('vamdemon', 'belialvamdemon'),
    ('weregarurumon', 'metalgarurumon'), ('metalgarurumon', 'omnimon'),
    ('wargreymon', 'omnimon'), ('omnimon', 'omegamonzwart'),
    ('herculeskabuterimon', 'tyrantkabuterimon'),
    ('rosemon', 'rosemonburst'), ('lilithmon', 'lilithmon'),
    ('beelzemon', 'beelzebumonblast'), ('duftmon', 'duftmon'),
    ('chronomonholy', 'chronomon'), ('alphamon', 'alphamon'),
    ('shinegreymon', 'shinegreymonruin'), ('ravemon', 'ravemon'),
    ('miragegaogamon', 'miragegaogamon'), ('dianamon', 'dianamon'),
    ('apollomon', 'apollomon'), ('minervamon', 'minervamon'),
    ('neptunemon', 'neptunmon'), ('giga seadramon', 'leviamon'),
    ('gigaseadramon', 'leviamon'), ('metal seadramon', 'leviamon'),
    ('phoenixmon', 'hououmon'), ('varodurumon', 'valdurmon'),
    ('bancholeomon', 'bantyoleomon'), ('sleipmon', 'kentaurosmon'),
    ('craniamon', 'craniummon'), ('dynasmon', 'dynasmon'),
    ('ulforceveedramon', 'alforcevdramon'), ('exveemon', 'ulforceveedramon'),
    ('vdramon', 'ulforceveedramon'), ('aerovdramon', 'ulforceveedramon'),
    ('gallantmon', 'chaosgallantmon'), ('megidramon', 'megidramon'),
    ('wargrowlmon', 'megidramon'), ('chaosgallantmon', 'chaosdukemon'),
    ('imperialdramonpaladinmode', 'imperialdramonpaladinmode'),
    ('seraphimon', 'seraphimon'), ('ophanimon', 'orphanimon'),
    ('holydramon', 'holydramon'), ('goddramon', 'goddramon'),
    ('magnaangemon', 'goddramon'), ('birdramon', 'goddramon'),
    ('leomon', 'saber leomon'), ('grappleomon', 'saberleomon'),
    ('panjyamon', 'saberleomon'), ('etemon', 'metaletemon'),
    ('pikmon', 'jyureimon'), ('woodmon', 'jyureimon'),
    ('redvegiemon', 'jyureimon'), ('vegiemon', 'redvegiemon'),
    ('palmon', 'vegiemon'), ('floramon', 'veggiemon'),
    ('gotsumon', 'icemon'), ('gotsumon', 'golemon'),
    ('hagurumon', 'tankmon'), ('toyagumon', 'tankmon'),
    ('agumon', 'greymon'), ('greymon', 'metalgreymon'),
    ('gabumon', 'garurumon'), ('garurumon', 'weregarurumon'),
    ('weregarurumonblack', 'metalgarurumon'),
    ('blackwargreymon', 'blackwargreymon'),
    ('darktyrannomon', 'metaltyrannomon'),
    ('tyrannomon', 'mastertyrannomon'),
    ('veemon', 'exveemon'), ('exveemon', 'paildramon'),
    ('wormmon', 'stingmon'), ('stingmon', 'dinobeemon'),
    ('hawkmon', 'aquilamon'), ('aquilamon', 'silphymon'),
    ('armadillomon', 'ankylomon'), ('ankylomon', 'shakkoumon'),
    ('guilmon', 'growlmon'), ('growlmon', 'wargrowlmon'), ('wargrowlmon', 'gallantmon'),
    ('renamon', 'kyubimon'), ('kyubimon', 'taomon'),
    ('terriermon', 'gargomon'), ('gargomon', 'rapidmon'),
    ('impmon', 'wizardmon'), ('wizardmon', 'mystimon'),
    ('impmon', 'devimon'), ('lopmon', 'turuiemon'), ('lopmon', 'antylamon'),
    ('antylamon', 'cherubimonvirus'), ('patamon', 'angemon'),
    ('salamon', 'gatomon'), ('gatomon', 'angewomon'),
    ('biyomon', 'birdramon'), ('birdramon', 'garudamon'),
    ('tentomon', 'kabuterimon'), ('kabuterimon', 'megakabuterimon'),
    ('gomamon', 'ikakkumon'), ('ikakkumon', 'zudomon'),
    ('palmon', 'togemon'), ('togemon', 'lillymon'),
    ('dorumon', 'dorugamon'), ('dorugamon', 'dorugreymon'), ('dorugreymon', 'dorugoramon'),
    ('falcomon', 'peckmon'), ('peckmon', 'yatagaramon'),
    ('kudamon', 'reppamon'), ('reppamon', 'tyilinmon'), ('tyilinmon', 'sleipmon'),
    ('kamemon', 'gwappamon'), ('gwappamon', 'shawujingmon'),
    ('lalamon', 'sunflowmon'), ('sunflowmon', 'lilamon'), ('lilamon', 'rosemon'),
    ('gaomon', 'gaogamon'), ('gaogamon', 'machgaogamon'),
    ('agumon', 'geogreymon'), ('geogreymon', 'rizegreymon'), ('rizegreymon', 'shinegreymon'),
    ('commandramon', 'sealsdramon'), ('sealsdramon', 'tankdramon'),
    ('dracmon', 'sangloupmon'), ('sangloupmon', 'matadormon'),
    ('falcomon', 'diatrymon'), ('kotemon', 'dinohumon'), ('kotemon', 'dinohyumon'),
    ('kotemon', 'musyamon'), ('kokuwamon', 'blitzmon'),
    ('commandramon', 'hi commandramon'),
    ('pawnchessmonwhite', 'knightchessmonwhite'),
    ('pawnchessmonblack', 'knightchessmonblack'),
    ('knightchessmonwhite', 'bishopchessmon'),
    ('knightchessmonblack', 'rookchessmon'),
    ('bishopchessmon', 'queenchessmon'),
    ('rookchessmon', 'kingchessmon'),
    ('lucemon', 'lucemonchaosmode'), ('lucemonfdm', 'lucemonchaosmode'),
    ('beelzemon', 'beelzemon'), ('impmon', 'beelzemon'),
    ('wizardmon', 'mystimon'), ('sorcerymon', 'mystimon'),
    ('bakemon', 'phantomon'), ('phantomon', 'pharaohmon'),
    ('devimon', 'neo devimon'), ('ladydevimon', 'lilithmon'),
    ('marine devimon', 'pukumon'), ('marinedevimon', 'pukumon'),
    ('gesomon', 'marinedevimon'), ('octomon', 'dagomon'),
    ('coelamon', 'anomalocarimon'), ('ebidramon', 'anomalocarimon'),
    ('seadramon', 'waruseadramon'), ('megaseadramon', 'waruseadramon'),
    ('airdramon', 'megadramon'), ('coredramon', 'wingdramon'),
    ('wingdramon', 'slayerdramon'), ('groundramon', 'breakdramon'),
    ('veedramon', 'aeroveedramon'), ('xvmon', 'paildramon'),
    ('flamedramon', 'magnamon'), ('magnamon', 'magnamon'),
    ('shurimon', 'silphymon'), ('togemogumon', 'togemon'),
    ('goblimon', 'ogremon'), ('goburimon', 'ogremon'),
    ('gazimon', 'devidramon'), ('gazimon', 'darktyrannomon'),
    ('tsukaimon', 'devimon'), ('demidevimon', 'devimon'),
    ('keramon', 'chrysalimon'), ('chrysalimon', 'infermon'), ('infermon', 'diablomon'),
    ('diablomon', 'armagemon'), ('kurisarimon', 'chrysalimon'),
    ('pagumon', 'gazimon'), ('gazimon', 'nanimon'),
    ('muchomon', 'akatorimon'), ('akatorimon', 'sinduramon'),
    ('penguinmon', 'dolphmon'), ('rukamon', 'zudomon'),
    ('ganimon', 'coelamon'), ('syakomon', 'gesomon'),
    ('gomamon', 'ikakkumon'), ('ikakkumon', 'zudomon'), ('zudomon', 'vikemon'),
    ('patamon', 'angemon'), ('angemon', 'magnaangemon'), ('magnaangemon', 'seraphimon'),
    ('salamon', 'gatomon'), ('gatomon', 'angewomon'), ('angewomon', 'ophanimon'),
    ('biyomon', 'birdramon'), ('birdramon', 'garudamon'), ('garudamon', 'phoenixmon'),
    ('tentomon', 'kabuterimon'), ('kabuterimon', 'atlurkabuterimon'),
    ('atlurkabuterimon', 'herculeskabuterimon'),
    ('palmon', 'togemon'), ('togemon', 'lillymon'), ('lillymon', 'rosemon'),
    ('gomamon', 'ikakkumon'),
    ('agumon', 'greymon'), ('greymon', 'metalgreymon'), ('metalgreymon', 'wargreymon'),
    ('gabumon', 'garurumon'), ('garurumon', 'weregarurumon'),
    ('weregarurumon', 'metalgarurumon'),
    ('veemon', 'veedramon'), ('veedramon', 'aeroveedramon'),
    ('hawkmon', 'holsmon'), ('armadillomon', 'ankylomon'),
    ('guilmon', 'growlmon'), ('renamon', 'kyubimon'), ('terriermon', 'galgomon'),
    ('impmon', 'beelzemon'), ('lopmon', 'antylamon'),
    ('wormmon', 'stingmon'), ('stingmon', 'jewelbeemon'),
    ('dorumon', 'dorugamon'), ('falcomon', 'peckmon'),
    ('kudamon', 'reppamon'), ('kamemon', 'gawappamon'),
    ('lalamon', 'sunflowmon'), ('gaomon', 'gaogamon'),
    ('coronamon', 'firamon'), ('firamon', 'flaremon'), ('flaremon', 'apollomon'),
    ('lunamon', 'lekismon'), ('lekismon', 'crescemon'), ('crescemon', 'dianamon'),
    ('kotemon', 'musyamon'), ('musyamon', 'asuramon'),
    ('kokatorimon', 'sinduramon'), ('akatorimon', 'sinduramon'),
    ('unimon', 'hippogriffomon'), ('pegasusmon', 'magnamon'),
    ('centarumon', 'mammothmon'), ('mojyamon', 'mammothmon'),
    ('fridgimon', 'zudomon'), ('icemon', 'meteormon'),
    ('meramon', 'skullmeramon'), ('meramon', 'bluer meramon'),
    ('devimon', 'myotismon'), ('myotismon', 'venommyotismon'),
    ('myotismon', 'malomyotismon'), ('ladydevimon', 'lilithmon'),
    ('bakemon', 'phantomon'), ('soulmon', 'phantomon'),
    ('wizardmon', 'mystimon'), ('sorcermon', 'wisemon'),
    ('nanimon', 'digitamamon'), ('geremon', 'etemon'), ('numemon', 'monzaemon'),
    ('sukamon', 'etemon'), ('etemon', 'metaletemon'), ('etemon', 'kingetemon'),
    ('andromon', 'hiandromon'), ('metalmamemon', 'prince mamemon'),
    ('mamemon', 'prince mamemon'), ('bigmamemon', 'prince mamemon'),
    ('mekamon', 'andromon'), ('guardromon', 'andromon'),
    ('clockmon', 'andromon'), ('revolmon', 'andromon'),
    ('tankmon', 'andromon'), ('mechanorimon', 'andromon'),
    ('golemon', 'volcamon'), ('monochromon', 'triceramon'),
    ('monochromon', 'vermilimon'), ('tyrannomon', 'metaltyrannomon'),
    ('darktyrannomon', 'metaltyrannomon'), ('metaltyrannomon', 'machinedramon'),
    ('skullgreymon', 'machinedramon'), ('megadramon', 'machinedramon'),
    ('gigadramon', 'machinedramon'), ('megadramon', 'giga seadramon'),
    ('devidramon', 'megadramon'), ('airdramon', 'megadramon'),
    ('seadramon', 'megaseadramon'), ('megaseadramon', 'metalseadramon'),
    ('whamon', 'neptunemon'), ('zudomon', 'plesiomon'),
    ('zudomon', 'vikemon'), ('ikakkumon', 'zudomon'),
    ('coelamon', 'plesiomon'), ('anomalocarimon', 'plesiomon'),
    ('hangyomon', 'plesiomon'), ('tylomon', 'plesiomon'),
    ('gesomon', 'dagomon'), ('octomon', 'dagomon'),
    ('shellmon', 'plesiomon'), ('ebidramon', 'waruseadramon'),
    ('birdramon', 'garudamon'), ('garudamon', 'phoenixmon'),
    ('saberdramon', 'karatenmon'), ('akatorimon', 'sinduramon'),
    ('kokatorimon', 'deramon'), ('deramon', 'grypomon'),
    ('deramon', 'gryphomon'), ('parotmon', 'crossmon'),
    ('parrotmon', 'crossmon'), ('crossmon', 'varodurumon'),
    ('yatagaramon', 'varodurumon'), ('peppemon', 'yatagaramon'),
    ('peckmon', 'crowmon'), ('crowmon', 'ravemon'),
    ('yatagaramon', 'ravemon'), ('falcomon', 'peckmon'),
    ('hawkmon', 'aquilamon'), ('silphymon', 'valkyrimon'),
    ('angemon', 'magnaangemon'), ('ankylomon', 'shakkoumon'),
    ('shakkoumon', 'clavisangemon'), ('shakkoumon', 'slashangemon'),
    ('magnaangemon', 'seraphimon'), ('seraphimon', 'shadowseraphimon'),
    ('angewomon', 'ophanimon'), ('gatomon', 'angewomon'),
    ('salamon', 'gatomon'), ('tailmon', 'angewomon'),
    ('leomon', 'grappleomon'), ('grappleomon', 'bancholeomon'),
    ('leomon', 'panjyamon'), ('panjyamon', 'saberleomon'),
    ('icemon', 'zudomon'), ('mojyamon', 'mammothmon'),
    ('mammothmon', 'skullmammothmon'), ('yukidarumon', 'mammon'),
    ('frigimon', 'zudomon'), ('hyogamon', 'zudomon'),
    ('ogremon', 'weregarurumon'), ('fugamon', 'etemon'),
    ('woodmon', 'cherrymon'), ('cherrymon', 'pupumon'),
    ('togemon', 'lillymon'), ('lillymon', 'rosemon'),
    ('sunflowmon', 'lilamon'), ('lilamon', 'rosemon'),
    ('palmon', 'woodmon'), ('floramon', 'woodmon'),
    ('vegiemon', 'redvegiemon'), ('redvegiemon', 'cherrymon'),
    ('kunemon', 'flymon'), ('flymon', 'okuwamon'),
    ('tentomon', 'kabuterimon'), ('kuwagamon', 'okuwamon'),
    ('okuwamon', 'grankuwagamon'), ('kabuterimon', 'atlurkabuterimon'),
    ('atlurkabuterimon', 'herculeskabuterimon'),
    ('snimon', 'okuwamon'), ('dokugamon', 'arukenimon'),
    ('dokugamon', 'archnemon'), ('flymon', 'jewelbeemon'),
    ('wormmon', 'stingmon'), ('stingmon', 'dinobeemon'),
    ('exveemon', 'paildramon'), ('stingmon', 'paildramon'),
    ('paildramon', 'imperialdramon'), ('dinobeemon', 'imperialdramon'),
    ('vdramon', 'aerovdramon'), ('aerovdramon', 'ulforceveedramon'),
    ('xvmon', 'paildramon'), ('veemon', 'flamedramon'),
    ('veemon', 'exveemon'), ('flamedramon', 'raidramon'),
    ('veemon', 'veedramon'), ('guilmon', 'growlmon'),
    ('growlmon', 'wargrowlmon'), ('wargrowlmon', 'gallantmon'),
    ('gallantmon', 'gallantmoncrimsonmode'), ('wargrowlmon', 'megidramon'),
    ('guilmon', 'blackgrowlmon'), ('blackgrowlmon', 'blackwargrowlmon'),
    ('blackwargrowlmon', 'chaosgallantmon'),
    ('renamon', 'kyubimon'), ('kyubimon', 'taomon'), ('taomon', 'sakuyamon'),
    ('terriermon', 'gargomon'), ('gargomon', 'rapidmon'), ('rapidmon', 'megagargomon'),
    ('lopmon', 'turuiemon'), ('turuiemon', 'antylamon'),
    ('antylamon', 'cherubimon'), ('lopmon', 'wendimon'),
    ('impmon', 'beelzemon'), ('beelzemon', 'beelzebumonblast'),
    ('wizardmon', 'baalmon'), ('demidevimon', 'devimon'),
    ('tsukaimon', 'devimon'), ('devimon', 'neo devimon'),
    ('devimon', 'myotismon'), ('myotismon', 'venommyotismon'),
    ('myotismon', 'malomyotismon'), ('ladydevimon', 'lilithmon'),
    ('agumon', 'greymon'), ('greymon', 'skullgreymon'),
    ('greymon', 'metalgreymon'), ('metalgreymon', 'wargreymon'),
    ('wargreymon', 'omnimon'), ('metalgarurumon', 'omnimon'),
    ('agumon', 'geogreymon'), ('geogreymon', 'rizegreymon'),
    ('rizegreymon', 'shinegreymon'), ('shinegreymon', 'shinegreymonburst'),
    ('shinegreymon', 'shinegreymonruin'),
    ('gabumon', 'garurumon'), ('garurumon', 'weregarurumon'),
    ('weregarurumon', 'metalgarurumon'), ('blackagumon', 'darktyrannomon'),
    ('gabumon', 'blackgarurumon'), ('blackgarurumon', 'weregarurumonblack'),
    ('gaomon', 'gaogamon'), ('gaogamon', 'machgaogamon'),
    ('machgaogamon', 'miragegaogamon'), ('miragegaogamon', 'miragegaogamonburst'),
    ('falcomon', 'peckmon'), ('peckmon', 'yatagaramon'), ('yatagaramon', 'ravemon'),
    ('ravemon', 'ravemonburst'), ('yatagaramon', 'varodurumon'),
    ('kudamon', 'reppamon'), ('reppamon', 'tyilinmon'), ('tyilinmon', 'kentaurosmon'),
    ('tyilinmon', 'sleipmon'), ('pawnchessmonwhite', 'knightchessmonwhite'),
    ('knightchessmonwhite', 'bishopchessmon'), ('bishopchessmon', 'queenchessmon'),
    ('pawnchessmonblack', 'knightchessmonblack'),
    ('knightchessmonblack', 'rookchessmon'), ('rookchessmon', 'kingchessmon'),
    ('coronamon', 'firamon'), ('firamon', 'flaremon'), ('flaremon', 'apollomon'),
    ('lunamon', 'lekismon'), ('lekismon', 'crescemon'), ('crescemon', 'dianamon'),
    ('dorumon', 'dorugamon'), ('dorugamon', 'dorugreymon'),
    ('dorugreymon', 'dorugoramon'), ('ryudamon', 'ginryumon'),
    ('ryudamon', 'ginryumon'), ('ginryumon', 'hisaryumon'),
    ('hisaryumon', 'owryumon'), ('dracomon', 'coredramon'),
    ('coredramon', 'wingdramon'), ('wingdramon', 'slayerdramon'),
    ('coredramongreen', 'groundramon'), ('groundramon', 'breakdramon'),
    ('hackmon', 'baohuckmon'), ('baohuckmon', 'saviorhackmon'),
    ('zubamon', 'zuba eagermon'),
]


def norm(value: str) -> str:
    return ''.join(ch for ch in value.lower() if ch.isalnum())


def cleaned_sheet_name(name: str) -> str:
    """Drop filename disambiguators. The species itself stays intact."""
    text = re.sub(r'-\d+$', '', name).strip()
    changed = True
    while changed:
        changed = False
        low = text.lower()
        for suffix in ('garmmon', 'walking', 'large', 'small', 'core', 'xrosboss'):
            if low.endswith(suffix) and len(low) > len(suffix) + 3:
                text = text[:-len(suffix)]
                changed = True
                break
    return text.strip()


def species_key(name: str) -> str:
    key = norm(name)
    changed = True
    while changed:
        changed = False
        for suffix in ('garmmon', 'walking', 'large', 'small', 'core', 'xrosboss'):
            if key.endswith(suffix) and len(key) > len(suffix) + 3:
                key = key[:-len(suffix)]
                changed = True
        if key.startswith('dot') and len(key) > 6:
            key = key[3:]
            changed = True
    return key


def slug(name: str) -> str:
    chars = []
    for ch in name.lower():
        chars.append(ch if ch.isalnum() else '-')
    text = ''.join(chars).strip('-')
    while '--' in text:
        text = text.replace('--', '-')
    return text[:63]


def element(name: str) -> str:
    key = norm(name)
    ember = ('flame', 'fire', 'greymon', 'meramon', 'volc', 'birdra', 'flare', 'agni', 'burn', 'magma', 'dramon', 'tyranno')
    tide = ('sea', 'ocean', 'marin', 'coela', 'geso', 'ebi', 'octo', 'shell', 'ice', 'snow', 'yuki', 'hyoga', 'aqua', 'whale', 'diver', 'fish')
    grove = ('wood', 'veg', 'plant', 'insect', 'kuwaga', 'kabuteri', 'flower', 'lily', 'leaf', 'mushroom', 'bug', 'flor')
    if any(part in key for part in ember):
        return 'ember'
    if any(part in key for part in tide):
        return 'tide'
    if any(part in key for part in grove):
        return 'grove'
    return 'neutral'


def head_json(path: str):
    return json.loads(subprocess.check_output(['git', 'show', f'HEAD:{path}'], cwd=ROOT))


def short_label(name: str, form_id: int, suffix: str, used: set[str]) -> str:
    base = ''.join(ch for ch in name if ch.isalnum()) or 'Form'
    room = 32 - len(suffix)
    token = base[:room]
    label = f'{token}{suffix}'
    if label.casefold() in used:
        stamp = str(form_id)
        token = base[: max(1, room - len(stamp))]
        label = f'{token}{stamp}{suffix}'
    if label.casefold() in used or len(label.encode()) > 32:
        label = f'F{form_id}{suffix}'
    used.add(label.casefold())
    return label


def stage_of(index: int | None, key: str) -> tuple[str, str] | None:
    if key in ARMOR:
        return 'Armor', 'Champion'
    if index is None:
        named = EXTRA_STAGE.get(key)
        if named == 'Armor':
            return 'Armor', 'Champion'
        if named in ('Champion', 'Ultimate', 'Mega'):
            return named, named
        return None
    if index == 417:
        return None
    if index == 418:
        return 'Champion', 'Champion'
    if index == 419:
        return 'Ultimate', 'Ultimate'
    if index < 85:
        return None
    if index < 194:
        return 'Champion', 'Champion'
    if index < 298:
        return 'Ultimate', 'Ultimate'
    return 'Mega', 'Mega'


def sheet_rows(zf: zipfile.ZipFile):
    grouped = defaultdict(list)
    for info in zf.infolist():
        base = info.filename.split('/')[-1]
        if not base.lower().endswith('.png'):
            continue
        stem = base[:-4]
        number = None
        name = stem
        head, _, tail = stem.partition('_')
        if head.isdigit():
            number = int(head)
            name = tail or stem
        if norm(name) in MAPS:
            continue
        grouped[species_key(name)].append((number if number is not None else 10_000, name, info.filename, info.file_size))
    chosen = []
    for key, rows in grouped.items():
        rows.sort(key=lambda row: (0 if row[0] < 10_000 else 1, row[0], len(row[1]), row[1]))
        chosen.append(rows[0])
    chosen.sort(key=lambda row: (row[0], row[1]))
    return chosen


def pack_module():
    spec = importlib.util.spec_from_file_location('pack_dawn_dusk_sheets', ROOT / 'scripts' / 'pack-dawn-dusk-sheets.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def keep_packable(added):
    """Keep only sheets that yield a real battle idle, attack, and 3-frame walk."""
    pack = pack_module()
    kept, skipped = [], []
    with zipfile.ZipFile(ZIP_PATH) as archive:
        for row in added:
            image = pack.isolate(pack.Image.open(io.BytesIO(archive.read(row['file']))))
            try:
                result = pack.pack_sprite(image)
            except (ValueError, OSError) as error:
                skipped.append((row['sheetName'], str(error)))
                continue
            if not result:
                skipped.append((row['sheetName'], 'no idle, attack, and walk'))
                continue
            kept.append(row)
    return kept, skipped


def select(existing: set[str]):
    added, skipped = [], []
    seen = set(existing)
    with zipfile.ZipFile(ZIP_PATH) as zf:
        for number, raw_name, filename, size in sheet_rows(zf):
            name = cleaned_sheet_name(raw_name)
            key = species_key(name)
            if number is not None and number < 85:
                skipped.append((name, 'below champion'))
                continue
            if key in SKIP_EXTRAS or norm(name) in SKIP_EXTRAS:
                skipped.append((name, 'below champion'))
                continue
            mapped = ALIASES.get(key, key)
            if key in seen or mapped in seen:
                skipped.append((name, 'already on the roster'))
                continue
            stage = stage_of(None if number == 10_000 else number, key)
            if stage is None:
                skipped.append((name, 'no champion-or-above stage'))
                continue
            seen.add(key)
            seen.add(mapped)
            added.append({
                'index': None if number == 10_000 else number,
                'sheetName': name,
                'file': filename,
                'bytes': size,
                'key': key,
                'alias': mapped,
                'sourceStage': stage[0],
                'combatTier': stage[1],
            })
    added_keys = {row['key'] for row in added} | set(existing)
    normalized = []
    for name, reason in skipped:
        key = species_key(name)
        mapped = ALIASES.get(key, key)
        if reason == 'no champion-or-above stage' and mapped in added_keys:
            reason = 'already on the roster'
        normalized.append((name, reason))
    return added, normalized


def apply(added):
    catalog_entries = head_json('data/world-ds-catalog.json')['entries']
    existing_norms = {norm(row['displayName']) for row in catalog_entries}
    ids = head_json('data/world-ds-ids.json')
    inventory = head_json('docs/research/world-ds-inventory.json')
    families = head_json('data/world-ds-family-identities.json')
    authored = head_json('docs/research/world-ds-authored-late.json')
    official = head_json('docs/research/world-ds-official-late.json')
    evolutions = head_json('data/world-ds-evolutions.json')
    rarity = head_json('data/encounter-rarity.json')
    preserved = head_json('docs/research/preserved-forms.json')
    used_skills = set()
    for path in ('docs/research/world-ds-authored-early.json', 'docs/research/world-ds-authored-late.json'):
        for row in head_json(path)['entries']:
            for label in row['skills'].values():
                used_skills.add(label.casefold())
    for form in preserved['forms']:
        for label in form['skills'].values():
            used_skills.add(label.casefold())
    used_keys = {row['entryKey'] for row in ids['entries']}
    next_id = max(row['formId'] for row in ids['entries']) + 1
    if next_id != 277:
        raise SystemExit(f'HEAD roster does not end at form 276 (next {next_id})')
    role_index = Counter()
    rarity_index = Counter()
    by_norm = {}
    for row in catalog_entries:
        by_norm[norm(row['displayName'])] = row['formId']
    children = Counter()
    for edge in evolutions['edges']:
        children[edge['fromFormId']] += 1
    for form in preserved['forms']:
        for child in form['children']:
            if child:
                children[form['id']] += 1
    pairs = {(edge['fromFormId'], edge['toFormId']) for edge in evolutions['edges']}
    for form in preserved['forms']:
        for child in form['children']:
            if child:
                pairs.add((form['id'], child))
    kept = []
    for row in added:
        if next_id > MAX_FORM_ID:
            break
        key = slug(row['sheetName'])
        if not key or key in used_keys:
            row['skip'] = 'entry key collision'
            continue
        stage = row['sourceStage']
        role = ROLE_PATTERN[stage][role_index[stage] % len(ROLE_PATTERN[stage])]
        role_index[stage] += 1
        bucket = RARITY_PATTERN[stage][rarity_index[stage] % len(RARITY_PATTERN[stage])]
        rarity_index[stage] += 1
        display = row['sheetName'].replace('_', ' ')
        form_id = next_id
        skills = {
            'physical': short_label(display, form_id, ' Jab', used_skills),
            'heavy': short_label(display, form_id, ' Rush', used_skills),
            'magic': short_label(display, form_id, ' Hex', used_skills),
        }
        record = {
            **row,
            'entryKey': key,
            'displayName': display,
            'formId': form_id,
            'lineageId': form_id + 1000,
            'role': role,
            'type': element(display),
            'skills': skills,
            'rarity': bucket,
        }
        kept.append(record)
        used_keys.add(key)
        by_norm[norm(display)] = form_id
        by_norm[row['key']] = form_id
        if row['alias'] not in existing_norms:
            by_norm[row['alias']] = form_id
        next_id += 1
    left_for_cap = [row['sheetName'] for row in added if row not in kept and 'skip' not in row and row['sheetName'] not in {item['sheetName'] for item in kept}]
    # `added` items that never entered `kept` because the id cap broke the loop.
    kept_names = {row['sheetName'] for row in kept}
    left_for_cap = [row['sheetName'] for row in added if row['sheetName'] not in kept_names and 'skip' not in row]

    def resolve(name: str):
        key = norm(name)
        if key in by_norm:
            return by_norm[key]
        folded = species_key(name)
        mapped = ALIASES.get(folded, folded)
        return by_norm.get(mapped) or by_norm.get(folded)

    new_edges = []
    for parent_name, child_name in LINES:
        parent = resolve(parent_name)
        child = resolve(child_name)
        if not parent or not child or parent == child:
            continue
        if parent <= 276 and child <= 276:
            continue
        if (parent, child) in pairs or children[parent] >= 2:
            continue
        parent_stage = 'Rookie'
        child_stage = None
        for row in kept:
            if row['formId'] == parent:
                parent_stage = row['combatTier']
            if row['formId'] == child:
                child_stage = row['combatTier']
        if child_stage is None:
            for entry in catalog_entries:
                if entry['formId'] == child:
                    child_stage = entry['combatTier']
        if child_stage not in GATES:
            continue
        parent_level = GATES.get(parent_stage, (1, 0))[0]
        child_level, child_bond = GATES[child_stage]
        if child_level <= parent_level:
            continue
        edge = {
            'fromFormId': parent,
            'toFormId': child,
            'minimumLevel': child_level,
            'minimumBond': child_bond,
            'origin': 'dawn-dusk-sheet-line',
            'reviewStatus': 'reviewed-for-prototype',
            'canonicalClaim': False,
            'rationale': f'{parent_name} -> {child_name} uses the existing stage level and bond gates.',
        }
        new_edges.append(edge)
        pairs.add((parent, child))
        children[parent] += 1

    for row in kept:
        ids['entries'].append({
            'entryKey': row['entryKey'],
            'displayName': row['displayName'],
            'sourceStage': row['sourceStage'],
            'formId': row['formId'],
            'preservedFormId': None,
            'lineageId': row['lineageId'],
            'bindingReason': 'Dawn/Dusk sprite sheet with battle idle, attack, and walk frames.',
        })
        families['identities'].append({
            'formId': row['formId'], 'lineageId': row['lineageId'], 'parent': 0, 'children': [],
        })
        inventory['entries'].append({
            'entryKey': row['entryKey'],
            'listedName': row['displayName'],
            'section': f"{row['sourceStage']} Digimon",
            'sourceStage': row['sourceStage'],
            'sheetId': None,
            'sheetUrl': None,
            'siteUploader': None,
            'sheetCreatorCredit': 'Digimon World Dawn/Dusk sprite sheet',
            'detailStatus': 'dawn-dusk-png-sheet',
            'canonicalName': row['displayName'],
            'canonicalStage': row['sourceStage'],
            'officialReferenceUrl': None,
            'canonicalStatus': 'name-from-dawn-dusk-sheet',
            'visualVariant': 'preserve-distinct-sheet',
            'sourceGame': 'Digimon World Dawn/Dusk',
            'listSourceUrl': SOURCE_URL,
        })
        authored['entries'].append({
            'entryKey': row['entryKey'],
            'displayName': row['displayName'],
            'sourceStage': row['sourceStage'],
            'combatTier': row['combatTier'],
            'role': row['role'],
            'type': row['type'],
            'skills': row['skills'],
            'roleRationale': f"An authored {row['role']} reading of {row['displayName']} on the existing {row['sourceStage']} budget.",
            'skillLabelOrigin': 'authored',
        })
        official['entries'].append({
            'entryKey': row['entryKey'],
            'listedName': row['displayName'],
            'sourceGame': 'Digimon World Dawn/Dusk',
            'sourceSection': f"{row['sourceStage']} Digimon",
            'sourceGameStage': row['sourceStage'],
            'sourceListUrl': SOURCE_URL,
            'canonicalName': row['displayName'],
            'canonicalStage': row['sourceStage'],
            'officialSpeciesType': 'Unidentified',
            'officialAttribute': 'Unknown',
            'signatureSkillLabel': None,
            'officialReferenceUrl': None,
            'canonicalEntityKey': row['entryKey'],
            'canonicalStatus': 'sprite-name-only',
            'sheetVariantReviewRequired': True,
            'notes': ['Battle idle, attack, and walk frames come from the Dawn/Dusk PNG sheet. Hurt, sleep, and care reuse the idle frames. Stats use the existing stage role budget.'],
            'officialReferences': [],
        })
        rarity['forms'].append({
            'formId': row['formId'],
            'name': row['displayName'],
            'combatTier': row['combatTier'],
            'rarity': row['rarity'],
        })
    evolutions['edges'].extend(new_edges)
    evolved = {edge['fromFormId'] for edge in evolutions['edges']}
    has_parent = {edge['toFormId'] for edge in evolutions['edges']}
    evolutions['dispositions'] = [
        item for item in evolutions.get('dispositions', [])
        if item['formId'] not in evolved
        and not (item.get('status') == 'independent' and item['formId'] in has_parent)
    ]
    sections = inventory['counts']['sections']
    for row in kept:
        label = f"{row['sourceStage']} Digimon"
        sections[label] = sections.get(label, 0) + 1
    inventory['counts']['individuallyNamedDigimonSheets'] = len(inventory['entries'])

    def dump(path: Path, value):
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')

    dump(ROOT / 'data/world-ds-ids.json', ids)
    dump(ROOT / 'data/world-ds-family-identities.json', families)
    dump(ROOT / 'docs/research/world-ds-inventory.json', inventory)
    dump(ROOT / 'docs/research/world-ds-authored-late.json', authored)
    dump(ROOT / 'docs/research/world-ds-official-late.json', official)
    dump(ROOT / 'data/world-ds-evolutions.json', evolutions)
    dump(ROOT / 'data/encounter-rarity.json', rarity)
    manifest = {
        'added': [{key: row[key] for key in ('formId', 'displayName', 'sourceStage', 'role', 'file', 'index')} for row in kept],
        'edges': len(new_edges),
        'leftForCap': left_for_cap,
    }
    (ROOT / '.personal-assets').mkdir(exist_ok=True)
    dump(ROOT / '.personal-assets/sheet-roster-plan.json', manifest)
    return kept, new_edges, left_for_cap


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    catalog = head_json('data/world-ds-catalog.json')
    existing = {norm(row['displayName']) for row in catalog['entries']}
    added, skipped = select(existing)
    packable, unpackable = keep_packable(added)
    skipped.extend(unpackable)
    added = packable
    print(f'candidates {len(added)} skipped {len(skipped)} cap {MAX_FORM_ID - 276}')
    print(Counter(row['sourceStage'] for row in added))
    print('SKIP reasons', Counter(reason for _, reason in skipped))
    if not args.write:
        for row in added:
            print(f"{row['index'] or 0:4} {row['sourceStage']:10} {row['sheetName']}")
        return
    kept, edges, left = apply(added)
    print(f'wrote {len(kept)} forms and {len(edges)} edges; cap left {len(left)}')


if __name__ == '__main__':
    main()
