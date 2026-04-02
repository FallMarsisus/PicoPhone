import paho.mqtt.client as mqtt
import ssl
import json

# ==========================================
# ⚙️ CONFIGURATION À REMPLIR
# ==========================================
PRINTER_SERIAL = "0309DA542100818" 

# D'après la doc : Username = <user_id> et Password = <mqtt_token>
MQTT_USER = "u_3437172096"  # Remplacez par votre identifiant Bambu Lab (ex: u_12345678)
MQTT_PASS = "AAD1T9SE-tA3m_0y9O_Nm794UWHhlN2Nv9wUGReyZvS_pXgK-un5qBSX-uFo4Qob9ksAKPi81R4t3jXoCkYoxSdTj4NJjtCeWE0oEjmnve-aTlB3Ip_kJzlysTyc4vZPf_MOLXdSvCpcvO9I"

MQTT_HOST = "us.mqtt.bambulab.com"  
MQTT_PORT = 8883

TOPIC_REPORT = f"device/{PRINTER_SERIAL}/report"
TOPIC_REQUEST = f"device/{PRINTER_SERIAL}/request"

# ==========================================
# GESTION DE LA CONNEXION MQTT
# ==========================================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"\n✅ Connecté avec succès au serveur Cloud Bambu Lab !")
        print(f"📡 Écoute sur le canal : {TOPIC_REPORT}")
        client.subscribe(TOPIC_REPORT)
        
        # Envoi de la commande 'pushall' exactement comme spécifié dans votre doc
        print("⚡ Envoi de la commande 'pushall' pour exiger le statut complet...")
        pushall_cmd = json.dumps({
            "pushing": {
                "command": "pushall"
            }
        })
        client.publish(TOPIC_REQUEST, pushall_cmd)
    else:
        print(f"❌ Échec de la connexion MQTT. Code erreur : {rc}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        data = json.loads(payload)
        
        # L'imprimante envoie ses données dans le bloc "print" (confirmé par votre doc)
        if "print" in data:
            p = data["print"]
            
            # On affiche uniquement si les données de base sont présentes
            if "mc_percent" in p or "nozzle_temper" in p:
                print("\n" + "="*40)
                if "gcode_state" in p:
                    print(f"Statut        : {p['gcode_state']}")
                if "mc_percent" in p:
                    print(f"Progression   : {p['mc_percent']} %")
                if "mc_remaining_time" in p:
                    print(f"Temps restant : {p['mc_remaining_time']} min")
                
                # Récupération des températures (avec la cible entre parenthèses)
                if "nozzle_temper" in p:
                    print(f"Temp. Buse    : {p['nozzle_temper']} °C (Cible: {p.get('nozzle_target_temper', 0)} °C)")
                if "bed_temper" in p:
                    print(f"Temp. Plateau : {p['bed_temper']} °C (Cible: {p.get('bed_target_temper', 0)} °C)")
                
                if "layer_num" in p and "total_layer_num" in p:
                    print(f"Couche        : {p['layer_num']} / {p['total_layer_num']}")
                
                print("="*40)
    except Exception as e:
        # On ignore silencieusement les messages système qui ne nous intéressent pas
        pass

# ==========================================
# LANCEMENT DU PROGRAMME
# ==========================================
print("\n🚀 Démarrage du client MQTT Bambu Lab...")
client = mqtt.Client()
client.username_pw_set(MQTT_USER, MQTT_PASS)

# Configuration de la sécurité TLS obligatoire (port 8883)
client.tls_set(cert_reqs=ssl.CERT_NONE, tls_version=ssl.PROTOCOL_TLS)
client.tls_insecure_set(True) 

client.on_connect = on_connect
client.on_message = on_message

try:
    print(f"Connexion à {MQTT_HOST}:{MQTT_PORT}...")
    client.connect(MQTT_HOST, MQTT_PORT, 60)
    client.loop_forever() # Boucle infinie pour écouter l'imprimante
except KeyboardInterrupt:
    print("\n🛑 Arrêt manuel du script.")
    client.disconnect()