#!/bin/sh
# Regenerates the PDF exports of the design documents (docs/design-cycle.md, step 1).
# Needs pandoc and xelatex (brew install pandoc; MacTeX or TeX Live).
set -eu
cd "$(dirname "$0")"

for doc in architecture implementation-plan; do
    [ -f "$doc.md" ] || continue
    # pandoc's own Markdown reader wraps long table cells (the gfm reader doesn't);
    # the extension accepts lists right after a paragraph line, as GitHub does
    pandoc "$doc.md" --from markdown-smart+lists_without_preceding_blankline --columns=60 \
        --lua-filter=pdf-tables.lua --pdf-engine=xelatex \
        -V geometry:margin=2cm \
        -V fontsize=10pt \
        -V mainfont="Arial Unicode MS" \
        -V mainfontoptions="BoldFont=Arial Bold, ItalicFont=Arial Italic, BoldItalicFont=Arial Bold Italic" \
        -V monofont="Menlo" \
        -V colorlinks=true \
        -o "$doc.pdf"
    echo "docs/$doc.pdf"
done
