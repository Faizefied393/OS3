# CYB3053 Project 3 - Multithreaded Web Server

## Author
Faiz Tariq (Faizefied393)  
University of Tulsa  
Spring 2025

---

## Project Overview

This project implements a multithreaded web server in C that is capable of handling multiple HTTP GET requests concurrently. It includes a thread pool, a bounded buffer, and request scheduling policies. The server serves only static content and includes security against directory traversal attacks.

### Core Features

- Fixed-size thread pool for handling requests concurrently
- Bounded buffer shared between the main thread and worker threads
- Three request scheduling policies:
  - First-In-First-Out (FIFO)
  - Smallest File First (SFF)
  - Random
- Protection from directory traversal attacks using `realpath()` and path validation
- Serves static content (HTML, text, images)
- All logic implemented in `src/request.c`

---

## Environment Setup (WSL)

1. Install development tools:

```bash
sudo apt update && sudo apt install -y build-essential cmake git
Clone the starter repository:

mkdir ~/cyb3053 && cd ~/cyb3053
git clone https://github.com/NSchrick-UTulsa/OSProj3.git
cd OSProj3
Set up Git remotes:

git remote rename origin upstream
git remote add origin https://github.com/Faizefied393/OS3.git
Building the Project

cd build
chmod +x build.sh
./build.sh
If the build is successful, you will see both the server and client binaries in the build/ directory.

Running the Server and Client
Start the Server
Run one of the following commands in Terminal 1, depending on the desired scheduling policy:

./server -t 4 -b 8 -s 0  # FIFO
./server -t 4 -b 8 -s 1  # Smallest File First (SFF)
./server -t 4 -b 8 -s 2  # Random
Run the Client
In Terminal 2:

./client localhost 10000 /files/test1.html
Make sure that the requested file exists in the build/files/ directory.

Creating Test Files
To add files to serve:

mkdir -p build/files
echo "Test 1" > build/files/test1.html
echo "Test 2" > build/files/test2.html
Repeat as needed to add more test content.

Project Structure
src/: Source code files

build/: Compiled binaries and files for serving

build.sh: Shell script to configure and build the project using CMake

Security Measures
Directory traversal is prevented using the following logic in request_handle():

realpath() is used to resolve the full path of the requested file.

getcwd() retrieves the server's working directory.

If the resolved path does not start with the working directory, the request is rejected with a 403 Forbidden response.


