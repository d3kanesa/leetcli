<p align="center">
  <img src="logo.png" width="140" alt="leetcli logo"/>
</p>

<h1 align="center">leetcli</h1>

<p align="center">A terminal client for LeetCode.</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="C++17"/>
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT License"/>
</p>

---

leetcli brings LeetCode into your terminal. Run it on its own for an interactive UI where you can browse problems, read them, and pull them into your workspace — or use the individual commands to fetch, run, and submit solutions in any language LeetCode supports. It talks to LeetCode's own API, so your test cases and starter code are the real thing.

## Why

Solving LeetCode problems means bouncing between a browser tab and your editor. leetcli keeps everything in one place: fetch a problem, solve it in the editor and language you already use, and run it against LeetCode's judge without leaving the terminal.

## Install

**Homebrew** (macOS / Linux):

```sh
brew install gaminrick7/leetcli/leetcli
```

**Docker** — no C++ toolchain needed:

```sh
docker pull d3kanesa/leetcli
```

Then alias it (in `~/.zshrc` or `~/.bashrc`) so config and problems land on your host:

```sh
alias leetcli='docker run --rm -it \
  -v "$HOME/.leetcli:/workspace/.leetcli" \
  -v "$(pwd)/problems:/workspace/problems" \
  d3kanesa/leetcli'
```

Because solution files are bind-mounted, edit them with your local editor — the built-in `solve` command can't launch one from inside the container.

**From source** — needs CMake, a C++17 compiler, and [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set:

```sh
vcpkg install cpr nlohmann-json ftxui

cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
cmake --install build            # or: --prefix ~/.local
```

## Getting started

```sh
mkdir ~/leetcode && cd ~/leetcode
leetcli init      # create the workspace, pick a default language
leetcli login     # paste your LeetCode session + CSRF token
leetcli           # open the interactive UI
```

`init` makes a `problems/` folder here and saves your settings to `~/.leetcli/config.json`. `login` stores the cookies leetcli needs to run and submit on your behalf — copy `LEETCODE_SESSION` and the CSRF token from your browser's cookies for leetcode.com.

## The interactive UI

Running `leetcli` with no arguments opens a full-screen UI with two tabs:

- **My Problems** — everything you've fetched, with difficulty badges. Filter the list, read the full description (rendered inline, images and all), jump into your solution, and expand the topics and hints for a problem.
- **Browse All** — search and page through the entire LeetCode catalog, and fetch any problem into your workspace.

Works with both keyboard and mouse — click tabs and rows, scroll with the wheel.

## Commands

Prefer plain commands? Everything's scriptable too. Any `<slug>` accepts `daily` for the daily challenge, and every solving command takes an optional `--lang=<language>` for any language LeetCode supports.

```
leetcli                          Open the interactive UI
leetcli init                     Set up the workspace in the current directory
leetcli login                    Store your LeetCode session + CSRF token
leetcli fetch <slug>             Fetch a problem into your workspace
leetcli solve <slug>             Open the solution file in your editor
leetcli run <slug>               Run your solution on LeetCode's test cases
leetcli submit <slug>            Submit your solution to LeetCode
leetcli list                     List everything you've fetched
leetcli topics <slug>            Show the problem's topic tags
leetcli hints <slug>             Show LeetCode's official hints
leetcli runtime <slug>           Estimate time/space complexity (Gemini)
leetcli hint <slug>              Get a hint based on your current code (Gemini)
leetcli config set-gemini-key <key>   Set your Gemini API key
leetcli help                     Show this list
```

## AI features

`runtime` and `hint` use Google Gemini. Set a key once and they light up:

```sh
leetcli config set-gemini-key <your-key>
```

```
$ leetcli runtime two-sum
🧠 Gemini Runtime Analysis:
  Time:  O(n)
  Space: O(n)
```

## How it works

leetcli is written in C++17 and talks to LeetCode over its GraphQL and REST APIs using [cpr](https://github.com/libcpr/cpr) and [nlohmann/json](https://github.com/nlohmann/json). The interactive UI is built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI), and the AI features call the [Gemini API](https://ai.google.dev/). Fetched problems live in `problems/`, one folder each, alongside your global config in `~/.leetcli/config.json`.

## Contributing

Issues and pull requests are welcome. For anything substantial, open an issue first so we can talk it through.

## License

[MIT](LICENSE)
