// loads every <div data-foo-editor> on the page, including ones the site adds later
// (e.g. when a project page is rendered). just include it with:
//   <script type="module" src="foo/editor/auto.js"></script>

import { mountEmbed } from "./embed.js";

const SELECTOR = "[data-foo-editor]";

function scan(root) {
    if (root.matches?.(SELECTOR)) mountEmbed(root);
    root.querySelectorAll?.(SELECTOR).forEach(mountEmbed);
}

scan(document);

new MutationObserver(mutations => {
    for (const mutation of mutations)
        for (const node of mutation.addedNodes)
            if (node.nodeType === 1) scan(node);
}).observe(document.documentElement, { childList: true, subtree: true });