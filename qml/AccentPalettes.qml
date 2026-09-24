pragma Singleton
import QtQuick

// Accent colours from three palettes, used as they are rather than as
// Material seeds: each one sets the accent roles only, and the default
// surfaces stay. The light theme takes each palette's light variant and the
// dark theme its dark one. Theme.qml moves a colour only as far as it has to
// for 4.5:1 against every surface.
//
// The values are copied from each project's own repository:
//   Catppuccin: catppuccin/palette palette.json v1.8.0, Latte and Mocha accents
//     (commit 07d02aa110ef9eb7e7427afca5c73ba9cf7f8ebd). MIT.
//   Gruvbox: morhetz/gruvbox colors/gruvbox.vim, faded_* for a light
//     background and bright_* for a dark one, as that file assigns them
//     (commit ef8864bb42bf244f0295d1c5a403b27e3d139695). MIT/X11, per its README.
//   Rosé Pine: rose-pine/palette palette.json, Dawn and main
//     (commit 92af52b465ab6e47437aca223c9b8d3009a2023b). MIT.
QtObject {
    readonly property var palettes: [
        {key: "catppuccin", name: "Catppuccin", accents: [
            {key:"rosewater", name:"Rosewater", light:"#dc8a78", dark:"#f5e0dc"},
            {key:"flamingo", name:"Flamingo", light:"#dd7878", dark:"#f2cdcd"},
            {key:"pink", name:"Pink", light:"#ea76cb", dark:"#f5c2e7"},
            {key:"mauve", name:"Mauve", light:"#8839ef", dark:"#cba6f7"},
            {key:"red", name:"Red", light:"#d20f39", dark:"#f38ba8"},
            {key:"maroon", name:"Maroon", light:"#e64553", dark:"#eba0ac"},
            {key:"peach", name:"Peach", light:"#fe640b", dark:"#fab387"},
            {key:"yellow", name:"Yellow", light:"#df8e1d", dark:"#f9e2af"},
            {key:"green", name:"Green", light:"#40a02b", dark:"#a6e3a1"},
            {key:"teal", name:"Teal", light:"#179299", dark:"#94e2d5"},
            {key:"sky", name:"Sky", light:"#04a5e5", dark:"#89dceb"},
            {key:"sapphire", name:"Sapphire", light:"#209fb5", dark:"#74c7ec"},
            {key:"blue", name:"Blue", light:"#1e66f5", dark:"#89b4fa"},
            {key:"lavender", name:"Lavender", light:"#7287fd", dark:"#b4befe"}]},
        {key: "gruvbox", name: "Gruvbox", accents: [
            {key:"red", name:"Red", light:"#9d0006", dark:"#fb4934"},
            {key:"green", name:"Green", light:"#79740e", dark:"#b8bb26"},
            {key:"yellow", name:"Yellow", light:"#b57614", dark:"#fabd2f"},
            {key:"blue", name:"Blue", light:"#076678", dark:"#83a598"},
            {key:"purple", name:"Purple", light:"#8f3f71", dark:"#d3869b"},
            {key:"aqua", name:"Aqua", light:"#427b58", dark:"#8ec07c"},
            {key:"orange", name:"Orange", light:"#af3a03", dark:"#fe8019"}]},
        {key: "rosepine", name: "Rosé Pine", accents: [
            {key:"love", name:"Love", light:"#b4637a", dark:"#eb6f92"},
            {key:"gold", name:"Gold", light:"#ea9d34", dark:"#f6c177"},
            {key:"rose", name:"Rose", light:"#d7827e", dark:"#ebbcba"},
            {key:"pine", name:"Pine", light:"#286983", dark:"#31748f"},
            {key:"foam", name:"Foam", light:"#56949f", dark:"#9ccfd8"},
            {key:"iris", name:"Iris", light:"#907aa9", dark:"#c4a7e7"}]}
    ]
    // A stored choice reads "palette/accent", such as "catppuccin/mauve".
    function find(token) {
        const parts = String(token || "").split("/")
        if (parts.length !== 2) return null
        for (const palette of palettes)
            if (palette.key === parts[0])
                for (const accent of palette.accents)
                    if (accent.key === parts[1]) return {palette: palette, accent: accent}
        return null
    }
    function hex(token, dark) { const found = find(token); return found ? (dark ? found.accent.dark : found.accent.light) : "" }
    function name(token) { const found = find(token); return found ? found.palette.name + " " + found.accent.name : "" }
}
