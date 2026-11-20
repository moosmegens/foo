// highlight.js: colors foo source code. works on any element, no editor needed.
//
//   import { highlight } from "./highlight.js";
//   highlight(document.querySelector("code"), source);
//
// every token becomes <span class="foo-t-...">, see editor.css for the colors.

const KEYWORDS  = new Set(["fn", "struct", "import", "var", "if", "else", "while", "for", "in", "check", "return", "ref", "continue", "break", "as"]);
const CONSTANTS = new Set(["true", "false", "none", "self"]);
const TYPES     = new Set(["int", "float", "bool", "string", "any", "void"]);

// one pass over the text; anything that doesn't match stays plain
const TOKEN = new RegExp([
    String.raw`(?<comment>\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$))`,
    String.raw`(?<string>"(?:[^"\\\n]|\\.)*(?:"|(?=\n)|$))`,                  // also colors unterminated strings
    String.raw`(?<number>\d+(?:\.\d+)?)`,
    String.raw`(?<lit>[#@][A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\[\]|\?)*)`,       // #Point{...}  #lib::Point{...}  @int.f()
    String.raw`(?<word>[A-Za-z_]\w*)`,
    String.raw`(?<op>=>|->|\.\.=|\.\.|::)`,
    String.raw`(?<punct>[^\s\w"])`,                                            // any other single character
].join("|"), "gu");

// what follows a word: "(" makes it a call, "::" makes it a namespace
const CALL_AHEAD = /[ \t]*\(/y;
const PATH_AHEAD = /[ \t]*::/y;
const ahead = (re, text, pos) => { re.lastIndex = pos; return re.test(text); };

export function highlight(target, text, { trailingNewline = false } = {}) {
    const frag = document.createDocumentFragment();
    let pos = 0;
    let prev = "";                 // last token that wasn't whitespace or a comment

    // type annotations: from ":" or "->" until the type expression ends
    let inType = false;
    let depth = 0;                 // [ ] nesting, for int[] and [string, int]
    let complete = false;          // a whole type name was just seen

    const emit = (token, cls) => {
        if (!cls) { frag.append(token); return; }
        const span = document.createElement("span");
        span.className = cls;
        span.textContent = token;
        frag.append(span);
    };

    for (const m of text.matchAll(TOKEN)) {
        const gap = text.slice(pos, m.index);
        if (gap) {
            frag.append(gap);
            if (gap.includes("\n")) inType = false;      // a type never continues on the next line
        }

        const token = m[0];
        const g = m.groups;
        pos = m.index + token.length;

        if (g.comment !== undefined) {
            emit(token, "foo-t-comment");
            continue;                                    // comments don't change any state
        }

        let cls = "";

        if (g.string !== undefined)      { cls = "foo-t-string"; inType = false; }
        else if (g.number !== undefined) { cls = "foo-t-number"; inType = false; }
        else if (g.lit !== undefined)    { cls = "foo-t-lit";    inType = false; }
        else if (g.word !== undefined) {
            const call = ahead(CALL_AHEAD, text, pos);
            const path = ahead(PATH_AHEAD, text, pos);

            // a second word after a complete type, or a keyword, ends the annotation
            if (inType && (complete || KEYWORDS.has(token))) inType = false;

            if (inType) {
                cls = path ? "foo-t-ns" : "foo-t-type";
                complete = true;
            }
            else if (prev === ".")         cls = call ? "foo-t-fn" : "";    // a member is never a keyword or type
            else if (KEYWORDS.has(token))  cls = "foo-t-kw";
            else if (CONSTANTS.has(token)) cls = "foo-t-const";
            else if (path)                 cls = "foo-t-ns";
            else if (call)                 cls = "foo-t-fn";                // int(x) is a call, not the type
            else if (prev === "struct" || TYPES.has(token)) cls = "foo-t-type";
        }
        else if (token === ":" || token === "->") {
            inType = true; depth = 0; complete = false;
        }
        else if (inType) {
            if (token === "::")                   complete = false;
            else if (token === "[")               { depth++; complete = false; }
            else if (token === "]")               { depth--; complete = true; if (depth < 0) inType = false; }
            else if (token === "?")               complete = true;
            else if (token === "," && depth > 0)  complete = false;
            else                                  inType = false;
        }

        emit(token, cls);
        prev = token;
    }

    const rest = text.slice(pos);
    if (rest) frag.append(rest);
    if (trailingNewline) frag.append("\n");
    target.replaceChildren(frag);
}