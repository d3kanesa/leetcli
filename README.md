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
1. Clone the repository:
   ```sh
   git clone https://github.com/yourusername/leetcli.git
   cd leetcli
   ```
2. Install dependencies with vcpkg (or ensure they are installed):
   ```sh
   vcpkg install cpr nlohmann-json
   ```
3. Build and install:
   ```sh
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build --target install
   ```
   Optionally set a prefix:
   ```sh
   cmake --install build --prefix ~/.local
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
leetcli help                        Show this help message.
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
