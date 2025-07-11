import socket
import json
import base64
import time
from opcua import Client, ua

# === OPC UA Setup ===
OPCUA_SERVER_URL = "opc.tcp://localhost:4840"
client = Client(OPCUA_SERVER_URL)
client.connect()
print("✅ Connected to OPC UA server")

# === NodeId declarations (replace the "..." with your real NodeIds!) ===
node_s_server_status    = ...  # STRING - General server status
node_s_system_message_e = ...  # STRING - Ethanol valve status
node_s_system_message_o = ...  # STRING - N2O valve status

node_b_Homing_E         = ...  # BOOL - Homing command Ethanol
node_w_Main_EV          = ...  # INT  - Target opening Ethanol
node_b_SingleStep_E     = ...  # BOOL - Single step fine adjust Ethanol
node_b_Reboot_Valve_E   = ...  # BOOL - Reboot command Ethanol
node_status_epos_e      = ...  # INT  - Status feedback Ethanol

# === TCP Server Setup ===
HOST = "0.0.0.0"
PORT = 4840
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind((HOST, PORT))
sock.listen(2)
print(f"📡 TCP Server listening on {PORT}")

ethanol_client = None
n2o_client = None

def update_system_status():
    if ethanol_client and n2o_client:
        client.get_node(node_s_server_status).set_value("✅ 🅴 Ethanol and 🅾️ N₂O valves connected.")
    elif ethanol_client:
        client.get_node(node_s_server_status).set_value("✅ 🅴 Ethanol connected. ⏳ Waiting for 🅾️ N₂O valve...")
    elif n2o_client:
        client.get_node(node_s_server_status).set_value("✅ 🅾️ N₂O connected. ⏳ Waiting for 🅴 Ethanol valve...")
    else:
        client.get_node(node_s_server_status).set_value("✅ System running. ⏳ Waiting for 🅴 Ethanol and 🅾️ N₂O valves...")

def update_valve_status(valve, connected):
    if valve == "ethanol":
        node = node_s_system_message_e
        if connected:
            client.get_node(node).set_value("🔌 🅴 Ethanol valve online.")
        else:
            client.get_node(node).set_value("🛑 🅴 Ethanol valve offline.")
    if valve == "n2o":
        node = node_s_system_message_o
        if connected:
            client.get_node(node).set_value("🔌 🅾️ N₂O valve online.")
        else:
            client.get_node(node).set_value("🛑 🅾️ N₂O valve offline.")

try:
    while True:
        sock.settimeout(0.5)
        try:
            conn, addr = sock.accept()
            print(f"🔌 New device connected from {addr}")
            if ethanol_client is None:
                ethanol_client = conn
                update_valve_status("ethanol", True)
            elif n2o_client is None:
                n2o_client = conn
                update_valve_status("n2o", True)
            update_system_status()
        except socket.timeout:
            pass

        # 1. Read variables from OPC UA
        try:
            homing_e = client.get_node(node_b_Homing_E).get_value()
            main_ev = client.get_node(node_w_Main_EV).get_value()
            single_step_e = client.get_node(node_b_SingleStep_E).get_value()
            reboot_valve_e = client.get_node(node_b_Reboot_Valve_E).get_value()
        except Exception as e:
            print(f"⚠️ OPC UA read failed: {e}")
            homing_e, main_ev, single_step_e, reboot_valve_e = 0, 0, 0, 0

        # 2. Send to valves over TCP
        payload = {
            "b_Homing_E": homing_e,
            "w_Main_EV": main_ev,
            "b_SingleStep_E": single_step_e,
            "b_Reboot_Valve_E": reboot_valve_e
        }
        data = json.dumps(payload) + "\n"

        try:
            if ethanol_client:
                ethanol_client.sendall(data.encode())
        except Exception as e:
            print("⚠️ Ethanol disconnected")
            ethanol_client = None
            update_valve_status("ethanol", False)
            update_system_status()

        try:
            if n2o_client:
                n2o_client.sendall(data.encode())
        except Exception as e:
            print("⚠️ N₂O disconnected")
            n2o_client = None
            update_valve_status("n2o", False)
            update_system_status()

        # 3. Receive feedback (non-blocking)
        for client_conn, valve_name in [(ethanol_client, "ethanol"), (n2o_client, "n2o")]:
            if client_conn:
                client_conn.settimeout(0.1)
                try:
                    feedback = client_conn.recv(512).decode().strip()
                    for line in feedback.splitlines():
                        try:
                            msg = json.loads(line)
                            if "status_epos_e" in msg and valve_name == "ethanol":
                                val = int(msg["status_epos_e"])
                                client.get_node(node_status_epos_e).set_value(val)
                                print(f"📥 Ethanol valve position: {val}%")
                        except Exception as e:
                            print(f"⚠️ JSON error from {valve_name}: {e}")
                except socket.timeout:
                    pass
                except Exception as e:
                    print(f"⚠️ Connection lost with {valve_name}: {e}")
                    if valve_name == "ethanol":
                        ethanol_client = None
                        update_valve_status("ethanol", False)
                    if valve_name == "n2o":
                        n2o_client = None
                        update_valve_status("n2o", False)
                    update_system_status()

        time.sleep(0.2)

except KeyboardInterrupt:
    print("🛑 Shutting down...")
    sock.close()
    client.disconnect()

