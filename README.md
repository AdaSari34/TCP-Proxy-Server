# 🌐 TCP Proxy Server (C - Winsock)

This project implements a multi-threaded TCP-based proxy server in C using the Winsock2 API.

---

## 📊 Project Overview

The proxy server acts as an intermediary between clients and destination servers.
It supports both HTTP request forwarding and HTTPS tunneling via the CONNECT method.

---

## ⚙️ Features

* Multi-threaded architecture (thread-per-client model)
* HTTP request parsing and forwarding
* HTTPS tunneling using CONNECT method
* Full-duplex communication between client and server
* Timeout mechanism using select()
* Performance metrics:

  * Time-To-First-Byte (TTFB)
  * Bytes transferred
  * Session duration

---

## 🛠️ Technologies Used

* C Programming Language
* Winsock2 (Windows Sockets API)
* Windows API (CreateThread)

---

## 🧠 How It Works

* The server listens on port 8080
* Accepts incoming client connections
* Creates a new thread for each client
* Parses incoming HTTP/HTTPS requests
* Forwards traffic to the target server
* Relays responses back to the client

---

## 🔐 HTTPS Support

For HTTPS traffic, the proxy establishes a tunnel using the CONNECT method and relays encrypted data transparently.

---

## 📈 Metrics

The system calculates:

* Time-To-First-Byte (TTFB)
* Total data transferred
* Session duration

---

## 📄 Documentation

Project report included in repository.

---

## 🚀 Purpose

This project demonstrates low-level network programming, socket management, and concurrent system design.

---

## 👩‍💻 Author

Ada Şevval Sarı
# TCP-Proxy-Server
Multi-threaded TCP proxy server in C with HTTP forwarding, HTTPS tunneling, and performance metrics.
