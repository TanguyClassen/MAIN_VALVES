# ✅ OPC UA to Valve TCP Bridge (Windows, full dual-valve support)
# Connects to WAGO OPC UA server and relays variables to each valve (Ethanol & N2O) over TCP

import socket
import json
import base64
import time
from opcua import Client, ua

# === OPC UA Setup ===
OPCUA_SERVER_URL = "opc.tcp://192.168.1.17:4840"
client = Client(OPCUA_SERVER_URL)
client.set_user("")
client.set_password("")
client.connect()
print("✅ Connected to OPC UA valve system")

# === NodeId declarations ===
# Ethanol
node_b_Homing_E      = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOovcCHuE6i5ztsZA"), 5, ua.NodeIdType.ByteString)
node_b_SingleStep_E  = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOovcE32H5CxxuvclZLbGQA=="), 5, ua.NodeIdType.ByteString)
node_i_status_epos_E = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOoDcE2CI9zVntuYwe5rcBRQ="), 5, ua.NodeIdType.ByteString)
node_w_main_E        = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOp7cDXWA7QVCtsZA"), 5, ua.NodeIdType.ByteString)

# N2O (Oxidizer)
node_b_Homing_O      = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOovcCHuE6i5ztsxA"), 5, ua.NodeIdType.ByteString)
node_b_SingleStep_O  = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOovcE32H5CxxuvclZLbMQA=="), 5, ua.NodeIdType.ByteString)
node_i_status_epos_O = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOoDcE2CI9zVntuYwe5rcDxQ="), 5, ua.NodeIdType.ByteString)
node_w_main_O        = ua.NodeId(base64.b64decode("AQAAAKbhKnGK9zM6o+Y1NI3mYGeQ7iJ7heYzOp7cDXWA7QVCtsxA"), 5, ua.NodeIdType.ByteString)

# === TCP server setup ===
HOST = "0.0.0.0"
PORT = 4840
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind((HOST, PORT))
sock.listen(2)
print("🖧 Valve system server running on port 4840")
print("⏳ Waiting for 🅴 Ethanol and 🅾️ N₂O valves to connect...")

ethanol_conn = None
n2o_conn = None

while not ethanol_conn or not n2o_conn:
    conn, addr = sock.accept()
    if not ethanol_conn:
        ethanol_conn = conn
        print(f"✅ 🅴 Ethanol valve connected from {addr}")
    elif not n2o_conn:
        n2o_conn = conn
        print(f"✅ 🅾️ N₂O valve connected from {addr}")

try:
    while True:
        # 1. Read values from OPC UA
        try:
            homing_e = client.get_node(node_b_Homing_E).get_value()
            homing_o = client.get_node(node_b_Homing_O).get_value()
            main_e   = client.get_node(node_w_main_E).get_value()
            main_o   = client.get_node(node_w_main_O).get_value()
            single_e = client.get_node(node_b_SingleStep_E).get_value()
            single_o = client.get_node(node_b_SingleStep_O).get_value()
        except Exception as e:
            print(f"⚠️ OPC UA read failed: {e}")
            homing_e = homing_o = single_e = single_o = 0
            main_e = main_o = 0

        # 2. Send data to each valve
        data_e = json.dumps({
            "b_Homing_E": homing_e,
            "w_Main_EV": main_e,
            "b_SingleStep_E": single_e
        }) + "\n"

        data_o = json.dumps({
            "b_Homing_E": homing_o,
            "w_Main_EV": main_o,
            "b_SingleStep_E": single_o
        }) + "\n"

        try:
            ethanol_conn.sendall(data_e.encode())
            n2o_conn.sendall(data_o.encode())
        except Exception as e:
            print(f"⚠️ TCP send error: {e}")

        # 3. Read feedback from valves and update OPC UA
        for conn, status_node in [(ethanol_conn, node_i_status_epos_E), (n2o_conn, node_i_status_epos_O)]:
            try:
                conn.settimeout(0.1)
                feedback = conn.recv(256).decode().strip()
                for line in feedback.splitlines():
                    try:
                        msg = json.loads(line)
                        if "status_epos_e" in msg:
                            val = int(msg["status_epos_e"])
                            client.get_node(status_node).set_value(val)
                            print(f"📥 Valve status update → {status_node}: {val}")
                    except Exception as je:
                        print(f"⚠️ JSON error: {je}")
            except socket.timeout:
                pass

        time.sleep(0.2)

except KeyboardInterrupt:
    print("🛑 Shutting down server")
    ethanol_conn.close()
    n2o_conn.close()
    sock.close()
    client.disconnect()
