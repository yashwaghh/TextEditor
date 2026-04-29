# Vim-Inspired Terminal Text Editor

A lightweight, feature-rich, terminal-based text editor built in C++ that emulates the core mechanics of Vim. It features multiple modes (Normal, Insert, Command, Search), familiar keybindings, and advanced under-the-hood algorithms for text searching and analysis.

## Features

* **Vim Modes**: Seamlessly switch between Normal, Insert, Command, and Search modes.
* **Classic Keybindings**: Use standard Vim movement (`h`, `j`, `k`, `l`) and editing commands (`dd`, `yy`, `p`, `u`, `Ctrl+R`).
* **Fast Searching**: Utilizes the **Rabin-Karp rolling hash algorithm** for efficient O(n+m) average time pattern searching.
* **Instant Word Lookup**: Employs an **unordered_map (hash table)** index for O(1) average lookup time when finding words.
* **Graph-Based Text Analysis**: Builds a Word Co-occurrence Graph to provide advanced features:
  * Word relationship analysis (Breadth-First Search)
  * Spell checking and suggestions (Levenshtein Edit Distance)
  * Graph statistics

## Getting Started

### Prerequisites

You will need a C++ compiler that supports C++17 (e.g., `g++`).

### Compilation

```bash
g++ -o vim_editor.exe vim_editor.cpp -std=c++17
```

### Running the Editor

```bash
# Open with an empty buffer
.\vim_editor.exe

# Open an existing file
.\vim_editor.exe myfile.txt
```

## Documentation

For a comprehensive list of commands, keybindings, and detailed explanations of the internal algorithms used, please read the [Usage Guide](USAGE_GUIDE.md).

## How to Exit (If you're stuck!)

Press `Esc` a few times, type `:q!`, and press `Enter`.
