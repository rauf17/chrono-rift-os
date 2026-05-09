# ✨ Chrono-Rift-OS 🚀

This repository hosts `Chrono-Rift-OS`, a multi-process C++17 Linux game meticulously engineered to demonstrate fundamental operating system concepts. Developed with a focus on concurrency and inter-process communication, this project serves as an educational tool and a practical example of building a complex system using core OS primitives.

**Key Concepts Explored:**
*   **🐧 POSIX Shared Memory:** 💾 Facilitates efficient data exchange between concurrent processes, enabling a unified game state.
*   **🚦 Semaphores:** Manages access to critical sections and synchronizes operations across processes, preventing race conditions.
*   **🧵 Pthreads:** Utilized for multi-threading within processes, showcasing concurrent execution patterns.
*   **🐚 ncurses TUI:** Provides a terminal-based user interface, offering an interactive gaming experience.
*   **📡 Signal Handling:** Implements robust error handling and graceful termination mechanisms for inter-process communication.

The game is structured around three concurrent processes—Arbiter, ASP, and HIP—each with distinct responsibilities, communicating to create a cohesive experience. This architecture provides a rich environment for understanding process lifecycle, synchronization, and resource management in a practical, game-oriented context.

**Group Members:**
*   Abdul Rauf
*   Ahmad Bilal

## 🛠️ Installation Guide

This project can be set up and run using two primary methods: via Docker for a consistent environment, or by building natively on a Linux system.

### 🐳 Method 1: Using Docker (Recommended)

Docker provides an isolated and consistent environment, ensuring all dependencies are met without affecting your host system.

1.  **📦 Prerequisites:**
    *   Ensure Docker is installed on your system. You can download it from [docker.com](https://www.docker.com/get-started).

2.  **➡️ Navigate to the Submission Directory:**
    ```bash
    cd submission
    ```

3.  **⬆️ Build the Docker Image:**
    This command will build the Docker image named `chrono-rift-os-image` based on the `Dockerfile` in the `submission` directory.
    ```bash
    docker build -t chrono-rift-os-image .
    ```

4.  **🔗 Run the Docker Container:**
    Once the image is built, you can run the application. The `-it` flags provide an interactive terminal, and `--rm` ensures the container is removed after exit.
    ```bash
    docker run -it --rm chrono-rift-os-image
    ```
    The application will launch directly within the Docker container's terminal.

### 💻 Method 2: Native Build (Linux)

For developers who prefer to compile and run the project directly on their Linux machine.

1.  **📦 Prerequisites:**
    *   A C++17 compatible compiler (e.g., g++).
    *   `make` utility.
    *   `ncurses` development libraries:
        *   On Debian/Ubuntu: `sudo apt-get install build-essential libncurses-dev`
        *   On Fedora: `sudo dnf install @development-tools ncurses-devel`
        *   On Arch Linux: `sudo pacman -S base-devel ncurses`

2.  **➡️ Navigate to the Submission Directory:**
    ```bash
    cd submission
    ```

3.  **⬆️ Compile the Project:**
    Use the provided `Makefile` to compile all executables.
    ```bash
    make
    ```
    This command will generate the `arbiter`, `asp`, and `hip` executables in their respective directories (`submission/arbiter`, `submission/asp`, `submission/hip`).

## ▶️ Usage Guide

Once the project is successfully built (either via Docker or natively), you can launch the game.

### 🕹️ Running the Application

*   **If running via Docker:**
    The application will automatically launch upon executing the `docker run` command as described in the installation section. You will be greeted by the `ncurses` TUI.

*   **If running natively:**
    After compiling with `make` in the `submission` directory, you can execute the `arbiter` process. The `arbiter` is responsible for spawning and managing the other processes (`asp` and `hip`).

    1.  **➡️ Navigate to the Submission Directory:**
        ```bash
        cd submission
        ```
    2.  **🚀 Execute the Arbiter:**
        ```bash
        ./arbiter/arbiter
        ```
        This will initiate the game. The `arbiter` will fork and exec the `asp` (Application Specific Process) and `hip` (High-level Interface Process), establishing shared memory and semaphore connections.

### 👁️ Game Interface (ncurses TUI)

The game features a text-based user interface developed with `ncurses`.
*   Expect a split-screen layout or various windows displaying game state, player information, and potential interaction prompts.
*   Use standard terminal controls (e.g., arrow keys, 'q' to quit, 'WASD') if applicable, though specific controls will depend on the game's design.
*   Observe how different processes contribute to the visual output and game logic, all orchestrated through POSIX IPC.

### 💬 Interacting with the Game

The `chrono-rift-os` experience showcases the dynamic interplay between the three processes:
*   The `arbiter` manages game resources and orchestrates overall flow.
*   The `asp` might handle core game logic, calculations, or AI.
*   The `hip` likely manages user input and renders the game state to the `ncurses` display.
*   Experiment with different inputs if the game allows, and observe the system's responsiveness and how signals/semaphores ensure consistent state.

## 🤝 Contributing

We welcome contributions to `chrono-rift-os`! Whether you're interested in adding new features, improving existing code, or refining documentation, your input is highly valued.

### 🐞 Bug Reports

If you discover any issues or unexpected behavior:
*   Open an issue on the GitHub repository.
*   Clearly describe the bug, including steps to reproduce it.
*   Provide any relevant error messages or console output.

### 💡 Feature Requests

Have an idea for an enhancement or a new feature?
*   Open an issue on the GitHub repository.
*   Describe your proposed feature and its potential benefits.

### 📥 Code Contributions

We encourage pull requests for bug fixes, new features, or performance improvements.
1.  **📝 Fork** the repository.
2.  **🌿 Create a new branch** for your changes (e.g., `feature/my-new-feature` or `fix/bug-description`).
3.  **💻 Make your changes** and ensure they adhere to the existing coding style.
4.  **🧪 Test your changes** thoroughly.
5.  **⬆️ Commit your changes** with clear, concise commit messages.
6.  **🚀 Push your branch** to your forked repository.
7.  **🔄 Open a Pull Request** to the `main` branch of the original repository.
    *   Provide a detailed description of your changes.
    *   Reference any related issues.
