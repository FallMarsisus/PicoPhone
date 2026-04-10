import requests
import time

# --- TOUT PARIS (Intra-muros + Bois) ---
GLOBAL_LAT_MIN = 48.8155
GLOBAL_LON_MIN = 2.2241
GLOBAL_LAT_MAX = 48.9021
GLOBAL_LON_MAX = 2.4699

# On garde la grille de 3x3 (9 requêtes)
GRID_STEPS = 5
lat_step = (GLOBAL_LAT_MAX - GLOBAL_LAT_MIN) / GRID_STEPS
lon_step = (GLOBAL_LON_MAX - GLOBAL_LON_MIN) / GRID_STEPS

OVERPASS_URL = "http://overpass-api.de/api/interpreter"

processed_way_ids = set()
all_segments_c_code = []

chunk_number = 1
total_chunks = GRID_STEPS * GRID_STEPS

print(f"🌍 Mode FORCE BRUTE activé. Découpage en {total_chunks} zones...")

# --- BOUCLE SUR LA GRILLE ---
for x in range(GRID_STEPS):
    for y in range(GRID_STEPS):
        
        lat_min = GLOBAL_LAT_MIN + (x * lat_step)
        lat_max = lat_min + lat_step
        lon_min = GLOBAL_LON_MIN + (y * lon_step)
        lon_max = lon_min + lon_step
        
        print(f"\n🔄 Traitement de la zone {chunk_number}/{total_chunks}...")

        query = f"""
        [out:json][timeout:90];
        (
          way["highway"~"motorway|trunk|primary|secondary|tertiary|residential"]({lat_min},{lon_min},{lat_max},{lon_max});
          way["waterway"~"river|canal"]({lat_min},{lon_min},{lat_max},{lon_max});
          way["natural"="water"]({lat_min},{lon_min},{lat_max},{lon_max});
          way["leisure"="park"]({lat_min},{lon_min},{lat_max},{lon_max});
          way["landuse"~"forest|grass|recreation_ground"]({lat_min},{lon_min},{lat_max},{lon_max});
        );
        out geom;
        """

        # 🛡️ LA BOUCLE DE RETRY INFINIE
        success = False
        attempt = 1
        
        while not success:
            try:
                # Timeout un peu plus long pour laisser au serveur le temps de respirer
                response = requests.get(OVERPASS_URL, params={'data': query}, timeout=120)
                
                if response.status_code == 429:
                    print(f"  ⚠️ [Essai {attempt}] Trop de requêtes (429). On force une pause de 10s et on retente...")
                    time.sleep(10)
                    attempt += 1
                    continue

                if "application/json" not in response.headers.get("Content-Type", ""):
                    print(f"  ❌ [Essai {attempt}] Le serveur a craqué (Code {response.status_code}). Pause de 5s et on retente...")
                    time.sleep(5)
                    attempt += 1
                    continue
                
                # Si on arrive ici, c'est que la requête a fonctionné !
                data = response.json()
                success = True 

            except Exception as e:
                print(f"  🔌 [Essai {attempt}] Coupure réseau ou Timeout ({e}). Pause de 5s et on retente...")
                time.sleep(5)
                attempt += 1

        # --- TRAITEMENT DU CARRÉ ---
        print("  ✅ Données téléchargées ! Extraction...")
        for element in data.get('elements', []):
            way_id = element.get('id')
            
            if way_id in processed_way_ids:
                continue
            processed_way_ids.add(way_id)
            
            if element['type'] == 'way' and 'geometry' in element:
                geom = element['geometry']
                tags = element.get('tags', {})
                element_type = 2 
                
                if 'highway' in tags:
                    if tags['highway'] in ['primary', 'secondary', 'trunk', 'motorway']:
                        element_type = 1
                elif 'waterway' in tags or 'natural' in tags:
                    element_type = 3 # EAU
                elif 'leisure' in tags or 'landuse' in tags:
                    element_type = 4 # NATURE

                if len(geom) < 2: continue
                    
                last_p = geom[0] 
                for i in range(1, len(geom)):
                    curr_p = geom[i]
                    diff_lat = abs(curr_p['lat'] - last_p['lat'])
                    diff_lon = abs(curr_p['lon'] - last_p['lon'])
                    
                    if diff_lat < 0.00040 and diff_lon < 0.00040:
                        if i != len(geom) - 1:
                            continue
                    
                    all_segments_c_code.append(f"    {{{last_p['lat']}f, {last_p['lon']}f, {curr_p['lat']}f, {curr_p['lon']}f, {element_type}}},")
                    last_p = curr_p 

        chunk_number += 1
        
        # Le Cooldown drastiquement réduit (2 secondes)
        time.sleep(2)

# --- SAUVEGARDE FINALE ---
print("\n✅ TOUTES LES ZONES SONT TÉLÉCHARGÉES !")

with open("map_data.h", "w") as f:
    f.write("// Fichier généré : TOUT PARIS (Force Brute - Quadrillage 3x3)\n")
    f.write("#include <Arduino.h>\n\n")
    f.write("struct VectorRoad {\n")
    f.write("    float lat1, lon1;\n")
    f.write("    float lat2, lon2;\n")
    f.write("    uint8_t type;\n") 
    f.write("};\n\n")
    f.write("const VectorRoad map_database[] = {\n")
    
    for line in all_segments_c_code:
        f.write(line + "\n")
        
    f.write("};\n")
    f.write(f"const int NUM_ROADS = {len(all_segments_c_code)};\n")

print(f"🚀 VICTOIRE ABSOLUE ! {len(all_segments_c_code)} segments exportés dans 'map_data.h'.")