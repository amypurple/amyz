# AMYZ Graphics II corpus results

Format: AMYZ 1.0. One fitted 15-byte table shared by each picture's pattern and color tables.

Every stream was decoded and compared byte-for-byte with its 12,288-byte source.

| Picture | Shared payload | Shared first use | Fast first use | Tiny payload | Tiny first use |
|---|---:|---:|---:|---:|---:|
| Cake | 2843 | 3040 | 3091 | 2925 | 3077 |
| Commando | 5164 | 5361 | 5412 | 5285 | 5437 |
| Warrior | 2771 | 2968 | 3019 | 2846 | 2998 |
| Barbarian | 4164 | 4361 | 4412 | 4246 | 4398 |
| 421 title | 1011 | 1208 | 1259 | 1042 | 1194 |
| Dacman title | 938 | 1135 | 1186 | 972 | 1124 |
| Dacman info | 1203 | 1400 | 1451 | 1238 | 1390 |
| Dacman 2 title | 1118 | 1315 | 1366 | 1156 | 1308 |
| Chateau title | 1578 | 1775 | 1826 | 1615 | 1767 |
| Almighty God - Aliquis Acuario Ingens | 7094 | 7291 | 7342 | 7233 | 7385 |
| Aloha (ArtField 2010, 1) | 6796 | 6993 | 7044 | 6915 | 7067 |
| Aorante - Lady Saturn | 3481 | 3678 | 3729 | 3627 | 3779 |
| bejeweled title public.pp | 1471 | 1668 | 1719 | 1537 | 1689 |
| benchmark-anime-portrait | 2042 | 2239 | 2290 | 2156 | 2308 |
| benchmark-band-logo | 2124 | 2321 | 2372 | 2177 | 2329 |
| benchmark-bat-character | 905 | 1102 | 1153 | 954 | 1106 |
| benchmark-blue-portrait | 5893 | 6090 | 6141 | 6048 | 6200 |
| benchmark-fantasy-camp | 5979 | 6176 | 6227 | 6125 | 6277 |
| benchmark-feliz-navidad | 4023 | 4220 | 4271 | 4132 | 4284 |
| benchmark-laser-squad | 7399 | 7596 | 7647 | 7532 | 7684 |
| benchmark-mermaid-portrait | 5889 | 6086 | 6137 | 6055 | 6207 |
| benchmark-reservoir-dogs | 2199 | 2396 | 2447 | 2311 | 2463 |
| benchmark-robot-quote | 1945 | 2142 | 2193 | 2084 | 2236 |
| benchmark-space-soldier | 5508 | 5705 | 5756 | 5662 | 5814 |
| benchmark-turban-character | 7749 | 7946 | 7997 | 7914 | 8066 |
| benchmark-wizard-sketch | 2111 | 2308 | 2359 | 2167 | 2319 |
| Blair (Chaos Constructions 2009, 6) | 5186 | 5383 | 5434 | 5362 | 5514 |
| Bugs (Demobit 1995, 4) | 2012 | 2209 | 2260 | 2095 | 2247 |
| f1spp | 4132 | 4329 | 4380 | 4297 | 4449 |
| Kozmonaut (Forever 7, 7) | 2683 | 2880 | 2931 | 2778 | 2930 |
| lenna | 9188 | 9385 | 9436 | 9420 | 9572 |
| maze maniac.pp | 2602 | 2799 | 2850 | 2704 | 2856 |
| mona lisa | 9291 | 9488 | 9539 | 9477 | 9629 |
| NewColeco ROM File Edition | 837 | 1034 | 1085 | 885 | 1037 |
| Pegasus (Millennium 1903, 1) | 6233 | 6430 | 6481 | 6386 | 6538 |
| Phantis (3BM OpenAir 2014, 1) | 8204 | 8401 | 8452 | 8323 | 8475 |
| Robee Blaster title | 2005 | 2202 | 2253 | 2081 | 2233 |
| smurf challenge | 1204 | 1401 | 1452 | 1250 | 1402 |
| Stainly Rainbow (ArtField 2006, 3) | 2130 | 2327 | 2378 | 2222 | 2374 |
| Vranov (Chaos Constructions 2001, 7) | 3379 | 3576 | 3627 | 3463 | 3615 |
| Xzema (Chaos Constructions 2001, 2) | 6588 | 6785 | 6836 | 6779 | 6931 |
| Znachok (Enlight 1996, 15) | 2076 | 2273 | 2324 | 2149 | 2301 |

Shared first use counts payload + one 15-byte table + the 182-byte Z80 VRAM decoder.
Fast first use counts the same stream payload + one expanded 45-byte table + the 203-byte fast decoder.
Tiny first use counts payload + the table-free 152-byte decoder.
