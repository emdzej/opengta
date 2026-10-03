import { defineConfig } from "vitepress";

// The notes write placeholders like "<city> Chapter <n>" or "Error <code>.<module>" in plain text. Markdown
// passes them through as HTML, which Vue then rejects as unclosed elements. So inline HTML is kept only
// for real elements that are closed in the same paragraph (or void, like <br> and <img>), and an HTML
// block only when its tag is a real element; anything else is shown as text. The notes need no escaping.
const HTML_TAGS = new Set(
  ("a abbr b br code dd del details div dl dt em figcaption figure h1 h2 h3 h4 h5 h6 hr i img kbd li ol p " +
    "picture pre s section small source span strong style sub summary sup table tbody td th thead tr u ul")
    .split(" "),
);
const VOID_TAGS = new Set(["br", "hr", "img", "source"]);
const tagOf = (html: string) => {
  const m = /^<(\/?)([A-Za-z][\w-]*)[^>]*?(\/?)>$/.exec(html.trim());
  return m ? { close: m[1] === "/", name: m[2].toLowerCase(), self: m[3] === "/" } : null;
};
function htmlAsTextUnlessReal(md: any) {
  md.core.ruler.push("html_as_text", (state: any) => {
    for (const block of state.tokens) {
      if (block.type === "html_block") {
        const t = /^<\/?([A-Za-z][\w-]*)/.exec(block.content.trim());
        if (t && !HTML_TAGS.has(t[1].toLowerCase())) {
          block.type = "paragraph_text";
        }
        continue;
      }
      if (block.type !== "inline" || !block.children) continue;
      const kids = block.children;
      const open: { name: string; i: number }[] = [];
      const bad = new Set<number>();
      kids.forEach((k: any, i: number) => {
        if (k.type !== "html_inline") return;
        const t = tagOf(k.content);
        if (!t || !HTML_TAGS.has(t.name)) return bad.add(i);
        if (t.self || VOID_TAGS.has(t.name)) return;
        if (!t.close) return open.push({ name: t.name, i });
        const j = open.map((o) => o.name).lastIndexOf(t.name);
        if (j < 0) return bad.add(i);
        for (const o of open.splice(j).slice(1)) bad.add(o.i);   // opened inside it, never closed
      });
      for (const o of open) bad.add(o.i);
      for (const i of bad) kids[i].type = "text";
    }
  });
  // An HTML block demoted above: its source as an escaped paragraph.
  md.renderer.rules.paragraph_text = (tokens: any[], idx: number) =>
    `<p>${md.utils.escapeHtml(tokens[idx].content.trim())}</p>\n`;
}

export default defineConfig({
  title: "OpenGTA",
  description:
    "A faithful reimplementation of Grand Theft Auto (DMA Design, 1997) in C11 for the gasm runtime, using the data from your own copy of the game.",
  cleanUrls: true,
  lastUpdated: true,
  srcExclude: ["README.md", "AGENTS.md"],
  // The reverse-engineering notes keep their README.md (it is also what GitHub shows in docs/re/).
  rewrites: { "re/README.md": "re/index.md" },
  sitemap: { hostname: "https://opengta.emdzej.pl" },
  // The browser player is a static page in public/play/, not a Markdown page.
  ignoreDeadLinks: [/^\/play\//],
  markdown: { config: htmlAsTextUnlessReal },

  head: [
    ["link", { rel: "icon", href: "/favicon.svg", type: "image/svg+xml" }],
    ["meta", { name: "theme-color", content: "#f08000" }],
    ["meta", { property: "og:title", content: "OpenGTA: Grand Theft Auto, reimplemented" }],
    [
      "meta",
      {
        property: "og:description",
        content:
          "A faithful reimplementation of GTA 1 (1997) in C11, ported from the original executable. Runs on gasm, natively and in the browser, with the data from your own copy.",
      },
    ],
    ["meta", { property: "og:image", content: "https://opengta.emdzej.pl/screenshots/menu.jpg" }],
    ["meta", { property: "og:url", content: "https://opengta.emdzej.pl/" }],
  ],

  themeConfig: {
    siteTitle: "OpenGTA",
    logo: "/favicon.svg",

    nav: [
      { text: "Guide", link: "/guide/", activeMatch: "/guide/" },
      { text: "How-tos", link: "/howto/build-from-source", activeMatch: "/howto/" },
      {
        text: "Internals",
        link: "/internals/",
        activeMatch: "/(internals|formats|render|sprites|text-fonts|frontend|audio|game-core|missions|peds|cars|hud|movie|traffic|objects|trains|police|hires|skins)",
      },
      { text: "Reverse engineering", link: "/re/", activeMatch: "/re/" },
      // A static app in docs/public/play/: target _self makes it a full page load, not a VitePress route.
      { text: "Play", link: "/play/", target: "_self" },
    ],

    sidebar: {
      "/guide/": [
        {
          text: "Guide",
          items: [
            { text: "Introduction", link: "/guide/" },
            { text: "Status", link: "/guide/status" },
            { text: "Installing", link: "/guide/install" },
            { text: "Game data", link: "/guide/game-data" },
            { text: "Running on gasm", link: "/guide/running" },
            { text: "Playing in the browser", link: "/guide/browser" },
            { text: "Launch parameters", link: "/guide/parameters" },
            { text: "Controls", link: "/guide/controls" },
            { text: "Troubleshooting", link: "/guide/troubleshooting" },
          ],
        },
      ],
      "/howto/": [
        {
          text: "How-tos",
          items: [
            { text: "Build from source", link: "/howto/build-from-source" },
            { text: "Make a release", link: "/howto/release" },
            { text: "Run the tests", link: "/howto/tests" },
            { text: "Headless runs and hash checks", link: "/howto/headless" },
            { text: "Create a skin", link: "/howto/create-a-skin" },
            { text: "Reverse-engineering workflow", link: "/howto/reverse-engineering" },
          ],
        },
      ],
      "/re/": [
        {
          text: "Reverse engineering",
          items: [
            { text: "Overview", link: "/re/" },
            { text: "Workflow", link: "/howto/reverse-engineering" },
          ],
        },
        {
          text: "Function inventory",
          items: [
            { text: "1: 0x401000-0x417d90", link: "/re/inventory-1" },
            { text: "2: 0x418390-0x43ce90", link: "/re/inventory-2" },
            { text: "3: 0x43cef0-0x4630e0", link: "/re/inventory-3" },
            { text: "4: 0x463100-0x476500", link: "/re/inventory-4" },
            { text: "5: 0x476550-0x48a310", link: "/re/inventory-5" },
            { text: "6: 0x48a320-0x49cac6", link: "/re/inventory-6" },
          ],
        },
      ],
      "/": [
        {
          text: "Internals",
          items: [
            { text: "Overview", link: "/internals/" },
            { text: "Game core", link: "/game-core" },
            { text: "Frontend", link: "/frontend" },
            { text: "HUD", link: "/hud" },
          ],
        },
        {
          text: "Simulation",
          items: [
            { text: "Peds, player, input", link: "/peds" },
            { text: "Cars", link: "/cars" },
            { text: "Traffic", link: "/traffic" },
            { text: "Objects, explosions, power-ups", link: "/objects" },
            { text: "Trains", link: "/trains" },
            { text: "Police, emergency services", link: "/police" },
            { text: "Missions", link: "/missions" },
          ],
        },
        {
          text: "Graphics",
          items: [
            { text: "City renderer", link: "/render" },
            { text: "Sprites", link: "/sprites" },
            { text: "Text, fonts, images", link: "/text-fonts" },
            { text: "Hires renderer (not original)", link: "/hires" },
            { text: "Skins (not original)", link: "/skins" },
          ],
        },
        {
          text: "Sound",
          items: [
            { text: "Sound and music", link: "/audio" },
            { text: "Intro movie (Smacker)", link: "/movie" },
          ],
        },
        {
          text: "Data",
          items: [
            { text: "Data formats", link: "/formats" },
            { text: "Reverse-engineering notes", link: "/re/" },
          ],
        },
      ],
    },

    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/opengta" }],

    editLink: {
      pattern: "https://github.com/emdzej/opengta/edit/main/docs/:path",
      text: "Edit this page on GitHub",
    },

    outline: { level: [2, 3] },

    search: { provider: "local" },

    footer: {
      message:
        "OpenGTA is released under the GPL-3.0. Grand Theft Auto is © 1997 DMA Design / Rockstar Games. OpenGTA contains no original code or assets and is not affiliated with them: it runs the game from your own copy. It runs on <a href=\"https://gasm.emdzej.pl\">gasm</a>.",
    },
  },
});
