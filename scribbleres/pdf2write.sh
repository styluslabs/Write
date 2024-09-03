#!/bin/bash
# Create a Write document from a PDF by generating page images

echo "Converting PDF to images..."
(command -v pdftoppm >/dev/null 2>&1 && pdftoppm -png -r 300 $1 out) || convert -density 300 -scene 1 $1 out-%03d.png
if [ ! -f "out-1.png" ] && [ ! -f "out-01.png" ] && [ ! -f "out-001.png" ]; then
  echo "No page images found: make sure pdftoppm (from poppler-utils) or imagemagick and ghostscript are installed"
  exit 1
fi
SVGOUT=$(basename $1 pdf)svg
echo "Generating Write document..."

printf '<svg id="write-document" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">\n' > $SVGOUT
printf '<rect id="write-doc-background" width="100%%" height="100%%" fill="#808080"/>\n' >> $SVGOUT

for PNGPAGE in out-*.png
do
  read WIDTH2 HEIGHT2 < <(identify -format "%w %h" $PNGPAGE)
  # page images generated at 300 DPI but Write uses 150 DPI as reference
  WIDTH=$((WIDTH2/2))
  HEIGHT=$((HEIGHT2/2))

  printf '<svg class="write-page" color-interpolation="linearRGB" x="10" y="10" width="%dpx" height="%dpx" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">\n' $WIDTH $HEIGHT >> $SVGOUT
  printf '  <g class="write-content write-v3" xruling="0" yruling="40" marginLeft="100" papercolor="#FFFFFF" rulecolor="#9F0000FF">\n' >> $SVGOUT
  printf '    <g class="ruleline write-no-dup" shape-rendering="crispEdges">\n' >> $SVGOUT
  printf '      <rect class="pagerect" fill="#FFFFFF" stroke="none" x="0" y="0" width="%d" height="%d" />\n' $WIDTH $HEIGHT >> $SVGOUT

  printf '      <image x="0" y="0" width="%d" height="%d" xlink:href="data:image/png;base64,' $WIDTH $HEIGHT >> $SVGOUT
  # doesn't seem to be a way to prevent base64 from appending newline
  base64 $PNGPAGE >> $SVGOUT
  printf '"/>\n    </g>\n  </g>\n</svg>' >> $SVGOUT
done

printf "\n</svg>" >> $SVGOUT

gzip -S z $SVGOUT
rm out-*.png
echo "Finished creating $SVGOUT"z
