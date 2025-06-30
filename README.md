<p align="center">
  <img src="logo.png" width="150" alt="leetcli logo"/>
</p>


# leetcli — A Modern LeetCode Command Line Tool

`leetcli` is a powerful and intuitive command-line tool built in **C++17** that allows you to interact with LeetCode directly from your terminal. Whether you're preparing for interviews or grinding through problems, `leetcli` makes the process faster and more developer-friendly.

## 🚀 Tech Stack
- **C++17** — fast, efficient, and portable
- **[cpr](https://github.com/libcpr/cpr)** — a modern C++ HTTP client library used for interacting with LeetCode's REST and GraphQL APIs
- **[nlohmann/json](https://github.com/nlohmann/json)** — easy-to-use JSON parsing for handling API data
- **Google Gemini API** — used for AI-powered features like runtime analysis and intelligent hints

## ✨ Features
- 📝 **Fetch problems** (including the daily question)
- 🧠 **AI-powered analysis** (runtime complexity + hints via Gemini)
- 💻 **Edit solutions in your preferred language** (C++, Python, Java)
- ✅ **Run solutions remotely on LeetCode** with real test cases
- 📤 **Submit solutions** directly from the CLI
- 📂 **Organized project structure** with per-problem folders
- 🔑 **Session management** for authenticated actions
## 🔧 Installation

### ⚡ Quick Install (Recommended)

**For macOS and Linux users:**
```bash
curl -fsSL https://raw.githubusercontent.com/d3kanesa/leetcli/main/install-leetcli.sh | bash
```

---

### 🐧 Linux/macOS

1. **Install prerequisites**:
   ```sh
   # Install Homebrew if you don't have it
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   
   # Install CMake
   brew install cmake
   ```

2. **Set up vcpkg from GitHub**:
   ```sh
   # Clone vcpkg to a permanent location
   git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
   
   # Set the VCPKG_ROOT environment variable
   echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.zshrc
   source ~/.zshrc
   
   # Bootstrap vcpkg
   cd ~/vcpkg
   ./bootstrap-vcpkg.sh
   ```

3. **Clone the repository**:
   ```sh
   git clone https://github.com/d3kanesa/leetcli.git
   cd leetcli
   ```

4. **Install dependencies**:
   ```sh
   vcpkg install cpr nlohmann-json
   ```

5. **Build and install**:
   ```sh
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
   cmake --build build --target install
   ```
   
   **Alternative installation to user directory**:
   ```sh
   cmake --install build --prefix ~/.local
   echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc
   source ~/.zshrc
   ```

6. **Test the installation**:
   ```sh
   leetcli --help
   ```

---

### 🪟 Windows

1. **Install prerequisites**:
   ```cmd
   # Install Chocolatey if you don't have it (run in PowerShell as Administrator)
   Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
   
   # Install Git, CMake, and Visual Studio Build Tools
   choco install git cmake visualstudio2022buildtools
   ```

2. **Set up vcpkg from GitHub**:
   ```cmd
   # Clone vcpkg to a permanent location (e.g., C:\vcpkg)
   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
   
   # Set the VCPKG_ROOT environment variable
   setx VCPKG_ROOT "C:\vcpkg"
   
   # Bootstrap vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   ```

3. **Clone the repository (in a directory of your choice)**:
   ```cmd
   git clone https://github.com/d3kanesa/leetcli.git
   cd leetcli
   ```
4.  **Generate a build folder**
   ```cmd
    mkdir build
    cd build
   ```
5.  **Install dependencies**:
   ```cmd
   vcpkg install cpr nlohmann-json
   ```
6. **Build and install**:
   ```cmd
   cmake -S .. -B . `
          -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
          -DVCPKG_TARGET_TRIPLET=x64-windows `
          -DVCPKG_APPLOCAL_DEPS=ON
   cmake --build . --config Release 
   ```
   
   **Alternative installation to user directory**:
   ```cmd
   cmake --install build --prefix %USERPROFILE%\.local
   setx PATH "%PATH%;%USERPROFILE%\.local\bin"
   ```

7. **Add to PATH (if not using alternative installation)**:
   - Press `Win + S` and search for **"Environment Variables"**
   - Click **"Edit the system environment variables"**
   - In the **System Properties** window, click **"Environment Variables…"**
   - Under **User variables**, find and select `Path`, then click **Edit**
   - Click **New** and add `C:\Program Files\leetcli\bin` (or wherever leetcli.exe was installed)
   - Click **OK** on all windows to apply changes

8. **Test the installation**:
   Open a new terminal (e.g., `cmd`, `PowerShell`, or Windows Terminal) and run:
   ```cmd
   leetcli help
   ```

## ⚖️ Initialization
Run `init` once at the root of your development folder:
```sh
leetcli init
```
This command sets up your `leetcli` workspace:
- Creates a `problems/` folder in the current directory to store fetched problems.
- Asks you to choose your preferred programming language (`cpp`, `python`, `java`), which is used as the default when fetching problems.
- Saves configuration in a `.leetcli_config.json` file.

Additionally, you'll want to authorize your LeetCode session to enable features like problem submission and test running:
- Run the login command:
  ```sh
  leetcli login
  ```
- You'll be prompted to enter your LeetCode session token and CSRF token, which are stored securely for future use.

## 🛠️ Configuration
To set your Gemini API key:
```sh
leetcli config set-gemini-key <your-gemini-key>
```

## 📚 Usage
```sh
leetcli init                        Initialize the problems directory in your current directory
leetcli fetch slug [--lang=...]     Fetch a problem by slug or use 'daily' for the daily question
leetcli solve slug [--lang=...]     Open the solution file in your default editor
leetcli list                        List all fetched problems
leetcli login                       Set your LEETCODE_SESSION and CSRF token
leetcli run slug [--lang=...]       Run your solution against LeetCode testcases
leetcli submit slug [--lang=...]    Submit your solution to LeetCode
leetcli runtime slug [--lang=...]   Analyze time/space complexity using Gemini
leetcli hint slug [--lang=...]      Ask Gemini for a helpful hint based on your solution progress
leetcli hints slug                  Gets the hints for the given problem in leetcode
leetcli topics slug                 Gets the topics for the given problem in leetcode
leetcli config set-gemini-key key   Set your Gemini API key
leetcli help                        Show this help message
```

## 🧠 Example: Runtime Analysis
```sh
leetcli runtime two-sum
```
🧠 Gemini Runtime Analysis:
```
  Time:  O(n)
  Space: O(n)
```

## 🤝 Contributing
Pull requests are welcome! For major changes, please open an issue first to discuss what you would like to change.

## 📄 License
MIT License
