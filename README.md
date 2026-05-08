# 🚀 Chrono Rift OS

"Chrono Rift OS" is a sophisticated C++17 Linux game engineered to provide a practical, interactive demonstration of fundamental operating system concepts. This multi-process application leverages a suite of POSIX primitives, including shared memory, semaphores, pthreads, and robust signal handling, across three concurrently executing processes. Featuring an engaging `ncurses` Text-User Interface (TUI), this project transforms complex OS principles into a tangible, observable experience. It stands as an excellent educational resource for anyone looking to deepen their understanding of inter-process communication (IPC), concurrency management, and low-level system programming in a real-world context.

## 🌟 Key Features

*   🎮 **Multi-Process Architecture**: Designed with three distinct, concurrently executing processes (`arbiter`, `asp`, `hip`) to simulate a distributed application.
*   🧠 **POSIX Shared Memory**: Utilizes shared memory segments for efficient and direct data exchange between processes, demonstrating critical IPC mechanisms.
*   🚦 **POSIX Semaphores**: Implements semaphores for robust synchronization, ensuring mutual exclusion and proper coordination when accessing shared resources.
*   🧵 **POSIX Pthreads**: Explores concepts related to thread management and concurrency (implicitly, through the design of concurrent processes leveraging POSIX primitives).
*   🚨 **Signal Handling**: Incorporates advanced signal handling mechanisms for graceful process termination, error management, and inter-process event notification.
*   🖥️ **`ncurses` TUI**: Provides an interactive Text-User Interface, offering a dynamic visual representation of game state and process interactions.
*   🛠️ **Modern C++17 Implementation**: Developed using contemporary C++ features for maintainability, performance, and best practices.
*   🐳 **Dockerized Environment**: Ships with a Dockerfile for easy setup, ensuring a consistent and isolated execution environment across different systems.

## 📐 Architecture Overview

The Chrono Rift OS project is structured around three primary, distinct processes that communicate and synchronize using POSIX IPC mechanisms:

*   **Arbiter (`arbiter`)**:
    The central control process. It manages the overall game state, player inventory, and artifact collection logic. The `arbiter` orchestrates interactions between the `asp` and `hip` processes, acting as the primary coordinator, utilizing shared memory and semaphores for state management and synchronization.
*   **ASP (`asp`)**:
    The "Artifact Search Process." This process is dedicated to simulating the search for game artifacts within the Chrono Rift environment. It communicates findings and requests for resources to the `arbiter`.
*   **HIP (`hip`)**:
    The "Hazard Identification Process." This process monitors for in-game hazards, events, or anomalies. It is responsible for detecting critical situations and relaying vital information back to the `arbiter` for appropriate action.

**Shared State**: All processes interact with a common memory region, defined by `shared_state.h`, which serves as the central data store. Access to this shared memory is strictly controlled by POSIX semaphores to prevent race conditions and ensure data integrity.

## ⚙️ Technical Stack

*   **Primary Language**: C++17
*   **Build System**: GNU Make
*   **Core Libraries**: POSIX (for IPC, Pthreads, Signals), `ncurses`
*   **Containerization**: Docker

## 📂 File Structure

The repository is organized as follows:

```
.
├── README.md
├── submission/
│   ├── Dockerfile                           # Defines the Docker image build process
│   ├── Makefile                             # Manages the compilation and linking of all processes
│   ├── arbiter/                             # Source code and build artifacts for the Arbiter process
│   │   ├── arbiter                          # Compiled executable for the Arbiter
│   │   ├── arbiter.cpp                      # Main implementation of the Arbiter's logic
│   │   ├── arbiter.o                        # Object file for arbiter.cpp
│   │   ├── artifact_manager.cpp             # Implements artifact management logic
│   │   ├── artifact_manager.h               # Header for artifact_manager.cpp
│   │   ├── inventory.cpp                    # Implements player inventory management
│   │   └── inventory.h                      # Header for inventory.cpp
│   ├── asp/                                 # Source code and build artifacts for the Artifact Search Process
│   │   ├── asp                              # Compiled executable for the ASP
│   │   └── asp.cpp                          # Main implementation of the ASP's logic
│   ├── hip/                                 # Source code and build artifacts for the Hazard Identification Process
│   │   ├── hip                              # Compiled executable for the HIP
│   │   └── hip.cpp                          # Main implementation of the HIP's logic
│   └── shared/                              # Shared headers and definitions for inter-process communication
│       └── shared_state.h                   # Defines the structure of the shared memory segment
└── ...
```

## 🚀 Installation

To get Chrono Rift OS up and running, choose your preferred method below.

### 📋 Prerequisites

*   **Git**: Required to clone the repository.
*   **Docker** (Recommended for ease of setup) OR
*   **GNU C++ Compiler (g++ C++17 compatible)** and **GNU Make** (For building from source).

### ⬇️ Clone the Repository

Begin by cloning the project repository to your local machine:

```bash
git clone https://github.com/your-username/chrono-rift-os.git
cd chrono-rift-os/submission
```

### 🐳 Option 1: Using Docker (Recommended)

Docker provides a consistent and isolated environment, simplifying the setup process.

1.  **Build the Docker Image**:
    Navigate to the `submission` directory and build the Docker image. This process compiles the application within the Docker container.
    ```bash
    docker build -t chrono-rift-os .
    ```
2.  The `chrono-rift-os` Docker image is now ready for execution.

### 🔧 Option 2: Building from Source

If you prefer to compile and run the application directly on your system:

1.  **Install Dependencies**: Ensure you have `g++` (C++17 compatible) and `make` installed. You may also need `ncurses` development libraries (e.g., `libncurses-dev` on Debian/Ubuntu).
    ```bash
    # Example for Debian/Ubuntu
    sudo apt update
    sudo apt install build-essential libncurses-dev
    ```
2.  **Navigate to the `submission` directory**:
    ```bash
    cd submission
    ```
3.  **Compile the Project**:
    Use the provided `Makefile` to compile all processes:
    ```bash
    make
    ```
    This command will generate the `arbiter`, `asp`, and `hip` executables within their respective directories (`./arbiter/`, `./asp/`, `./hip/`).

## ▶️ Usage

Once installed, you can launch Chrono Rift OS using either Docker or directly from the compiled executables.

### 🐳 Option 1: Running with Docker

Execute the Docker image to start the game:

```bash
docker run -it --rm chrono-rift-os
```

*   The `-it` flags are crucial for interacting with the `ncurses` Text-User Interface.
*   The `--rm` flag automatically cleans up the container upon exit.

### ⚙️ Option 2: Running from Source

1.  **Navigate to the `submission` directory**:
    ```bash
    cd submission
    ```
2.  **Launch the Arbiter**:
    Start the main `arbiter` process. The `arbiter` is responsible for launching and managing the `asp` and `hip` processes, initiating the multi-process game environment.
    ```bash
    ./arbiter/arbiter
    ```
3.  The `ncurses` TUI should initialize, presenting the game's interface in your terminal.

**Termination**: To gracefully exit the game, typically pressing `Ctrl+C` in the terminal where the `arbiter` is running will trigger the implemented signal handling mechanisms to shut down all concurrent processes.

## 🤝 Contributing

Contributions are welcome and greatly appreciated! If you have suggestions for improvements, bug reports, or want to add new features, please follow these guidelines:

1.  📝 **Fork the Repository**: Start by forking the `chrono-rift-os` repository to your GitHub account.
2.  ✨ **Create a New Branch**: Create a new branch for your feature or bug fix:
    ```bash
    git checkout -b feature/your-feature-name
    ```
3.  💡 **Implement Your Changes**: Make your modifications, ensuring adherence to the project's coding style and C++17 standards.
4.  ⬆️ **Commit and Push**: Commit your changes with a clear, descriptive message and push them to your forked repository:
    ```bash
    git commit -m 'feat: Add a new feature or fix a bug'
    git push origin feature/your-feature-name
    ```
5.  ✉️ **Open a Pull Request**: Submit a pull request to the `main` branch of the original `chrono-rift-os` repository. Provide a detailed description of your changes.
