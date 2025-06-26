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

### 📦 Quick Install (Recommended)

Download the latest release for your platform from: https://github.com/d3kanesa/leetcli/releases

#### 🪟 Windows
1. Download `leetcli-windows.zip`
2. Extract the ZIP file
3. **Run `install.bat` as Administrator**
4. Add the `install\bin` directory to your PATH

#### 🐧 Linux/macOS
1. Download `leetcli-linux.zip` or `leetcli-macos.zip`
2. Extract the ZIP file
3. Run: `chmod +x install.sh && ./install.sh`
4. Or manually copy `leetcli` to `/usr/local/bin`

---

### 🔨 Build from Source

#### Prerequisites
- **CMake** (3.14 or higher)
- **Git**
- **C++17 compatible compiler**
  - Windows: Visual Studio 2019/2022 or Build Tools
  - Linux: GCC 7+ or Clang 5+
  - macOS: Xcode Command Line Tools

#### 🐧 Linux/macOS

1. **Clone the repository**:
   ```sh
   git clone https://github.com/d3kanesa/leetcli.git
   cd leetcli
   ```

2. **Run the installation script**:
   ```sh
   chmod +x install.sh
   ./install.sh
   ```

   Or build without installing:
   ```sh
   chmod +x build.sh
   ./build.sh
   ```

   Or build manually:
   ```sh
   # Install vcpkg and dependencies
   git clone https://github.com/microsoft/vcpkg.git
   ./vcpkg/bootstrap-vcpkg.sh
   ./vcpkg/vcpkg install cpr nlohmann-json
   
   # Build and install
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build . --config Release
   sudo cmake --install . --prefix /usr/local
   ```

#### 🪟 Windows

1. **Clone the repository**:
   ```cmd
   git clone https://github.com/d3kanesa/leetcli.git
   cd leetcli
   ```

2. **Run the installation script**:
   ```cmd
   install.bat
   ```

   Or build without installing:
   ```cmd
   build.bat
   ```

   Or build manually:
   ```cmd
   REM Install vcpkg and dependencies
   git clone https://github.com/microsoft/vcpkg.git
   vcpkg\bootstrap-vcpkg.bat
   vcpkg\vcpkg.exe install cpr nlohmann-json
   
   REM Build and install
   mkdir build
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=vcpkg\scripts\buildsystems\vcpkg.cmake
   cmake --build build --config Release
   cmake --install build --prefix install
   ```

3. **Add to PATH** (if using manual build):
   - Locate the `install\bin` folder
   - Add it to your system PATH environment variable

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
