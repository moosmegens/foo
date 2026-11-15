import { mountEmbed } from "./embed.js";

const SELECTOR = "[data-foo-editor]";

window.fooEditors = new Map();

function scan(root) {
    if (root.matches?.(SELECTOR))
        mount(root);

    root.querySelectorAll?.(SELECTOR).forEach(mount);
}

function mount(element) {
    if (element.dataset.fooMounted !== undefined) return;

    const editor = mountEmbed(element);

    if (editor) {
        const id = element.id || crypto.randomUUID();
        window.fooEditors.set(id, editor);
    }
}

scan(document);

new MutationObserver(mutations => {
    for (const mutation of mutations)
        for (const node of mutation.addedNodes)
            if (node.nodeType === 1)
                scan(node);

}).observe(document.documentElement, {
    childList: true,
    subtree: true
});