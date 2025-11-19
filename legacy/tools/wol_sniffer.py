#!/usr/bin/env python3
import socket

# Configuración
PORT = 9  # Puerto estándar de Wake-on-LAN
BIND_IP = ''  # Escucha en todas las interfaces

print(f"Escuchando paquetes WOL en UDP puerto {PORT}...")

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((BIND_IP, PORT))
    while True:
        data, addr = s.recvfrom(2048)
        if len(data) >= 102 and data.startswith(b'\xff' * 6):
            mac = ':'.join(f'{b:02x}' for b in data[6:12])
            print(f"\n¡Paquete WOL recibido de {addr[0]}:{addr[1]}!")
            print(f"MAC objetivo: {mac}")
            print(f"Tamaño: {len(data)} bytes")
        else:
            print(f"Paquete UDP recibido de {addr}, tamaño {len(data)} bytes (no parece WOL)")
