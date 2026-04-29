# Vim Editor — Usage Guide

A terminal-based text editor inspired by Vim, written in C++.

---

## Compiling & Running

```bash
# Compile
g++ -o vim_editor.exe vim_editor.cpp -std=c++17

# Run (empty file)
.\vim_editor.exe

# Run with a file
.\vim_editor.exe myfile.txt
```

---

## ⚡ How to EXIT (Most Important!)

> **Stuck? Press `Esc` a few times, then type `:q!` and press `Enter`.**

| Action | Keys |
|--------|------|
| Quit (no unsaved changes) | `Esc` → `:q` → `Enter` |
| **Force quit** (discard changes) | `Esc` → `:q!` → `Enter` |
| Save and quit | `Esc` → `:wq` → `Enter` |
| Save only | `Esc` → `:w` → `Enter` |

---

## The 4 Modes

| Mode | Purpose | How to Enter | How to Leave |
|------|---------|-------------|-------------|
| 🔵 **NORMAL** | Navigate, delete, copy, paste | Press `Esc` from any mode | — |
| 🟢 **INSERT** | Type / edit text | Press `i`, `a`, `o`, etc. | Press `Esc` |
| 🟠 **COMMAND** | Run commands (save, quit, open) | Press `:` | `Enter` to run, `Esc` to cancel |
| 🟣 **SEARCH** | Find text | Press `/` | `Enter` to search, `Esc` to cancel |

The current mode is always shown at the bottom-left of the screen.

---

## Normal Mode — Movement

| Key | Action |
|-----|--------|
| `h` or `←` | Move left |
| `l` or `→` | Move right |
| `j` or `↓` | Move down one line |
| `k` or `↑` | Move up one line |
| `w` | Jump to next word |
| `b` | Jump to previous word |
| `0` or `Home` | Go to beginning of line |
| `$` or `End` | Go to end of line |
| `gg` | Go to **first line** of file |
| `G` | Go to **last line** of file |
| `Ctrl+D` or `PgDn` | Scroll down one page |
| `Ctrl+U` or `PgUp` | Scroll up one page |

---

## Normal Mode — Entering Insert Mode

| Key | What happens |
|-----|-------------|
| `i` | Start typing **before** the cursor |
| `a` | Start typing **after** the cursor |
| `I` | Start typing at the **beginning** of the line |
| `A` | Start typing at the **end** of the line |
| `o` | Create a **new line below** and start typing |
| `O` | Create a **new line above** and start typing |

Press **`Esc`** to stop typing and return to Normal mode.

---

## Normal Mode — Editing

### Deleting

| Key | Action |
|-----|--------|
| `x` | Delete the character **under** the cursor |
| `X` | Delete the character **before** the cursor |
| `dd` | Delete the **entire current line** |
| `D` | Delete from cursor to **end of line** |

### Copy & Paste

| Key | Action |
|-----|--------|
| `yy` | **Copy (yank)** the current line |
| `p` | **Paste below** the current line |
| `P` | **Paste above** the current line |

### Other

| Key | Action |
|-----|--------|
| `J` | **Join** current line with the next line |
| `u` | **Undo** the last change |
| `Ctrl+R` | **Redo** the last undone change |

---

## Insert Mode Keys

| Key | Action |
|-----|--------|
| Any character | Types that character |
| `Enter` | Creates a new line |
| `Backspace` | Deletes character before cursor |
| `Delete` | Deletes character under cursor |
| `Tab` | Inserts 4 spaces |
| Arrow keys | Move cursor |
| `Home` / `End` | Jump to start / end of line |
| **`Esc`** | **Go back to Normal mode** |

---

## Command Mode (`:`)

Press `:` in Normal mode, type a command, press `Enter`.

| Command | Action |
|---------|--------|
| `:w` | **Save** the file |
| `:w myfile.txt` | **Save as** a new file |
| `:q` | **Quit** (fails if there are unsaved changes) |
| `:q!` | **Force quit** without saving |
| `:wq` or `:x` | **Save and quit** |
| `:e myfile.txt` | **Open** a different file |
| `:find hello` | **Hash lookup** — instantly find a word using hash index |
| `:f hello` | Shorthand for `:find` |
| `:related word`| Find words that often appear near 'word' |
| `:suggest word`| Find similar spellings for 'word' |
| `:wordgraph`   | Show statistics about word relationships |
| `:42` | **Jump** to line 42 |
| `:set nu` | Turn **on** line numbers |
| `:set nonu` | Turn **off** line numbers |

Press `Esc` to cancel without running the command.

---

## Searching

### Pattern Search (`/`) — Rabin-Karp Algorithm

The `/` search uses the **Rabin-Karp rolling hash algorithm** instead of naive
character-by-character comparison. This gives **O(n+m) average time** vs O(n×m) worst case.

**How it works internally:**
1. Compute a hash of the search pattern
2. Slide a window across each line, computing a rolling hash in O(1) per position
3. Only compare characters when hashes match (avoids unnecessary comparisons)

**Usage:**
1. Press **`/`** in Normal mode
2. Type your search text (e.g. `hello`)
3. Press **`Enter`** — jumps to the first match
4. Press **`n`** to go to the **next** match
5. Press **`N`** to go to the **previous** match

The status bar will show `[Rabin-Karp hash]` to confirm hashing is being used.

### Word Lookup (`:find`) — Hash Table Index

The `:find` command uses an **`unordered_map` (hash table)** that indexes every
word in the file by its location. This gives **O(1) average lookup time**.

**How it works internally:**
1. A hash table maps each word → list of `(line, column)` positions
2. The index is built lazily (only when you first use `:find` or after edits)
3. Lookup is instant — no scanning through lines at all

**Usage:**
```
:find hello      Jump to the word "hello" (case-insensitive)
:f hello         Shorthand
```
Repeating `:find hello` cycles through all occurrences.

### Comparison: Linear vs Hashing

| Method | Algorithm | Time Complexity | Used By |
|--------|-----------|----------------|---------|
| Naive linear | `string::find()` | O(n × m) worst | *(not used anymore)* |
| Rolling hash | Rabin-Karp | O(n + m) average | `/` search |
| Hash table | `unordered_map` | O(1) average lookup | `:find` command |

---

## Graph-Based Features

The editor builds a **Word Co-occurrence Graph** behind the scenes. 
* **Nodes:** Every unique word in the document.
* **Edges:** Connections between words that appear on the same line.
* **Weights:** How many times the words co-occur.

This enables advanced text analysis features directly in the editor:

### Word Relationships (`:related`)
Uses **Breadth-First Search (BFS)** on the graph to find words associated with a target word.
```
:related data    Shows words often used in the same sentence as "data"
:rel data        Shorthand
```

### Spell Check / Suggestions (`:suggest`)
Finds similarly spelled words in your document. It uses the graph nodes (known words) and computes the **Levenshtein Edit Distance** to suggest corrections.
```
:suggest teh     Might suggest "the", "ten", etc. based on what's in the file
:sug teh         Shorthand
```

### Graph Statistics (`:wordgraph`)
Shows the overall size and connectivity of the word network in your document.
```
:wordgraph       Shows node/edge count, most connected word (hub), and strongest pair
:wg              Shorthand
```

---

## Understanding the Status Bar

```
 NORMAL   myfile.txt [+]                    Ln 5/20, Col 12
```

| Part | Meaning |
|------|---------|
| `NORMAL` | Current mode (blue = Normal, green = Insert) |
| `myfile.txt` | File you are editing |
| `[+]` | You have **unsaved changes** |
| `Ln 5/20` | Cursor on line 5 out of 20 lines |
| `Col 12` | Cursor at column 12 |

---

## Quick Cheat Sheet

```
┌─────────────────────────────────────────────────┐
│  OPEN FILE      vim_editor.exe myfile.txt       │
│                 :e myfile.txt                    │
│                                                 │
│  START TYPING   i  (then type, then Esc)        │
│                                                 │
│  SAVE           :w   + Enter                    │
│  QUIT           :q   + Enter                    │
│  SAVE & QUIT    :wq  + Enter                    │
│  FORCE QUIT     :q!  + Enter                    │
│                                                 │
│  MOVE           h j k l  (← ↓ ↑ →)             │
│  DELETE LINE    dd                              │
│  COPY LINE      yy                              │
│  PASTE          p                               │
│  UNDO           u                               │
│  REDO           Ctrl+R                          │
│  SEARCH         /text  + Enter  (n/N = next)    │
│  FIND WORD      :find word  (hash table O(1))   │
│  RELATED        :related word   (graph BFS)     │
│  SUGGEST        :suggest word   (edit dist)     │
│                                                 │
│  STUCK?         Esc  then  :q!  Enter           │
└─────────────────────────────────────────────────┘
```

---

## Typical Workflow

1. **Open** → `.\vim_editor.exe myfile.txt`
2. **Navigate** → use `j`/`k` to go to the line you want
3. **Edit** → press `i` to enter Insert mode, type your text
4. **Stop editing** → press `Esc`
5. **Save** → type `:w` and press `Enter`
6. **Quit** → type `:q` and press `Enter`

---

*Vim Editor v1.0 — Terminal Text Editor Project*
