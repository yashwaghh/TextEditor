/*
 * VIM-LIKE TERMINAL TEXT EDITOR v1.0
 * Single-file C++ implementation
 * Supports: modal editing, undo/redo, search, file I/O
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stack>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <queue>
#include <set>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

using namespace std;

// ── Key codes ──
enum {
    K_ESC = 27, K_ENTER = 13, K_BACKSPACE = 8, K_TAB = 9,
    K_CTRL_R = 18, K_CTRL_S = 19, K_CTRL_G = 7,
    K_CTRL_U = 21, K_CTRL_D = 4,
    K_DEL = 1000, K_UP, K_DOWN, K_LEFT, K_RIGHT,
    K_HOME, K_END, K_PGUP, K_PGDN
};

// ── Mode ──
enum Mode { NORMAL, INSERT, COMMAND, SEARCH };

// ── Platform layer ──
#ifdef _WIN32
void setupTerminal() {
    SetConsoleOutputCP(65001);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD m; GetConsoleMode(h, &m);
    SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
void restoreTerminal() {
    printf("\033[?25h\033[0m");
    // Clear screen and reset cursor on exit
    printf("\033[2J\033[H");
}
int readKey() {
    int c = _getch();
    if (c == 0 || c == 224) {
        c = _getch();
        switch(c) {
            case 72: return K_UP;    case 80: return K_DOWN;
            case 75: return K_LEFT;  case 77: return K_RIGHT;
            case 71: return K_HOME;  case 79: return K_END;
            case 73: return K_PGUP;  case 81: return K_PGDN;
            case 83: return K_DEL;   default: return -1;
        }
    }
    return c;
}
void getTermSize(int &rows, int &cols) {
    CONSOLE_SCREEN_BUFFER_INFO ci;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
    cols = ci.srWindow.Right - ci.srWindow.Left + 1;
    rows = ci.srWindow.Bottom - ci.srWindow.Top + 1;
}
#else
static struct termios origTerm;
void setupTerminal() {
    tcgetattr(STDIN_FILENO, &origTerm);
    struct termios raw = origTerm;
    raw.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
    raw.c_cc[VMIN]=1; raw.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
void restoreTerminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTerm);
    printf("\033[?25h\033[0m\033[2J\033[H");
}
int readKey() {
    char c; if(read(STDIN_FILENO,&c,1)!=1) return -1;
    if(c==27){
        char s[3];
        if(read(STDIN_FILENO,&s[0],1)!=1) return K_ESC;
        if(read(STDIN_FILENO,&s[1],1)!=1) return K_ESC;
        if(s[0]=='['){
            if(s[1]>='0'&&s[1]<='9'){
                if(read(STDIN_FILENO,&s[2],1)!=1) return K_ESC;
                if(s[2]=='~') switch(s[1]){
                    case '3':return K_DEL; case '5':return K_PGUP;
                    case '6':return K_PGDN; case '1':case '7':return K_HOME;
                    case '4':case '8':return K_END;
                }
            } else switch(s[1]){
                case 'A':return K_UP; case 'B':return K_DOWN;
                case 'C':return K_RIGHT; case 'D':return K_LEFT;
                case 'H':return K_HOME; case 'F':return K_END;
            }
        }
        return K_ESC;
    }
    if(c==127) return K_BACKSPACE;
    return c;
}
void getTermSize(int &rows, int &cols) {
    struct winsize w;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&w)==-1){rows=24;cols=80;}
    else{rows=w.ws_row;cols=w.ws_col;}
}
#endif

// ── Snapshot for undo/redo ──
struct Snap { vector<string> lines; int cx, cy; };

// ── Editor ──
class VimEditor {
    vector<string> lines;
    string fname;
    bool dirty;
    int cx, cy;          // cursor col, row in file
    int sx, sy;          // scroll offsets
    int rows, cols;      // terminal size
    int trows;           // text area rows (rows - 2)
    Mode mode;
    string cmdBuf, searchQ, msg;
    int searchDir;
    stack<Snap> undoS, redoS;
    vector<string> clip;
    bool clipLine;       // was the yank line-wise?
    int pending;         // for gg, dd, yy
    bool running;
    bool showNums;
    bool indexDirty;     // word index needs rebuild?
    bool graphDirty;     // word graph needs rebuild?

    // ── Hash-based word index: word -> list of (line, col) ──
    unordered_map<string, vector<pair<int,int>>> wordIndex;

    // ── Rabin-Karp constants ──
    static const long long RK_BASE  = 256;
    static const long long RK_MOD   = 1000000007;

    // ── Rabin-Karp: find pattern in text, returns position or -1 ──
    //    Uses rolling hash for O(n+m) average instead of O(n*m)
    int rabinKarpFind(const string &text, const string &pattern, int startPos = 0) {
        int n = (int)text.size(), m = (int)pattern.size();
        if(m == 0 || startPos + m > n) return -1;

        // Compute hash of pattern and first window
        long long patHash = 0, txtHash = 0, h = 1;

        // h = RK_BASE^(m-1) % RK_MOD
        for(int i = 0; i < m - 1; i++)
            h = (h * RK_BASE) % RK_MOD;

        // Initial hashes
        for(int i = 0; i < m; i++) {
            patHash = (RK_BASE * patHash + pattern[i]) % RK_MOD;
            txtHash = (RK_BASE * txtHash + text[startPos + i]) % RK_MOD;
        }

        // Slide the window
        for(int i = startPos; i <= n - m; i++) {
            if(patHash == txtHash) {
                // Hash match — verify character by character
                bool match = true;
                for(int j = 0; j < m; j++) {
                    if(text[i + j] != pattern[j]) { match = false; break; }
                }
                if(match) return i;
            }
            // Compute hash for next window
            if(i < n - m) {
                txtHash = (RK_BASE * (txtHash - text[i] * h) + text[i + m]) % RK_MOD;
                if(txtHash < 0) txtHash += RK_MOD;
            }
        }
        return -1;
    }

    // ── Rabin-Karp: reverse find (last occurrence up to endPos) ──
    int rabinKarpRFind(const string &text, const string &pattern, int endPos) {
        int n = (int)text.size(), m = (int)pattern.size();
        if(m == 0 || m > n) return -1;
        int lastFound = -1;
        int limit = min(endPos, n - m);
        // Run forward search, remember last match up to limit
        int pos = rabinKarpFind(text, pattern, 0);
        while(pos != -1 && pos <= limit) {
            lastFound = pos;
            pos = rabinKarpFind(text, pattern, pos + 1);
        }
        return lastFound;
    }

    // ── Build word hash index from all lines ──
    void rebuildWordIndex() {
        wordIndex.clear();
        for(int i = 0; i < (int)lines.size(); i++) {
            string word;
            int wordStart = 0;
            for(int j = 0; j <= (int)lines[i].size(); j++) {
                if(j < (int)lines[i].size() && (isalnum(lines[i][j]) || lines[i][j] == '_')) {
                    if(word.empty()) wordStart = j;
                    word += lines[i][j];
                } else {
                    if(!word.empty()) {
                        // Store lowercase version for case-insensitive lookup
                        string key = word;
                        for(auto &c : key) c = tolower(c);
                        wordIndex[key].push_back({i, wordStart});
                        word.clear();
                    }
                }
            }
        }
        indexDirty = false;
    }

    // ── Lookup word in hash index — O(1) average ──
    bool hashLookup(const string &word) {
        if(indexDirty) rebuildWordIndex();
        string key = word;
        for(auto &c : key) c = tolower(c);
        auto it = wordIndex.find(key);
        if(it == wordIndex.end() || it->second.empty()) {
            msg = "Word not found: " + word;
            return false;
        }
        const auto &locs = it->second;
        // Find next occurrence after current cursor position
        for(const auto &p : locs) {
            if(p.first > cy || (p.first == cy && p.second > cx)) {
                cy = p.first; cx = p.second;
                clampCursor();
                msg = "Found '" + word + "' (" + to_string(locs.size()) + " total, hash lookup)";
                return true;
            }
        }
        // Wrap around to first occurrence
        cy = locs[0].first; cx = locs[0].second;
        clampCursor();
        msg = "Found '" + word + "' (wrapped, " + to_string(locs.size()) + " total, hash lookup)";
        return true;
    }

    // ════════════════════════════════════════════════
    //  GRAPH DATA STRUCTURE: Word Co-occurrence Graph
    // ════════════════════════════════════════════════
    //  Nodes  = unique words in the document
    //  Edges  = two words appear on the same line
    //  Weight = how many times they co-occur
    //  Stored as adjacency list: word -> { neighbor -> weight }

    unordered_map<string, unordered_map<string, int>> wordGraph;

    // Extract words from a line (lowercase, alphanumeric + underscore)
    vector<string> extractWords(const string &line) {
        vector<string> words;
        string w;
        for(char c : line) {
            if(isalnum(c) || c == '_') {
                w += tolower(c);
            } else {
                if(!w.empty() && w.size() >= 2) words.push_back(w);
                w.clear();
            }
        }
        if(!w.empty() && w.size() >= 2) words.push_back(w);
        return words;
    }

    // Build the co-occurrence graph from all lines
    void rebuildWordGraph() {
        wordGraph.clear();
        for(const auto &line : lines) {
            vector<string> words = extractWords(line);
            // Remove duplicates within same line for cleaner edges
            set<string> unique(words.begin(), words.end());
            vector<string> uwords(unique.begin(), unique.end());
            // Connect every pair of words on this line
            for(int i = 0; i < (int)uwords.size(); i++) {
                for(int j = i + 1; j < (int)uwords.size(); j++) {
                    wordGraph[uwords[i]][uwords[j]]++;
                    wordGraph[uwords[j]][uwords[i]]++;
                }
            }
        }
        graphDirty = false;
    }

    // BFS: find words related to 'start' within 'depth' hops
    string bfsRelated(const string &startWord, int maxDepth = 2) {
        if(graphDirty) rebuildWordGraph();
        string key = startWord;
        for(auto &c : key) c = tolower(c);

        if(wordGraph.find(key) == wordGraph.end())
            return "Word '" + startWord + "' not found in graph";

        // BFS traversal
        unordered_map<string, int> visited;  // word -> depth
        queue<pair<string, int>> q;
        q.push({key, 0});
        visited[key] = 0;

        // Collect results by depth level
        vector<vector<pair<string, int>>> levels(maxDepth + 1);

        while(!q.empty()) {
            auto [word, depth] = q.front(); q.pop();
            if(depth > 0) {
                int weight = 0;
                if(wordGraph[key].count(word)) weight = wordGraph[key][word];
                levels[depth].push_back({word, weight});
            }
            if(depth < maxDepth) {
                // Sort neighbors by weight (strongest first)
                vector<pair<int, string>> neighbors;
                for(auto &[nb, w] : wordGraph[word])
                    neighbors.push_back({w, nb});
                sort(neighbors.rbegin(), neighbors.rend());

                int count = 0;
                for(auto &[w, nb] : neighbors) {
                    if(visited.find(nb) == visited.end() && count < 8) {
                        visited[nb] = depth + 1;
                        q.push({nb, depth + 1});
                        count++;
                    }
                }
            }
        }

        // Format output
        string result = "Related to '" + startWord + "': ";
        bool any = false;
        for(int d = 1; d <= maxDepth; d++) {
            if(levels[d].empty()) continue;
            // Sort by weight descending
            sort(levels[d].begin(), levels[d].end(),
                 [](const pair<string,int>&a, const pair<string,int>&b){ return a.second > b.second; });
            if(any) result += " | ";
            result += "[depth " + to_string(d) + "] ";
            int shown = 0;
            for(auto &[w, wt] : levels[d]) {
                if(shown > 0) result += ", ";
                result += w;
                if(wt > 0) result += "(" + to_string(wt) + ")";
                shown++;
                if(shown >= 6) { result += "..."; break; }
            }
            any = true;
        }
        if(!any) result += "(no connections found)";
        return result;
    }

    // Edit distance (Levenshtein) between two words
    int editDistance(const string &a, const string &b) {
        int m = a.size(), n = b.size();
        vector<vector<int>> dp(m+1, vector<int>(n+1));
        for(int i = 0; i <= m; i++) dp[i][0] = i;
        for(int j = 0; j <= n; j++) dp[0][j] = j;
        for(int i = 1; i <= m; i++)
            for(int j = 1; j <= n; j++) {
                if(a[i-1] == b[j-1]) dp[i][j] = dp[i-1][j-1];
                else dp[i][j] = 1 + min({dp[i-1][j], dp[i][j-1], dp[i-1][j-1]});
            }
        return dp[m][n];
    }

    // Find similar words using graph nodes + edit distance
    string suggestWord(const string &input) {
        if(graphDirty) rebuildWordGraph();
        string key = input;
        for(auto &c : key) c = tolower(c);

        // Collect all words (graph nodes) and find closest by edit distance
        vector<pair<int, string>> candidates;
        for(auto &[word, _] : wordGraph) {
            if(word == key) continue;
            int dist = editDistance(key, word);
            if(dist <= 3) candidates.push_back({dist, word});
        }
        sort(candidates.begin(), candidates.end());

        if(candidates.empty())
            return "No suggestions for '" + input + "'";

        string result = "Suggestions for '" + input + "': ";
        int shown = 0;
        for(auto &[dist, word] : candidates) {
            if(shown > 0) result += ", ";
            result += word + "(dist=" + to_string(dist) + ")";
            shown++;
            if(shown >= 5) break;
        }
        return result;
    }

    // Show graph statistics
    string graphStats() {
        if(graphDirty) rebuildWordGraph();
        int nodes = wordGraph.size();
        int edges = 0;
        string mostConnected;
        int maxEdges = 0;
        int maxWeight = 0;
        string strongestPair;

        for(auto &[word, neighbors] : wordGraph) {
            edges += neighbors.size();
            if((int)neighbors.size() > maxEdges) {
                maxEdges = neighbors.size();
                mostConnected = word;
            }
            for(auto &[nb, w] : neighbors) {
                if(w > maxWeight) {
                    maxWeight = w;
                    strongestPair = word + " <-> " + nb;
                }
            }
        }
        edges /= 2; // undirected

        string result = "Graph: " + to_string(nodes) + " words, "
                      + to_string(edges) + " edges";
        if(!mostConnected.empty())
            result += " | Hub: '" + mostConnected + "'(" + to_string(maxEdges) + " connections)";
        if(!strongestPair.empty())
            result += " | Strongest: " + strongestPair + "(" + to_string(maxWeight) + ")";
        return result;
    }

    void pushUndo() {
        undoS.push({lines, cx, cy});
        while(!redoS.empty()) redoS.pop();
        indexDirty = true;  // edits invalidate word index
        graphDirty = true;  // edits invalidate word graph
    }

    void clampCursor() {
        if(cy < 0) cy = 0;
        if(cy >= (int)lines.size()) cy = (int)lines.size()-1;
        int maxX = (int)lines[cy].size() - (mode==NORMAL ? 1 : 0);
        if(maxX < 0) maxX = 0;
        if(cx > maxX) cx = maxX;
        if(cx < 0) cx = 0;
    }

    void scrollToCursor() {
        if(cy < sy) sy = cy;
        if(cy >= sy + trows) sy = cy - trows + 1;
        int lnw = lineNumW();
        int tw = cols - lnw;
        if(cx < sx) sx = cx;
        if(cx >= sx + tw) sx = cx - tw + 1;
    }

    int lineNumW() {
        if(!showNums) return 0;
        return max(3, (int)to_string(lines.size()).size()) + 2;
    }

    // ── Rendering ──
    void render() {
        getTermSize(rows, cols);
        trows = rows - 2;
        if(trows < 1) trows = 1;
        scrollToCursor();
        int lnw = lineNumW();
        int tw = cols - lnw;

        string buf;
        buf.reserve(rows * cols * 2);
        buf += "\033[?25l\033[H"; // hide cursor, go home

        for(int y = 0; y < trows; y++) {
            int fr = sy + y;
            if(fr < (int)lines.size()) {
                if(showNums) {
                    string n = to_string(fr+1);
                    int pad = lnw - 2 - (int)n.size();
                    buf += "\033[38;5;243m";
                    for(int i=0;i<pad;i++) buf+=' ';
                    buf += n + " \033[38;5;238m|\033[0m";
                }
                const string &line = lines[fr];
                int start = sx;
                int len = (int)line.size() - start;
                if(len < 0) len = 0;
                if(len > tw) len = tw;
                if(start < (int)line.size())
                    buf += line.substr(start, len);
            } else {
                buf += "\033[38;5;241m~\033[0m";
            }
            buf += "\033[K\r\n";
        }

        // Status bar
        string modeStr;
        string modeColor;
        switch(mode) {
            case NORMAL:  modeStr=" NORMAL ";  modeColor="\033[48;5;24m\033[38;5;255m"; break;
            case INSERT:  modeStr=" INSERT ";  modeColor="\033[48;5;22m\033[38;5;255m"; break;
            case COMMAND: modeStr=" COMMAND "; modeColor="\033[48;5;130m\033[38;5;255m"; break;
            case SEARCH:  modeStr=" SEARCH ";  modeColor="\033[48;5;90m\033[38;5;255m"; break;
        }
        string left = modeColor + modeStr + "\033[48;5;236m\033[38;5;252m ";
        left += (fname.empty() ? "[No Name]" : fname);
        if(dirty) left += " [+]";
        left += " ";

        string right = " Ln " + to_string(cy+1) + "/" + to_string(lines.size())
                      + ", Col " + to_string(cx+1) + " ";

        // Calculate visible length (without ANSI codes)
        auto visLen = [](const string &s) {
            int len = 0; bool esc = false;
            for(char c : s) {
                if(c == '\033') esc = true;
                else if(esc && c == 'm') esc = false;
                else if(!esc) len++;
            }
            return len;
        };

        int lv = visLen(left), rv = visLen(right);
        int pad = cols - lv - rv;
        if(pad < 0) pad = 0;

        buf += left;
        for(int i=0;i<pad;i++) buf += ' ';
        buf += right;
        buf += "\033[0m\r\n";

        // Command / message bar
        buf += "\033[K";
        if(mode == COMMAND) {
            buf += ":" + cmdBuf;
        } else if(mode == SEARCH) {
            buf += (searchDir == 1 ? "/" : "?");
            buf += searchQ;
        } else if(!msg.empty()) {
            buf += msg;
        }

        // Position cursor
        int scY = cy - sy + 1;
        int scX = cx - sx + lnw + 1;
        buf += "\033[" + to_string(scY) + ";" + to_string(scX) + "H";
        buf += "\033[?25h"; // show cursor

        cout << buf;
        cout.flush();
    }

    // ── Word movement ──
    void wordForward() {
        const string &l = lines[cy];
        if(cx < (int)l.size()) {
            // skip current word chars
            while(cx < (int)l.size() && !isspace(l[cx])) cx++;
            // skip spaces
            while(cx < (int)l.size() && isspace(l[cx])) cx++;
        }
        if(cx >= (int)l.size() && cy < (int)lines.size()-1) {
            cy++; cx = 0;
            while(cx < (int)lines[cy].size() && isspace(lines[cy][cx])) cx++;
        }
        clampCursor();
    }

    void wordBackward() {
        if(cx == 0 && cy > 0) {
            cy--;
            cx = max(0, (int)lines[cy].size()-1);
        }
        const string &l = lines[cy];
        if(cx > 0) cx--;
        while(cx > 0 && isspace(l[cx])) cx--;
        while(cx > 0 && !isspace(l[cx-1])) cx--;
        clampCursor();
    }

    // ── Search (Rabin-Karp rolling hash) ──
    bool doSearch(int dir) {
        if(searchQ.empty()) return false;
        int startY = cy, startX = cx + dir;
        for(int i = 0; i < (int)lines.size(); i++) {
            int y = (startY + i * dir + (int)lines.size()) % (int)lines.size();
            const string &l = lines[y];
            int pos;
            if(i == 0) {
                if(dir == 1) {
                    if(startX < 0) startX = 0;
                    pos = rabinKarpFind(l, searchQ, startX);
                } else {
                    int sx2 = (startX >= (int)l.size()) ? (int)l.size()-1 : startX;
                    if(sx2 < 0) { continue; }
                    pos = rabinKarpRFind(l, searchQ, sx2);
                }
            } else {
                if(dir == 1) pos = rabinKarpFind(l, searchQ, 0);
                else pos = rabinKarpRFind(l, searchQ, (int)l.size()-1);
            }
            if(pos != -1) {
                cy = y; cx = pos;
                clampCursor();
                msg = "/" + searchQ + "  [Rabin-Karp hash]";
                return true;
            }
        }
        msg = "Pattern not found: " + searchQ;
        return false;
    }

    // ── Normal mode ──
    void processNormal(int k) {
        msg.clear();
        // Movement
        switch(k) {
            case 'h': case K_LEFT:  if(cx>0) cx--; break;
            case 'l': case K_RIGHT: cx++; clampCursor(); break;
            case 'j': case K_DOWN:  if(cy<(int)lines.size()-1) cy++; clampCursor(); break;
            case 'k': case K_UP:    if(cy>0) cy--; clampCursor(); break;
            case '0': case K_HOME:  cx=0; break;
            case '$': case K_END:   cx=max(0,(int)lines[cy].size()-1); break;
            case 'w': wordForward(); break;
            case 'b': wordBackward(); break;
            case K_PGDN: case K_CTRL_D:
                cy = min(cy + trows, (int)lines.size()-1);
                clampCursor(); break;
            case K_PGUP: case K_CTRL_U:
                cy = max(cy - trows, 0);
                clampCursor(); break;

            // ── gg / G ──
            case 'g':
                if(pending == 'g') { cy=0; cx=0; pending=0; }
                else pending = 'g';
                return; // don't reset pending below

            case 'G':
                cy = (int)lines.size()-1; cx=0; clampCursor(); break;

            // ── Enter insert mode ──
            case 'i': mode=INSERT; break;
            case 'a':
                if(!lines[cy].empty()) cx++;
                mode=INSERT; break;
            case 'I': cx=0; mode=INSERT; break;
            case 'A': cx=(int)lines[cy].size(); mode=INSERT; break;
            case 'o':
                pushUndo();
                lines.insert(lines.begin()+cy+1, "");
                cy++; cx=0; dirty=true; mode=INSERT; break;
            case 'O':
                pushUndo();
                lines.insert(lines.begin()+cy, "");
                cx=0; dirty=true; mode=INSERT; break;

            // ── Editing ──
            case 'x': // delete char
                if(!lines[cy].empty() && cx < (int)lines[cy].size()) {
                    pushUndo();
                    clip = { string(1, lines[cy][cx]) };
                    clipLine = false;
                    lines[cy].erase(cx, 1);
                    dirty = true;
                    clampCursor();
                }
                break;
            case 'X': // delete char before
                if(cx > 0) {
                    pushUndo();
                    cx--;
                    lines[cy].erase(cx, 1);
                    dirty = true;
                    clampCursor();
                }
                break;

            case 'd':
                if(pending == 'd') { // dd - delete line
                    pushUndo();
                    clip = { lines[cy] };
                    clipLine = true;
                    lines.erase(lines.begin()+cy);
                    if(lines.empty()) lines.push_back("");
                    dirty = true;
                    clampCursor();
                    pending = 0;
                    msg = "1 line deleted";
                } else { pending = 'd'; return; }
                break;

            case 'D': // delete to end of line
                if(cx < (int)lines[cy].size()) {
                    pushUndo();
                    clip = { lines[cy].substr(cx) };
                    clipLine = false;
                    lines[cy].erase(cx);
                    dirty = true;
                    clampCursor();
                }
                break;

            case 'J': // join lines
                if(cy < (int)lines.size()-1) {
                    pushUndo();
                    if(!lines[cy].empty() && !lines[cy+1].empty())
                        lines[cy] += " ";
                    lines[cy] += lines[cy+1];
                    lines.erase(lines.begin()+cy+1);
                    dirty = true;
                }
                break;

            // ── Yank / Paste ──
            case 'y':
                if(pending == 'y') { // yy - yank line
                    clip = { lines[cy] };
                    clipLine = true;
                    msg = "1 line yanked";
                    pending = 0;
                } else { pending = 'y'; return; }
                break;

            case 'p': // paste after
                if(!clip.empty()) {
                    pushUndo();
                    if(clipLine) {
                        lines.insert(lines.begin()+cy+1, clip.begin(), clip.end());
                        cy++;
                        cx = 0;
                    } else {
                        int pos = min(cx+1, (int)lines[cy].size());
                        lines[cy].insert(pos, clip[0]);
                        cx = pos + (int)clip[0].size() - 1;
                    }
                    dirty = true;
                    clampCursor();
                }
                break;

            case 'P': // paste before
                if(!clip.empty()) {
                    pushUndo();
                    if(clipLine) {
                        lines.insert(lines.begin()+cy, clip.begin(), clip.end());
                        cx = 0;
                    } else {
                        lines[cy].insert(cx, clip[0]);
                    }
                    dirty = true;
                    clampCursor();
                }
                break;

            // ── Undo / Redo ──
            case 'u':
                if(!undoS.empty()) {
                    redoS.push({lines, cx, cy});
                    auto s = undoS.top(); undoS.pop();
                    lines=s.lines; cx=s.cx; cy=s.cy;
                    dirty=true; clampCursor();
                    msg = "Undo";
                } else msg = "Already at oldest change";
                break;

            case K_CTRL_R:
                if(!redoS.empty()) {
                    undoS.push({lines, cx, cy});
                    auto s = redoS.top(); redoS.pop();
                    lines=s.lines; cx=s.cx; cy=s.cy;
                    dirty=true; clampCursor();
                    msg = "Redo";
                } else msg = "Already at newest change";
                break;

            // ── Command / Search mode ──
            case ':': mode=COMMAND; cmdBuf.clear(); break;
            case '/': mode=SEARCH; searchQ.clear(); searchDir=1; break;
            case 'n': doSearch(searchDir); break;
            case 'N': doSearch(-searchDir); break;

            // ── Info ──
            case K_CTRL_G:
                msg = "\"" + (fname.empty()?"[No Name]":fname) + "\" "
                    + to_string(lines.size()) + " lines"
                    + (dirty ? " [Modified]" : "");
                break;

            default: break;
        }
        pending = 0;
    }

    // ── Insert mode ──
    void processInsert(int k) {
        switch(k) {
            case K_ESC:
                mode = NORMAL;
                if(cx > 0) cx--;
                clampCursor();
                break;
            case K_ENTER: {
                pushUndo();
                string tail = lines[cy].substr(cx);
                lines[cy].erase(cx);
                lines.insert(lines.begin()+cy+1, tail);
                cy++; cx=0; dirty=true;
                break;
            }
            case K_BACKSPACE: case 127:
                if(cx > 0) {
                    pushUndo();
                    cx--;
                    lines[cy].erase(cx, 1);
                    dirty=true;
                } else if(cy > 0) {
                    pushUndo();
                    cx = (int)lines[cy-1].size();
                    lines[cy-1] += lines[cy];
                    lines.erase(lines.begin()+cy);
                    cy--; dirty=true;
                }
                break;
            case K_DEL:
                if(cx < (int)lines[cy].size()) {
                    pushUndo();
                    lines[cy].erase(cx, 1);
                    dirty=true;
                } else if(cy < (int)lines.size()-1) {
                    pushUndo();
                    lines[cy] += lines[cy+1];
                    lines.erase(lines.begin()+cy+1);
                    dirty=true;
                }
                break;
            case K_TAB:
                pushUndo();
                lines[cy].insert(cx, "    ");
                cx += 4; dirty=true;
                break;
            case K_LEFT:  if(cx>0) cx--; break;
            case K_RIGHT: if(cx<(int)lines[cy].size()) cx++; break;
            case K_UP:    if(cy>0) cy--; clampCursor(); break;
            case K_DOWN:  if(cy<(int)lines.size()-1) cy++; clampCursor(); break;
            case K_HOME:  cx=0; break;
            case K_END:   cx=(int)lines[cy].size(); break;
            case K_PGUP:  cy=max(cy-trows,0); clampCursor(); break;
            case K_PGDN:  cy=min(cy+trows,(int)lines.size()-1); clampCursor(); break;
            default:
                if(k >= 32 && k < 127) {
                    pushUndo();
                    lines[cy].insert(cx, 1, (char)k);
                    cx++; dirty=true;
                }
                break;
        }
    }

    // ── Command mode ──
    void processCommand(int k) {
        if(k == K_ESC) { mode=NORMAL; msg.clear(); return; }
        if(k == K_ENTER) {
            executeCommand(cmdBuf);
            mode = NORMAL;
            return;
        }
        if(k == K_BACKSPACE || k == 127) {
            if(!cmdBuf.empty()) cmdBuf.pop_back();
            else mode = NORMAL;
            return;
        }
        if(k >= 32 && k < 127) cmdBuf += (char)k;
    }

    void executeCommand(const string &cmd) {
        msg.clear();
        // Trim
        string c = cmd;
        while(!c.empty() && c[0]==' ') c.erase(0,1);
        while(!c.empty() && c.back()==' ') c.pop_back();

        if(c == "q") {
            if(dirty) { msg = "No write since last change (add ! to override)"; return; }
            running = false;
        } else if(c == "q!") {
            running = false;
        } else if(c == "w") {
            saveFile();
        } else if(c.substr(0,2) == "w ") {
            fname = c.substr(2);
            while(!fname.empty() && fname[0]==' ') fname.erase(0,1);
            saveFile();
        } else if(c == "wq" || c == "x") {
            if(saveFile()) running = false;
        } else if(c.substr(0,2) == "e ") {
            string f = c.substr(2);
            while(!f.empty() && f[0]==' ') f.erase(0,1);
            openFile(f);
        } else if(c == "set nu" || c == "set number") {
            showNums = true;
        } else if(c == "set nonu" || c == "set nonumber") {
            showNums = false;
        } else if(c.substr(0, 5) == "find " || c.substr(0, 2) == "f ") {
            // ── Hash-based word lookup: :find <word> ──
            string word = (c[0] == 'f' && c[1] == ' ') ? c.substr(2) : c.substr(5);
            while(!word.empty() && word[0]==' ') word.erase(0,1);
            while(!word.empty() && word.back()==' ') word.pop_back();
            if(!word.empty()) hashLookup(word);
            else msg = "Usage: :find <word>";
        } else if(c.substr(0, 8) == "related " || c.substr(0, 4) == "rel ") {
            // ── Graph BFS: find related words ──
            string word = (c.substr(0, 4) == "rel ") ? c.substr(4) : c.substr(8);
            while(!word.empty() && word[0]==' ') word.erase(0,1);
            while(!word.empty() && word.back()==' ') word.pop_back();
            if(!word.empty()) msg = bfsRelated(word);
            else msg = "Usage: :related <word>";
        } else if(c.substr(0, 8) == "suggest " || c.substr(0, 4) == "sug ") {
            // ── Graph: spell suggestions via edit distance ──
            string word = (c.substr(0, 4) == "sug ") ? c.substr(4) : c.substr(8);
            while(!word.empty() && word[0]==' ') word.erase(0,1);
            while(!word.empty() && word.back()==' ') word.pop_back();
            if(!word.empty()) msg = suggestWord(word);
            else msg = "Usage: :suggest <word>";
        } else if(c == "wordgraph" || c == "wg") {
            // ── Graph stats ──
            msg = graphStats();
        } else {
            // Try as line number
            bool isNum = !c.empty();
            for(char ch : c) if(!isdigit(ch)) isNum = false;
            if(isNum) {
                int ln = stoi(c);
                cy = max(0, min(ln-1, (int)lines.size()-1));
                cx = 0; clampCursor();
            } else {
                msg = "Not an editor command: " + c;
            }
        }
    }

    // ── Search mode ──
    void processSearch(int k) {
        if(k == K_ESC) { mode=NORMAL; msg.clear(); return; }
        if(k == K_ENTER) {
            mode = NORMAL;
            doSearch(searchDir);
            return;
        }
        if(k == K_BACKSPACE || k == 127) {
            if(!searchQ.empty()) searchQ.pop_back();
            else mode = NORMAL;
            return;
        }
        if(k >= 32 && k < 127) searchQ += (char)k;
    }

    // ── File I/O ──
    void openFile(const string &path) {
        ifstream f(path);
        if(!f.is_open()) {
            // New file
            lines.clear();
            lines.push_back("");
            fname = path;
            dirty = false;
            cx = cy = sx = sy = 0;
            while(!undoS.empty()) undoS.pop();
            while(!redoS.empty()) redoS.pop();
            msg = "\"" + path + "\" [New File]";
            return;
        }
        lines.clear();
        string line;
        while(getline(f, line)) {
            // Remove trailing \r
            if(!line.empty() && line.back()=='\r') line.pop_back();
            lines.push_back(line);
        }
        f.close();
        if(lines.empty()) lines.push_back("");
        fname = path;
        dirty = false;
        cx = cy = sx = sy = 0;
        while(!undoS.empty()) undoS.pop();
        while(!redoS.empty()) redoS.pop();
        msg = "\"" + path + "\" " + to_string(lines.size()) + "L";
    }

    bool saveFile() {
        if(fname.empty()) {
            msg = "No file name - use :w filename";
            return false;
        }
        ofstream f(fname);
        if(!f.is_open()) {
            msg = "Error: Cannot write to \"" + fname + "\"";
            return false;
        }
        for(int i=0;i<(int)lines.size();i++) {
            f << lines[i];
            if(i+1<(int)lines.size()) f << "\n";
        }
        f.close();
        dirty = false;
        msg = "\"" + fname + "\" " + to_string(lines.size()) + "L written";
        return true;
    }

public:
    VimEditor() : dirty(false), cx(0), cy(0), sx(0), sy(0),
                  rows(24), cols(80), trows(22), mode(NORMAL),
                  searchDir(1), clipLine(false), pending(0),
                  running(true), showNums(true), indexDirty(true), graphDirty(true) {
        lines.push_back("");
    }

    void run(const string &file = "") {
        setupTerminal();
        if(!file.empty()) openFile(file);
        else msg = "VIM Editor -- type :q to quit, i to insert, :e <file> to open";

        while(running) {
            render();
            int k = readKey();
            if(k == -1) continue;

            switch(mode) {
                case NORMAL:  processNormal(k); break;
                case INSERT:  processInsert(k); break;
                case COMMAND: processCommand(k); break;
                case SEARCH:  processSearch(k); break;
            }
        }
        restoreTerminal();
    }
};

int main(int argc, char *argv[]) {
    VimEditor editor;
    editor.run(argc >= 2 ? argv[1] : "");
    return 0;
}
