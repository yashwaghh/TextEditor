/*
 * ============================================================
 *  Terminal Text Editor - A Menu-Driven C++ Text Editor
 * ============================================================
 *  Features:
 *    - New file / Load file / Save / Save As
 *    - Line-based editing (insert, delete, replace, append)
 *    - Undo & Redo (snapshot-based)
 *    - Find & Replace
 *    - Go to line
 *    - Word & line count statistics
 *    - Clear file contents
 *    - Runs entirely in the terminal
 * ============================================================
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stack>
#include <sstream>
#include <algorithm>
#include <limits>
#include <cstdlib>

using namespace std;

// ─────────────────────────────────────────────
//  Utility helpers
// ─────────────────────────────────────────────

void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void pauseScreen() {
    cout << "\n  Press Enter to continue...";
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

void printSeparator(char ch = '-', int len = 58) {
    cout << "  ";
    for (int i = 0; i < len; i++) cout << ch;
    cout << endl;
}

// ─────────────────────────────────────────────
//  Snapshot for undo / redo
// ─────────────────────────────────────────────

struct Snapshot {
    vector<string> lines;
    bool modified;
};

// ─────────────────────────────────────────────
//  TextEditor class
// ─────────────────────────────────────────────

class TextEditor {
private:
    vector<string>  lines;        // the text buffer
    string          filename;     // current file path (empty = untitled)
    bool            modified;     // dirty flag
    stack<Snapshot>  undoStack;
    stack<Snapshot>  redoStack;

    // ── Save current state to undo stack ──
    void pushUndo() {
        Snapshot snap;
        snap.lines    = lines;
        snap.modified = modified;
        undoStack.push(snap);
        // Any new edit invalidates the redo history
        while (!redoStack.empty()) redoStack.pop();
    }

public:
    TextEditor() : modified(false) {}

    // ── Getters ──
    bool isModified()       const { return modified; }
    string getFilename()    const { return filename.empty() ? "Untitled" : filename; }
    int lineCount()         const { return (int)lines.size(); }

    // ── Word count ──
    int wordCount() const {
        int count = 0;
        for (const auto& line : lines) {
            istringstream iss(line);
            string word;
            while (iss >> word) count++;
        }
        return count;
    }

    // ── Character count ──
    int charCount() const {
        int count = 0;
        for (const auto& line : lines)
            count += (int)line.size();
        return count;
    }

    // ───────────── NEW FILE ─────────────
    void newFile() {
        if (modified && !lines.empty()) {
            cout << "  Current file has unsaved changes. Discard? (y/n): ";
            char ch; cin >> ch; cin.ignore(numeric_limits<streamsize>::max(), '\n');
            if (ch != 'y' && ch != 'Y') return;
        }
        lines.clear();
        filename.clear();
        modified = false;
        while (!undoStack.empty()) undoStack.pop();
        while (!redoStack.empty()) redoStack.pop();
        cout << "  >> New file created.\n";
    }

    // ───────────── LOAD / OPEN FILE ─────────────
    bool loadFile(const string& path) {
        ifstream fin(path);
        if (!fin.is_open()) {
            cout << "  [Error] Could not open file: " << path << endl;
            return false;
        }
        if (modified && !lines.empty()) {
            cout << "  Current file has unsaved changes. Discard? (y/n): ";
            char ch; cin >> ch; cin.ignore(numeric_limits<streamsize>::max(), '\n');
            if (ch != 'y' && ch != 'Y') { fin.close(); return false; }
        }
        lines.clear();
        while (!undoStack.empty()) undoStack.pop();
        while (!redoStack.empty()) redoStack.pop();

        string line;
        while (getline(fin, line))
            lines.push_back(line);

        fin.close();
        filename = path;
        modified = false;
        cout << "  >> Loaded " << lines.size() << " line(s) from \"" << path << "\"\n";
        return true;
    }

    // ───────────── SAVE FILE ─────────────
    bool saveFile() {
        if (filename.empty()) {
            cout << "  No filename set. Use 'Save As' instead.\n";
            return false;
        }
        ofstream fout(filename);
        if (!fout.is_open()) {
            cout << "  [Error] Could not write to file: " << filename << endl;
            return false;
        }
        for (size_t i = 0; i < lines.size(); i++) {
            fout << lines[i];
            if (i + 1 < lines.size()) fout << "\n";
        }
        fout.close();
        modified = false;
        cout << "  >> File saved to \"" << filename << "\"\n";
        return true;
    }

    // ───────────── SAVE AS ─────────────
    bool saveAs(const string& path) {
        filename = path;
        return saveFile();
    }

    // ───────────── DISPLAY TEXT ─────────────
    void display() const {
        if (lines.empty()) {
            cout << "  (empty file)\n";
            return;
        }
        printSeparator('=');
        int width = to_string(lines.size()).length();
        for (size_t i = 0; i < lines.size(); i++) {
            cout << "  ";
            // right-align line numbers
            string num = to_string(i + 1);
            for (int j = 0; j < width - (int)num.size(); j++) cout << ' ';
            cout << num << " | " << lines[i] << "\n";
        }
        printSeparator('=');
    }

    // ───────────── INSERT LINE ─────────────
    void insertLine(int pos, const string& text) {
        if (pos < 1 || pos > (int)lines.size() + 1) {
            cout << "  [Error] Invalid line number.\n";
            return;
        }
        pushUndo();
        lines.insert(lines.begin() + pos - 1, text);
        modified = true;
        cout << "  >> Inserted at line " << pos << ".\n";
    }

    // ───────────── APPEND LINE ─────────────
    void appendLine(const string& text) {
        pushUndo();
        lines.push_back(text);
        modified = true;
        cout << "  >> Appended as line " << lines.size() << ".\n";
    }

    // ───────────── DELETE LINE ─────────────
    void deleteLine(int pos) {
        if (pos < 1 || pos > (int)lines.size()) {
            cout << "  [Error] Line " << pos << " does not exist.\n";
            return;
        }
        pushUndo();
        cout << "  >> Deleted line " << pos << ": \"" << lines[pos - 1] << "\"\n";
        lines.erase(lines.begin() + pos - 1);
        modified = true;
    }

    // ───────────── REPLACE / EDIT LINE ─────────────
    void replaceLine(int pos, const string& text) {
        if (pos < 1 || pos > (int)lines.size()) {
            cout << "  [Error] Line " << pos << " does not exist.\n";
            return;
        }
        pushUndo();
        lines[pos - 1] = text;
        modified = true;
        cout << "  >> Line " << pos << " updated.\n";
    }

    // ───────────── CLEAR ALL TEXT ─────────────
    void clearAll() {
        if (lines.empty()) {
            cout << "  File is already empty.\n";
            return;
        }
        pushUndo();
        lines.clear();
        modified = true;
        cout << "  >> All text cleared.\n";
    }

    // ───────────── FIND TEXT ─────────────
    void findText(const string& query) const {
        bool found = false;
        for (size_t i = 0; i < lines.size(); i++) {
            size_t pos = 0;
            while ((pos = lines[i].find(query, pos)) != string::npos) {
                if (!found) {
                    cout << "  Matches found:\n";
                    found = true;
                }
                cout << "    Line " << (i + 1) << ", Col " << (pos + 1) << ": " << lines[i] << "\n";
                pos += query.size();
            }
        }
        if (!found)
            cout << "  No matches found for \"" << query << "\".\n";
    }

    // ───────────── FIND & REPLACE ─────────────
    void findAndReplace(const string& query, const string& replacement) {
        int count = 0;
        pushUndo();
        for (auto& line : lines) {
            size_t pos = 0;
            while ((pos = line.find(query, pos)) != string::npos) {
                line.replace(pos, query.size(), replacement);
                pos += replacement.size();
                count++;
            }
        }
        if (count > 0) {
            modified = true;
            cout << "  >> Replaced " << count << " occurrence(s).\n";
        } else {
            // Nothing changed, so pop the undo we just pushed
            undoStack.pop();
            cout << "  No occurrences of \"" << query << "\" found.\n";
        }
    }

    // ───────────── UNDO ─────────────
    void undo() {
        if (undoStack.empty()) {
            cout << "  Nothing to undo.\n";
            return;
        }
        // Save current state to redo
        Snapshot current;
        current.lines    = lines;
        current.modified = modified;
        redoStack.push(current);

        // Restore from undo stack
        Snapshot snap = undoStack.top();
        undoStack.pop();
        lines    = snap.lines;
        modified = snap.modified;
        cout << "  >> Undo successful.\n";
    }

    // ───────────── REDO ─────────────
    void redo() {
        if (redoStack.empty()) {
            cout << "  Nothing to redo.\n";
            return;
        }
        // Save current state to undo
        Snapshot current;
        current.lines    = lines;
        current.modified = modified;
        undoStack.push(current);

        // Restore from redo stack
        Snapshot snap = redoStack.top();
        redoStack.pop();
        lines    = snap.lines;
        modified = snap.modified;
        cout << "  >> Redo successful.\n";
    }

    // ───────────── GO TO LINE ─────────────
    void goToLine(int pos) const {
        if (pos < 1 || pos > (int)lines.size()) {
            cout << "  [Error] Line " << pos << " does not exist (1-" << lines.size() << ").\n";
            return;
        }
        cout << "  Line " << pos << ": " << lines[pos - 1] << "\n";
    }

    // ───────────── STATISTICS ─────────────
    void showStats() const {
        cout << "  File      : " << getFilename() << (modified ? " [modified]" : "") << "\n";
        cout << "  Lines     : " << lineCount() << "\n";
        cout << "  Words     : " << wordCount() << "\n";
        cout << "  Characters: " << charCount() << "\n";
    }
};

// ─────────────────────────────────────────────
//  Menu display
// ─────────────────────────────────────────────

void showMenu(const TextEditor& editor) {
    clearScreen();
    cout << "\n";
    printSeparator('=');
    cout << "       TERMINAL TEXT EDITOR  v1.0\n";
    printSeparator('=');
    cout << "  File: " << editor.getFilename()
         << (editor.isModified() ? "  [modified]" : "") << "\n";
    cout << "  Lines: " << editor.lineCount() << "\n";
    printSeparator('-');
    cout << "   [1]  New File           [10] Find Text\n";
    cout << "   [2]  Open / Load File   [11] Find & Replace\n";
    cout << "   [3]  Save               [12] Go To Line\n";
    cout << "   [4]  Save As            [13] Clear All Text\n";
    cout << "   [5]  View File          [14] Statistics\n";
    cout << "   [6]  Append Line        [15] Undo\n";
    cout << "   [7]  Insert Line        [16] Redo\n";
    cout << "   [8]  Edit / Replace Line\n";
    cout << "   [9]  Delete Line         [0] Exit\n";
    printSeparator('-');
    cout << "  Enter choice: ";
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────

int main(int argc, char* argv[]) {
    TextEditor editor;

    // If a filename was passed as argument, open it
    if (argc >= 2) {
        editor.loadFile(argv[1]);
    }

    int choice = -1;
    while (true) {
        showMenu(editor);

        if (!(cin >> choice)) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        cout << "\n";

        switch (choice) {

        // ── New File ──
        case 1: {
            editor.newFile();
            pauseScreen();
            break;
        }

        // ── Open / Load ──
        case 2: {
            string path;
            cout << "  Enter file path to open: ";
            getline(cin, path);
            editor.loadFile(path);
            pauseScreen();
            break;
        }

        // ── Save ──
        case 3: {
            if (editor.getFilename() == "Untitled") {
                string path;
                cout << "  Enter file name to save: ";
                getline(cin, path);
                editor.saveAs(path);
            } else {
                editor.saveFile();
            }
            pauseScreen();
            break;
        }

        // ── Save As ──
        case 4: {
            string path;
            cout << "  Enter new file name: ";
            getline(cin, path);
            editor.saveAs(path);
            pauseScreen();
            break;
        }

        // ── View File ──
        case 5: {
            editor.display();
            pauseScreen();
            break;
        }

        // ── Append Line ──
        case 6: {
            string text;
            cout << "  Enter text to append: ";
            getline(cin, text);
            editor.appendLine(text);
            pauseScreen();
            break;
        }

        // ── Insert Line ──
        case 7: {
            int pos;
            cout << "  Insert at line number (1-" << editor.lineCount() + 1 << "): ";
            cin >> pos;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            string text;
            cout << "  Enter text: ";
            getline(cin, text);
            editor.insertLine(pos, text);
            pauseScreen();
            break;
        }

        // ── Edit / Replace Line ──
        case 8: {
            if (editor.lineCount() == 0) {
                cout << "  File is empty. Nothing to edit.\n";
            } else {
                int pos;
                cout << "  Enter line number to edit (1-" << editor.lineCount() << "): ";
                cin >> pos;
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                editor.goToLine(pos);
                string text;
                cout << "  Enter new text for this line: ";
                getline(cin, text);
                editor.replaceLine(pos, text);
            }
            pauseScreen();
            break;
        }

        // ── Delete Line ──
        case 9: {
            if (editor.lineCount() == 0) {
                cout << "  File is empty. Nothing to delete.\n";
            } else {
                int pos;
                cout << "  Enter line number to delete (1-" << editor.lineCount() << "): ";
                cin >> pos;
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                editor.deleteLine(pos);
            }
            pauseScreen();
            break;
        }

        // ── Find Text ──
        case 10: {
            string query;
            cout << "  Enter text to find: ";
            getline(cin, query);
            editor.findText(query);
            pauseScreen();
            break;
        }

        // ── Find & Replace ──
        case 11: {
            string query, replacement;
            cout << "  Enter text to find: ";
            getline(cin, query);
            cout << "  Replace with: ";
            getline(cin, replacement);
            editor.findAndReplace(query, replacement);
            pauseScreen();
            break;
        }

        // ── Go To Line ──
        case 12: {
            int pos;
            cout << "  Enter line number: ";
            cin >> pos;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            editor.goToLine(pos);
            pauseScreen();
            break;
        }

        // ── Clear All ──
        case 13: {
            cout << "  Are you sure you want to clear all text? (y/n): ";
            char ch; cin >> ch;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            if (ch == 'y' || ch == 'Y')
                editor.clearAll();
            pauseScreen();
            break;
        }

        // ── Statistics ──
        case 14: {
            editor.showStats();
            pauseScreen();
            break;
        }

        // ── Undo ──
        case 15: {
            editor.undo();
            pauseScreen();
            break;
        }

        // ── Redo ──
        case 16: {
            editor.redo();
            pauseScreen();
            break;
        }

        // ── Exit ──
        case 0: {
            if (editor.isModified()) {
                cout << "  You have unsaved changes. Save before exiting? (y/n): ";
                char ch; cin >> ch;
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                if (ch == 'y' || ch == 'Y') {
                    if (editor.getFilename() == "Untitled") {
                        string path;
                        cout << "  Enter file name to save: ";
                        getline(cin, path);
                        editor.saveAs(path);
                    } else {
                        editor.saveFile();
                    }
                }
            }
            cout << "\n  Goodbye!\n\n";
            return 0;
        }

        default:
            cout << "  Invalid choice. Try again.\n";
            pauseScreen();
            break;
        }
    }

    return 0;
}
