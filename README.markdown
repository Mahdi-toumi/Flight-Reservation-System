# Flight Reservation System

## About
Flight Reservation System is a C-based client-server application developed by Mahdi Toumi, Youssef Srasra, Mohamed Houssem Salhi, and Jihed Mekni at the National Engineering School of Carthage. It simulates an online flight booking system where travel agencies (clients) interact with a central airline server to manage flight reservations, cancellations, and invoices using TCP and UDP protocols.

## Features
- **Client-Server Architecture**: Agencies (clients) communicate with a central airline server to manage flight data.
- **Reservation Management**: Book or cancel flights, with real-time updates to available seats.
- **Data Persistence**: Stores flight details, transaction history, and invoices in `vols.txt`, `histo.txt`, and `facture.txt`.
- **Protocol Support**: Supports both TCP (reliable, connection-oriented) and UDP (connectionless) communication.
- **Concurrency Handling**: Manages simultaneous client requests with thread-based TCP and mutex-protected UDP.
- **Command-Line Interface**: Simple interface for agencies to list flights, reserve seats, cancel bookings, and view invoices.

## Installation
1. **Prerequisites**:
   - GCC compiler
   - POSIX-compliant system (Linux/UNIX recommended)
   - Standard C libraries
2. **Setup**:
   - Clone the repository: `git clone https://github.com/Mahdi-toumi/Flight-Reservation-System.git`
   - Navigate to the project directory: `cd flight-reservation-system`
3. **Compile**:
   - Compile server: `gcc server.c -o server -pthread`
   - Compile client: `gcc client.c -o client`
4. **Run**:
   - Start server: `./server [tcp|udp]`
   - Start client: `./client [tcp|udp] <agency_name>`

## Project Structure
- **server.c**: Implements the airline server, handling client requests and file updates.
- **client.c**: Implements the agency client, sending reservation/cancellation requests.
- **Data Files**:
  - `vols.txt`: Stores flight details and available seats.
  - `histo.txt`: Logs transaction history.
  - `facture.txt`: Records invoice details.

## Usage
1. Launch the server with the desired protocol (e.g., `./server tcp`).
2. Start one or more clients (e.g., `./client udp Agence1`).
3. Use the command-line interface to:
   - List available flights (`LIST`).
   - Reserve seats (`RESERVER <flight_id> <agency_name>`).
   - Cancel reservations (`ANNULER <flight_id> <agency_name>`).
   - View invoices (`FACTURE`).
4. Check `facture.txt` for generated invoices and `histo.txt` for transaction logs.

## Limitations
- Relies on text files for data persistence, limiting scalability.
- No graphical user interface; uses command-line interaction.
- UDP reliability depends on manual retransmission and timeout handling.
- Lacks authentication for agency requests.

## Future Improvements
- Implement a relational database (e.g., SQLite) for better data management.
- Develop a graphical interface using C/GTK or a web-based frontend.
- Add agency authentication for enhanced security.
- Support dynamic flight management (add/remove flights in real-time).
- Enable secure communication with SSL/TLS.

## Contributors
- **Mahdi Toumi**
- **Youssef Srasra**
- **Mohamed Houssem Salhi**
- **Jihed Mekni**

## Acknowledgments
Developed as part of the System and Network Programming course at the National Engineering School of Carthage, University of Carthage, for the academic year 2024-2025.
