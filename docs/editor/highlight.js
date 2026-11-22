// highlight.js: colors foo source code. works on any element, no editor needed.
//
//   import { highlight } from "./highlight.js";
//   highlight(document.querySelector("code"), source);
//
// every token becomes <span class="foo-t-...">, see editor.css for the colors.

const KEYWORDS  = new Set(["fn", "struct", "enum", "import", "var", "if", "else", "while", "for", "in", "check", "return", "ref", "priv", "continue", "break", "as"]);
const CONSTANTS = new Set(["true", "false", "none", "self"]);
const TYPES     = new Set(["int", "float", "bool", "string", "any", "void"]);

// one pass over the text; anything that doesn't match stays plain
const TOKEN_SRC = [
    String.raw`(?<comment>\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$))`,
    String.raw`(?<string>"(?:[^"\\\n]|\\.)*(?:"|(?=\n)|$))`,                  // also colors unterminated strings
    String.raw`(?<number>\d+(?:\.\d+)?)`,
    String.raw`(?<lit>[#@][A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\[\]|\?)*)`,       // #Point{...}  #lib::Point{...}  @int.f()
    String.raw`(?<word>[A-Za-z_]\w*)`,
    String.raw`(?<op>=>|->|\.\.=|\.\.|::)`,
    String.raw`(?<fq>')`,                                                      // start of a format string: 'x is {x}'
    String.raw`(?<punct>[^\s\w"])`,                                            // any other single character
].join("|");

// what follows a word: "(" makes it a call, "::" makes it a namespace, ":" makes it a label,
// "name =" after `type` makes it an alias declaration
const CALL_AHEAD  = /[ \t]*\(/y;
const PATH_AHEAD  = /[ \t]*::/y;
const LABEL_AHEAD = /[ \t]*:(?!:)/y;
const ALIAS_AHEAD = /[ \t]+[A-Za-z_]\w*[ \t]*=(?![=>])/y;
const ahead = (re, text, pos) => { re.lastIndex = pos; return re.test(text); };

const span = (cls, text) => {
    const el = document.createElement("span");
    el.className = cls;
    el.textContent = text;
    return el;
};

// the index of the "}" that closes the "{" at `open`, or -1.
// strings inside the interpolation may contain braces, and it never continues on the next line
function interpolationEnd(text, open) {
    let depth = 1;

    for (let j = open + 1; j < text.length && text[j] !== "\n"; j++) {
        const c = text[j];

        if (c === '"') {
            j++;
            while (j < text.length && text[j] !== '"' && text[j] !== "\n") j += text[j] === "\\" ? 2 : 1;
            if (text[j] !== '"') return -1;
        }
        else if (c === "{") depth++;
        else if (c === "}" && --depth === 0) return j;
    }

    return -1;
}

// 'text {expr} text': the text is a string, what is inside the braces is colored as code.
// appends to `frag`, returns the index after the closing quote (or the end of the line when it is unterminated)
function formatString(text, start, frag) {
    let i = start + 1;
    let seg = start;                                     // where the pending string text begins

    const flush = (to) => { if (to > seg) frag.append(span("foo-t-string", text.slice(seg, to))); };

    while (i < text.length && text[i] !== "\n") {
        const c = text[i];

        if (c === "'") {
            i++;
            flush(i);
            return i;
        }

        if (c === "\\") {                                // \n  \'  \{  \x41 ...
            i += (i + 1 >= text.length || text[i + 1] === "\n") ? 1 : 2;
            continue;
        }

        if (c === "{") {
            const close = interpolationEnd(text, i);

            if (close >= 0) {
                flush(i);
                frag.append("{");
                build(text.slice(i + 1, close), frag);
                frag.append("}");

                i = close + 1;
                seg = i;
                continue;
            }
        }

        i++;
    }

    flush(i);
    return i;
}

// colors `text` and appends it to `frag`
function build(text, frag) {
    const re = new RegExp(TOKEN_SRC, "gu");              // one per call, interpolations are colored recursively
    let pos = 0;
    let prev = "";                                       // last token that wasn't whitespace or a comment

    // type annotations: from ":" "->" "as" or "@" until the type expression ends
    let inType = false;
    let depth = 0;                                       // [ ] and ( ) nesting: int[]  [string, int]  (int | string)  fn(int) -> int
    let complete = false;                                // a whole type name was just seen

    // `type Name = ...`: 1 expects the name, 2 expects the "="
    let aliasStage = 0;

    const startType = () => { inType = true; depth = 0; complete = false; };

    const emit = (token, cls) => {
        frag.append(cls ? span(cls, token) : token);
    };

    let m;
    while ((m = re.exec(text)) !== null) {
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

        if (g.fq !== undefined) {
            pos = formatString(text, m.index, frag);
            re.lastIndex = pos;
            inType = false;
            prev = "'";
            continue;
        }

        let cls = "";

        if (g.string !== undefined)      { cls = "foo-t-string"; inType = false; }
        else if (g.number !== undefined) { cls = "foo-t-number"; inType = false; }
        else if (g.lit !== undefined)    { cls = "foo-t-lit";    inType = false; }
        else if (g.word !== undefined) {
            const call = ahead(CALL_AHEAD, text, pos);
            const path = ahead(PATH_AHEAD, text, pos);

            // `fn` inside a type starts a function type: fn(int, string) -> bool
            const fnType = inType && !complete && token === "fn";

            // a second word after a complete type, or a keyword, ends the annotation
            if (inType && !fnType && (complete || KEYWORDS.has(token))) inType = false;

            if (aliasStage === 1) {                      // type Name = ...
                cls = "foo-t-type";
                aliasStage = 2;
            }
            else if (fnType) cls = "foo-t-kw";
            else if (inType) {
                if (depth > 0 && ahead(LABEL_AHEAD, text, pos)) {
                    cls = "";                            // (name: string, age: int), the name is not a type
                } else {
                    cls = path ? "foo-t-ns" : "foo-t-type";
                    complete = true;
                }
            }
            else if (prev === ".")         cls = call ? "foo-t-fn" : "";    // a member is never a keyword or type
            else if (token === "type" && ahead(ALIAS_AHEAD, text, pos)) {
                cls = "foo-t-kw";
                aliasStage = 1;
            }
            else if (KEYWORDS.has(token))  {
                cls = "foo-t-kw";
                if (token === "as") startType();         // x as int
            }
            else if (CONSTANTS.has(token)) cls = "foo-t-const";
            else if (path)                 cls = "foo-t-ns";
            else if (call)                 cls = "foo-t-fn";                // int(x) is a call, not the type
            else if (prev === "struct" || prev === "enum" || TYPES.has(token)) cls = "foo-t-type";
        }
        else if (token === ":" || token === "->") {
            if (inType && depth > 0)                      complete = false;  // a label in (name: T), or the return type of a nested fn type
            else if (inType && token === "->" && complete) complete = false;  // fn(int) -> int keeps going
            else                                          startType();
        }
        else if (token === "@")                           startType();       // @[int].from(1, 2), @(int | string).f()
        else if (token === "=" && aliasStage === 2) {
            aliasStage = 0;
            startType();
        }
        else if (inType) {
            if (token === "::")                           complete = false;
            else if (token === "(") {
                if (complete && depth === 0) inType = false;                 // not part of the type
                else { depth++; complete = false; }
            }
            else if (token === ")")                       { depth--; complete = true; if (depth < 0) inType = false; }
            else if (token === "[")                       { depth++; complete = false; }
            else if (token === "]")                       { depth--; complete = true; if (depth < 0) inType = false; }
            else if (token === "?")                       complete = true;
            else if ((token === "," || token === "|") && depth > 0) complete = false;
            else                                          inType = false;
        }

        emit(token, cls);
        prev = token;
    }

    const rest = text.slice(pos);
    if (rest) frag.append(rest);
}

export function highlight(target, text, { trailingNewline = false } = {}) {
    const frag = document.createDocumentFragment();

    build(text, frag);

    if (trailingNewline) frag.append("\n");
    target.replaceChildren(frag);
}